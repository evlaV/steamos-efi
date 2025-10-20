// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2018-2019,2021-2023 Collabora Ltd
// Copyright © 2018-2019,2021-2023 Valve Corporation

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

#ifndef NO_EFI_TYPES
#include <efi.h>
#include <efistdarg.h>
#include "debug.h"
#else
#include "bootconf/efi.h"
#endif

#define opt __attribute__ ((unused))

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define GNUC_PREREQ(maj,min) \
	((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#  define GNUC_PREREQ(maj,min) 0
#endif

#define ARRAY_SIZE(a) (sizeof((a)) / sizeof((a)[0]))

#ifndef EFI_FILE_RESERVED
#ifdef  EFI_FILE_RESERVIED
#define EFI_FILE_RESERVED EFI_FILE_RESERVIED
#endif
#endif

#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#define MAXFSNAMLEN 200

#define OLDCONFPATH  L"SteamOS\\bootconf"
#define NEWCONFPATH  L"\\SteamOS\\conf"
#define EFIDIR       L"\\EFI"

#define GRUBLDR     EFIDIR L"\\steamos\\grubx64.efi"
#define SYSTEMDLDR  EFIDIR L"\\SYSTEMD\\SYSTEMD-BOOTX64.EFI"
#define DEFAULTLDR  EFIDIR L"\\Boot\\bootx64.efi"
#define STEAMOSLDR  GRUBLDR
#define CHAINLDR    EFIDIR L"\\Shell\\steamcl.efi"

#define FLAGFILE_RESTRICT L"steamcl-restricted"
#define FLAGFILE_VERBOSE  L"steamcl-verbose"
#define FLAGFILE_NVDEBUG  L"steamcl-nvram-debug"
#define FLAGFILE_MENU     L"steamcl-menu"
#define DEFAULT_FONT      L"fonts/default.pf2"

#ifndef EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID
#define EFI_LOADED_IMAGE_DEVICE_PATH_PROTOCOL_GUID              \
    { 0xbc62157e, 0x3e33, 0x4fec,                               \
      { 0x99, 0x20, 0x2d, 0x3b, 0x36, 0xd7, 0x50, 0xdf } }
#endif

#ifndef NO_EFI_TYPES
extern EFI_GUID NULL_GUID;

// 32 hex digits + 4 separators + NUL
#define GUID_STRLEN 37
#define GUID_STRFMT L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x"
#define GUID_FMTARG(g) \
    (g)->Data1, \
    (g)->Data2, \
    (g)->Data3, \
    (g)->Data4[0], (g)->Data4[1], \
    (g)->Data4[2], (g)->Data4[3], (g)->Data4[4], \
    (g)->Data4[5], (g)->Data4[6], (g)->Data4[7]

VOID *efi_alloc (IN UINTN s);
VOID  efi_free  (IN VOID *p);

CONST CHAR16 *efi_statstr (EFI_STATUS s);
CONST CHAR16 *efi_memtypestr (EFI_MEMORY_TYPE m);

EFI_STATUS get_handle_protocol (EFI_HANDLE *handle,
                                EFI_GUID *id,
                                OUT VOID **protocol);

EFI_STATUS get_protocol_handles (EFI_GUID *guid,
                                 OUT EFI_HANDLE **handles,
                                 OUT UINTN *count);

EFI_STATUS get_protocol_instance_handle (EFI_GUID *id,
                                         VOID *protocol,
                                         OUT EFI_HANDLE *handle);

EFI_STATUS get_protocol (EFI_GUID *id,
                         VOID *registration,
                         OUT VOID **protocol);

EFI_DEVICE_PATH *make_absolute_device_path (EFI_HANDLE device, CHAR16 *path);
EFI_HANDLE get_self_handle (VOID);
EFI_HANDLE get_self_loaded_image (VOID);
EFI_HANDLE get_self_device_handle (VOID);
EFI_DEVICE_PATH *get_self_device_path (VOID);
EFI_DEVICE_PATH *get_self_file (VOID);

VOID initialise (EFI_HANDLE image, EFI_SYSTEM_TABLE *sys_table);

UINT32 wchar_codepoint (CHAR16 c);

UINTN strlen_w (const CHAR16 *str);
UINTN strlen_a (const CHAR8 *str);

INTN  strcmp_w  (const CHAR16 *a, const CHAR16 *b);
INTN  strncmp_w (const CHAR16 *a, const CHAR16 *b, UINTN len);

CHAR16 *strstr_w (const CHAR16 *haystack, const CHAR16 *needle);
CHAR16 *strdup_w (const CHAR16 *s);
INTN    appendstr_w (CHAR16 *dest, UINTN size, CHAR16 *add);

UINTN   sprintf_a  (CHAR8 *buf, UINT64 size, const char *fmt, ...);
UINTN   vsprintf_a (CHAR8 *buf, UINT64 size, const char *fmt, va_list args);

UINTN   sprintf_w  (CHAR16 *buf, UINT64 size, const CHAR16 *fmt, ...);
UINTN   vsprintf_w (CHAR16 *buf, UINT64 size, const CHAR16 *fmt, va_list args);

VOID mem_copy (void *dest, const VOID *src, UINTN len);
INTN mem_cmp  (const VOID *a, const VOID *b, UINTN len);
VOID mem_set  (VOID *dest, UINT8 c, UINTN n);
INTN guid_cmp (const VOID *a, const VOID *b);

EFI_DEVICE_PATH *handle_device_path (EFI_HANDLE *handle);
EFI_GUID device_path_partition_uuid (EFI_DEVICE_PATH *dp);
CHAR16 *device_path_string (EFI_DEVICE_PATH *dp);
BOOLEAN on_same_device (EFI_DEVICE_PATH *a, EFI_DEVICE_PATH *b);
CHAR16 *guid_str (EFI_GUID *guid);

UINT64 efi_time_to_timestamp (EFI_TIME *time);
VOID   efi_time_to_utc (EFI_TIME *time);
VOID   seconds_to_dhms (UINT64 sec, UINT64 *d, UINT8 *h, UINT8 *m, UINT8 *s);

#endif

CHAR16 *strwiden (CONST CHAR8 *narrow);
CHAR8  *strnarrow (CONST CHAR16 *wide);
CHAR8  *strlower (CHAR8 *str);

CHAR16 *resolve_path (CONST VOID *path, CONST CHAR16* relative_to, UINTN widen);

UINT64 time_usec (VOID);
