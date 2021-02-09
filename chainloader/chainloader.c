// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2018,2021 Collabora Ltd
// Copyright © 2018,2021 Valve Corporation
// Copyright © 2018,2020 Vivek Das Mohapatra <vivek@etla.org>

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

#include "chainloader.h"
#include "variable.h"

EFI_STATUS
BootServiceStall (
    IN UINTN                    Microseconds
    )
{
    return uefi_call_wrapper( BS->Stall, 1, Microseconds );
}

EFI_STATUS RunTimeResetSystem (
    IN EFI_RESET_TYPE           ResetType,
    IN EFI_STATUS               ResetStatus,
    IN UINTN                    DataSize,
    IN CHAR16                   *ResetData OPTIONAL
    )
{
    return uefi_call_wrapper( RT->ResetSystem, 4, ResetType, ResetStatus,
                              DataSize, ResetData );
}

BOOLEAN reboot_into_firmware_is_supported (VOID)
{
    UINT64 os_indications_supported;
    UINTN size;
    VOID *val;

    val = LibGetVariableAndSize( L"OsIndicationsSupported",
                                 &gEfiGlobalVariableGuid, &size );
    if( !val )
        return FALSE;

    v_msg( L"OsIndicationsSupported: %016x\n", *(UINT64 *)val);
    os_indications_supported = *(UINT64 *)val;
    FreePool( val );

    if( os_indications_supported & EFI_OS_INDICATIONS_BOOT_TO_FW_UI )
        return TRUE;

    return FALSE;
}

EFI_STATUS reboot_into_firmware (VOID)
{
    UINT64 os_indications = EFI_OS_INDICATIONS_BOOT_TO_FW_UI;
    UINTN size, res;
    VOID *val;

    val = LibGetVariableAndSize( L"OsIndications",
                                 &gEfiGlobalVariableGuid, &size );
    if( val )
    {
        v_msg( L"OsIndications: %016x\n", *(UINT64 *)val);
        os_indications |= *(UINT64 *)val;
        FreePool( val );
    }

    v_msg( L"OsIndications: %016x\n", os_indications );
    res = LibSetNVVariable( L"OsIndications", &gEfiGlobalVariableGuid,
                            sizeof(os_indications), &os_indications );
    if( EFI_ERROR( res ) )
    {
        Print( L"Failed to LibSetNVVariable: %r\n", res );
        return res;
    }

    res = RunTimeResetSystem( EfiResetCold, EFI_SUCCESS, 0, NULL );
    if( EFI_ERROR( res ) )
    {
        Print( L"Failed to ResetSystem: %r\n", res );
        return res;
    }

    return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *sys_table)
{
    EFI_GUID fs_guid = SIMPLE_FILE_SYSTEM_PROTOCOL;
    EFI_HANDLE* filesystems = NULL;
    UINTN count = 0;
    EFI_STATUS res = EFI_SUCCESS;
    bootloader steamos = {0};

    InitializeLib( image_handle, sys_table );
    initialise( image_handle );
    set_steamos_loader_criteria( &steamos );
    set_loader_time_init_usec();
    set_loader_info();
    set_loader_firmware_info();
    set_loader_firmware_type();
    set_loader_features();
    set_loader_device_part_uuid();
    set_loader_image_identifier();
    set_chainloader_device_part_uuid( LibImageHandle );
    set_chainloader_image_identifier( LibImageHandle );

    res = get_protocol_handles( &fs_guid, &filesystems, &count );
    ERROR_JUMP( res, cleanup, L"get_fs_handles" );

    for( int i = 0; i < (int)count; i++ )
    {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = NULL;

        res = get_handle_protocol( &filesystems[ i ], &fs_guid, (VOID **)&fs );
        ERROR_CONTINUE( res, L"simple fs protocol" );
    }

    res = choose_steamos_loader( filesystems, count, &steamos );
    ERROR_JUMP( res, cleanup, L"no valid steamos loader found" );

    set_loader_time_exec_usec();

    res = exec_bootloader( &steamos );
    ERROR_JUMP( res, cleanup, L"exec failed" );

cleanup:
    efi_free( filesystems );

    if( reboot_into_firmware_is_supported() )
    {
        Print( L"Rebooting into firmware...\n" );
        res = reboot_into_firmware();
        Print( L"Failed to reboot into firmware: %r\n", res );
    }

    Print( L"Rebooting into 5s...\n" );
    res = BootServiceStall( 5 * 1000000 );
    if( EFI_ERROR( res ) )
        Print( L"Failed to BootServiceStall: %r\n", res );

    return res;
}
