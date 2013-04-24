/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>

#include <gegl.h>
#include <gegl-plugin.h>
#include <gtk/gtk.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpcolor/gimpcolor.h"
#include "libgimpconfig/gimpconfig.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "tools-types.h"

#include "gegl/gimp-gegl-config-proxy.h"

#include "core/gimpdrawable.h"
#include "core/gimperror.h"
#include "core/gimpimage.h"
#include "core/gimpimagemap.h"
#include "core/gimpimagemapconfig.h"
#include "core/gimplist.h"
#include "core/gimpparamspecs-duplicate.h"

#include "widgets/gimphelp-ids.h"
#include "widgets/gimppropwidgets.h"

#include "display/gimpdisplay.h"

#include "gimpcoloroptions.h"
#include "gimpoperationtool.h"

#include "gimp-intl.h"


/*  local function prototypes  */

static void        gimp_operation_tool_finalize        (GObject           *object);

static GeglNode  * gimp_operation_tool_get_operation   (GimpImageMapTool  *im_tool,
                                                        GObject          **config,
                                                        gchar            **undo_desc);
static void        gimp_operation_tool_map             (GimpImageMapTool  *im_tool);
static void        gimp_operation_tool_dialog          (GimpImageMapTool  *im_tool);
static void        gimp_operation_tool_reset           (GimpImageMapTool  *im_tool);
static GtkWidget * gimp_operation_tool_get_settings_ui (GimpImageMapTool  *image_map_tool,
                                                        GimpContainer     *settings,
                                                        const gchar       *settings_filename,
                                                        const gchar       *import_dialog_title,
                                                        const gchar       *export_dialog_title,
                                                        const gchar       *file_dialog_help_id,
                                                        const gchar       *default_folder,
                                                        GtkWidget        **settings_box);
static void        gimp_operation_tool_color_picked    (GimpImageMapTool  *im_tool,
                                                        gpointer           identifier,
                                                        const Babl        *sample_format,
                                                        const GimpRGB     *color);


G_DEFINE_TYPE (GimpOperationTool, gimp_operation_tool,
               GIMP_TYPE_IMAGE_MAP_TOOL)

#define parent_class gimp_operation_tool_parent_class


void
gimp_operation_tool_register (GimpToolRegisterCallback  callback,
                              gpointer                  data)
{
  (* callback) (GIMP_TYPE_OPERATION_TOOL,
                GIMP_TYPE_COLOR_OPTIONS,
                gimp_color_options_gui,
                0,
                "gimp-operation-tool",
                _("GEGL Operation"),
                _("Operation Tool: Use an arbitrary GEGL operation"),
                N_("_GEGL Operation..."), NULL,
                NULL, GIMP_HELP_TOOL_GEGL,
                GIMP_STOCK_GEGL,
                data);
}

static void
gimp_operation_tool_class_init (GimpOperationToolClass *klass)
{
  GObjectClass          *object_class  = G_OBJECT_CLASS (klass);
  GimpImageMapToolClass *im_tool_class = GIMP_IMAGE_MAP_TOOL_CLASS (klass);

  object_class->finalize         = gimp_operation_tool_finalize;

  im_tool_class->dialog_desc     = _("GEGL Operation");

  im_tool_class->get_operation   = gimp_operation_tool_get_operation;
  im_tool_class->map             = gimp_operation_tool_map;
  im_tool_class->dialog          = gimp_operation_tool_dialog;
  im_tool_class->reset           = gimp_operation_tool_reset;
  im_tool_class->get_settings_ui = gimp_operation_tool_get_settings_ui;
  im_tool_class->color_picked    = gimp_operation_tool_color_picked;
}

static void
gimp_operation_tool_init (GimpOperationTool *tool)
{
  GIMP_IMAGE_MAP_TOOL_GET_CLASS (tool)->settings_name = NULL; /* XXX hack */
}

static void
gimp_operation_tool_finalize (GObject *object)
{
  GimpOperationTool *tool = GIMP_OPERATION_TOOL (object);

  if (tool->operation)
    {
      g_free (tool->operation);
      tool->operation = NULL;
    }

  if (tool->config)
    {
      g_object_unref (tool->config);
      tool->config = NULL;
    }

  if (tool->undo_desc)
    {
      g_free (tool->undo_desc);
      tool->undo_desc = NULL;
    }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GeglNode *
gimp_operation_tool_get_operation (GimpImageMapTool  *im_tool,
                                   GObject          **config,
                                   gchar            **undo_desc)
{
  GimpOperationTool *tool = GIMP_OPERATION_TOOL (im_tool);

  if (tool->config)
    *config = g_object_ref (tool->config);

  if (tool->undo_desc)
    *undo_desc = g_strdup (tool->undo_desc);

  if (tool->operation)
    return gegl_node_new_child (NULL,
                                "operation", tool->operation,
                                NULL);

  return g_object_new (GEGL_TYPE_NODE, NULL);
}

static void
gimp_operation_tool_map (GimpImageMapTool *image_map_tool)
{
  GimpOperationTool *tool = GIMP_OPERATION_TOOL (image_map_tool);

  if (tool->config)
    gimp_gegl_config_proxy_sync (tool->config, image_map_tool->operation);
}

static void
gimp_operation_tool_dialog (GimpImageMapTool *image_map_tool)
{
  GimpOperationTool *tool = GIMP_OPERATION_TOOL (image_map_tool);
  GtkWidget         *main_vbox;

  main_vbox = gimp_image_map_tool_dialog_get_vbox (image_map_tool);

  /*  The options vbox  */
  tool->options_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), tool->options_box,
                      FALSE, FALSE, 0);
  gtk_widget_show (tool->options_box);

  if (tool->options_table)
    {
      gtk_container_add (GTK_CONTAINER (tool->options_box),
                         tool->options_table);
      gtk_widget_show (tool->options_table);
    }

  if (tool->undo_desc)
    g_object_set (GIMP_IMAGE_MAP_TOOL (tool)->dialog,
                  "description", tool->undo_desc,
                  NULL);
}

