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
#include <gtk/gtk.h>

#include "libgimpconfig/gimpconfig.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "tools-types.h"

#include "operations/gimpcolorizeconfig.h"

#include "core/gimpdrawable.h"
#include "core/gimperror.h"
#include "core/gimpimage.h"

#include "widgets/gimpcolorpanel.h"
#include "widgets/gimphelp-ids.h"

#include "display/gimpdisplay.h"

#include "gimpcolorizetool.h"
#include "gimpcoloroptions.h"

#include "gimp-intl.h"


#define SLIDER_WIDTH  200
#define SPINNER_WIDTH 4


/*  local function prototypes  */

static gboolean   gimp_colorize_tool_initialize    (GimpTool         *tool,
                                                    GimpDisplay      *display,
                                                    GError          **error);

static GeglNode * gimp_colorize_tool_get_operation (GimpImageMapTool *im_tool,
                                                    GObject         **config,
                                                    gchar           **undo_desc);
static void       gimp_colorize_tool_dialog        (GimpImageMapTool *im_tool);
static void       gimp_colorize_tool_color_picked  (GimpImageMapTool *im_tool,
                                                    gpointer          identifier,
                                                    const Babl       *sample_format,
                                                    const GimpRGB    *color);

static void       gimp_colorize_tool_config_notify (GObject          *object,
                                                    GParamSpec       *pspec,
                                                    GimpColorizeTool *col_tool);

static void       colorize_hue_changed             (GtkAdjustment    *adj,
                                                    GimpColorizeTool *col_tool);
static void       colorize_saturation_changed      (GtkAdjustment    *adj,
                                                    GimpColorizeTool *col_tool);
static void       colorize_lightness_changed       (GtkAdjustment    *adj,
                                                    GimpColorizeTool *col_tool);
static void       colorize_color_changed           (GtkWidget        *button,
                                                    GimpColorizeTool *col_tool);


G_DEFINE_TYPE (GimpColorizeTool, gimp_colorize_tool, GIMP_TYPE_IMAGE_MAP_TOOL)

#define parent_class gimp_colorize_tool_parent_class


void
gimp_colorize_tool_register (GimpToolRegisterCallback  callback,
                             gpointer                  data)
{
  (* callback) (GIMP_TYPE_COLORIZE_TOOL,
                GIMP_TYPE_COLOR_OPTIONS,
                gimp_color_options_gui,
                0,
                "gimp-colorize-tool",
                _("Colorize"),
                _("Colorize Tool: Colorize the image"),
                N_("Colori_ze..."), NULL,
                NULL, GIMP_HELP_TOOL_COLORIZE,
                GIMP_STOCK_TOOL_COLORIZE,
                data);
}

static void
gimp_colorize_tool_class_init (GimpColorizeToolClass *klass)
{
  GimpToolClass         *tool_class    = GIMP_TOOL_CLASS (klass);
  GimpImageMapToolClass *im_tool_class = GIMP_IMAGE_MAP_TOOL_CLASS (klass);

  tool_class->initialize             = gimp_colorize_tool_initialize;

  im_tool_class->dialog_desc         = _("Colorize the Image");
  im_tool_class->settings_name       = "colorize";
  im_tool_class->import_dialog_title = _("Import Colorize Settings");
  im_tool_class->export_dialog_title = _("Export Colorize Settings");

  im_tool_class->get_operation       = gimp_colorize_tool_get_operation;
  im_tool_class->dialog              = gimp_colorize_tool_dialog;
  im_tool_class->color_picked        = gimp_colorize_tool_color_picked;
}

static void
gimp_colorize_tool_init (GimpColorizeTool *col_tool)
{
}

static gboolean
gimp_colorize_tool_initialize (GimpTool     *tool,
                               GimpDisplay  *display,
                               GError      **error)
{
  GimpImage    *image    = gimp_display_get_image (display);
  GimpDrawable *drawable = gimp_image_get_active_drawable (image);

  if (! drawable)
    return FALSE;

  if (gimp_drawable_is_gray (drawable))
    {
      g_set_error_literal (error, GIMP_ERROR, GIMP_FAILED,
			   _("Colorize does not operate on grayscale layers."));
      return FALSE;
    }

  if (! GIMP_TOOL_CLASS (parent_class)->initialize (tool, display, error))
    {
      return FALSE;
    }

  return TRUE;
}

static GeglNode *
gimp_colorize_tool_get_operation (GimpImageMapTool  *im_tool,
                                  GObject          **config,
                                  gchar            **undo_desc)
{
  GimpColorizeTool *col_tool = GIMP_COLORIZE_TOOL (im_tool);

  col_tool->config = g_object_new (GIMP_TYPE_COLORIZE_CONFIG, NULL);

  g_signal_connect_object (col_tool->config, "notify",
                           G_CALLBACK (gimp_colorize_tool_config_notify),
                           G_OBJECT (col_tool), 0);

  *config = G_OBJECT (col_tool->config);

  return gegl_node_new_child (NULL,
                              "operation", "gimp:colorize",
                              "config",    col_tool->config,
                              NULL);
}


/***************************/
/*  Hue-Saturation dialog  */
/***************************/

