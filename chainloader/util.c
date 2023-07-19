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
#include <efistdarg.h>

#include "err.h"
#include "util.h"

VOID *efi_alloc (UINTN s) { return AllocateZeroPool( s ); }
VOID  efi_free  (VOID *p) { if( p ) FreePool( p); }

static EFI_HANDLE self_image;
EFI_GUID NULL_GUID = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };

static UINTN asprint_size_factor = 0;

CONST CHAR16 *efi_statstr (EFI_STATUS s)
{
    switch( s )
    {
      case EFI_SUCCESS:              return L"EFI_SUCCESS";
      case EFI_LOAD_ERROR:           return L"EFI_LOAD_ERROR";
      case EFI_INVALID_PARAMETER:    return L"EFI_INVALID_PARAMETER";
      case EFI_UNSUPPORTED:          return L"EFI_UNSUPPORTED";
      case EFI_BAD_BUFFER_SIZE:      return L"EFI_BAD_BUFFER_SIZE";
      case EFI_BUFFER_TOO_SMALL:     return L"EFI_BUFFER_TOO_SMALL";
      case EFI_NOT_READY:            return L"EFI_NOT_READY";
      case EFI_DEVICE_ERROR:         return L"EFI_DEVICE_ERROR";
      case EFI_WRITE_PROTECTED:      return L"EFI_WRITE_PROTECTED";
      case EFI_OUT_OF_RESOURCES:     return L"EFI_OUT_OF_RESOURCES";
      case EFI_VOLUME_CORRUPTED:     return L"EFI_VOLUME_CORRUPTED";
      case EFI_VOLUME_FULL:          return L"EFI_VOLUME_FULL";
      case EFI_NO_MEDIA:             return L"EFI_NO_MEDIA";
      case EFI_MEDIA_CHANGED:        return L"EFI_MEDIA_CHANGED";
      case EFI_NOT_FOUND:            return L"EFI_NOT_FOUND";
      case EFI_ACCESS_DENIED:        return L"EFI_ACCESS_DENIED";
      case EFI_NO_RESPONSE:          return L"EFI_NO_RESPONSE";
      case EFI_NO_MAPPING:           return L"EFI_NO_MAPPING";
      case EFI_TIMEOUT:              return L"EFI_TIMEOUT";
      case EFI_NOT_STARTED:          return L"EFI_NOT_STARTED";
      case EFI_ALREADY_STARTED:      return L"EFI_ALREADY_STARTED";
      case EFI_ABORTED:              return L"EFI_ABORTED";
      case EFI_ICMP_ERROR:           return L"EFI_ICMP_ERROR";
      case EFI_TFTP_ERROR:           return L"EFI_TFTP_ERROR";
      case EFI_PROTOCOL_ERROR:       return L"EFI_PROTOCOL_ERROR";
      case EFI_INCOMPATIBLE_VERSION: return L"EFI_INCOMPATIBLE_VERSION";
      case EFI_SECURITY_VIOLATION:   return L"EFI_SECURITY_VIOLATION";
      case EFI_CRC_ERROR:            return L"EFI_CRC_ERROR";
      case EFI_END_OF_MEDIA:         return L"EFI_END_OF_MEDIA";
      case EFI_END_OF_FILE:          return L"EFI_END_OF_FILE";
      case EFI_INVALID_LANGUAGE:     return L"EFI_INVALID_LANGUAGE";
      case EFI_COMPROMISED_DATA:     return L"EFI_COMPROMISED_DATA";
      default:
        return L"-UNKNOWN-";
    }
}

CONST CHAR16 *efi_memtypestr (EFI_MEMORY_TYPE m)
{
    switch( m )
    {
      case EfiReservedMemoryType:      return L"Reserved";
      case EfiLoaderCode:              return L"Loader Code";
      case EfiLoaderData:              return L"Loader Data";
      case EfiBootServicesCode:        return L"Boot Services Code";
      case EfiBootServicesData:        return L"Boot Services Data";
      case EfiRuntimeServicesCode:     return L"Runtime Services Code";
      case EfiRuntimeServicesData:     return L"Runtime Services Data";
      case EfiConventionalMemory:      return L"Conventional Memory";
      case EfiUnusableMemory:          return L"Unusable Memory";
      case EfiACPIReclaimMemory:       return L"ACPI Reclaim Memory";
      case EfiACPIMemoryNVS:           return L"ACPI Memory NVS";
      case EfiMemoryMappedIO:          return L"Memory Mapped IO";
      case EfiMemoryMappedIOPortSpace: return L"Memory Mapped IO Port Space";
      case EfiPalCode:                 return L"Pal Code";
      case EfiMaxMemoryType:           return L"(INVALID)";
      default:
        return L"(OUT OF RANGE)";
    }
}

