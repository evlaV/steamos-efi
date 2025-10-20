// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2022-2023 Collabora Ltd
// Copyright © 2022-2023 Valve Corporation

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
#include <eficon.h>

#include "err.h"
#include "util.h"
#include "timer.h"
#include "con/console.h"
#include "console-ex.h"

static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *console;

static BOOLEAN
init_console_ex (void)
{
    EFI_STATUS res;
    static EFI_GUID input_guid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;

    if( !console )
    {
        EFI_KEY_TOGGLE_STATE state = EFI_KEY_STATE_EXPOSED;

        res = get_handle_protocol( &ST->ConsoleInHandle, &input_guid,
                                   (VOID **)&console );
        ERROR_RETURN( res, FALSE, L"console-ex init failed" );

        // clear out any buffered keys etc
        reset_console();

        // In theory this allows things like incomplete keypresses
        // (possibly key-press but no key-release yet? - docs unclear)
        // to be detected but not all UEFI firmware supports this
        // (The deck, at least as of VANGOGH 101, does not):
        res = uefi_call_wrapper( console->SetState, 2, console, &state );

        if( EFI_ERROR(res) )
            v_msg( L"console-ex set_state error: %d (likely harmless)\n", res );
    }

    if( console )
        return TRUE;

    return FALSE;
}

EFI_STATUS reset_console (VOID)
{
    if( !init_console_ex() )
        return EFI_NOT_READY;

    return uefi_call_wrapper( console->Reset, 2, console, FALSE );
}

EFI_HANDLE *
bind_key (UINT16 scan, CHAR16 chr, IN EFI_KEY_NOTIFY_FUNCTION handler)
{
    EFI_STATUS res;
    EFI_HANDLE *binding;
    EFI_KEY_DATA key = { { SCAN_NULL, CHAR_NULL },
                         { 0, 0 } };

    key.Key.ScanCode = scan;
    key.Key.UnicodeChar = chr;

    if( !init_console_ex() )
        return NULL;

    res = uefi_call_wrapper( console->RegisterKeyNotify, 4, console,
                             &key, handler, (VOID **)&binding );

    ERROR_RETURN( res, NULL,
                  L"Cannot bind key {%u, 0x%04x} to callback\n", scan, chr );

    return binding;
}

EFI_STATUS
unbind_key (EFI_HANDLE *binding)
{
    if( !console )
        return EFI_NOT_READY;

    return uefi_call_wrapper( console->UnregisterKeyNotify, 2,
                              console, binding );
}

EFI_STATUS
wait_for_key (EFI_INPUT_KEY *key, UINT64 millisec)
{
    EFI_STATUS res;
    EFI_EVENT waiters[2];
    // our queue is at most 2 deep, so this can only be 1 or 0 after a wait
    UINTN index = 2;

    if( key )
    {
        key->ScanCode = SCAN_NULL;
        key->UnicodeChar = CHAR_NULL;
    }

    // we're only allowing a minute maximum for the timeout, if specified:
    millisec = MIN( millisec, 60000 );

    if( millisec > 0 )
    {
        // timer is measured in 100 ns units
        UINTN index = 2;
        EFI_EVENT timer;

        // we do not call timer_create because we're specifically _not_
        // setting up a timer with a callback:
        res = uefi_call_wrapper( BS->CreateEvent, 5,
                                 EVT_TIMER, 0, NULL, NULL, &timer);
        ERROR_RETURN( res, res, L"Creating timeout" );

        res = timer_sched( timer, FALSE, millisec );
        ERROR_RETURN( res, res, L"Starting %lu millisec timer", millisec );

        waiters[0] = ST->ConIn->WaitForKey;
        waiters[1] = timer;

        res = uefi_call_wrapper( BS->WaitForEvent, 3, 2, waiters, &index );

        // the timeout event fired successfully before a keypress
        if( !EFI_ERROR(res) )
        {
            if( index == 1 )
                res = EFI_TIMEOUT;
            else if ( key )
                res = con_read_key( key );
        }

        // no point checking the return code here, we either
        // free the event or we… don't?
        timer_destroy( timer );
    }
    else
    {
        waiters[0] = ST->ConIn->WaitForKey;
        res = uefi_call_wrapper( BS->WaitForEvent, 3, 1, waiters, &index );

        if( !EFI_ERROR(res) )
            res = con_read_key( key );
    }

    return res;
}
