// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2021,2023 Collabora Ltd
// Copyright © 2021,2023 Valve Corporation

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

// TODO: audit where we are and aren't checking EFI_STATUS return values
// (we don't _have_ to check them all - some of them may be in places
// where there's no real recovery path, but we should do the audit).

#include <efi.h>
#include <efilib.h>
#include <efiprot.h>
#include <eficon.h>

#include "../err.h"
#include "../util.h"
#include "../menu.h"
#include "../console-ex.h"
#include "console.h"

//
// Text Console Menu support
//

static VOID con_del_menu (menu_engine *engine)
{
    efi_free( engine );
}

static EFI_STATUS console_mode (VOID)
{
    EFI_CONSOLE_CONTROL_SCREEN_MODE mode;
    EFI_CONSOLE_CONTROL_PROTOCOL *ccp;
    EFI_STATUS res;
    BOOLEAN locked;
    BOOLEAN uga;
    EFI_GUID ccp_guid = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;

    res = get_protocol( &ccp_guid, NULL, (VOID **)&ccp );
    if( res == EFI_NOT_FOUND )
        return res;
    ERROR_RETURN( res, res, L"Could not get_protocol: %r\n", res );

    res = conctl_get_mode( ccp, &mode, &uga, &locked );
    ERROR_RETURN( res, res, L"Could not conctl_get_mode: %r\n", res );

    if( mode == CONCTL_SCREEN_TEXT )
        return EFI_SUCCESS;

    res = conctl_set_mode( ccp, CONCTL_SCREEN_TEXT );
    ERROR_RETURN( res, res, L"Could not conctl_set_mode: %r\n", res );

    return EFI_SUCCESS;
}


// this is the console output attributes for the menu
#define SELECTED_ATTRIBUTES (EFI_MAGENTA   | EFI_BACKGROUND_BLACK)
#define DEFAULT_ATTRIBUTES  (EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK)
#define TITLE_ATTRIBUTES    (EFI_WHITE     | EFI_BACKGROUND_BLACK)

static CHAR16 *alloc_frame_line(UINTN xa, UINTN xz)
{
    return efi_alloc( (xz - xa + 2) * sizeof(CHAR16) );
}

static VOID fill_frame_line(CHAR16 *line,
                            UINTN xa, UINTN xz,
                            CHAR16 a, CHAR16 c, CHAR16 z)
{
    line[ 0 ]       = a;
    line[ xz - xa ] = z;
    line[ xz - xa + 1 ] = L'\0';

    for( UINTN x = 1; x < xz - xa; x++ )
        line[ x ] = c;
}

static VOID draw_info_frame(menu *ui, IN UINTN padding)
{
    const int col_a = padding;
    const int col_z = ui->screen.x - 1 - padding;
    const int row_a = ui->screen.y - 3;
    const int row_z = ui->screen.y - 1;
    CHAR16 *line = alloc_frame_line( col_a, col_z );

    fill_frame_line( line, col_a, col_z, L'+', L'-', L'+');
    con_set_cursor_position( col_a, row_a );
    con_output_text( line );
    con_set_cursor_position( col_a, row_z );
    con_output_text( line );

    fill_frame_line( line, col_a, col_z, L'|', L' ', L'|');
    con_set_cursor_position( col_a, row_a + 1 );
    con_output_text( line );

    efi_free( line );
}

static VOID show_option_info(menu *ui, IN UINTN nth)
{
    static const UINTN padding = 1;
    const UINTN info_line  = ui->screen.y - 2;
    // padding on either side, plus 2 spaces for the bracketing character, plus
    // 2 spaces for the gap between start and end bracketing chars and the text:
    // |<padding>[ TEXT... ]<padding>|
    const UINTN info_space = ui->screen.x - (2 * (padding + 2));
    CHAR16 *blurb = &ui->option[ nth ].blurb[ 0 ];

    con_set_output_attribute( DEFAULT_ATTRIBUTES );
    draw_info_frame( ui, 1 );
    con_set_cursor_position( padding + 2, info_line );

    if( *blurb )
    {
        UINTN blen = strlen_w( blurb );

        if( blen <= info_space )
        {
            con_output_text( blurb );
        }
        else
        {
            CHAR16 *buf = efi_alloc( (info_space + 1) * sizeof(*buf) );
            mem_copy( buf, blurb, info_space );
            buf[ info_space ] = L'\0';
            con_output_text( buf );
            efi_free( buf );
        }
    }
}

static VOID render_menu_option(menu *ui, IN UINTN nth, BOOLEAN on)
{
    con_set_output_attribute( on ? SELECTED_ATTRIBUTES : DEFAULT_ATTRIBUTES );
    con_set_cursor_position( ui->offset.x, ui->offset.y + nth );
    con_output_text( on ? L"> " : L"  " );
    con_output_text( &ui->option[ nth ].label[ 0 ] );
    con_set_cursor_position( ui->offset.x + ui->width + 2,
                             ui->offset.y + nth );
    con_output_text( on ? L" <" : L"  " );

    if( on )
        show_option_info( ui, nth );
}