EFI_STATUS get_handle_protocol (EFI_HANDLE *handle,
                                EFI_GUID *id,
                                OUT VOID **protocol)
{
    return uefi_call_wrapper( BS->HandleProtocol, 3, *handle, id, protocol );
}

EFI_STATUS get_protocol_handles (EFI_GUID *guid,
                                 OUT EFI_HANDLE **handles,
                                 OUT UINTN *count)
{
    return LibLocateHandle(ByProtocol, guid, NULL, count, handles);
}

EFI_STATUS get_protocol_instance_handle (EFI_GUID *id,
                                         VOID *protocol_instance,
                                         OUT EFI_HANDLE *handle)
{
    EFI_HANDLE *handles = NULL;
    UINTN max = 0;
    EFI_STATUS res;

    *handle = NULL;

    res = get_protocol_handles( id, &handles, &max );
    ERROR_RETURN( res, res, L"", (UINT64)id );

    for( UINTN i = 0; !*handle && (i < max); i++ )
    {
        VOID *found = NULL;
        res = get_handle_protocol( &handles[ i ], id, &found );
        ERROR_CONTINUE( res, L"handle %x does not support protocol %x. what.",
                        (UINT64)handles[ i ], (UINT64)id );

        if( found == protocol_instance )
            *handle = handles[ i ];
    }

    efi_free( handles );

    return EFI_SUCCESS;
}

EFI_STATUS get_protocol (EFI_GUID *id,
                         VOID *registration,
                         OUT VOID **protocol)
{
    return uefi_call_wrapper( BS->LocateProtocol, 3, id, registration, protocol );
}

EFI_HANDLE get_self_handle (VOID)
{
    return self_image;
}

EFI_HANDLE get_self_loaded_image (VOID)
{
    EFI_STATUS res;
    EFI_GUID lip_guid = LOADED_IMAGE_PROTOCOL;
    EFI_LOADED_IMAGE *li = NULL;

    if( !self_image )
        ERROR_RETURN( EFI_NOT_STARTED, NULL,
                      L"Chainloader is not initialised yet\n" );

    res = get_handle_protocol( &self_image, &lip_guid, (VOID **)&li );
    ERROR_RETURN( res, NULL, L"No loaded image protocol on %x\n", self_image );

    return li;
}

EFI_HANDLE get_self_device_handle (VOID)
{
    EFI_LOADED_IMAGE *li = get_self_loaded_image();

    return li ? li->DeviceHandle : NULL;
}

EFI_DEVICE_PATH *get_self_device_path (VOID)
{
    EFI_STATUS res;
    EFI_GUID dp_guid   = DEVICE_PATH_PROTOCOL;
    EFI_GUID lidp_guid = EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID;
    EFI_DEVICE_PATH *lpath = NULL;
    EFI_DEVICE_PATH *dpath = NULL;
    EFI_HANDLE dh = get_self_device_handle();

    if( !dh )
        return NULL;

    res = get_handle_protocol( &dh, &lidp_guid, (VOID **)&lpath );
    if( res != EFI_SUCCESS )
        res = get_handle_protocol( &dh, &dp_guid, (VOID **)&dpath );

    WARN_STATUS( res, L"No DEVICE PATH type protos on self device handle\n" );

    return dpath ?: lpath;
}

EFI_DEVICE_PATH *get_self_file (VOID)
{
    EFI_LOADED_IMAGE *li = get_self_loaded_image();

    return li ? li->FilePath : NULL;
}

VOID initialise (EFI_HANDLE image, EFI_SYSTEM_TABLE *sys_table)
{
    InitializeLib( image, sys_table );
    self_image = image;
}

