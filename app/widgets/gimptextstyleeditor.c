/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * GimpTextStyleEditor
 * Copyright (C) 2010  Michael Natterer <mitch@gimp.org>
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

#include "libgimpbase/gimpbase.h"

#include "widgets-types.h"

#include "gimptextbuffer.h"
#include "gimptextstyleeditor.h"


enum
{
  PROP_0,
  PROP_BUFFER,
  PROP_RESOLUTION_X,
  PROP_RESOLUTION_Y
};


static GObject * gimp_text_style_editor_constructor  (GType                  type,
                                                      guint                  n_params,
                                                      GObjectConstructParam *params);
static void      gimp_text_style_editor_dispose      (GObject               *object);
static void      gimp_text_style_editor_finalize     (GObject               *object);
static void      gimp_text_style_editor_set_property (GObject               *object,
                                                      guint                  property_id,
                                                      const GValue          *value,
                                                      GParamSpec            *pspec);
static void      gimp_text_style_editor_get_property (GObject               *object,
                                                      guint                  property_id,
                                                      GValue                *value,
                                                      GParamSpec            *pspec);

static GtkWidget *
                gimp_text_style_editor_create_toggle (GimpTextStyleEditor *editor,
                                                      GtkTextTag          *tag,
                                                      const gchar         *stock_id);

static void      gimp_text_style_editor_clear_tags   (GtkButton           *button,
                                                      GimpTextStyleEditor *editor);
static void      gimp_text_style_editor_tag_toggled  (GtkToggleButton     *toggle,
                                                      GimpTextStyleEditor *editor);

static void      gimp_text_style_editor_update       (GimpTextStyleEditor *editor);


G_DEFINE_TYPE (GimpTextStyleEditor, gimp_text_style_editor,
               GTK_TYPE_HBOX)

#define parent_class gimp_text_style_editor_parent_class


