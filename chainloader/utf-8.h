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
#include "util.h"

#define UTF8_UNENCODABLE ((UINT32)-1)
#define REPLACEMENT_CHAR 0xfffd

UINT8  utf8_encode_cp (UINT32 cp, UINT8 buf[6]);
CHAR8 *utf8_encode    (const UINT32 *codepoints, INTN len);
INTN   utf8_decode    (const CHAR8 *encoded, UINTN bytes, UINT32 **codepoints);
