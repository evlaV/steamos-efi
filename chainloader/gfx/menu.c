// steamos-efi  --  SteamOS EFI Chainloader

// SPDX-License-Identifier: GPL-2.0+
// Copyright © 2023,2025 Collabora Ltd
// Copyright © 2023,2025 Valve Corporation

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

#include "../err.h"
#include "../util.h"
#include "../menu.h"
#include "../console-ex.h"
#include "../timer.h"
#include "gfx.h"
#include "font.h"

//
// Graphics Mode menu support
//

// step size when adding height to the info frame (and its minimum size):
#define INFO_FRAME_STRIPE_HEIGHT 64

// minimum headroom wanted for the title (this is a guess since
// it depends on the font used):
#define MENU_TITLE_MIN_HEIGHT 8

typedef enum
{
    ATTR_DEFAULT,
    ATTR_SELECTED,
    ATTR_TITLE,
    ATTR_INFO,
} output_attributes;

typedef struct _gm_priv
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx;
    UINT32 foreground;
    UINT32 background;
    UINT32 outline;
    UINT16 info_frame_size;
    UINT32 mode;
    struct
    {
        UINT16 height;
        UINT16 width;
    } last_timer;
} gm_priv;

static VOID set_output_attributes (gm_priv *priv, output_attributes attr)
{
    switch( attr )
    {
      case ATTR_TITLE:
        priv->foreground = 0xffffff;
        priv->background = 0x000000;
        priv->outline    = 0x000000;
        break;

      case ATTR_SELECTED:
        priv->foreground = 0x87ceeb;
        priv->background = 0x2b2b2b;
        priv->outline    = 0xb0e0e6;
        break;

      case ATTR_INFO:
        priv->foreground = 0xffffff;
        priv->background = 0x000000;
        priv->outline    = 0x87ceeb;
        break;

      case ATTR_DEFAULT:
      default:
        priv->foreground = 0xdcdcdc;
        priv->background = 0x000000;
        priv->outline    = 0x000000;
    }
}

static VOID gfx_del_menu (menu_engine *engine)
{
    efi_free( engine->private );
    efi_free( engine );
}

static UINT32 choose_mode (gm_priv *priv opt)
{
    UINT32 mode = 0;
    UINT32 chosen = 0;
    UINT32 max_mode = 0;
    UINT32 last_score = 0;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx = NULL;

    gfx = gfx_get_interface();
    max_mode = gfx_max_mode( gfx );

    for( mode = 0; mode < max_mode; mode++ )
    {
        UINT32 score = gfx_mode_score( gfx, mode );

        if( score > last_score )
        {
            chosen = mode;
            last_score  = score;
        }
    }

    DEBUG_LOG("Scoring algorithm chose mode #%d", chosen);
    return chosen;
}

static EFI_STATUS set_mode (menu_engine *engine)
{
    EFI_STATUS res = EFI_SUCCESS;
    gm_priv *priv = engine->private;

    if( !priv->gfx )
        res = EFI_NOT_FOUND;
    ERROR_RETURN( res, res, L"Graphics node not available" );

    res = gfx_set_mode( priv->gfx, priv->mode );
    ERROR_RETURN( res, res, L"Graphics mode %d not set", priv->mode );

    return res;
}

#define SHRINK_RECTANGLE(x,y,w,h,delta) \
    ({ x += delta; y += delta; w -= delta * 2; h -= delta * 2; })
static VOID draw_info_frame( menu *ui, UINT16 *w, UINT16 *h)
{
    // make in info frame multiples of INFO_FRAME_STRIPE_HEIGHTpx in height
    // that's big enough to hold a text box h pixels high:
    UINT16 vertical = INFO_FRAME_STRIPE_HEIGHT;
    BOOLEAN redraw = 0;
    gm_priv *priv = ui->engine->private;

    while( (vertical + 1) < MIN(*h, ui->screen.y) )
        vertical += INFO_FRAME_STRIPE_HEIGHT;

    if( priv->info_frame_size < vertical )
        redraw = 1;

    // don't shrink the frame
    if( priv->info_frame_size > vertical )
        vertical = priv->info_frame_size;

    UINT16 top    = ui->screen.y - vertical - 1;
    UINT16 left   = 0;
    UINT16 width  = ui->screen.x - 1;
    UINT16 height = vertical - 1;

    // we skip this redraw if there's already a frame as  we avoid unsightly
    // flashing (as we're not double buffering)
    if( redraw )
    {
        gfx_draw_box( priv->gfx, left, top, width, height,
                      priv->outline, priv->background );

        priv->info_frame_size = vertical;
    }
    else
    {
        // clear the text area as the new text might be shorter than the old:
        SHRINK_RECTANGLE( left, top, width, height, 1 );
        gfx_fill_rectangle( priv->gfx, priv->background,
                            left, top, width, height );
    }

    top  = top + ((height - *h)/ 2);
    left = (width - *w) / 2;

    *w = left;
    *h = top;
}

