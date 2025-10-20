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

#include <efi.h>

// We used to use SIZE_OF_EFI_FILE_INFO + MAXFSNAMLEN for this,
// but it turns out that was a cargo culted value that could in
// theory be breached. 512 covers us for tha max allowed by vfat.
#define EFI_FILE_INFO_SAFE_SIZE (SIZE_OF_EFI_FILE_INFO + 512)

#define SEEK_TO_EOF 0xffffffffffffffff

EFI_STATUS efi_file_exists (EFI_FILE_PROTOCOL *dir, CONST CHAR16 *path);

EFI_STATUS efi_file_open (EFI_FILE_PROTOCOL *dir,
                          OUT EFI_FILE_PROTOCOL **opened,
                          CONST CHAR16 *path,
                          UINT64 mode,
                          UINT64 attr);

EFI_STATUS efi_mkdir_p (EFI_FILE_PROTOCOL *parent,
                        OUT EFI_FILE_PROTOCOL **dir,
                        CONST CHAR16 *name);

EFI_STATUS efi_file_close (EFI_FILE_PROTOCOL *file);

EFI_STATUS efi_readdir (EFI_FILE_PROTOCOL *dir,
                        IN OUT EFI_FILE_INFO **dirent,
                        IN OUT UINTN *dirent_size);

EFI_STATUS efi_file_seek (EFI_FILE_PROTOCOL *file, UINT64 pos);
EFI_STATUS efi_file_tell (EFI_FILE_PROTOCOL *file, UINT64 *pos);

EFI_STATUS efi_file_read (EFI_FILE_PROTOCOL *fh,
                          IN OUT CHAR8 *buf,
                          IN OUT UINTN *bytes);

EFI_STATUS efi_file_write (EFI_FILE_PROTOCOL *fh,
                           IN CHAR8 *buf,
                           IN OUT UINTN *bytes);

EFI_STATUS efi_mount (EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *part,
                      OUT EFI_FILE_PROTOCOL **root);

EFI_STATUS efi_unmount (IN OUT EFI_FILE_PROTOCOL **root);

VOID ls (EFI_FILE_PROTOCOL *dir,
         UINTN indent,
         CONST CHAR16 *name,
         UINTN recurse);

EFI_STATUS efi_file_stat (EFI_FILE_PROTOCOL *fh,
                          IN OUT EFI_FILE_INFO **info,
                          IN OUT UINTN *bufsize);

EFI_STATUS efi_file_xtime_cmp (EFI_FILE_PROTOCOL *a,
                               EFI_FILE_PROTOCOL *b,
                               INTN *result);

EFI_STATUS efi_file_to_mem (EFI_FILE_PROTOCOL *fh,
                            OUT CHAR8 **buf,
                            OUT UINTN *bytes,
                            OUT UINTN *alloc);
