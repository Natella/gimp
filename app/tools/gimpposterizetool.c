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

#include <gegl.h>
#include <gtk/gtk.h>

#include "libgimpmath/gimpmath.h"
#include "libgimpconfig/gimpconfig.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "tools-types.h"

#include "operations/gimpposterizeconfig.h"

#include "core/gimpdrawable.h"
#include "core/gimperror.h"
#include "core/gimpimage.h"

#include "widgets/gimphelp-ids.h"

#include "display/gimpdisplay.h"

#include "gimpimagemapoptions.h"
#include "gimpposterizetool.h"

#include "gimp-intl.h"


#define SLIDER_WIDTH 200


static gboolean   gimp_posterize_tool_initialize     (GimpTool          *tool,
                                                      GimpDisplay       *display,
                                                      GError           **error);

static GeglNode * gimp_posterize_tool_get_operation  (GimpImageMapTool  *im_tool,
                                                      GObject          **config,
                                                      gchar            **undo_desc);
static void       gimp_posterize_tool_dialog         (GimpImageMapTool  *im_tool);

static void       gimp_posterize_tool_config_notify  (GObject           *object,
                                                      GParamSpec        *pspec,
                                                      GimpPosterizeTool *posterize_tool);

static void       gimp_posterize_tool_levels_changed (GtkAdjustment     *adjustment,
                                                      GimpPosterizeTool *posterize_tool);


G_DEFINE_TYPE (GimpPosterizeTool, gimp_posterize_tool,
               GIMP_TYPE_IMAGE_MAP_TOOL)

#define parent_class gimp_posterize_tool_parent_class


void
gimp_posterize_tool_register (GimpToolRegisterCallback  callback,
                              gpointer                  data)
{
  (* callback) (GIMP_TYPE_POSTERIZE_TOOL,
                GIMP_TYPE_IMAGE_MAP_OPTIONS, NULL,
                0,
                "gimp-posterize-tool",
                _("Posterize"),
                _("Posterize Tool: Reduce to a limited set of colors"),
                N_("_Posterize..."), NULL,
                NULL, GIMP_HELP_TOOL_POSTERIZE,
                GIMP_STOCK_TOOL_POSTERIZE,
                data);
}

static void
gimp_posterize_tool_class_init (GimpPosterizeToolClass *klass)
{
  GimpToolClass         *tool_class    = GIMP_TOOL_CLASS (klass);
  GimpImageMapToolClass *im_tool_class = GIMP_IMAGE_MAP_TOOL_CLASS (klass);

  tool_class->initialize       = gimp_posterize_tool_initialize;

  im_tool_class->dialog_desc   = _("Posterize (Reduce Number of Colors)");

  im_tool_class->get_operation = gimp_posterize_tool_get_operation;
  im_tool_class->dialog        = gimp_posterize_tool_dialog;
}

static void
gimp_posterize_tool_init (GimpPosterizeTool *posterize_tool)
{
}

static gboolean
gimp_posterize_tool_initialize (GimpTool     *tool,
                                GimpDisplay  *display,
                                GError      **error)
{
  GimpPosterizeTool *posterize_tool = GIMP_POSTERIZE_TOOL (tool);

  if (! GIMP_TOOL_CLASS (parent_class)->initialize (tool, display, error))
    {
      return FALSE;
    }

  gtk_adjustment_set_value (posterize_tool->levels_data,
                            posterize_tool->config->levels);

  return TRUE;
}

static GeglNode *
gimp_posterize_tool_get_operation (GimpImageMapTool  *image_map_tool,
                                   GObject          **config,
                                   gchar            **undo_desc)
{
  GimpPosterizeTool *posterize_tool = GIMP_POSTERIZE_TOOL (image_map_tool);

  posterize_tool->config = g_object_new (GIMP_TYPE_POSTERIZE_CONFIG, NULL);

  g_signal_connect_object (posterize_tool->config, "notify",
                           G_CALLBACK (gimp_posterize_tool_config_notify),
                           G_OBJECT (posterize_tool), 0);

  *config = G_OBJECT (posterize_tool->config);

  return gegl_node_new_child (NULL,
                              "operation", "gimp:posterize",
                              "config",    posterize_tool->config,
                              NULL);
}


/**********************/
/*  Posterize dialog  */
/**********************/

static void
gimp_posterize_tool_dialog (GimpImageMapTool *image_map_tool)
{
  GimpPosterizeTool *posterize_tool = GIMP_POSTERIZE_TOOL (image_map_tool);
  GtkWidget         *main_vbox;
  GtkWidget         *table;
  GtkObject         *data;

  main_vbox = gimp_image_map_tool_dialog_get_vbox (image_map_tool);

  /*  The table containing sliders  */
  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_box_pack_start (GTK_BOX (main_vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                               _("Posterize _levels:"), SLIDER_WIDTH, -1,
                               posterize_tool->config->levels,
                               2.0, 256.0, 1.0, 10.0, 0,
                               TRUE, 0.0, 0.0,
                               NULL, NULL);

  gimp_scale_entry_set_logarithmic (data, TRUE);

  posterize_tool->levels_data = GTK_ADJUSTMENT (data);

  g_signal_connect (posterize_tool->levels_data, "value-changed",
                    G_CALLBACK (gimp_posterize_tool_levels_changed),
                    posterize_tool);
}

static void
gimp_posterize_tool_config_notify (GObject           *object,
                                   GParamSpec        *pspec,
                                   GimpPosterizeTool *posterize_tool)
{
  GimpPosterizeConfig *config = GIMP_POSTERIZE_CONFIG (object);

  if (! posterize_tool->levels_data)
    return;

  gtk_adjustment_set_value (posterize_tool->levels_data, config->levels);
}

static void
gimp_posterize_tool_levels_changed (GtkAdjustment     *adjustment,
                                    GimpPosterizeTool *posterize_tool)
{
  gint value = ROUND (gtk_adjustment_get_value (adjustment));

  if (posterize_tool->config->levels != value)
    {
      g_object_set (posterize_tool->config,
                    "levels", value,
                    NULL);
    }
}