static VOID show_option_info (menu *ui, IN UINTN nth)
{
    gm_priv *priv = ui->engine->private;
    CHAR16 *blurb = &ui->option[ nth ].blurb[ 0 ];
    UINT16 w, h;
    UINT16 x, y;

    set_output_attributes( priv, ATTR_INFO );
    font_string_display_size( NULL, blurb, &w, &h );

    x = w;
    y = h;
    draw_info_frame( ui, &x, &y );

    font_output_text( priv->gfx, NULL, blurb, 0,
                      x, y, priv->foreground, DECOR_NONE,
                      NULL, NULL );
}

static VOID render_menu_option(menu *ui, IN UINTN nth, BOOLEAN on)
{
    UINT16 dy = 0;
    gm_priv *priv = ui->engine->private;

    if( nth >= ui->entries )
        return;

    set_output_attributes( priv, on ? ATTR_SELECTED : ATTR_DEFAULT );

    for( UINTN i = 0; i < nth; i++ )
        dy += ui->option[ i ].display.height;

    // undraw the highlight box _before_ handling the unselected option text
    if( !on )
        gfx_draw_box( priv->gfx,
                      ui->offset.x, ui->offset.y + dy,
                      ui->width, ui->option[ nth ].display.height,
                      priv->background, ~0 );

    font_output_text( priv->gfx, NULL, ui->option[ nth ].label, 0,
                      ui->offset.x, ui->offset.y + dy,
                      priv->foreground, DECOR_NONE,
                      NULL, NULL );

    // draw the highlight box _after_ handling the selected option text
    if( on )
        gfx_draw_box( priv->gfx,
                      ui->offset.x, ui->offset.y + dy,
                      ui->width, ui->option[ nth ].display.height,
                      priv->outline, ~0 );

    if( on )
        show_option_info( ui, nth );
}

static VOID calculate_menu_layout (menu *ui)
{
    gm_priv *priv = ui->engine->private;

    ui->width  = 0;
    ui->height = 0;

    for( UINTN i = 0; i < ui->entries; i++ )
    {
        UINT16 h, w;
        UINTN olen =
          font_string_display_size( NULL, &ui->option[ i ].label[ 0 ], &w, &h );

        if( olen == 0 )
            continue;

        if( w > ui->width )
            ui->width = w;

        ui->option[ i ].display.height = h;
        ui->option[ i ].display.width  = w;
        ui->height += h;
    }

    gfx_current_resolution( priv->gfx, &ui->screen.x, &ui->screen.y );

    // centre the menu vertically
    ui->offset.y = (ui->screen.y - ui->height) / 2;

    // ==================================================================
    // … and horizontally:
    INTN offset = ui->width / 2;
    for( UINTN i = 0; i < ui->entries; i++ )
    {
        INTN o = (ui->screen.x - ui->option[ i ].display.width) / 2;

        if( o < 0 )
            o = 0;

        if( o < offset )
            offset = o;
    }

    ui->offset.x = offset;
}

static VOID gfx_show_timer (menu *ui)
{
    CHAR16 text[11]; // space for "Xh XXm XXs" - 10 chars + NUL
    UINT16 w, h;
    UINT16 x, y;
    gm_priv *priv;
    static GLYPH *spc = NULL;

    if( ui->timeout <= 0 )
        return;

    if( spc == NULL )
        spc = font_get_glyph( NULL, L' ' );

    priv = ui->engine->private;

    // prepare the display text
    menu_sprint_interval( text, sizeof(text), ui->countdown );

    // figure out how big it's going to appear:
    font_string_display_size( NULL, text, &w, &h );

    // Drawing to exact pixel locations so need to add blank padding
    // when the countdown label shrinks:
    if( priv->last_timer.width > w || priv->last_timer.height > h )
    {
        gfx_fill_rectangle( priv->gfx, 0,
                            ui->screen.x - priv->last_timer.width - 2, 0,
                            priv->last_timer.width, priv->last_timer.height );
    }

    priv->last_timer.width  = w;
    priv->last_timer.height = h;

    // always leaving 2 pixels on the end, last char is always 's'
    // so hopefully there will never be any jitter there:
    x = ui->screen.x - w - 2;

    // All our digits _should_ be the same height so we can probably
    // skip any snap-to-baseline grid calculations here:
    y = 0;

    // now paint it on the screen:
    set_output_attributes( priv, ATTR_TITLE );
    font_output_text( priv->gfx, NULL, text, 0,
                      x, y,
                      priv->foreground, DECOR_NONE,
                      NULL, NULL );
}

