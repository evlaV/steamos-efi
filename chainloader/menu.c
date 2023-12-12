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

#include "menu.h"
#include "con/menu.h"
#include "gfx/menu.h"
#include "timer.h"

menu *
menu_alloc (INTN entries, CONST CHAR16 *title)
{
    menu *ui = efi_alloc( sizeof(menu) );

    ui->title   = strdup_w( title ?: L"-" );
    ui->option  = efi_alloc( entries * sizeof(menu_option) );
    ui->entries = entries;
    ui->label_width = 0;

    // try various backends here:
    ui->engine = gfx_menu_engine();

    if( !ui->engine )
        ui->engine = con_menu_engine();

    DEBUG_LOG( "allocated menu %s<%a>[%d] engine: 0x%x",
               ui->title, ui->engine->type, entries, ui->engine );

    return ui;
}

VOID EFIAPI menu_timer_tick (EFI_EVENT opt timer, VOID *data)
{
    menu *ui = (menu *)data;

    if( ui->engine->show_timer )
        ui->engine->show_timer( ui );

    ui->countdown--;
}

static VOID setup_menu_timer (menu *ui)
{
    if( ui->timeout > 0 && ui->countdown > 0 )
    {
        if( ui->timer == NULL )
            ui->timer = timer_create( menu_timer_tick, ui );
    }
    else
    {
        if( ui->timer == NULL )
            return;

        timer_destroy( ui->timer );
        ui->timer = NULL;
    }
}

VOID
menu_timeout (menu *ui, INTN timeout)
{
    ui->timeout = timeout;
    ui->countdown = timeout;
    setup_menu_timer( ui );
}

VOID
menu_free (menu *ui)
{
    for( UINTN i = 0; i < ui->entries; i++ )
        efi_free( ui->option[ i ].data );

    if( ui->timer )
    {
        timer_stop( ui->timer );
        timer_destroy( ui->timer );
    }

    ui->engine->free( ui->engine );
    efi_free( ui->title );
    efi_free( ui->option );
    efi_free( ui );
}

INTN run_menu (menu *ui, UINTN start, OUT VOID **chosen)
{
    setup_menu_timer( ui );
    return ui->engine->run( ui, start, chosen );
}

BOOLEAN confirm (CONST CHAR16 *question, BOOLEAN default_answer)
{
    BOOLEAN answer = default_answer;
    menu *yn = menu_alloc( 2, question );
    const UINT64 llen = sizeof( yn->option[ 0 ].label );

    SPrint( &yn->option[ 0 ].label[ 0 ], llen, L"Yes" );
    SPrint( &yn->option[ 1 ].label[ 0 ], llen, L"No"  );

    answer = run_menu( yn, default_answer ? 0 : 1, NULL );

    menu_free( yn );

    return answer == 0;
}

UINTN menu_sprint_interval (CHAR16 *buf, UINTN bytes, UINT64 sec)
{
    UINT64 d;
    UINT8 h, m, s;
    UINTN rv = 0;

    seconds_to_dhms( sec , &d, &h, &m, &s );

    if( d )
        rv = sprintf_w( buf, bytes, L"%lud %02uh %02um %02us", d, h, m, s );
    else if( h )
        rv = sprintf_w( buf, bytes, L"%uh %02um %02us", h, m, s );
    else if( m )
        rv = sprintf_w( buf, bytes, L"%um %02us", m, s );
    else
        rv = sprintf_w( buf, bytes, L"%us", s );

    buf[ (bytes / sizeof(*buf)) - 1 ] = 0;

    return rv;
}
