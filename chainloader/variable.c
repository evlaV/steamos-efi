// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2021 Collabora Ltd
// Copyright © 2021 Valve Corporation

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

#include "err.h"
#include "util.h"
#include "variable.h"

#define LOADER_VARIABLE_GUID \
    { 0x4a67b082, 0x0a4c, 0x41cf, {0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f} }

#define CHAINLOADER_VARIABLE_GUID \
    { 0x399abb9b, 0x4bee, 0x4a18, {0xab, 0x5b, 0x45, 0xc6, 0xe0, 0xe8, 0xc7, 0x16} }

#define VARIABLE_STRING(s) (UINTN)( ( StrLen( s ) + 1 ) * sizeof( CHAR16 ) ), (VOID *)( s )
#define VARIABLE_BLOB(s) (UINTN)(sizeof( *s )), (VOID *)( s )

#define EFI_LOADER_FEATURE_CONFIG_TIMEOUT          (1L << 0)
#define EFI_LOADER_FEATURE_CONFIG_TIMEOUT_ONE_SHOT (1L << 1)
#define EFI_LOADER_FEATURE_ENTRY_ONESHOT           (1L << 3)

/* Some facilities that does not exist in the efi lib */

static EFI_STATUS
OpenProtocol (
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    OUT VOID **Interface OPTIONAL,
    IN EFI_HANDLE AgentHandle,
    IN EFI_HANDLE ControllerHandle,
    IN UINT32 Attributes
    )
{
    return uefi_call_wrapper( BS->OpenProtocol, 6, Handle, Protocol, Interface,
                              AgentHandle, ControllerHandle, Attributes );
}

static EFI_STATUS
CloseProtocol(
    IN EFI_HANDLE Handle,
    IN EFI_GUID *Protocol,
    IN EFI_HANDLE AgentHandle,
    IN EFI_HANDLE ControllerHandle
    )
{
    return uefi_call_wrapper( BS->CloseProtocol, 4, Handle, Protocol, AgentHandle,
                              ControllerHandle );
}

static EFI_GUID
GetMediaHardDriveSignature( EFI_DEVICE_PATH *device_path )
{
    HARDDRIVE_DEVICE_PATH *harddrive;
    EFI_GUID *guid = &NullGuid;

    while( device_path && !IsDevicePathEnd( device_path ) )
    {
        if( DevicePathType( device_path ) == MEDIA_DEVICE_PATH &&
            DevicePathSubType( device_path ) == MEDIA_HARDDRIVE_DP )
        {
            harddrive = (HARDDRIVE_DEVICE_PATH *)device_path;
            if( harddrive->SignatureType != SIGNATURE_TYPE_GUID )
                break;

            guid = (EFI_GUID *)&harddrive->Signature[0];
            break;
        }

        device_path = NextDevicePathNode( device_path );
    }

    return *guid;
}

static const CHAR16 *loader_info = L"steamcl " RELEASE_VERSION;
static const UINT64 loader_features = 
    EFI_LOADER_FEATURE_CONFIG_TIMEOUT          |
    EFI_LOADER_FEATURE_CONFIG_TIMEOUT_ONE_SHOT |
    EFI_LOADER_FEATURE_ENTRY_ONESHOT           |
    0;

EFI_STATUS set_loader_time_init_usec ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;
    UINT64 usec;

    usec = time_usec();
    str = PoolPrint( L"%u", usec );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
        return EFI_OUT_OF_RESOURCES;

    v_msg( L"LoaderTimeInitUSec: %s\n", str );
    res = LibSetVariable( L"LoaderTimeInitUSec", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

    return res;
}

EFI_STATUS set_loader_time_menu_usec ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;
    UINT64 usec;

    usec = time_usec();
    str = PoolPrint( L"%u", usec );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
        return EFI_OUT_OF_RESOURCES;

    v_msg( L"LoaderTimeMenuUSec: %s\n", str );
    res = LibSetVariable( L"LoaderTimeMenuUSec", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

    return res;
}

EFI_STATUS set_loader_time_exec_usec ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;
    UINT64 usec;

    usec = time_usec();
    str = PoolPrint( L"%u", usec );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
        return EFI_OUT_OF_RESOURCES;

    v_msg( L"LoaderTimeExecUSec: %s\n", str );
    res = LibSetVariable( L"LoaderTimeExecUSec", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

    return res;
}

