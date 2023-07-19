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

#define FONT_CODE_CHAR_MASK     0x001fffff
#define FONT_CODE_RIGHT_JOINED  0x80000000
#define FONT_CODE_LEFT_JOINED   0x40000000

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
    CHAR_INDEX_ENTRY *chr_index;
    UINT16 *bmp_index;
    EFI_FILE_PROTOCOL *file;
} FONT;

typedef struct _FONT_NODE FONT_NODE;
typedef struct _FONT_NODE
{
  FONT_NODE *next;
  FONT *value;
} FONT_NODE;


VOID font_system_init (VOID);
FONT *font_load (EFI_FILE_PROTOCOL *dir, CHAR16 *path);
