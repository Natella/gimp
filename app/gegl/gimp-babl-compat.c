/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimp-babl-compat.h
 * Copyright (C) 2012 Michael Natterer <mitch@gimp.org>
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

#include "gimp-gegl-types.h"

#include "gimp-babl-compat.h"


GimpImageType
gimp_babl_format_get_image_type (const Babl *format)
{
  g_return_val_if_fail (format != NULL, -1);

  if (format == babl_format ("Y u8")   ||
      format == babl_format ("Y' u8")  ||
      format == babl_format ("Y u16")  ||
      format == babl_format ("Y float"))
    {
      return GIMP_GRAY_IMAGE;
    }
  else if (format == babl_format ("Y'A u8")  ||
           format == babl_format ("YA u16")  ||
           format == babl_format ("YA float"))
    {
      return GIMP_GRAYA_IMAGE;
    }
  else if (format == babl_format ("R'G'B' u8") ||
           format == babl_format ("RGB u16")   ||
           format == babl_format ("RGB float"))
    {
      return GIMP_RGB_IMAGE;
    }
  else if (format == babl_format ("R'G'B'A u8") ||
           format == babl_format ("RGBA u16")   ||
           format == babl_format ("RGBA float"))
    {
      return GIMP_RGBA_IMAGE;
    }
  else if (babl_format_is_palette (format))
    {
      if (babl_format_has_alpha (format))
        return GIMP_INDEXEDA_IMAGE;
      else
        return GIMP_INDEXED_IMAGE;
    }

  g_return_val_if_reached (-1);
}