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
#include <eficon.h>
#include <efiprot.h>

#include "err.h"
#include "util.h"
#include "vga.h"

EFI_STATUS
vga_get_mode (EFI_GRAPHICS_OUTPUT_PROTOCOL *vga,
              UINT32 mode,
              UINTN *size,
              EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **info)
{
    return uefi_call_wrapper( vga->QueryMode, 4, vga, mode, size, info );
}

EFI_STATUS vga_dump_modes (VOID)
{
    EFI_STATUS res = EFI_SUCCESS;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *vga;
    EFI_GUID vga_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
    UINTN isize = 0;
    UINT32 m = 0;

    res = get_protocol( &vga_guid, NULL, (VOID **)&vga );
    ERROR_RETURN( res, res, L"Could not get pseudo-VGA mode: %r\n", res );

    for( m = 0; m < vga->Mode->MaxMode; m++ )
    {
        if( vga_get_mode( vga, m, &isize, &info ) == EFI_SUCCESS )
        {
            const char *pfmt = "????";

            switch( info->PixelFormat )
            {
              case PixelRedGreenBlueReserved8BitPerColor: pfmt = "RGBx"; break;
              case PixelBlueGreenRedReserved8BitPerColor: pfmt = "BGRx"; break;
              case PixelBitMask:                          pfmt = "MASK"; break;
              case PixelBltOnly:                          pfmt = "BLIT"; break;
              default:
                break;
            }

            DEBUG_LOG("VGA#%02d%c %04d x %04d [%4a] %x.%x.%x.%x L:%d",
                      m,
                      m == vga->Mode->Mode ? '*' : ' ',
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
            DEBUG_LOG("VGA#%02d v%d: %04d x %04d [%4s] %x.%x.%x.%x L:%d\n",
                      0, 0, 0, "????", 0 );
        }
    }

    return EFI_SUCCESS;
}
