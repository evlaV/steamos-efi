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

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#include "../err.h"
#include "../util.h"
#include "gfx.h"

static EFI_GUID gfx_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

// Per UEFI spec § 1.9.1
// https://uefi.org/specs/UEFI/2.10/01_Introduction.html#data-structure-descriptions
// The only processors and operating modes considered are little endian.
// This code would likely be broken on a big endian architecture but UEFI
// states that it does not support such machines anyway.

#define TRIPLET_RGBX(x) \
    (uint32_t)( 0xff000000              | \
                ((x & 0xff0000) >> 16)  | \
                (x & 0xff00)            | \
                ((x & 0xff) << 16) )

#define TRIPLET_BGRX(x) \
    (uint32_t)( 0xff000000 | x )

static UINT32
triplet_to_fbfmt (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 triplet)
{
    switch( gfx->Mode->Info->PixelFormat )
    {
      case PixelRedGreenBlueReserved8BitPerColor:
        return TRIPLET_RGBX(triplet);

      case PixelBlueGreenRedReserved8BitPerColor:
        return TRIPLET_BGRX(triplet);

      default:
        // just return the uint32 and hope for the best:
        DEBUG_LOG( "Unsupported framebuffer pixel format" );
        return triplet;
    }
}

EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx_get_interface (VOID)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx = NULL;
    EFI_STATUS res = get_protocol( &gfx_guid, NULL, (VOID **)&gfx );

    ERROR_RETURN(res, NULL, L"Looking for graphics interface" );

    return gfx;
}

EFI_STATUS gfx_get_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                         UINT32 mode,
                         UINTN *size,
                         EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info)
{
    EFI_STATUS res = EFI_SUCCESS;

    res = uefi_call_wrapper( gfx->QueryMode, 4, gfx, mode, size, info );
    ERROR_RETURN( res, res, L"Getting info for mode %d", mode );

    return res;
}

EFI_STATUS gfx_mode_supported (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 mode)
{
    UINTN size;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
    EFI_STATUS rc;

    rc = gfx_get_mode( gfx, mode, &size, &info );
    ERROR_RETURN( rc, rc, L"No graphics mode #%d", mode );

    switch ( info->PixelFormat )
    {
      case PixelRedGreenBlueReserved8BitPerColor:
      case PixelBlueGreenRedReserved8BitPerColor:
        break;

      default:
        ERROR_RETURN( EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER,
                      L"Unsupported pixel format %d", info->PixelFormat );
    }

    return EFI_SUCCESS;
}

EFI_STATUS gfx_set_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 mode)
{
    EFI_STATUS res = EFI_SUCCESS;

    res = uefi_call_wrapper( gfx->SetMode, 2, gfx, mode );
    ERROR_RETURN( res, res, L"Setting graphical mode %d", mode );

    return res;
}

EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *
gfx_current_mode_info (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx)
{
    return gfx->Mode->Info;
}

UINT32 gfx_current_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx)
{
    return gfx->Mode->Mode;
}

UINT32 gfx_current_resolution (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                               UINT16 *x, UINT16 *y)
{
    if( x )
        *x = gfx->Mode->Info->HorizontalResolution;

    if( y )
        *y = gfx->Mode->Info->VerticalResolution;

    return ( gfx->Mode->Info->VerticalResolution *
             gfx->Mode->Info->HorizontalResolution );
}

UINT16 gfx_current_stride (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx)
{
    return gfx->Mode->Info->PixelsPerScanLine;
}

EFI_STATUS
gfx_fill_rectangle (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                    UINT32 triplet,
                    UINT16 x, UINT16 y, UINT16 w, UINT16 h)
{
    union { EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel; UINT32 value; } fill;

    fill.value = triplet_to_fbfmt( gfx, triplet );

    return uefi_call_wrapper( gfx->Blt, 10,
                              gfx,
                              &fill.pixel,
                              EfiBltVideoFill,
                              0, 0, // from coords in bbuf (origin top left)
                              x, y, // to coords on screen (origin at top left)
                              w, h,
                              0 );

}

EFI_STATUS
gfx_fill_screen (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, UINT32 triplet)
{
    UINT16 w, h;

    gfx_current_resolution( gfx, &w, &h );

    return gfx_fill_rectangle( gfx, triplet, 0, 0, w, h );
}

EFI_STATUS
gfx_dealloc_blitbuffer (BLIT_BUFFER *bbuf)
{
    if( bbuf->len )
    {
        mem_set( bbuf->data, 0, bbuf->len * sizeof(*(bbuf->data)) );
        bbuf->len  = 0;
        bbuf->data = NULL;
    }

    return EFI_SUCCESS;
}

