// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2023 Collabora Ltd
// Copyright © 2023 Valve Corporation

// steamos-efi is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2.0 of the License, or
// (at your option) any later version.

// steamos-efi is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with steamos-efi.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include "gfx.h"

typedef enum
{
    DECOR_NONE      = 0,
    DECOR_UNDERLINE = 0x1,
    DECOR_OVERLINE  = 0x1 << 1,
    DECOR_BOXED     = 0x1 << 2,
} FONT_DECORATION;

typedef struct _FONT FONT;

typedef struct _GLYPH
{
    FONT  *font;
    UINT16 width;
    UINT16 height;
    struct { INT16 x; INT16 y; } offset;
    UINT16 device_width;
  /* Row-major order
     No row padding; rows can break within a byte.
     length is (width * height + 7) / 8.
     The most significant bit in each byte is the (left/upper)-most pixel.
     Pixels are bits, 1:opaque, 0:transparent.
     Array will be 0 padded to make it end on a byte boundary.
  */
    UINT8 bitmap[0];
} GLYPH;

typedef struct char_index_entry
{
    UINT32 code;
    UINT8  storage_flags;
    UINT32 offset;
    /* Glyph if loaded, or NULL otherwise.  */
    GLYPH *glyph;
} CHAR_INDEX_ENTRY;

typedef GLYPH * (*FONT_GET_GLYPH)(FONT *font, UINT32 codepoint);

typedef struct _FONT
{
    CHAR8 *name;
    CHAR8 *family;
    UINT16 point;
    UINT16 weight;
    struct { UINT16 width; UINT16 height; } max;
    UINT16 ascent;
    UINT16 descent;
    UINT16 leading;
    UINT32 chars;
    BOOLEAN bad; // something went wrong and the font should be ignored
    CHAR_INDEX_ENTRY *chr_index;
    UINT16 *bmp_index;
    EFI_FILE_PROTOCOL *file;
    FONT_GET_GLYPH lookup_glyph;
    BLIT_BUFFER blit_buffer;
} FONT;

typedef struct _FONT_NODE FONT_NODE;
typedef struct _FONT_NODE
{
  FONT_NODE *next;
  FONT *font;
} FONT_NODE;


FONT *font_load (EFI_FILE_PROTOCOL *dir, CHAR16 *path);
VOID unload_fonts (VOID);
GLYPH *font_get_glyph (FONT *font, UINT32 cp);

const CHAR8 *font_name   (FONT *f);
const CHAR8 *font_family (FONT *f);

UINT16 font_ascent     (FONT *f);
UINT16 font_descent    (FONT *f);
UINT16 font_max_width  (FONT *f);
UINT16 font_max_height (FONT *f);
UINT16 font_leading    (FONT *f);
UINT16 font_height     (FONT *f);
UINT16 font_xheight    (FONT *f, UINT16 *ssize);

UINT16 font_string_display_size (FONT *font, CONST CHAR16 *str,
                                 UINT16 *width, UINT16 *height);

EFI_STATUS font_draw_glyph_at_xy (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                                  GLYPH *glyph,
                                  UINT32 triplet,
                                  UINT32 x,
                                  UINT32 y);

UINT16 font_output_text (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                         FONT *font,
                         CHAR16 *txt,
                         UINT16 c_limit,
                         UINT16 x, UINT16 y,
                         UINT32 triplet,
                         FONT_DECORATION decor,
                         UINT16 *dx, UINT16 *dy);

VOID debug_glyph(UINT32 cp);
VOID font_demo_text_display (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx);