static void
gimp_operation_tool_reset (GimpImageMapTool *image_map_tool)
{
  GimpOperationTool *tool = GIMP_OPERATION_TOOL (image_map_tool);

  if (tool->config)
    gimp_config_reset (GIMP_CONFIG (tool->config));
}

static GtkWidget *
gimp_operation_tool_get_settings_ui (GimpImageMapTool  *image_map_tool,
                                     GimpContainer     *settings,
                                     const gchar       *settings_filename,
                                     const gchar       *import_dialog_title,
                                     const gchar       *export_dialog_title,
                                     const gchar       *file_dialog_help_id,
                                     const gchar       *default_folder,
                                     GtkWidget        **settings_box)
{
  GimpOperationTool *tool = GIMP_OPERATION_TOOL (image_map_tool);
  GType              type = G_TYPE_FROM_INSTANCE (tool->config);
  GtkWidget         *widget;
  gchar             *basename;
  gchar             *filename;
  gchar             *import_title;
  gchar             *export_title;

  settings = gimp_gegl_get_config_container (type);
  if (! gimp_list_get_sort_func (GIMP_LIST (settings)))
    gimp_list_set_sort_func (GIMP_LIST (settings),
                             (GCompareFunc) gimp_image_map_config_compare);

  basename = g_strconcat (G_OBJECT_TYPE_NAME (tool->config), ".settings", NULL);
  filename = g_build_filename (gimp_directory (), "filters", basename, NULL);
  g_free (basename);

  import_title = g_strdup_printf (_("Import '%s' Settings"), tool->undo_desc);
  export_title = g_strdup_printf (_("Export '%s' Settings"), tool->undo_desc);

  widget =
    GIMP_IMAGE_MAP_TOOL_CLASS (parent_class)->get_settings_ui (image_map_tool,
                                                               settings,
                                                               filename,
                                                               import_title,
                                                               export_title,
                                                               "help-foo",
                                                               g_get_home_dir (),
                                                               settings_box);

  g_free (filename);
  g_free (import_title);
  g_free (export_title);

  return widget;
}

static void
gimp_operation_tool_color_picked (GimpImageMapTool  *im_tool,
                                  gpointer           identifier,
                                  const Babl        *sample_format,
                                  const GimpRGB     *color)
{
  GimpOperationTool *tool = GIMP_OPERATION_TOOL (im_tool);

  g_object_set (tool->config,
                identifier, color,
                NULL);
}

void
gimp_operation_tool_set_operation (GimpOperationTool *tool,
                                   const gchar       *operation,
                                   const gchar       *undo_desc)
{
  g_return_if_fail (GIMP_IS_OPERATION_TOOL (tool));
  g_return_if_fail (operation != NULL);

  if (tool->operation)
    g_free (tool->operation);

  if (tool->undo_desc)
    g_free (tool->undo_desc);

  tool->operation = g_strdup (operation);
  tool->undo_desc = g_strdup (undo_desc);

  if (tool->config)
    g_object_unref (tool->config);

  tool->config = gimp_gegl_get_config_proxy (tool->operation,
                                             GIMP_TYPE_IMAGE_MAP_CONFIG);

  gimp_image_map_tool_get_operation (GIMP_IMAGE_MAP_TOOL (tool));

  if (undo_desc)
    GIMP_IMAGE_MAP_TOOL_GET_CLASS (tool)->settings_name = "yes"; /* XXX hack */
  else
    GIMP_IMAGE_MAP_TOOL_GET_CLASS (tool)->settings_name = NULL; /* XXX hack */

  if (tool->options_table)
    {
      gtk_widget_destroy (tool->options_table);
      tool->options_table = NULL;
    }

  if (tool->config)
    {
      tool->options_table =
        gimp_prop_table_new (G_OBJECT (tool->config),
                             G_TYPE_FROM_INSTANCE (tool->config),
                             GIMP_CONTEXT (GIMP_TOOL_GET_OPTIONS (tool)),
                             (GimpCreatePickerFunc) gimp_image_map_tool_add_color_picker,
                             tool);

      if (tool->options_box)
        {
          gtk_container_add (GTK_CONTAINER (tool->options_box),
                             tool->options_table);
          gtk_widget_show (tool->options_table);
        }
    }

  if (undo_desc && GIMP_IMAGE_MAP_TOOL (tool)->dialog)
    g_object_set (GIMP_IMAGE_MAP_TOOL (tool)->dialog,
                  "description", undo_desc,
                  NULL);

  if (GIMP_TOOL (tool)->drawable)
    gimp_image_map_tool_preview (GIMP_IMAGE_MAP_TOOL (tool));
}