EFI_STATUS set_loader_info ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;

    v_msg( L"LoaderInfo: %s\n", loader_info );
    res = LibSetVariable( L"LoaderInfo", &guid,
                          VARIABLE_STRING( loader_info ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    return res;
}

EFI_STATUS set_loader_firmware_info ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;

    str = PoolPrint( L"%s %d.%02d",
                     ST->FirmwareVendor,
                     ST->FirmwareRevision >> 16,
                     ST->FirmwareRevision & 0xffff );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
        return EFI_OUT_OF_RESOURCES;

    v_msg( L"LoaderFirmwareInfo: %s\n", str );
    res = LibSetVariable( L"LoaderFirmwareInfo", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

    return res;
}

EFI_STATUS set_loader_firmware_type ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;

    str = PoolPrint( L"UEFI %d.%02d",
                     ST->Hdr.Revision >> 16,
                     ST->Hdr.Revision & 0xffff );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
        return EFI_OUT_OF_RESOURCES;

    v_msg( L"LoaderFirmwareType: %s\n", str );
    res = LibSetVariable( L"LoaderFirmwareType", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

    return res;
}

EFI_STATUS set_loader_features ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;

    v_msg( L"LoaderFeatures: 0x%016x\n", loader_features );
    res = LibSetVariable( L"LoaderFeatures", &guid,
                          VARIABLE_BLOB( &loader_features ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    return res;
}

EFI_STATUS set_loader_device_part_uuid ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_LOADED_IMAGE *loaded_image;
    EFI_DEVICE_PATH *device_path;
    EFI_STATUS res = EFI_SUCCESS;
    EFI_GUID signature;
    CHAR16 *str = NULL;

    if( !LibImageHandle )
        return EFI_INVALID_PARAMETER;

    res = OpenProtocol( LibImageHandle, &LoadedImageProtocol,
                        (VOID **)&loaded_image, LibImageHandle, NULL,
                        EFI_OPEN_PROTOCOL_GET_PROTOCOL );
    WARN_STATUS( res, L"Failed to OpenProtocol()" );

    device_path = DevicePathFromHandle( loaded_image->DeviceHandle );
    WARN_STATUS( res, L"Failed to DevicePathFromHandle()" );

    signature = GetMediaHardDriveSignature( device_path );
    WARN_STATUS( ( CompareMem( &signature, &NullGuid, sizeof (EFI_GUID) ) == 0 ),
                 L"Failed to GetMediaHardDriveSignature" );

    str = PoolPrint( L"%g", &signature );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
    {
        res = EFI_OUT_OF_RESOURCES;
        goto exit;
    }

    v_msg( L"LoaderDevicePartUUID: %s\n", str );
    res = LibSetVariable( L"LoaderDevicePartUUID", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

exit:
    CloseProtocol( LibImageHandle, &LoadedImageProtocol, LibImageHandle, NULL );
    return res;
}

EFI_STATUS set_loader_entries (EFI_GUID **signatures)
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 data[2048];
    int len = 0;

    if( !signatures )
        return EFI_INVALID_PARAMETER;

    while( *signatures )
    {
        len += SPrint( &data[ len ], sizeof( data ) - len, L"auto-bootconf-%g",
                       *signatures++ );
        data[ len - 1 ] = 0;
    }

    v_msg( L"LoaderEntries:\n" );
    v_hex( 1, 0, len * sizeof( CHAR16 ), data );
    res = LibSetVariable( L"LoaderEntries", &guid, len * sizeof( CHAR16 ),
                          data );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    return res;
}

EFI_GUID get_loader_entry_oneshot ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_GUID res = NullGuid;
    UINTN size;
    VOID *val;

    val = LibGetVariableAndSize( L"LoaderEntryOneShot", &guid, &size );
    if( val )
    {
	CHAR16 *prefix, *str;
	UINTN len;

	str = (CHAR16 *)val;
        v_msg( L"LoaderEntryDefault: %s\n", str );

	prefix = L"auto-";
	len = StrLen( prefix );
        if( StrnCmp( prefix, str, len ) == 0 )
            str += len;

	prefix = L"bootconf-";
	len = StrLen( prefix );
        if( StrnCmp( prefix, str, len ) == 0 )
            str += len;

        if( str[ 8 ] == '-'  && str[ 13 ] == '-' && str[ 18 ] == '-' &&
            str[ 23 ] == '-' && str[ 36 ] == '\0' )
        {
            UINT64 data4 = xtoi( &str[ 19 ] ) << 48 | xtoi( &str[ 24 ] );
            res.Data1 = xtoi( &str[  0 ] );
            res.Data2 = xtoi( &str[  9 ] );
            res.Data3 = xtoi( &str[ 14 ] );
            res.Data4[ 0 ] = (data4 & 0xFF00000000000000UL) >> 56;
            res.Data4[ 1 ] = (data4 & 0x00FF000000000000UL) >> 48;
            res.Data4[ 2 ] = (data4 & 0x0000FF0000000000UL) >> 40;
            res.Data4[ 3 ] = (data4 & 0x000000FF00000000UL) >> 32;
            res.Data4[ 4 ] = (data4 & 0x00000000FF000000UL) >> 24;
            res.Data4[ 5 ] = (data4 & 0x0000000000FF0000UL) >> 16;
            res.Data4[ 6 ] = (data4 & 0x000000000000FF00UL) >>  8;
            res.Data4[ 7 ] = (data4 & 0x00000000000000FFUL) >>  0;
        }

        FreePool( val );

	LibDeleteVariable( L"LoaderEntryOneShot", &guid );
    }

    return res;
}

