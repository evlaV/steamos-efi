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

#include "config.h"
#include "console-ex.h"

typedef enum
{
    MENU_REASON_NONE = 0,
    MENU_REASON_FAILSAFE,    // triggered by boot failure count or similar
    MENU_REASON_INTERACTIVE, // keypress or similar
    MENU_REASON_CONFIG,      // flag file or boot config or similar
    MENU_REASON_CMDLINE,     // UEFI cmdline
    MENU_REASON_MISC,        // some other reason
} MENU_REASON;

typedef struct
{
    EFI_HANDLE partition;
    EFI_DEVICE_PATH *device_path;
    CHAR16 *loader_path;
    cfg_entry *config;
    CONST CHAR16 *args;
    struct
    {
        UINT8 is_restricted;
        EFI_DEVICE_PATH *device_path;
    } criteria;
} bootloader;

EFI_STATUS EFIAPI request_menu (IN EFI_KEY_DATA *k opt);
MENU_REASON boot_menu_requested (VOID);
VOID request_boot_menu (MENU_REASON why);
VOID request_verbose_boot (VOID);
EFI_STATUS console_mode (VOID);

EFI_STATUS valid_efi_binary (EFI_FILE_PROTOCOL *dir, CONST CHAR16 *path);
EFI_STATUS set_steamos_loader_criteria (OUT bootloader *loader);
EFI_STATUS migrate_bootconfs (EFI_HANDLE *handles,
                              CONST UINTN n_handles,
                              EFI_DEVICE_PATH *self_dev_path);
EFI_STATUS find_loaders (EFI_HANDLE *handles,
                         CONST UINTN n_handles,
                         IN OUT bootloader *chosen);
EFI_STATUS choose_steamos_loader (IN OUT bootloader *chosen);
EFI_STATUS exec_bootloader (bootloader *boot);
