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

#pragma once

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>

#include "util.h"

typedef struct _menu menu;
typedef struct _menu_engine menu_engine;

typedef INTN (*_run_menu)(menu *ui, UINTN start, OUT VOID **chosen);
typedef VOID (*_show_timer)(menu *ui);
typedef VOID (*_free_engine)(menu_engine *engine);

typedef struct _menu_engine
{
    void *private;
    const char *type;
    _run_menu run;
    _free_engine free;
    _show_timer show_timer;
} menu_engine;

typedef struct
{
    CHAR16 label[80];
    CHAR16 blurb[80];
    struct { UINT16 height; UINT16 width; } display;
    VOID *data;
} menu_option;

typedef struct _menu
{
    CHAR16 *title;
    struct { UINT16 x; UINT16 y; } offset;
    struct { UINT16 x; UINT16 y; } screen;
    UINT16 width;
    UINT16 height;
    UINTN entries;
    UINTN label_width;
    menu_engine *engine;
    menu_option *option;
    INTN timeout;
    INTN countdown;
    EFI_EVENT timer;
} menu;

menu *menu_alloc (INTN entries, CONST CHAR16 *title);
VOID menu_free   (menu *menu);
INTN run_menu    (menu *menu, UINTN start, OUT VOID **chosen);
BOOLEAN confirm  (CONST CHAR16 *question, BOOLEAN default_answer);
VOID menu_timeout(menu *menu, INTN timeout);
UINTN menu_sprint_interval (CHAR16 *buf, UINTN bytes, UINT64 sec);