EFI_STATUS set_loader_entry_default (EFI_GUID *signature)
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;

    if( !signature )
        return EFI_INVALID_PARAMETER;

    str = PoolPrint( L"auto-bootconf-%g", signature );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
        return EFI_OUT_OF_RESOURCES;

    v_msg( L"LoaderEntryDefault: %s\n", str );
    res = LibSetVariable( L"LoaderEntryDefault", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

    return res;
}

EFI_STATUS set_loader_entry_selected (EFI_GUID *signature)
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;

    if( !signature )
        return EFI_INVALID_PARAMETER;

    str = PoolPrint( L"auto-bootconf-%g", signature );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
        return EFI_OUT_OF_RESOURCES;

    v_msg( L"LoaderEntrySelected: %s\n", str );
    res = LibSetVariable( L"LoaderEntrySelected", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

    return res;
}

EFI_STATUS set_loader_image_identifier ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_LOADED_IMAGE *loaded_image;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;

    if( !LibImageHandle )
        return EFI_INVALID_PARAMETER;

    res = OpenProtocol( LibImageHandle, &LoadedImageProtocol,
                        (VOID **)&loaded_image, LibImageHandle, NULL,
                        EFI_OPEN_PROTOCOL_GET_PROTOCOL );
    WARN_STATUS( res, L"Failed to OpenProtocol()" );

    str = DevicePathToStr(loaded_image->FilePath);
    WARN_STATUS( ( str == NULL ), L"Failed to DevicePathToStr()" );

    if( !str )
    {
        res = EFI_OUT_OF_RESOURCES;
        goto exit;
    }

    v_msg( L"LoaderImageIdentifier: %s\n", str );
    res = LibSetVariable( L"LoaderImageIdentifier", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

exit:
    CloseProtocol( LibImageHandle, &LoadedImageProtocol, LibImageHandle, NULL );
    return res;
}

INTN get_loader_config_timeout ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    UINTN res = 5000000;
    UINTN size;
    VOID *val;

    val = LibGetVariableAndSize( L"LoaderConfigTimeout", &guid, &size );
    if( val )
    {
        res = Atoi( val );
        FreePool( val );
    }

    return res;
}

BOOLEAN is_loader_config_timeout_oneshot_set ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    UINTN size;
    VOID *val;

    val = LibGetVariableAndSize( L"LoaderConfigTimeoutOneShot", &guid, &size );
    if( val )
    {
        FreePool( val );

        return TRUE;
    }

    return FALSE;
}

INTN get_loader_config_timeout_oneshot ()
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    UINTN res = 0;
    UINTN size;
    VOID *val;

    val = LibGetVariableAndSize( L"LoaderConfigTimeoutOneShot", &guid, &size );
    if( val )
    {
        res = Atoi( val );
        FreePool( val );

        LibDeleteVariable( L"LoaderConfigTimeoutOneShot", &guid );
    }

    return res;
}