EFI_DEVICE_PATH *
make_absolute_device_path (EFI_HANDLE device, CHAR16 *path)
{
    return FileDevicePath( device, path );
}

CHAR16 *
strwiden (CONST CHAR8 *narrow)
{
    if( !narrow )
        return NULL;

    UINTN l = strlena( narrow ) + 1;
    CHAR16 *wide = ALLOC_OR_GOTO( l * sizeof(CHAR16), allocfail );

    for( UINTN i = 0; i < l; i++ )
        wide[ i ] = (CHAR16)narrow[ i ];
    return wide;

allocfail:
    return NULL;
}

CHAR8 *
strnarrow (CONST CHAR16 *wide)
{
    if( !wide )
        return NULL;

    UINTN l = StrLen( wide ) + 1;
    CHAR8 *narrow = ALLOC_OR_GOTO( l, allocfail );

    // if any high bit is set, set the 8th bit in the narrow character:
    for( UINTN i = 0; i < l; i++ )
        narrow[ i ] = (CHAR8)
          (0xff & ((wide[ i ] & 0xff80) ? (wide[ i ] | 0x80) : wide[ i ]));
    return narrow;

allocfail:
    return NULL;
}

CHAR16 *resolve_path (CONST VOID *path, CONST CHAR16* relative_to, UINTN widen)
{
    UINTN plen;
    UINTN rlen;
    CHAR16 *wide = NULL;
    CHAR16 *rel  = NULL;
    CHAR16 *abs  = NULL;

    if( !path )
        return NULL;

    // make sure wide is a wide copy of path
    wide = widen ? strwiden( (CHAR8 *)path ): StrDuplicate( (CHAR16 *)path );

    if( !wide )
        return NULL;

    // unset or zero-length relative path treated as / (root):
    if( relative_to && (StrLen( relative_to ) > 0) )
        rel = (CHAR16 *)relative_to;
    else
        rel = L"\\";

    plen = StrLen( wide );
    rlen = StrLen( rel  );

    // empty path, we don't want to resolve anything:
    if( plen == 0 )
    {
        efi_free( wide );
        return NULL;
    }

    // path separators flipped:
    for( UINTN i = 0; i < plen; i++ )
        if( wide[ i ] == (CHAR16)'/' )
            wide[ i ] = (CHAR16)'\\';

    // apth is absolute, we're good to go:
    if( wide[ 0 ] == (CHAR16)'\\' )
        return wide;

    rel = StrDuplicate( rel );

    // path separators flipped:
    for( UINTN i = 0; i < rlen; i++ )
        if( rel[ i ] == (CHAR16)'/' )
            rel[ i ] = (CHAR16)'\\';

    // We strip the path element after the last /
    for( INTN i = (INTN) rlen - 1; i >= 0; i-- )
        if( rel[ i ] == (CHAR16)'\\' )
        {
            rel[ i ] = (CHAR16)0;
            rlen = i;
            break;
        }

    // add a / at the start (maybe); and in between; plus a trailing NUL
    abs = ALLOC_OR_GOTO( (plen + rlen + 3) * sizeof(CHAR16), allocfail );
    abs[ 0 ] = (CHAR16)0;

    if( rel[ 0 ] != (CHAR16)'\\' )
        StrCat( abs, L"\\");
    StrCat( abs, rel );
    StrCat( abs, L"\\" );
    StrCat( abs, wide );

    efi_free( rel  );
    efi_free( wide );

    return abs;

allocfail:
    efi_free( rel  );
    efi_free( abs  );
    efi_free( wide );
    return NULL;
}
// ============================================================================
// EFI sadly has no UTC support so we need to roll our own:

static UINT8 max_month_day (EFI_TIME *time)
{
    UINT16 y;

    switch( time->Month )
    {
      case 1:  /* Jan */
      case 3:  /* Mar */
      case 5:  /* May */
      case 7:  /* Jul */
      case 8:  /* Aug */
      case 10: /* Oct */
      case 12: /* Dec */
        return 31;

      case 4:  /* Apr */
      case 6:  /* Jun */
      case 9:  /* Sep */
      case 11: /* Nov */
        return 30;

      case 2:  /* Feb */
        y = time->Year;
        // leap years divisible by 4 BUT centuries must also be div by 400:
        //       not-div-100  not-div-4             not-div-400
        return ( (y % 100) ? ((y % 4) ? 28 : 29) : ((y % 400) ? 28 : 29) );

      default:
        return 0;
    }
}

