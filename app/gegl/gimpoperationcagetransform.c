/* GIMP - The GNU Image Manipulation Program
 *
 * gimpoperationcage.c
 * Copyright (C) 2010 Michael Muré <batolettre@gmail.com>
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
#include <gegl-buffer-iterator.h>

#include "libgimpcolor/gimpcolor.h"
#include "libgimpmath/gimpmath.h"

#include "gimp-gegl-types.h"

#include "gimpoperationcagetransform.h"
#include "gimpcageconfig.h"


static void           gimp_operation_cage_transform_finalize                  (GObject              *object);
static void           gimp_operation_cage_transform_get_property              (GObject              *object,
                                                                               guint                 property_id,
                                                                               GValue               *value,
                                                                               GParamSpec           *pspec);
static void           gimp_operation_cage_transform_set_property              (GObject              *object,
                                                                               guint                 property_id,
                                                                               const GValue         *value,
                                                                               GParamSpec           *pspec);
static void           gimp_operation_cage_transform_prepare                   (GeglOperation        *operation);
static gboolean       gimp_operation_cage_transform_process                   (GeglOperation        *operation,
                                                                               GeglBuffer           *in_buf,
                                                                               GeglBuffer           *aux_buf,
                                                                               GeglBuffer           *out_buf,
                                                                               const GeglRectangle  *roi);
static void           gimp_operation_cage_transform_interpolate_source_coords_recurs
                                                                              (GimpOperationCageTransform  *oct,
                                                                               GeglBuffer           *out_buf,
                                                                               const GeglRectangle  *roi,
                                                                               GimpVector2           p1_s,
                                                                               GimpVector2           p1_d,
                                                                               GimpVector2           p2_s,
                                                                               GimpVector2           p2_d,
                                                                               GimpVector2           p3_s,
                                                                               GimpVector2           p3_d,
                                                                               gint                  recursion_depth,
                                                                               gfloat               *coords);
static GimpVector2    gimp_cage_transform_compute_destination                 (GimpCageConfig       *config,
                                                                               GeglBuffer           *coef_buf,
                                                                               GimpVector2           coords);
GeglRectangle         gimp_operation_cage_transform_get_cached_region         (GeglOperation        *operation,
                                                                               const GeglRectangle  *roi);
GeglRectangle         gimp_operation_cage_transform_get_required_for_output   (GeglOperation        *operation,
                                                                               const gchar          *input_pad,
                                                                               const GeglRectangle  *roi);
GeglRectangle         gimp_operation_cage_transform_get_bounding_box          (GeglOperation        *operation);

G_DEFINE_TYPE (GimpOperationCageTransform, gimp_operation_cage_transform,
                      GEGL_TYPE_OPERATION_COMPOSER)

#define parent_class gimp_operation_cage_transform_parent_class




static void
gimp_operation_cage_transform_class_init (GimpOperationCageTransformClass *klass)
{
  GObjectClass               *object_class    = G_OBJECT_CLASS (klass);
  GeglOperationClass         *operation_class = GEGL_OPERATION_CLASS (klass);
  GeglOperationComposerClass *filter_class    = GEGL_OPERATION_COMPOSER_CLASS (klass);

  object_class->get_property          = gimp_operation_cage_transform_get_property;
  object_class->set_property          = gimp_operation_cage_transform_set_property;
  object_class->finalize              = gimp_operation_cage_transform_finalize;

  /* FIXME: wrong categories and name, to appears in the gegl tool */
  operation_class->name         = "gimp:cage_transform";
  operation_class->categories   = "transform";
  operation_class->description  = "GIMP cage reverse transform";

  operation_class->prepare      = gimp_operation_cage_transform_prepare;

  operation_class->get_required_for_output  = gimp_operation_cage_transform_get_required_for_output;
  operation_class->get_cached_region        = gimp_operation_cage_transform_get_cached_region;
  operation_class->no_cache                 = FALSE;
  operation_class->get_bounding_box         = gimp_operation_cage_transform_get_bounding_box;

  filter_class->process         = gimp_operation_cage_transform_process;

  g_object_class_install_property (object_class,
                                   GIMP_OPERATION_CAGE_TRANSFORM_PROP_CONFIG,
                                   g_param_spec_object ("config", NULL, NULL,
                                                        GIMP_TYPE_CAGE_CONFIG,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT));

  g_object_class_install_property (object_class, GIMP_OPERATION_CAGE_TRANSFORM_PROP_FILL,
                                   g_param_spec_boolean ("fill_plain_color",
                                                         "Blocking render",
                                                         "Fill the original position of the cage with a plain color",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
}

static void
gimp_operation_cage_transform_init (GimpOperationCageTransform *self)
{
  self->format_coords = babl_format_n(babl_type("float"), 2);
}

static void
gimp_operation_cage_transform_finalize  (GObject  *object)
{
  GimpOperationCageTransform *self = GIMP_OPERATION_CAGE_TRANSFORM (object);

  if (self->config)
    {
      g_object_unref (self->config);
      self->config = NULL;
    }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gimp_operation_cage_transform_get_property (GObject    *object,
                                            guint       property_id,
                                            GValue     *value,
                                            GParamSpec *pspec)
{
  GimpOperationCageTransform *self = GIMP_OPERATION_CAGE_TRANSFORM (object);

  switch (property_id)
    {
    case GIMP_OPERATION_CAGE_TRANSFORM_PROP_CONFIG:
      g_value_set_object (value, self->config);
      break;
    case GIMP_OPERATION_CAGE_TRANSFORM_PROP_FILL:
      g_value_set_boolean (value, self->fill_plain_color);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gimp_operation_cage_transform_set_property (GObject      *object,
                                            guint         property_id,
                                            const GValue *value,
                                            GParamSpec   *pspec)
{
  GimpOperationCageTransform *self = GIMP_OPERATION_CAGE_TRANSFORM (object);

  switch (property_id)
    {
    case GIMP_OPERATION_CAGE_TRANSFORM_PROP_CONFIG:
      if (self->config)
        g_object_unref (self->config);
      self->config = g_value_dup_object (value);
      break;
    case GIMP_OPERATION_CAGE_TRANSFORM_PROP_FILL:
      self->fill_plain_color = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
gimp_operation_cage_transform_prepare (GeglOperation  *operation)
{
  GimpOperationCageTransform *oct    = GIMP_OPERATION_CAGE_TRANSFORM (operation);
  GimpCageConfig             *config = GIMP_CAGE_CONFIG (oct->config);

  gegl_operation_set_format (operation, "input",
                             babl_format_n (babl_type ("float"),
                                            2 * config->cage_vertice_number));
  gegl_operation_set_format (operation, "output",
                             babl_format_n (babl_type ("float"), 2));
}

static gboolean
gimp_operation_cage_transform_process (GeglOperation       *operation,
                                       GeglBuffer          *in_buf,
                                       GeglBuffer          *aux_buf,
                                       GeglBuffer          *out_buf,
                                       const GeglRectangle *roi)
{
  GimpOperationCageTransform *oct    = GIMP_OPERATION_CAGE_TRANSFORM (operation);
  GimpCageConfig             *config = GIMP_CAGE_CONFIG (oct->config);

  gint x, y;
  GeglRectangle cage_bb = gimp_cage_config_get_bounding_box (config);
  gfloat *coords = g_slice_alloc ( 2 * sizeof (gfloat));
  GimpVector2 p1_d, p2_d, p3_d, p4_d;
  GimpVector2 p1_s, p2_s, p3_s, p4_s;
  GimpVector2 plain_color;

  /* pre-fill the out buffer with no-displacement coordinate */
  GeglBufferIterator *it = gegl_buffer_iterator_new (out_buf, roi, NULL, GEGL_BUFFER_WRITE);

  plain_color.x = (gint) config->cage_vertices[0].x;
  plain_color.y = (gint) config->cage_vertices[0].y;

  while (gegl_buffer_iterator_next (it))
    {
      /* iterate inside the roi */
      gint        n_pixels = it->length;
      gint        x = it->roi->x; /* initial x                   */
      gint        y = it->roi->y; /*           and y coordinates */

      gfloat      *output = it->data[0];

      while(n_pixels--)
        {
          if (oct->fill_plain_color && gimp_cage_config_point_inside(config, x, y))
            {
              output[0] = plain_color.x;
              output[1] = plain_color.y;
            }
          else
            {
              output[0] = x;
              output[1] = y;
            }

          output += 2;

          /* update x and y coordinates */
          x++;
          if (x >= (it->roi->x + it->roi->width))
            {
              x = it->roi->x;
              y++;
            }
        }
    }

  /* compute, reverse and interpolate the transformation */
  for (x = cage_bb.x; x < cage_bb.x + cage_bb.width - 1; x++)
    {
      p1_s.x = x;
      p2_s.x = x+1;
      p3_s.x = x+1;
      p3_s.y = cage_bb.y;
      p4_s.x = x;
      p4_s.y = cage_bb.y;

      p3_d = gimp_cage_transform_compute_destination (config, aux_buf, p3_s);
      p4_d = gimp_cage_transform_compute_destination (config, aux_buf, p4_s);

      for (y = cage_bb.y; y < cage_bb.y + cage_bb.height - 1; y++)
        {
          p1_s = p4_s;
          p2_s = p3_s;
          p3_s.y = y+1;
          p4_s.y = y+1;

          p1_d = p4_d;
          p2_d = p3_d;
          p3_d = gimp_cage_transform_compute_destination (config, aux_buf, p3_s);
          p4_d = gimp_cage_transform_compute_destination (config, aux_buf, p4_s);

          if (gimp_cage_config_point_inside(config, x, y))
            {
              gimp_operation_cage_transform_interpolate_source_coords_recurs  (oct,
                                                                               out_buf,
                                                                               roi,
                                                                               p1_s, p1_d,
                                                                               p2_s, p2_d,
                                                                               p3_s, p3_d,
                                                                               0,
                                                                               coords);

              gimp_operation_cage_transform_interpolate_source_coords_recurs  (oct,
                                                                               out_buf,
                                                                               roi,
                                                                               p1_s, p1_d,
                                                                               p3_s, p3_d,
                                                                               p4_s, p4_d,
                                                                               0,
                                                                               coords);
            }
        }
    }

  g_slice_free1 (2 * sizeof (gfloat), coords);

  return TRUE;
}


static void
gimp_operation_cage_transform_interpolate_source_coords_recurs (GimpOperationCageTransform  *oct,
                                                                GeglBuffer *out_buf,
                                                                const GeglRectangle *roi,
                                                                GimpVector2 p1_s,
                                                                GimpVector2 p1_d,
                                                                GimpVector2 p2_s,
                                                                GimpVector2 p2_d,
                                                                GimpVector2 p3_s,
                                                                GimpVector2 p3_d,
                                                                gint recursion_depth,
                                                                gfloat *coords)
{
  gint xmin, xmax, ymin, ymax;
  GeglRectangle rect = { 0, 0, 1, 1 };

  if (p1_d.x > roi->x + roi->width) return;
  if (p2_d.x > roi->x + roi->width) return;
  if (p3_d.x > roi->x + roi->width) return;
  if (p1_d.y > roi->y + roi->height) return;
  if (p2_d.y > roi->y + roi->height) return;
  if (p3_d.y > roi->y + roi->height) return;

  if (p1_d.x <= roi->x) return;
  if (p2_d.x <= roi->x) return;
  if (p3_d.x <= roi->x) return;
  if (p1_d.y <= roi->y) return;
  if (p2_d.y <= roi->y) return;
  if (p3_d.y <= roi->y) return;

  xmin = xmax = p1_d.x;
  ymin = ymax = p1_d.y;

  if (xmin > p2_d.x) xmin = p2_d.x;
  if (xmin > p3_d.x) xmin = p3_d.x;

  if (xmax < p2_d.x) xmax = p2_d.x;
  if (xmax < p3_d.x) xmax = p3_d.x;

  if (ymin > p2_d.y) ymin = p2_d.y;
  if (ymin > p3_d.y) ymin = p3_d.y;

  if (ymax < p2_d.y) ymax = p2_d.y;
  if (ymax < p3_d.y) ymax = p3_d.y;

  /* test if there is no more pixel in the triangle */
  if (xmin == xmax || ymin == ymax)
    return;

  /* test if the triangle is implausibly large as manifested by too deep recursion */
  if (recursion_depth > 5)
    return;

  /* test if the triangle is small enough.
   * if yes, we compute the coefficient of the barycenter for the pixel (x,y) and see if a pixel is inside (ie the 3 coef have the same sign).
   */
  if (xmax - xmin == 1 && ymax - ymin == 1)
    {
      gdouble a, b, c, denom, x, y;

      rect.x = xmax;
      rect.y = ymax;

      x = (gdouble) xmax;
      y = (gdouble) ymax;

      denom = (p2_d.x - p1_d.x) * p3_d.y + (p1_d.x - p3_d.x) * p2_d.y + (p3_d.x - p2_d.x) * p1_d.y;
      a = ((p2_d.x - x) * p3_d.y + (x - p3_d.x) * p2_d.y + (p3_d.x - p2_d.x) * y) / denom;
      b = - ((p1_d.x - x) * p3_d.y + (x - p3_d.x) * p1_d.y + (p3_d.x - p1_d.x) * y) / denom;
      c = 1.0 - a - b;

      /* if a pixel is inside, we compute his source coordinate and set it in the output buffer */
      if ((a > 0 && b > 0 && c > 0) || (a < 0 && b < 0 && c < 0))
        {
          coords[0] = (a * p1_s.x + b * p2_s.x + c * p3_s.x);
          coords[1] = (a * p1_s.y + b * p2_s.y + c * p3_s.y);

          gegl_buffer_set (out_buf,
                           &rect,
                           oct->format_coords,
                           coords,
                           GEGL_AUTO_ROWSTRIDE);
        }

      return;
    }
  else
    {
      /* we cut the triangle in 4 sub-triangle and treat it recursively */
      /*
       *       /\
       *      /__\
       *     /\  /\
       *    /__\/__\
       *
       */

      GimpVector2 pm1_d, pm2_d, pm3_d;
      GimpVector2 pm1_s, pm2_s, pm3_s;

      gint next_depth = recursion_depth + 1;

      pm1_d.x = (p1_d.x + p2_d.x) / 2.0;
      pm1_d.y = (p1_d.y + p2_d.y) / 2.0;

      pm2_d.x = (p2_d.x + p3_d.x) / 2.0;
      pm2_d.y = (p2_d.y + p3_d.y) / 2.0;

      pm3_d.x = (p3_d.x + p1_d.x) / 2.0;
      pm3_d.y = (p3_d.y + p1_d.y) / 2.0;

      pm1_s.x = (p1_s.x + p2_s.x) / 2.0;
      pm1_s.y = (p1_s.y + p2_s.y) / 2.0;

      pm2_s.x = (p2_s.x + p3_s.x) / 2.0;
      pm2_s.y = (p2_s.y + p3_s.y) / 2.0;

      pm3_s.x = (p3_s.x + p1_s.x) / 2.0;
      pm3_s.y = (p3_s.y + p1_s.y) / 2.0;

      gimp_operation_cage_transform_interpolate_source_coords_recurs (oct,
                                                                      out_buf,
                                                                      roi,
                                                                      p1_s, p1_d,
                                                                      pm1_s, pm1_d,
                                                                      pm3_s, pm3_d,
                                                                      next_depth,
                                                                      coords);

      gimp_operation_cage_transform_interpolate_source_coords_recurs (oct,
                                                                      out_buf,
                                                                      roi,
                                                                      pm1_s, pm1_d,
                                                                      p2_s, p2_d,
                                                                      pm2_s, pm2_d,
                                                                      next_depth,
                                                                      coords);

      gimp_operation_cage_transform_interpolate_source_coords_recurs (oct,
                                                                      out_buf,
                                                                      roi,
                                                                      pm1_s, pm1_d,
                                                                      pm2_s, pm2_d,
                                                                      pm3_s, pm3_d,
                                                                      next_depth,
                                                                      coords);

      gimp_operation_cage_transform_interpolate_source_coords_recurs (oct,
                                                                      out_buf,
                                                                      roi,
                                                                      pm3_s, pm3_d,
                                                                      pm2_s, pm2_d,
                                                                      p3_s, p3_d,
                                                                      next_depth,
                                                                      coords);
    }
}

static GimpVector2
gimp_cage_transform_compute_destination (GimpCageConfig *config,
                                         GeglBuffer     *coef_buf,
                                         GimpVector2     coords)
{
  gfloat        *coef;
  gdouble        pos_x, pos_y;
  gint           i;
  GeglRectangle  rect;
  GimpVector2    result;
  gint           cvn = config->cage_vertice_number;
  Babl          *format_coef = babl_format_n (babl_type ("float"), 2 * cvn);

  rect.height = 1;
  rect.width  = 1;
  rect.x      = coords.x;
  rect.y      = coords.y;

  coef = g_malloc (config->cage_vertice_number * 2 * sizeof(gfloat));

  gegl_buffer_get (coef_buf, 1, &rect, format_coef, coef, GEGL_AUTO_ROWSTRIDE);

  pos_x = 0;
  pos_y = 0;

  for (i = 0; i < cvn; i++)
    {
      pos_x += coef[i] * config->cage_vertices_d[i].x;
      pos_y += coef[i] * config->cage_vertices_d[i].y;
    }

  for (i = 0; i < cvn; i++)
    {
      pos_x += coef[i + cvn] * config->scaling_factor[i] * config->normal_d[i].x;
      pos_y += coef[i + cvn] * config->scaling_factor[i] * config->normal_d[i].y;
    }

  result.x = pos_x;
  result.y = pos_y;

  g_free (coef);

  return result;
}

GeglRectangle
gimp_operation_cage_transform_get_cached_region (GeglOperation       *operation,
                                                 const GeglRectangle *roi)
{
  GeglRectangle result = *gegl_operation_source_get_bounding_box (operation,
                                                                  "input");

  return result;
}

GeglRectangle
gimp_operation_cage_transform_get_required_for_output (GeglOperation       *operation,
                                                       const gchar         *input_pad,
                                                       const GeglRectangle *roi)
{
  GeglRectangle result = *gegl_operation_source_get_bounding_box (operation,
                                                                  "input");

  return result;
}

GeglRectangle
gimp_operation_cage_transform_get_bounding_box (GeglOperation *operation)
{
  GeglRectangle result = *gegl_operation_source_get_bounding_box (operation,
                                                                  "input");

  return result;
}