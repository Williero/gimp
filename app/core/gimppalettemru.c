/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * gimppalettemru.c
 * Copyright (C) 2014 Michael Natterer <mitch@gimp.org>
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

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gegl.h>

#include "libgimpcolor/gimpcolor.h"
#include "libgimpconfig/gimpconfig.h"

#include "core-types.h"

#include "gimppalettemru.h"

#include "gimp-intl.h"


enum
{
  COLOR_HISTORY = 1
};


G_DEFINE_TYPE (GimpPaletteMru, gimp_palette_mru, GIMP_TYPE_PALETTE)

#define parent_class gimp_palette_mru_parent_class


static void
gimp_palette_mru_class_init (GimpPaletteMruClass *klass)
{
}

static void
gimp_palette_mru_init (GimpPaletteMru *palette)
{
}


/*  public functions  */

GimpData *
gimp_palette_mru_new (const gchar *name)
{
  GimpPaletteMru *palette;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (*name != '\0', NULL);

  palette = g_object_new (GIMP_TYPE_PALETTE_MRU,
                          "name",      name,
                          "mime-type", "application/x-gimp-palette",
                          NULL);

  return GIMP_DATA (palette);
}

void
gimp_palette_mru_load (GimpPaletteMru *mru,
                       GFile          *file)
{
  GimpPalette *palette;
  GScanner    *scanner;
  GTokenType   token;

  g_return_if_fail (GIMP_IS_PALETTE_MRU (mru));
  g_return_if_fail (G_IS_FILE (file));

  palette = GIMP_PALETTE (mru);

  scanner = gimp_scanner_new_gfile (file, NULL);
  if (! scanner)
    return;

  g_scanner_scope_add_symbol (scanner, 0, "color-history",
                              GINT_TO_POINTER (COLOR_HISTORY));

  token = G_TOKEN_LEFT_PAREN;

  while (g_scanner_peek_next_token (scanner) == token)
    {
      token = g_scanner_get_next_token (scanner);

      switch (token)
        {
        case G_TOKEN_LEFT_PAREN:
          token = G_TOKEN_SYMBOL;
          break;

        case G_TOKEN_SYMBOL:
          if (scanner->value.v_symbol == GINT_TO_POINTER (COLOR_HISTORY))
            {
              while (g_scanner_peek_next_token (scanner) == G_TOKEN_LEFT_PAREN)
                {
                  GimpRGB color;

                  if (! gimp_scanner_parse_color (scanner, &color))
                    goto error;

                  gimp_palette_add_entry (palette, -1,
                                          _("History Color"), &color);
                }
            }
          token = G_TOKEN_RIGHT_PAREN;
          break;

        case G_TOKEN_RIGHT_PAREN:
          token = G_TOKEN_LEFT_PAREN;
          break;

        default: /* do nothing */
          break;
        }
    }

 error:
  gimp_scanner_destroy (scanner);
}

void
gimp_palette_mru_save (GimpPaletteMru *mru,
                       GFile          *file)
{
  GimpPalette      *palette;
  GimpConfigWriter *writer;
  GList            *list;

  g_return_if_fail (GIMP_IS_PALETTE_MRU (mru));
  g_return_if_fail (G_IS_FILE (file));

  writer = gimp_config_writer_new_gfile (file,
                                         TRUE,
                                         "GIMP colorrc\n\n"
                                         "This file holds a list of "
                                         "recently used colors.",
                                         NULL);
  if (! writer)
    return;

  palette = GIMP_PALETTE (mru);

  gimp_config_writer_open (writer, "color-history");

  for (list = palette->colors; list; list = g_list_next (list))
    {
      GimpPaletteEntry *entry = list->data;
      gchar             buf[4][G_ASCII_DTOSTR_BUF_SIZE];

      g_ascii_formatd (buf[0],
                       G_ASCII_DTOSTR_BUF_SIZE, "%f", entry->color.r);
      g_ascii_formatd (buf[1],
                       G_ASCII_DTOSTR_BUF_SIZE, "%f", entry->color.g);
      g_ascii_formatd (buf[2],
                       G_ASCII_DTOSTR_BUF_SIZE, "%f", entry->color.b);
      g_ascii_formatd (buf[3],
                       G_ASCII_DTOSTR_BUF_SIZE, "%f", entry->color.a);

      gimp_config_writer_open (writer, "color-rgba");
      gimp_config_writer_printf (writer, "%s %s %s %s",
                                 buf[0], buf[1], buf[2], buf[3]);
      gimp_config_writer_close (writer);
    }

  gimp_config_writer_close (writer);

  gimp_config_writer_finish (writer, "end of colorrc", NULL);
}

void
gimp_palette_mru_add (GimpPaletteMru *mru,
                      const GimpRGB  *color)
{
  GimpPalette      *palette;
  GimpPaletteEntry *found = NULL;
  GList            *list;

  g_return_if_fail (GIMP_IS_PALETTE_MRU (mru));
  g_return_if_fail (color != NULL);

  palette = GIMP_PALETTE (mru);

  /*  is the added color already there?  */
  for (list = gimp_palette_get_colors (palette);
       list;
       list = g_list_next (list))
    {
      GimpPaletteEntry *entry = list->data;

      if (gimp_rgba_distance (&entry->color, color) < 0.0001)
        {
          found = entry;

          goto doit;
        }
    }

  /*  if not, are there two equal colors?  */
  if (! found)
    {
      for (list = gimp_palette_get_colors (palette);
           list;
           list = g_list_next (list))
        {
          GimpPaletteEntry *entry = list->data;
          GList            *list2;

          for (list2 = g_list_next (list); list2; list2 = g_list_next (list2))
            {
              GimpPaletteEntry *entry2 = list2->data;

              if (gimp_rgba_distance (&entry->color,
                                      &entry2->color) < 0.0001)
                {
                  found = entry2;

                  goto doit;
                }
            }
        }
    }

 doit:

  if (found)
    gimp_palette_delete_entry (palette, found);

  found = gimp_palette_add_entry (palette, 0, _("History Color"), color);
}