static inline void incr_month (EFI_TIME *time)
{
    if( time->Month == 12 )
    {
        time->Month = 1;
        time->Year++;
        return;
    }

    time->Month++;
}

static inline void incr_day (EFI_TIME *time)
{
    if( time->Day == max_month_day( time ) )
    {
        time->Day = 1;
        incr_month( time );
        return;
    }

    time->Day++;
}

static inline void incr_hour (EFI_TIME *time)
{
    if( time->Hour == 23 )
    {
        time->Hour = 0;
        incr_day( time );
        return;
    }

    time->Hour++;
}

static inline void incr_minute (EFI_TIME *time)
{
    if( time->Minute == 59 )
    {
        time->Minute = 0;
        incr_hour( time );
        return;
    }

    time->Minute++;
}

static inline void decr_month (EFI_TIME *time)
{
    if( time->Month == 1 )
    {
        time->Month = 12;
        time->Year--;
        return;
    }

    time->Month--;
}

static inline void decr_day (EFI_TIME *time)
{
    if( time->Day == 1 )
    {
        decr_month( time );
        time->Day = max_month_day( time );
        return;
    }

    time->Day--;
}

static inline void decr_hour (EFI_TIME *time)
{
    if( time->Hour == 0 )
    {
        time->Hour = 23;
        decr_day( time );
        return;
    }

    time->Hour--;
}

static inline void decr_minute (EFI_TIME *time)
{
    if( time->Minute == 0 )
    {
        time->Minute = 59;
        decr_hour( time );
        return;
    }

    time->Minute--;
}

// UTC = now + now.zone
// now.zoneis ± 24 hours (1440 minutes)
VOID efi_time_to_utc (EFI_TIME *time)
{
    if( time->TimeZone == EFI_UNSPECIFIED_TIMEZONE )
        return;

    if( time->TimeZone > 0 )
        for( ; time->TimeZone; time->TimeZone-- )
            incr_minute( time );
    else if( time->TimeZone < 0 )
        for( ; time->TimeZone; time->TimeZone++ )
            decr_minute( time );

    time->TimeZone = 0x0000;
}

UINT64 efi_time_to_timestamp (EFI_TIME *time)
{
    if( !time )
        return 0;

    efi_time_to_utc( time );

    return ( time->Second                 +
             (time->Minute * 100)         +
             (time->Hour   * 10000)       +
             (time->Day    * 1000000)     +
             (time->Month  * 100000000)   +
             (time->Year   * 10000000000) );

}

// Stolen from systemd (src/boot/efi/util.c).

// SPDX-License-Identifier: LGPL-2.1-or-later

#ifdef __x86_64__
static UINT64 ticks_read (VOID)
{
    UINT64 a, d;
    __asm__ volatile ("rdtsc" : "=a" (a), "=d" (d));
    return (d << 32) | a;
}
#elif defined(__i386__)
static UINT64 ticks_read (VOID)
{
    UINT64 val;
    __asm__ volatile ("rdtsc" : "=A" (val));
    return val;
}
#else
static UINT64 ticks_read (VOID)
{
    UINT64 val = 1;
    return val;
}
#endif

/* count TSC ticks during a millisecond delay */
static UINT64 ticks_freq (VOID)
{
    UINT64 ticks_start, ticks_end;

    ticks_start = ticks_read();
    uefi_call_wrapper(BS->Stall, 1, 1000);
    ticks_end = ticks_read();

    return (ticks_end - ticks_start) * 1000UL;
}

UINT64 time_usec (VOID)
{
    UINT64 ticks;
    static UINT64 freq;

    ticks = ticks_read();
    if (ticks == 0)
        return 0;

    if (freq == 0) {
        freq = ticks_freq();
        if (freq == 0)
            return 0;
    }

    return 1000UL * 1000UL * ticks / freq;
}

