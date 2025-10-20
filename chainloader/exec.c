// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2018-2019,2021-2022 Collabora Ltd
// Copyright © 2018-2019,2021-2022 Valve Corporation

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

#include "util.h"
#include "exec.h"
#include "err.h"

EFI_STATUS load_image (EFI_DEVICE_PATH *path, EFI_HANDLE *image)
{
    EFI_HANDLE current = get_self_handle();

    return
      uefi_call_wrapper( BS->LoadImage, 6, FALSE, current, path,
                         NULL, 0, image );
}

EFI_STATUS exec_image (EFI_HANDLE image, UINTN *code, CHAR16 **data)
{
    return uefi_call_wrapper( BS->StartImage, 3, image, code, data );
}

// There are no guarantees about the load options being LNUL terminated,
// so we alloc enough space for the load options plus 1 CHAR16 and make
// sure there's a L'\0' at the and.
EFI_STATUS get_image_cmdline (EFI_LOADED_IMAGE *image, OUT CHAR16 **args)
{
    if( !args )
        return EFI_INVALID_PARAMETER;

    *args = NULL;

    if( !image )
        return EFI_NOT_FOUND;

    *args = efi_alloc( (image->LoadOptionsSize + 1) * sizeof(CHAR16) );

    if( image->LoadOptionsSize )
        mem_copy( *args, image->LoadOptions,
                  image->LoadOptionsSize * sizeof(CHAR16) );

    *args[ image->LoadOptionsSize ] = L'\0';

    return EFI_SUCCESS;
}

EFI_STATUS set_image_cmdline (EFI_HANDLE *image, CONST CHAR16 *cmdline,
                              EFI_LOADED_IMAGE **child)
{
    EFI_STATUS res;
    EFI_GUID load_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

    res = get_handle_protocol( image, &load_guid, (VOID **) child );
    ERROR_RETURN( res, res, L"" );

    if( cmdline )
    {
        (*child)->LoadOptions = (CHAR16 *)cmdline;
        // The spec is unclear as to whether this is the char count
        // or the byte count. grub expects the char count.
        (*child)->LoadOptionsSize = strlen_w( cmdline );
    }
    else
    {
        (*child)->LoadOptions = L"";
        (*child)->LoadOptionsSize = 0;
    }

    return EFI_SUCCESS;
}