static VOID calculate_menu_layout (menu *ui)
{
    EFI_STATUS res;
    UINTN cols;
    UINTN rows;

    ui->width = 0;

    for( INTN i = 0; i < (INTN)ui->entries; i++ )
    {
        UINTN olen = strlen_w( &ui->option[ i ].label[ 0 ] );
        if( olen > ui->width )
            ui->width = olen;
    }

    res = con_output_mode_info( con_get_output_mode(), &cols, &rows);

    // fall back to punchard size if we don't know how big the console is:
    if( EFI_ERROR( res ) )
    {
        cols = 80;
        rows = 25;
    }

    ui->screen.x = cols;
    ui->screen.y = rows;

    // centre the menu vertically
    ui->offset.y = (rows - ui->entries) / 2;

    // ==================================================================
    // … and horizontally:
    INTN offset = cols / 2;
    for( INTN i = 0; i < (INTN)ui->entries; i++ )
    {
        INTN label_len = strlen_w( &ui->option[ i ].label[ 0 ] );
        INTN o = ((cols - label_len) / 2) - 2;

        if( o < 0 )
            o = 0;

        if( o < offset )
            offset = o;
    }

    ui->offset.x = offset;
}

static VOID render_menu (menu *ui, UINTN selected)
{
    calculate_menu_layout( ui );

    // If we have room for the title:
    if( ui->offset.y >= 1 )
    {
        UINTN t_yoff = ui->offset.y - 1;
        UINTN t_len  = strlen_w( ui->title );
        UINTN t_xoff = ( ui->offset.x + 2 +
                         (((INTN)(ui->width - t_len)) / 2) );

        con_set_cursor_position( t_xoff, t_yoff );
        con_set_output_attribute( TITLE_ATTRIBUTES );
        con_output_text( ui->title );
    }

    for( INTN i = 0; i < (INTN)ui->entries; i++ )
        render_menu_option( ui, i, i == (INTN)selected );
}

static INTN con_run_menu (menu *ui, UINTN start, OUT VOID **chosen)
{
    INTN i, selected;
    EFI_STATUS res;
    const INTN opt console_max_mode = con_get_max_output_mode();

    res = console_mode();
    if( EFI_ERROR( res ) && res != EFI_NOT_FOUND )
       return res;

    if( console_max_mode > 0 )
    {
        for( i = console_max_mode - 1; i != 0; i-- )
        {
            res = con_set_output_mode( i );
            if( EFI_ERROR( res ) )
                continue;

            break;
        }
    }

    con_clear_screen();
    con_enable_cursor( FALSE );

    if( start >= ui->entries )
        selected = 0;
    else
        selected = start;

    render_menu( ui, selected );

    con_set_output_attribute( DEFAULT_ATTRIBUTES );
    con_reset( FALSE );

    for( ;; )
    {
        INTN old_selected = selected;
        EFI_INPUT_KEY key;

        con_set_output_attribute( DEFAULT_ATTRIBUTES );

        // we want to wake up every 100 ms to check for a menu timeout
        res = wait_for_key( &key, 100 );

        if( res == EFI_TIMEOUT )
            continue;

        ERROR_BREAK( res, L"wait_for_key( 0x%x, %lu )", &key );

        if( ( key.UnicodeChar == CHAR_LINEFEED ) ||
            ( key.UnicodeChar == CHAR_CARRIAGE_RETURN ) )
        {
            break;
        }
        else if( ( key.ScanCode    == SCAN_ESC ) &&
                 ( key.UnicodeChar == 0        ) )
        {
            selected = -1;
            break;
        }
        else if( key.ScanCode == SCAN_UP )
        {
            if( selected > 0 )
                selected--;
            else
                selected = 0;
        }
        else if( key.ScanCode == SCAN_DOWN )
        {
            if( selected < (INTN)ui->entries - 1 )
                selected++;
            else
                selected = 0;
        }

        if( selected == -1 || selected == old_selected )
            continue;

        render_menu_option( ui, old_selected, FALSE );
        render_menu_option( ui, selected, TRUE );
    }

    if( chosen )
        *chosen = ui->option[ selected ].data;

    con_clear_screen();

    return selected;
}

menu_engine *con_menu_engine (VOID)
{
    menu_engine *engine = efi_alloc( sizeof(menu_engine) );

    if( engine )
    {
        engine->private = NULL;
        engine->type    = "con";
        engine->run     = con_run_menu;
        engine->free    = con_del_menu;
    }

    return engine;
}