// string utility functions:
UINTN strlen_w (const CHAR16 *str)
{
    return StrLen( str );
}

CHAR16 * strdup_w (const CHAR16 *str)
{
    return StrDuplicate( str );
}

UINTN strlen_a (const CHAR8 *str)
{
    return strlena( str );
}

CHAR16 *strstr_w (const CHAR16 *haystack, const CHAR16 *needle)
{
    if( !haystack || !needle )
        return NULL;

    UINTN h_len = strlen_w( haystack );
    UINTN n_len = strlen_w( needle );

    if( n_len > h_len )
        return NULL;

    for( UINTN offs = 0; offs <= h_len - n_len; offs++ )
        if( strncmp_w( haystack + offs, needle, n_len ) == 0 )
            return (CHAR16 *)haystack + offs;

    return NULL;
}

INTN strcmp_w (const CHAR16 *a, const CHAR16 *b)
{
    return StrCmp( a, b );
}

INTN strncmp_w (const CHAR16 *a, const CHAR16 *b, UINTN len)
{
    return StrnCmp( a, b, len );
}

// NOTE: size is the _byte_ size of the target buffer,
// not the short-wchar length.
INTN appendstr_w (CHAR16 *dest, UINTN size, CHAR16 *add)
{
    UINTN offs = 0;
    UINTN max  = 0;
    CHAR16 *start = NULL;
    UINTN i;

    if( !dest )
        return -1;

    offs  = strlen_w( dest );
    start = dest + offs;
    max   = (size / sizeof(CHAR16)) - offs;

    for( i = 0; i < max; i++ )
    {
        *(start + i) = *(add + i);
        if( *(add + i) == L'\0' )
            break;
    }

    return i;
}

CHAR8 * strlower (CHAR8 *str)
{
    if( str )
        for( CHAR8 *c = str; *c; c++ )
            if( *c >= 'A' && *c <= 'Z' )
                *c = *c + ('a' - 'A');

    return str;
}

static UINTN _init_asprint_size_factor (CHAR8 *buf, UINT64 size, const char *fmt, ...)
{
    va_list args;
    UINTN rv;

    va_start( args, fmt );
    rv = AsciiVSPrint( buf, size, (const CHAR8 *)fmt, args );
    va_end( args );

    return rv;
}

static void init_asprint_size_factor (void)
{
    UINTN wrote = 0;
    CHAR8 testbuf[4];

    if( asprint_size_factor > 0 )
        return;

    wrote = _init_asprint_size_factor( &testbuf[0], sizeof(testbuf), "%a", "abc" );

    // if AsciiVSPrint is still buggy, we will write one character,
    // because it thinks we're writing wide characters. If the bug has been
    // fixed, we'll write 3 characters, because we have space for them and a
    // trailing NUL (efi sprintf insists on the trailing NUL and allows space
    // for it, but does not report it as written (which is sensible)).
    switch( wrote )
    {
      case 1:
        asprint_size_factor = 2;
        break;
      case 3:
        asprint_size_factor = 1;
        break;
      default: // seriously WTF but Ok, let's just deal as best we can:
        asprint_size_factor = 1;
        break;
    }
}

UINTN sprintf_a (CHAR8 *buf, UINT64 size, const char *fmt, ...)
{
    va_list args;
    UINTN rv;

    init_asprint_size_factor();

    va_start( args, fmt );
    rv = AsciiVSPrint( buf, size * asprint_size_factor, (const CHAR8 *)fmt, args );
    va_end( args );

    return rv;
}

UINTN vsprintf_a (CHAR8 *buf, UINT64 size, const char *fmt, va_list args)
{
    init_asprint_size_factor();
    return AsciiVSPrint( buf, size * asprint_size_factor, (const CHAR8 *)fmt, args );
}

UINTN sprintf_w (CHAR16 *buf, UINT64 size, const CHAR16 *fmt, ...)
{
    va_list args;
    UINTN rv;

    va_start( args, fmt );
    rv = UnicodeVSPrint( buf, size, fmt, args );
    va_end( args );

    return rv;
}

UINTN vsprintf_w (CHAR16 *buf, UINT64 size, const CHAR16 *fmt, va_list args)
{
    return UnicodeVSPrint( buf, size, fmt, args );
}

