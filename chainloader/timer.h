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

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include <efistdarg.h>

#include "err.h"
#include "util.h"

// note that the callback must be tagged "EFIAPI" as it is invoked
// with the UEFI calling convention, not ELF.
// eg VOID EFIAPI callbackname (EFI_EVENT e, VOID *d)
EFI_EVENT  timer_create (EFI_EVENT_NOTIFY callback, VOID *context);

// a millisec value of 0 is equivalent to timer_stop
EFI_STATUS timer_sched  (EFI_EVENT timer, BOOLEAN recur, UINT64 millisec);

EFI_STATUS timer_stop   (EFI_EVENT timer);
EFI_STATUS timer_destroy(EFI_EVENT timer);
