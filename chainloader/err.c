// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2019,2021 Collabora Ltd
// Copyright © 2019,2021 Valve Corporation

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

#include "err.h"

UINTN verbose;
UINTN nvram_debug;

UINTN set_verbosity (UINTN level)
{
    UINTN old_level = verbose;

    verbose = level;

    return old_level;
}

UINTN set_nvram_debug (UINTN level)
{
    UINTN old_level = nvram_debug;

    nvram_debug = level;

    return old_level;
}