// memory utility functions
VOID mem_copy (void *dest, const VOID *src, UINTN len)
{
    CopyMem( dest, src, len );
}

INTN mem_cmp (const VOID *a, const VOID *b, UINTN len)
{
    return CompareMem( a, b, len );
}

VOID mem_set (VOID *dest, UINT8 c, UINTN n)
{
    SetMem( dest, n, c );
}

INTN guid_cmp (const VOID *a, const VOID *b)
{
    return CompareMem( a, b, sizeof(EFI_GUID) );
}

// device path/media utilities:
EFI_DEVICE_PATH *handle_device_path (EFI_HANDLE *handle)
{
    return DevicePathFromHandle( handle );
}

EFI_GUID device_path_partition_uuid (EFI_DEVICE_PATH *dp)
{
    if( !dp )
        return NULL_GUID;

    while( dp && !IsDevicePathEnd( dp ) )
    {
        if( DevicePathType( dp )    == MEDIA_DEVICE_PATH &&
            DevicePathSubType( dp ) == MEDIA_HARDDRIVE_DP )
        {
            HARDDRIVE_DEVICE_PATH *hd = (HARDDRIVE_DEVICE_PATH *)dp;
            EFI_GUID *guid;

            if( hd->SignatureType != SIGNATURE_TYPE_GUID )
                break;

            guid = (EFI_GUID *) (&hd->Signature[0]);
            return *guid;
        }

        dp = NextDevicePathNode( dp );
    }

    return NULL_GUID;
}

CHAR16 *guid_str (EFI_GUID *guid)
{
    CHAR16 str[ sizeof(CHAR16) * GUID_STRLEN ] = L"";

    // SPrint expects the _byte_ length
    SPrint( str, sizeof(str), GUID_STRFMT, GUID_FMTARG(guid) );
    str[ (sizeof(str) / sizeof(CHAR16)) - 1 ] = (CHAR16)0;

    return strdup_w( str );
}

CHAR16 *device_path_string (EFI_DEVICE_PATH *dp)
{
    return DevicePathToStr( dp );
}

// This code compares the _medium_ part of two EFI_DEVICE_PATHS,
// and returns TRUE if they are the same.
//
// It does NOT consider the filesystem-path part of the EFI_DEVICE_PATHS.
// It does NOT consider the partition part of EFI_DEVICE_PATHS.
//
// Its main use is to determine if two files reside on the same
// physical storage device (ie same disk or disk equivalent).
BOOLEAN on_same_device (EFI_DEVICE_PATH *a, EFI_DEVICE_PATH *b)
{
    if( !a || !b )
        return FALSE;

    // iterate over all the path components; both devices are located on the
    // same drive only if both components reach hard-drive partitions.
    while( !IsDevicePathEnd( a ) && !IsDevicePathEnd( b ) )
    {
        if( DevicePathNodeLength( a ) != DevicePathNodeLength( b ) ||
            DevicePathType( a )       != DevicePathType( b )       ||
            DevicePathSubType( a )    != DevicePathSubType( b ) )
            return FALSE;

        // both components are hard-drive partitions.
        // if we get this far then our job is done, the two
        // files are on the same device and partition.
        if( DevicePathType( a )    == MEDIA_DEVICE_PATH &&
            DevicePathSubType( a ) == MEDIA_HARDDRIVE_DP )
            return TRUE;

        // These structs use windows style ‘put a pointer to an array
        // at the end of a struct and deliberately overflow it’ storage.
        // (ie allocated storage is larger than the declared struct size).
        // See /usr/include/efi/efidevp.h
        // As such we must trust DevicePathNodeLength and not let the
        // compiler worry about the theoretical length of the struct
        // because that happens to be a lie:
        for( UINT16 x = 0; x < DevicePathNodeLength( a ); x++ )
            if( *((UINT8 *)&(a->Length[ 0 ]) + 2 + x) !=
                *((UINT8 *)&(b->Length[ 0 ]) + 2 + x) )
                return FALSE;

        a = NextDevicePathNode( a );
        b = NextDevicePathNode( b );
    }

    return FALSE;
}