static void
gimp_text_style_editor_class_init (GimpTextStyleEditorClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor  = gimp_text_style_editor_constructor;
  object_class->dispose      = gimp_text_style_editor_dispose;
  object_class->finalize     = gimp_text_style_editor_finalize;
  object_class->set_property = gimp_text_style_editor_set_property;
  object_class->get_property = gimp_text_style_editor_get_property;

  g_object_class_install_property (object_class, PROP_BUFFER,
                                   g_param_spec_object ("buffer",
                                                        NULL, NULL,
                                                        GIMP_TYPE_TEXT_BUFFER,
                                                        GIMP_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class, PROP_RESOLUTION_X,
                                   g_param_spec_double ("resolution-x",
                                                        NULL, NULL,
                                                        GIMP_MIN_RESOLUTION,
                                                        GIMP_MAX_RESOLUTION,
                                                        1.0,
                                                        GIMP_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, PROP_RESOLUTION_Y,
                                   g_param_spec_double ("resolution-y",
                                                        NULL, NULL,
                                                        GIMP_MIN_RESOLUTION,
                                                        GIMP_MAX_RESOLUTION,
                                                        1.0,
                                                        GIMP_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));
}

static void
gimp_text_style_editor_init (GimpTextStyleEditor *editor)
{
  GtkWidget *image;

  editor->clear_button = gtk_button_new ();
  gtk_widget_set_can_focus (editor->clear_button, FALSE);
  gtk_box_pack_start (GTK_BOX (editor), editor->clear_button, FALSE, FALSE, 0);
  gtk_widget_show (editor->clear_button);

  g_signal_connect (editor->clear_button, "clicked",
                    G_CALLBACK (gimp_text_style_editor_clear_tags),
                    editor);

  image = gtk_image_new_from_stock (GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (editor->clear_button), image);
  gtk_widget_show (image);

  editor->size_label = gtk_label_new ("0.0");
  gtk_misc_set_padding (GTK_MISC (editor->size_label), 2, 0);
  gtk_box_pack_end (GTK_BOX (editor), editor->size_label, FALSE, FALSE, 0);
  gtk_widget_show (editor->size_label);
}

static GObject *
gimp_text_style_editor_constructor (GType                  type,
                                    guint                  n_params,
                                    GObjectConstructParam *params)
{
  GObject             *object;
  GimpTextStyleEditor *editor;

  object = G_OBJECT_CLASS (parent_class)->constructor (type, n_params, params);

  editor = GIMP_TEXT_STYLE_EDITOR (object);

  g_assert (GIMP_IS_TEXT_BUFFER (editor->buffer));

  gimp_text_style_editor_create_toggle (editor, editor->buffer->bold_tag,
                                        GTK_STOCK_BOLD);
  gimp_text_style_editor_create_toggle (editor, editor->buffer->italic_tag,
                                        GTK_STOCK_ITALIC);
  gimp_text_style_editor_create_toggle (editor, editor->buffer->underline_tag,
                                        GTK_STOCK_UNDERLINE);
  gimp_text_style_editor_create_toggle (editor, editor->buffer->strikethrough_tag,
                                        GTK_STOCK_STRIKETHROUGH);

  g_signal_connect_data (editor->buffer, "changed",
                         G_CALLBACK (gimp_text_style_editor_update),
                         editor, 0,
                         G_CONNECT_AFTER | G_CONNECT_SWAPPED);
  g_signal_connect_data (editor->buffer, "apply-tag",
                         G_CALLBACK (gimp_text_style_editor_update),
                         editor, 0,
                         G_CONNECT_AFTER | G_CONNECT_SWAPPED);
  g_signal_connect_data (editor->buffer, "remove-tag",
                         G_CALLBACK (gimp_text_style_editor_update),
                         editor, 0,
                         G_CONNECT_AFTER | G_CONNECT_SWAPPED);
  g_signal_connect_data (editor->buffer, "mark-set",
                         G_CALLBACK (gimp_text_style_editor_update),
                         editor, 0,
                         G_CONNECT_AFTER | G_CONNECT_SWAPPED);

  return object;
}

static void
gimp_text_style_editor_dispose (GObject *object)
{
  GimpTextStyleEditor *editor = GIMP_TEXT_STYLE_EDITOR (object);

  if (editor->buffer)
    {
      g_signal_handlers_disconnect_by_func (editor->buffer,
                                            gimp_text_style_editor_update,
                                            editor);
    }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gimp_text_style_editor_finalize (GObject *object)
{
  GimpTextStyleEditor *editor = GIMP_TEXT_STYLE_EDITOR (object);

  if (editor->buffer)
    {
      g_object_unref (editor->buffer);
      editor->buffer = NULL;
    }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gimp_text_style_editor_set_property (GObject      *object,
                                     guint         property_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  GimpTextStyleEditor *editor = GIMP_TEXT_STYLE_EDITOR (object);

  switch (property_id)
    {
    case PROP_BUFFER:
      editor->buffer = g_value_dup_object (value);
      break;
    case PROP_RESOLUTION_X:
      editor->resolution_x = g_value_get_double (value);
      break;
    case PROP_RESOLUTION_Y:
      editor->resolution_y = g_value_get_double (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gimp_text_style_editor_get_property (GObject    *object,
                                     guint       property_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  GimpTextStyleEditor *editor = GIMP_TEXT_STYLE_EDITOR (object);

  switch (property_id)
    {
    case PROP_BUFFER:
      g_value_set_object (value, editor->buffer);
      break;
    case PROP_RESOLUTION_X:
      g_value_set_double (value, editor->resolution_x);
      break;
    case PROP_RESOLUTION_Y:
      g_value_set_double (value, editor->resolution_y);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}


/*  public functions  */

GtkWidget *
gimp_text_style_editor_new (GimpTextBuffer *buffer,
                            gdouble         resolution_x,
                            gdouble         resolution_y)
{
  g_return_val_if_fail (GIMP_IS_TEXT_BUFFER (buffer), NULL);
  g_return_val_if_fail (resolution_x > 0.0, NULL);
  g_return_val_if_fail (resolution_y > 0.0, NULL);

  return g_object_new (GIMP_TYPE_TEXT_STYLE_EDITOR,
                       "buffer",       buffer,
                       "resolution-x", resolution_x,
                       "resolution-y", resolution_y,
                       NULL);
}

GList *
gimp_text_style_editor_list_tags (GimpTextStyleEditor *editor)
{
  GList *toggles;
  GList *tags = NULL;

  g_return_val_if_fail (GIMP_IS_TEXT_STYLE_EDITOR (editor), NULL);

  for (toggles = editor->toggles; toggles; toggles = g_list_next (toggles))
    {
      if (gtk_toggle_button_get_active (toggles->data))
        {
          tags = g_list_prepend (tags,
                                 g_object_get_data (toggles->data, "tag"));
        }
    }

  return g_list_reverse (tags);
}


/*  private functions  */

static GtkWidget *
gimp_text_style_editor_create_toggle (GimpTextStyleEditor *editor,
                                      GtkTextTag          *tag,
                                      const gchar         *stock_id)
{
  GtkWidget *toggle;
  GtkWidget *image;

  toggle = gtk_toggle_button_new ();
  gtk_widget_set_can_focus (toggle, FALSE);
  gtk_box_pack_start (GTK_BOX (editor), toggle, FALSE, FALSE, 0);
  gtk_widget_show (toggle);

  editor->toggles = g_list_append (editor->toggles, toggle);
  g_object_set_data (G_OBJECT (toggle), "tag", tag);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_text_style_editor_tag_toggled),
                    editor);

  image = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_MENU);
  gtk_container_add (GTK_CONTAINER (toggle), image);
  gtk_widget_show (image);

  return toggle;
}

static void
gimp_text_style_editor_clear_tags (GtkButton           *button,
                                   GimpTextStyleEditor *editor)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (editor->buffer);

  if (gtk_text_buffer_get_has_selection (buffer))
    {
      GtkTextIter start, end;

      gtk_text_buffer_get_selection_bounds (buffer, &start, &end);

      gtk_text_buffer_remove_all_tags (buffer, &start, &end);
    }
}

static void
gimp_text_style_editor_tag_toggled (GtkToggleButton     *toggle,
                                    GimpTextStyleEditor *editor)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (editor->buffer);
  GtkTextTag    *tag    = g_object_get_data (G_OBJECT (toggle), "tag");
  GList         *list;

  if (gtk_text_buffer_get_has_selection (buffer))
    {
      GtkTextIter start, end;

      gtk_text_buffer_get_selection_bounds (buffer, &start, &end);

      gtk_text_buffer_begin_user_action (buffer);

      if (gtk_toggle_button_get_active (toggle))
        {
          gtk_text_buffer_apply_tag (buffer, tag, &start, &end);
        }
      else
        {
          gtk_text_buffer_remove_tag (buffer, tag, &start, &end);
        }

      gtk_text_buffer_end_user_action (buffer);
    }

  list = gimp_text_style_editor_list_tags (editor);
  gimp_text_buffer_set_insert_tags (editor->buffer, list);
}

static void
gimp_text_style_editor_set_toggle (GimpTextStyleEditor *editor,
                                   GtkToggleButton     *toggle,
                                   gboolean             active)
{
  g_signal_handlers_block_by_func (toggle,
                                   gimp_text_style_editor_tag_toggled,
                                   editor);

  gtk_toggle_button_set_active (toggle, active);

  g_signal_handlers_unblock_by_func (toggle,
                                     gimp_text_style_editor_tag_toggled,
                                     editor);
}

static void
gimp_text_style_editor_update (GimpTextStyleEditor *editor)
{
  GtkTextBuffer *buffer = GTK_TEXT_BUFFER (editor->buffer);

  if (gtk_text_buffer_get_has_selection (buffer))
    {
      GtkTextIter  start, end;
      GtkTextIter  iter;
      GList       *list;

      gtk_text_buffer_get_selection_bounds (buffer, &start, &end);
      gtk_text_iter_order (&start, &end);

      /*  first, switch all toggles on  */
      for (list = editor->toggles; list; list = g_list_next (list))
        {
          GtkToggleButton *toggle = list->data;

          gimp_text_style_editor_set_toggle (editor, toggle, TRUE);
        }

      for (iter = start;
           gtk_text_iter_in_range (&iter, &start, &end);
           gtk_text_iter_forward_cursor_position (&iter))
        {
          gboolean any_active = FALSE;

          for (list = editor->toggles; list; list = g_list_next (list))
            {
              GtkToggleButton *toggle = list->data;
              GtkTextTag      *tag    = g_object_get_data (G_OBJECT (toggle),
                                                           "tag");

              if (! gtk_text_iter_has_tag (&iter, tag))
                {
                  gimp_text_style_editor_set_toggle (editor, toggle, FALSE);
                }
              else
                {
                  any_active = TRUE;
                }
            }

          if (! any_active)
            break;
       }

      gtk_label_set_text (GTK_LABEL (editor->size_label), "---");
    }
  else
    {
      GtkTextIter  cursor;
      GSList      *tags;
      GSList      *tags_on;
      GSList      *tags_off;
      GList       *list;
      gchar       *str;

      gtk_text_buffer_get_iter_at_mark (buffer, &cursor,
                                        gtk_text_buffer_get_insert (buffer));

      tags     = gtk_text_iter_get_tags (&cursor);
      tags_on  = gtk_text_iter_get_toggled_tags (&cursor, TRUE);
      tags_off = gtk_text_iter_get_toggled_tags (&cursor, FALSE);

      for (list = editor->toggles; list; list = g_list_next (list))
        {
          GtkToggleButton *toggle = list->data;
          GtkTextTag      *tag    = g_object_get_data (G_OBJECT (toggle),
                                                       "tag");

          gimp_text_style_editor_set_toggle (editor, toggle,
                                             (g_slist_find (tags, tag) &&
                                              ! g_slist_find (tags_on, tag)) ||
                                             g_slist_find (tags_off, tag));
        }

      str = g_strdup_printf ("%0.2f", editor->resolution_y);
      gtk_label_set_text (GTK_LABEL (editor->size_label), str);
      g_free (str);

      g_slist_free (tags);
      g_slist_free (tags_on);
      g_slist_free (tags_off);
    }
}