VOID
gfx_clear_blitbuffer (BLIT_BUFFER *bbuf)
{
    if( bbuf->len )
        mem_set( bbuf->data, 0, bbuf->len * sizeof(*(bbuf->data)) );
}

VOID
gfx_fill_blitbuffer (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                     BLIT_BUFFER *bbuf, UINT32 triplet)
{
    UINT32 fill = triplet_to_fbfmt( gfx, triplet );
    //DEBUG_LOG("filling with #%08x", fill);

    for( UINT32 i = 0; i < bbuf->len; i++ )
        bbuf->data[ i ] = fill;
}

EFI_STATUS
gfx_alloc_blitbuffer (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                      BLIT_BUFFER *bbuf, UINT16 w, UINT16 h)
{
    UINT32 len = 0;

    if( w > gfx->Mode->Info->HorizontalResolution ||
        h > gfx->Mode->Info->VerticalResolution   )
        ERROR_RETURN( EFI_OUT_OF_RESOURCES, EFI_OUT_OF_RESOURCES,
                      L"blit buffer out of bounds {%d x %d} vs {%d x %d}",
                      w, h,
                      gfx->Mode->Info->HorizontalResolution,
                      gfx->Mode->Info->VerticalResolution  );

    switch( gfx->Mode->Info->PixelFormat )
    {
      case PixelRedGreenBlueReserved8BitPerColor:
      case PixelBlueGreenRedReserved8BitPerColor:
        len = w * h;
        break;
      default:
        ERROR_RETURN( EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER,
                      L"Unsupported pixel format (not RGBx or BGRx)" );
    }

    if( len == 0 )
        return EFI_INVALID_PARAMETER;

    if( bbuf->len == len )
        return EFI_SUCCESS;

    if( bbuf->len )
    {
        gfx_clear_blitbuffer( bbuf );
        efi_free( bbuf->data );
    }

    bbuf->data = efi_alloc( len * sizeof(*(bbuf->data)) );
    bbuf->len  = bbuf->data ? len : 0;

    if( bbuf->data == NULL )
        ERROR_RETURN( EFI_OUT_OF_RESOURCES, EFI_OUT_OF_RESOURCES,
                      L"Failed to allocate %d x %d blitbuffer", w, h );

    return EFI_SUCCESS;
}

static inline BOOLEAN
nth_bit_is_set (UINT8 *src, UINT32 offset)
{
    return (src[offset / 8] & (0x80 >> (offset % 8))) ? 1 : 0;
}

EFI_STATUS
gfx_convert_bitmap (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                    UINT8 *src,
                    UINT16 width,
                    UINT16 height,
                    UINT16 bpp,
                    UINT32 triplet,
                    BLIT_BUFFER *bbuf,
                    UINT16 bbuf_width,
                    UINT16 bbuf_height,
                    UINT16 x_offset,
                    UINT16 y_offset)
{
    EFI_STATUS res = EFI_INVALID_PARAMETER;
    UINT32 pixel = 0;

    if( bpp != 1 )
        ERROR_RETURN( EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER,
                      L"Unsupported bpp value for source bitmap (%d)", bpp );

    if( (bbuf_width * bbuf_height) > bbuf->len )
        ERROR_RETURN( EFI_OUT_OF_RESOURCES, EFI_OUT_OF_RESOURCES,
                      L"blit buffer (%d pixels) cannot hold %d x %d bitmap",
                      bbuf->len, bbuf_width, bbuf_height );

    switch( gfx->Mode->Info->PixelFormat )
    {
      case PixelRedGreenBlueReserved8BitPerColor:
      case PixelBlueGreenRedReserved8BitPerColor:
        pixel = triplet_to_fbfmt( gfx, triplet );
        break;

      default:
        ERROR_RETURN( res, 0, L"Unsupported framebuffer pixel format" );
    }

    gfx_clear_blitbuffer( bbuf );

    if( pixel == 0 )
        return EFI_SUCCESS;

    UINT32 bbuf_offset = (y_offset * bbuf_width) + x_offset;
    UINT32 set_count = 0;

    for( UINT32 j = 0; j < height && bbuf_offset < bbuf->len; j++ )
    {
        for( UINT32 i = 0; i < width && bbuf_offset < bbuf->len; i++ )
        {
            bbuf_offset = ((j + y_offset) * bbuf_width) + (i + x_offset);

            if( bbuf_offset >= bbuf->len )
                break;

            if( nth_bit_is_set( src, (j * width) + i ) )
            {
                bbuf->data[ bbuf_offset ] = pixel;
                set_count++;
            }
            else
            {
                bbuf->data[ bbuf_offset ] = TRIPLET_BGRX(0);
            }
        }
    }

    return EFI_SUCCESS;
}

