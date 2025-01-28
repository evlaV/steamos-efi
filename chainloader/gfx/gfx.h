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

// NOTE: Many of these functions return an EFI_STATUS, which will always be
// EFI_SUCCESS if the call worked. Thanks to their nature as part of the
// grpahics drawing infrastructure it may not be practical to check their
// return types everywhere but the return type is provided to aid in
// development and debugging.

#pragma once

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include "../util.h"

typedef struct _blit_buffer
{
    UINT32 len;
    UINT32 *data;
} BLIT_BUFFER;

EFI_STATUS
gfx_get_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
              UINT32 mode,
              UINTN *size,
              EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info);

EFI_STATUS gfx_dump_modes (VOID);

EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx_get_interface (VOID);

UINT32     gfx_max_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx);

EFI_STATUS gfx_get_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                         UINT32 mode,
                         UINTN *size,
                         EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info);

EFI_STATUS gfx_set_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 mode);

EFI_STATUS gfx_mode_supported (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 mode);

UINT32     gfx_mode_score     (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 mode);

EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *
gfx_current_mode_info (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx);

UINT32 gfx_get_mode_resolution (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info,
                                UINT32 *x,
                                UINT32 *y,
                                UINT32 *s);

UINT32 gfx_current_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx);

UINT32 gfx_current_resolution (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                               UINT16 *x, UINT16 *y);
UINT16 gfx_current_stride (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx);

VOID gfx_clear_blitbuffer (BLIT_BUFFER *bbuf);
EFI_STATUS gfx_dealloc_blitbuffer (BLIT_BUFFER *bbuf);
EFI_STATUS gfx_alloc_blitbuffer (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                                 BLIT_BUFFER *bbuf, UINT16 w, UINT16 h);


EFI_STATUS gfx_fill_rectangle (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                               UINT32 triplet,
                               UINT16 x, UINT16 y,
                               UINT16 w, UINT16 h);

EFI_STATUS gfx_fill_screen (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 triplet);

// a fill value greater than 0xffffff (eg (UINT32)-1) means "do not fill"
EFI_STATUS gfx_draw_box(EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                        UINT16 x, UINT16 y,
                        UINT16 width, UINT16 height,
                        UINT32 border, UINT32 fill);

EFI_STATUS gfx_blit_out (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                         BLIT_BUFFER *bbuf, // bitmap source
                         UINT16 width,      // source pixel width
                         UINT16 height,     // source pixel height
                         UINT16 x,          // x-coord (origin at left)
                         UINT16 y);         // y-coord (origin at top)

EFI_STATUS
gfx_blit_in (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
             BLIT_BUFFER *bbuf,
             UINT16 width,  // source pixel width
             UINT16 height, // source pixel height
             UINT16 x,      // x-coord (origin at left)
             UINT16 y);     // y-coord (origin at top)

EFI_STATUS gfx_convert_bitmap (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                               UINT8 *src,
                               UINT16 width,
                               UINT16 height,
                               UINT16 bpp,
                               UINT32 triplet,
                               BLIT_BUFFER *bbuf,
                               UINT16 bbuf_width,
                               UINT16 bbuf_height,
                               UINT16 x_offset,
                               UINT16 y_offset);
