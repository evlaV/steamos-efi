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

#include "timer.h"

EFI_EVENT  timer_create (EFI_EVENT_NOTIFY callback, VOID *context)
{
    EFI_EVENT timer = NULL;

    uefi_call_wrapper( BS->CreateEvent, 5,
                       EVT_TIMER|EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                       callback, context, &timer );

    return timer;
}

EFI_STATUS timer_sched  (EFI_EVENT timer, BOOLEAN recur, UINT64 millisec)
{
    if( millisec > 0 )
    {
        // timer is measured in 100 ns units
        UINT64 hns = millisec * 10000;

        return uefi_call_wrapper( BS->SetTimer, 3,
                                  timer,
                                  recur ? TimerPeriodic : TimerRelative,
                                  hns );
    }
    else
    {
        // UEFI allows the delay to be 0, in which case the timer fires on
        // every clock tick - but that would just wedge th eloader, so we
        // are going to treat 0 as "cancel timer".
        return timer_stop( timer );
    }
}

EFI_STATUS timer_stop (EFI_EVENT timer)
{
    return uefi_call_wrapper( BS->SetTimer, 3, timer, TimerCancel, 0 );
}

EFI_STATUS timer_destroy (EFI_EVENT timer)
{
    return uefi_call_wrapper( BS->CloseEvent, 1, timer );
}