static void
gimp_colorize_tool_dialog (GimpImageMapTool *image_map_tool)
{
  GimpColorizeTool *col_tool = GIMP_COLORIZE_TOOL (image_map_tool);
  GtkWidget        *main_vbox;
  GtkWidget        *table;
  GtkWidget        *frame;
  GtkWidget        *vbox;
  GtkWidget        *hbox;
  GtkWidget        *button;
  GtkObject        *data;
  GimpRGB           color;

  main_vbox = gimp_image_map_tool_dialog_get_vbox (image_map_tool);

  frame = gimp_frame_new (_("Select Color"));
  gtk_box_pack_start (GTK_BOX (main_vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  /*  The table containing sliders  */
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  table = gtk_table_new (3, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  /*  Create the hue scale widget  */
  data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                               _("_Hue:"), SLIDER_WIDTH, SPINNER_WIDTH,
                               col_tool->config->hue * 360.0,
                               0.0, 359.99, 1.0, 15.0, 0,
                               TRUE, 0.0, 0.0,
                               NULL, NULL);
  col_tool->hue_data = GTK_ADJUSTMENT (data);

  g_signal_connect (data, "value-changed",
                    G_CALLBACK (colorize_hue_changed),
                    col_tool);

  /*  Create the saturation scale widget  */
  data = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
                               _("_Saturation:"), SLIDER_WIDTH, SPINNER_WIDTH,
                               col_tool->config->saturation * 100.0,
                               0.0, 100.0, 1.0, 10.0, 0,
                               TRUE, 0.0, 0.0,
                               NULL, NULL);
  col_tool->saturation_data = GTK_ADJUSTMENT (data);

  g_signal_connect (data, "value-changed",
                    G_CALLBACK (colorize_saturation_changed),
                    col_tool);

  /*  Create the lightness scale widget  */
  data = gimp_scale_entry_new (GTK_TABLE (table), 0, 2,
                               _("_Lightness:"), SLIDER_WIDTH, SPINNER_WIDTH,
                               col_tool->config->lightness * 100.0,
                               -100.0, 100.0, 1.0, 10.0, 0,
                               TRUE, 0.0, 0.0,
                               NULL, NULL);
  col_tool->lightness_data = GTK_ADJUSTMENT (data);

  g_signal_connect (data, "value-changed",
                    G_CALLBACK (colorize_lightness_changed),
                    col_tool);

  /*  Create the color button  */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  gimp_colorize_config_get_color (col_tool->config, &color);

  col_tool->color_button = gimp_color_panel_new (_("Colorize Color"),
                                                 &color,
                                                 GIMP_COLOR_AREA_FLAT,
                                                 128, 24);
  gimp_color_button_set_update (GIMP_COLOR_BUTTON (col_tool->color_button),
                                TRUE);
  gimp_color_panel_set_context (GIMP_COLOR_PANEL (col_tool->color_button),
                                GIMP_CONTEXT (GIMP_TOOL_GET_OPTIONS (col_tool)));
  gtk_box_pack_start (GTK_BOX (hbox), col_tool->color_button, TRUE, TRUE, 0);
  gtk_widget_show (col_tool->color_button);

  g_signal_connect (col_tool->color_button, "color-changed",
                    G_CALLBACK (colorize_color_changed),
                    col_tool);

  button = gimp_image_map_tool_add_color_picker (image_map_tool,
                                                 "colorize",
                                                 GIMP_STOCK_COLOR_PICKER_GRAY,
                                                 _("Pick color from image"));
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  gtk_widget_show (button);
}

static void
gimp_colorize_tool_color_picked (GimpImageMapTool *im_tool,
                                 gpointer          identifier,
                                 const Babl       *sample_format,
                                 const GimpRGB    *color)
{
  GimpColorizeTool *col_tool = GIMP_COLORIZE_TOOL (im_tool);

  gimp_colorize_config_set_color (col_tool->config, color);
}

static void
gimp_colorize_tool_config_notify (GObject          *object,
                                  GParamSpec       *pspec,
                                  GimpColorizeTool *col_tool)
{
  GimpColorizeConfig *config = GIMP_COLORIZE_CONFIG (object);
  GimpRGB             color;

  if (! col_tool->hue_data)
    return;

  if (! strcmp (pspec->name, "hue"))
    {
      gtk_adjustment_set_value (col_tool->hue_data,
                                config->hue * 360.0);
    }
  else if (! strcmp (pspec->name, "saturation"))
    {
      gtk_adjustment_set_value (col_tool->saturation_data,
                                config->saturation * 100.0);
    }
  else if (! strcmp (pspec->name, "lightness"))
    {
      gtk_adjustment_set_value (col_tool->lightness_data,
                                config->lightness * 100.0);
    }

  gimp_colorize_config_get_color (col_tool->config, &color);
  gimp_color_button_set_color (GIMP_COLOR_BUTTON (col_tool->color_button),
                               &color);
}

static void
colorize_hue_changed (GtkAdjustment    *adjustment,
                      GimpColorizeTool *col_tool)
{
  gdouble value = gtk_adjustment_get_value (adjustment) / 360.0;

  if (col_tool->config->hue != value)
    {
      g_object_set (col_tool->config,
                    "hue", value,
                    NULL);
    }
}

static void
colorize_saturation_changed (GtkAdjustment    *adjustment,
                             GimpColorizeTool *col_tool)
{
  gdouble value = gtk_adjustment_get_value (adjustment) / 100.0;

  if (col_tool->config->saturation != value)
    {
      g_object_set (col_tool->config,
                    "saturation", value,
                    NULL);
    }
}

static void
colorize_lightness_changed (GtkAdjustment    *adjustment,
                            GimpColorizeTool *col_tool)
{
  gdouble value = gtk_adjustment_get_value (adjustment) / 100.0;

  if (col_tool->config->lightness != value)
    {
      g_object_set (col_tool->config,
                    "lightness", value,
                    NULL);
    }
}

static void
colorize_color_changed (GtkWidget        *button,
                        GimpColorizeTool *col_tool)
{
  GimpRGB color;

  gimp_color_button_get_color (GIMP_COLOR_BUTTON (button), &color);
  gimp_colorize_config_set_color (col_tool->config, &color);
}