EFI_STATUS set_chainloader_device_part_uuid (EFI_HANDLE image_handle)
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_LOADED_IMAGE *loaded_image;
    EFI_DEVICE_PATH *device_path;
    EFI_STATUS res = EFI_SUCCESS;
    EFI_GUID signature;
    CHAR16 *str = NULL;

    if( !image_handle )
        return EFI_INVALID_PARAMETER;

    res = OpenProtocol( image_handle, &LoadedImageProtocol,
                        (VOID **)&loaded_image, image_handle, NULL,
                        EFI_OPEN_PROTOCOL_GET_PROTOCOL );
    WARN_STATUS( res, L"Failed to OpenProtocol()" );

    device_path = DevicePathFromHandle( loaded_image->DeviceHandle );
    WARN_STATUS( res, L"Failed to DevicePathFromHandle()" );

    signature = GetMediaHardDriveSignature( device_path );
    WARN_STATUS( ( CompareMem( &signature, &NullGuid, sizeof (EFI_GUID) ) == 0 ),
                 L"Failed to GetMediaHardDriveSignature" );

    str = PoolPrint( L"%g", &signature );
    WARN_STATUS( ( str == NULL ), L"Failed to PoolPrint()" );

    if( !str )
    {
        res = EFI_OUT_OF_RESOURCES;
        goto exit;
    }

    v_msg( L"ChainLoaderDevicePartUUID: %s\n", str );
    res = LibSetVariable( L"ChainLoaderDevicePartUUID", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

exit:
    CloseProtocol( image_handle, &LoadedImageProtocol, image_handle, NULL );
    return res;
}

EFI_STATUS set_chainloader_image_identifier (EFI_HANDLE image_handle)
{
    EFI_GUID guid = LOADER_VARIABLE_GUID;
    EFI_LOADED_IMAGE *loaded_image;
    EFI_STATUS res = EFI_SUCCESS;
    CHAR16 *str = NULL;

    if( !image_handle )
        return EFI_INVALID_PARAMETER;

    res = OpenProtocol( image_handle, &LoadedImageProtocol,
                        (VOID **)&loaded_image, image_handle, NULL,
                        EFI_OPEN_PROTOCOL_GET_PROTOCOL );
    WARN_STATUS( res, L"Failed to OpenProtocol()" );

    str = DevicePathToStr(loaded_image->FilePath);
    WARN_STATUS( ( str == NULL ), L"Failed to DevicePathToStr()" );

    if( !str )
    {
        res = EFI_OUT_OF_RESOURCES;
        goto exit;
    }

    v_msg( L"ChainLoaderImageIdentifier: %s\n", str );
    res = LibSetVariable( L"ChainLoaderImageIdentifier", &guid,
                          VARIABLE_STRING( str ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    FreePool( str );

exit:
    CloseProtocol( image_handle, &LoadedImageProtocol, image_handle, NULL );
    return res;
}

EFI_STATUS set_chainloader_entry_flags (UINT64 flags)
{
    EFI_GUID guid = CHAINLOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;

    v_msg( L"ChainLoaderEntryFlags: 0x%016x\n", flags );
    res = LibSetVariable( L"ChainLoaderEntryFlags", &guid,
                          VARIABLE_BLOB( &flags ) );
    WARN_STATUS( res, L"Failed to SetVariable()" );

    return res;
}

UINTN get_chainloader_boot_attempts ()
{
    EFI_GUID guid = CHAINLOADER_VARIABLE_GUID;
    UINTN res = 0;
    UINTN size;
    VOID *val;

    val = LibGetVariableAndSize( L"ChainLoaderBootAttempts", &guid, &size );
    if( val )
    {
        res = *(UINTN *)val;
        FreePool( val );
    }

    return res;
}

EFI_STATUS set_chainloader_boot_attempts ()
{
    EFI_GUID guid = CHAINLOADER_VARIABLE_GUID;
    EFI_STATUS res = EFI_SUCCESS;
    UINTN attempts = 0;

    attempts = get_chainloader_boot_attempts();
    attempts++;
    v_msg( L"ChainLoaderBootAttempts: %d\n", attempts );
    res = LibSetNVVariable( L"ChainLoaderBootAttempts", &guid,
                            VARIABLE_BLOB( &attempts ) );
    WARN_STATUS( res, L"Failed to SetNVVariable()" );

    return res;
}