EFI_STATUS
gfx_blit_out (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
              BLIT_BUFFER *bbuf,
              UINT16 width,  // source pixel width
              UINT16 height, // source pixel height
              UINT16 x,      // x-coord (origin at left)
              UINT16 y)      // y-coord (origin at top)

{
    if( (width * height) > bbuf->len )
        ERROR_RETURN( EFI_OUT_OF_RESOURCES, EFI_OUT_OF_RESOURCES,
                      L"blitbuffer too small (%d pixels vs %d x %d out)",
                      bbuf->len, width, height );
    //DEBUG_LOG("BLT OUT %d x %d @ %d x %d", width, height, x, y );
    return uefi_call_wrapper( gfx->Blt, 10,
                              gfx,
                              (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)bbuf->data,
                              EfiBltBufferToVideo,
                              0, 0, // from coords in bbuf (origin top left)
                              x, y, // to coords on screen (origin at top left)
                              width, height,
                              0 );
}

EFI_STATUS
gfx_blit_in (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
             BLIT_BUFFER *bbuf,
             UINT16 width,  // source pixel width
             UINT16 height, // source pixel height
             UINT16 x,      // x-coord (origin at left)
             UINT16 y)      // y-coord (origin at top)

{
    gfx_alloc_blitbuffer( gfx, bbuf, width, height );

    return uefi_call_wrapper( gfx->Blt, 10,
                              gfx,
                              (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)bbuf->data,
                              EfiBltVideoToBltBuffer,
                              x, y, // from coords in fb (origin top left)
                              0, 0, // to coords in bbuf (origin at top left)
                              width, height,
                              0 );
}

// a fill value greater than 0xffffff (eg (UINT32)-1) means "do not fill"
EFI_STATUS
gfx_draw_box(EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
             UINT16 x, UINT16 y, UINT16 width, UINT16 height,
             UINT32 border,
             UINT32 fill)
{
    EFI_STATUS res;
    static BLIT_BUFFER bbuf = { 0 };
    UINT32 pixel = triplet_to_fbfmt( gfx, border );
    UINT32 fpixel;
    BOOLEAN filled = ( fill <= 0xffffff );

    res = gfx_blit_in( gfx, &bbuf, width, height, x, y );
    ERROR_RETURN( res, res, L"blit in failed" );

    if( filled )
        fpixel = triplet_to_fbfmt( gfx, fill );

    // TODO: optimise this so we precalculate if we're going to
    // exceed our boundaries instead of doing it every iteration:
    const UINT32 max_col = width  - 1;
    const UINT32 max_row = height - 1;

    for( UINT32 o = 0; o < bbuf.len; o++ )
    {
        UINT32 col = o % width;
        if( o <= max_col           || // top
            col == 0               || // left
            col == max_col         || // right
            o >= (max_row * width) )  // bottom
            bbuf.data[ o ] = pixel;
        else if ( filled )
            bbuf.data[ o ] = fpixel;
    }

    res = gfx_blit_out( gfx, &bbuf, width, height, x, y );
    ERROR_RETURN( res, res, L"blit out failed" );

    return res;
}

EFI_STATUS gfx_dump_modes (VOID)
{
    EFI_STATUS res = EFI_SUCCESS;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
    UINTN isize = 0;
    UINT32 m = 0;

    res = get_protocol( &gfx_guid, NULL, (VOID **)&gfx );
    ERROR_RETURN( res, res, L"Could not get pseudo-VGA mode: %r\n", res );

    for( m = 0; m < gfx->Mode->MaxMode; m++ )
    {
        if( gfx_get_mode( gfx, m, &isize, &info ) == EFI_SUCCESS )
        {
            const char *pfmt = "????";

            switch( info->PixelFormat )
            {
              case PixelRedGreenBlueReserved8BitPerColor: pfmt = "RGB8"; break;
              case PixelBlueGreenRedReserved8BitPerColor: pfmt = "BGR8"; break;
              case PixelBitMask:                          pfmt = "MASK"; break;
              case PixelBltOnly:                          pfmt = "BLIT"; break;
              default:
                break;
            }

            DEBUG_LOG("GFX#%02d%c %04d x %04d [%4a] %x.%x.%x.%x L:%d",
                      m,
                      m == gfx->Mode->Mode ? '*' : ' ',
                      info->HorizontalResolution,
                      info->VerticalResolution,
                      pfmt,
                      info->PixelInformation.RedMask,
                      info->PixelInformation.GreenMask,
                      info->PixelInformation.BlueMask,
                      info->PixelInformation.ReservedMask,
                      info->PixelsPerScanLine );
        }
        else
        {
            DEBUG_LOG("GFX#%02d v%d: %04d x %04d [%4s] %x.%x.%x.%x L:%d\n",
                      0, 0, 0, "????", 0 );
        }
    }

    return EFI_SUCCESS;
}