static VOID render_menu (menu *ui, UINTN selected)
{
    UINTN t_yoff = 0;
    gm_priv *priv = ui->engine->private;

    calculate_menu_layout( ui );
    gfx_fill_screen( priv->gfx, 0x000000 );
    priv->info_frame_size = 0;

    // If we might have room for the title (min title height: 8px)
    if( ui->offset.y >= MENU_TITLE_MIN_HEIGHT )
    {
        UINTN t_xoff = 0;
        UINT16 t_height, t_width;

        font_string_display_size( NULL, ui->title, &t_width, &t_height );
        t_xoff = (ui->screen.x - t_width) / 2;

        // _definitely_ have (vertical) space for the title
        if( ui->offset.y >= t_height )
        {
            set_output_attributes( priv, ATTR_TITLE );
            font_output_text( priv->gfx, NULL, ui->title, 0,
                              t_xoff, t_yoff,
                              priv->foreground, DECOR_UNDERLINE,
                              NULL, NULL );
        }
    }

    for( UINTN i = 0; i < ui->entries; i++ )
        render_menu_option( ui, i, i == selected );
}

static INTN gfx_run_menu (menu *ui, UINTN start, OUT VOID **chosen)
{
    INTN selected;
    EFI_STATUS res;
    gm_priv *priv = ui->engine->private;

    res = set_mode( ui->engine );
    ERROR_RETURN( res, res, L"No graphics mode available" );

    if( start >= ui->entries )
        selected = 0;
    else
        selected = start;

    render_menu( ui, selected );

    if( ui->timer )
        timer_sched( ui->timer, TRUE, 1000 );

    set_output_attributes( priv, ATTR_DEFAULT );

    for( ;; )
    {
        INTN old_selected = selected;
        EFI_INPUT_KEY key;

        set_output_attributes( priv, ATTR_DEFAULT );

        // we want to wake up every 100 ms to check for a menu timeout
        res = wait_for_key( &key, 100 );

        if( res == EFI_TIMEOUT )
        {
            if( ui->timeout > 0 && ui->countdown <= 0 )
            {
                DEBUG_LOG( "timeout %d expired", ui->timeout );
                break;
            }

            continue;
        }

        ERROR_BREAK( res, L"wait_for_key( 0x%x, %lu )", &key );

        // key press. reset timeout countdown:
        ui->countdown = ui->timeout;
        gfx_show_timer( ui );

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

    gfx_fill_screen( priv->gfx, 0x000000 );

    if( ui->timer )
        timer_stop( ui->timer );

    return selected;
}

menu_engine *gfx_menu_engine (VOID)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx = gfx_get_interface();
    menu_engine *engine = NULL;
    gm_priv *priv = NULL;

    if( !gfx )
        goto cleanup;

    engine = efi_alloc( sizeof(menu_engine) );

    if( !engine )
        goto cleanup;

    priv = efi_alloc( sizeof(gm_priv) );

    if( !priv )
        goto cleanup;

    // check we can actually render text:
    for( UINT16 cp = L' '; cp <= L'z'; cp++ )
    {
        if( font_get_glyph( NULL, cp ) != NULL )
            continue;

        DEBUG_LOG( "No glyph for #x%02x, graphics menu not viable", cp );
        goto cleanup;
    }

    // make sure the chosen graphics mode is one we can handle
    UINT32 mode = choose_mode( priv );
    EFI_STATUS rc = gfx_mode_supported( gfx, mode );
    ERROR_JUMP( rc, cleanup, "Graphics mode %d", mode );

    priv->gfx  = gfx;
    priv->mode = mode;

    // required members
    engine->private = priv;
    engine->type    = "gfx";
    engine->run     = gfx_run_menu;
    engine->free    = gfx_del_menu;
    // optional members:
    engine->show_timer = gfx_show_timer;

    return engine;

cleanup:
    efi_free( priv );
    efi_free( engine );

    return NULL;
}
