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

// PFF2 fonts were created by and for grub2 and the code and documentation
// from that project were used as a guideline & reference for steamos-efi's
// font support.

#include "font.h"
#include "pff2.h"
#include "../util.h"
#include "../utf-16.h"
#include "../fileio.h"
#include "../err.h"

#define DEBUG_CHAR(x)                                                   \
    ({ _debug_glyph = 1;                                                \
       debug_glyph( (UINT16)x );                                        \
       font_draw_glyph_at_xy( gfx, font_get_glyph(NULL, x), 0xffffff, 0, 0 ); \
       _debug_glyph = 0; })

static UINT8 _debug_glyph;

static FONT_NODE *font_list = NULL;
static GLYPH *unknown_glyph = NULL;
static UINT8 unknown_bitmap[] =
{
    0x7c,  /*  #####  */
    0x82,  /* #     # */
    0xba,  /* # ### # */
    0xaa,  /* # # # # */
    0xaa,  /* # # # # */
    0x8a,  /* #   # # */
    0x9a,  /* #  ## # */
    0x92,  /* #  #  # */
    0x92,  /* #  #  # */
    0x92,  /* #  #  # */
    0x92,  /* #  #  # */
    0x82,  /* #     # */
    0x92,  /* #  #  # */
    0x82,  /* #     # */
    0x7c,  /*  #####  */
    0x00   /*         */
};

static FONT empty_font;

static void font_init (FONT *font)
{
  font->name       = 0;
  font->file       = 0;
  font->family     = 0;
  font->point      = 0;
  font->weight     = 0;
  font->leading    = 1;
  font->max.width  = 0;
  font->max.height = 0;
  font->ascent     = 0;
  font->descent    = 0;
  font->chars      = 0;
  font->chr_index  = NULL;
  font->bmp_index  = NULL;
}

static FONT *
font_alloc (VOID)
{
    FONT *font = efi_alloc( sizeof(FONT) );

    font_init( font );

    return font;
}

static EFI_STATUS
font_alloc_blitbuffer (FONT *font, EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                       UINT16 *width, UINT16 *height)
{
    // We need to allocate enough space to hold the full ascent + descent,
    // which is potentially GREATER than the max.height, since max.height
    // does not consider descenders, eg in the letter 'y':
    UINT16 w = font->max.width;
    UINT16 h = MAX( font->max.height, font->ascent + font->descent );

    if( width )
        *width = w;

    if( height )
        *height = h;

    return gfx_alloc_blitbuffer( gfx, &font->blit_buffer, w, h );
}

static VOID
font_free (FONT *font)
{
    if( !font )
        return;

    if( font->file )
        efi_file_close( font->file );

    efi_free( font->name );
    efi_free( font->family );
    gfx_dealloc_blitbuffer( &(font->blit_buffer) );

    if( font->chr_index )
        for( UINT64 i = 0; i < font->chars; i++ )
            efi_free( (font->chr_index + i)->glyph );

    efi_free( font->chr_index );
    efi_free( font->bmp_index );
    // for( i = 0; i < font->chars; i++ ) free each char glyph?
    mem_set( font, 0, sizeof(FONT) );
    efi_free( font );
}

static VOID font_system_init (VOID)
{
    if( unknown_glyph )
        return;

    _debug_glyph = 0;

    unknown_glyph = efi_alloc( sizeof(GLYPH) + sizeof(unknown_bitmap) );
    unknown_glyph->width        = 8;
    unknown_glyph->height       = 16;
    unknown_glyph->offset.x     = 0;
    unknown_glyph->offset.y     = -3;
    unknown_glyph->device_width = 8;
    unknown_glyph->font         = &empty_font;
    mem_copy( unknown_glyph->bitmap,
              unknown_bitmap, sizeof(unknown_bitmap) );

    font_init (&empty_font);

    empty_font.name       = (CHAR8 *) "<No Font>";
    empty_font.family     = (CHAR8 *) "<No Family>";
    empty_font.ascent     = unknown_glyph->height - 3;
    empty_font.descent    = 3;
    empty_font.max.width  = unknown_glyph->width;
    empty_font.max.height = unknown_glyph->height;
    empty_font.chars      = 1;

    DEBUG_LOG("font system initialised");
}

static VOID add_font (FONT *font)
{
    FONT_NODE *node = efi_alloc( sizeof(FONT_NODE) );

    node->font = font;
    node->next = font_list;
    font_list  = node;
}

static VOID del_font (FONT *font)
{
    FONT_NODE **nextp = &font_list;
    FONT_NODE *cur    = *nextp;

    while( cur )
    {
      if (cur->font == font)
	{
	  *nextp = cur->next;
	  efi_free (cur);
	  return;
	}

      nextp = &cur->next;
      cur   = cur->next;
    }
}

VOID unload_fonts (VOID)
{
    while( font_list )
    {
        FONT *del = font_list->font;
        del_font( del );
        font_free( del );
    }
}

FONT *
font_load (EFI_FILE_PROTOCOL *dir, CHAR16 *path)
{
    FONT *font = NULL;
    EFI_FILE_PROTOCOL *src = NULL;
    EFI_STATUS res;

    font_system_init();

    res = efi_file_open( dir, &src, path, EFI_FILE_MODE_READ, 0 );
    ERROR_JUMP( res, cleanup, L"Open font %s failed", path );

    font = font_alloc();
    res = pff2_load_file( src, font );
    ERROR_JUMP( res, cleanup, L"PFF2 load %s failed", path );

    DEBUG_LOG( "Loaded font: '%a'.'%a'",
               font->family ?: (CHAR8 *)"-none-",
               font->name   ?: (CHAR8 *)"-****-" );
    DEBUG_LOG( "Ascent: %d; Descent: %d; Max %d x %d; Chars: %d",
               font->ascent, font->descent,
               font->max.width, font->max.height,
               font->chars );

    add_font( font );
    return font;

cleanup:
    font_free( font );
    efi_file_close( src );
    return NULL;
}

GLYPH *
font_get_glyph (FONT *font, UINT32 cp)
{
    GLYPH *glyph = NULL;

    if( font )
        return font->lookup_glyph( font, cp );

    for( FONT_NODE *node = font_list; node && !glyph; node = node->next )
        glyph = node->font->lookup_glyph( node->font, cp );

    return glyph;
}

UINT16
font_string_display_size (FONT *font, CONST CHAR16 *str,
                          UINT16 *width, UINT16 *height)
{
    GLYPH *g;
    UINT16 w = 0;
    UINT16 h = 0;
    UINT16 l = 0;
    UINT16 str_len = 0;

    if( width )
        *width = 0;

    if( height )
        *height = 0;

    UINT32 *codepoint = NULL;
    UINTN chars = utf16_decode( (const CHAR8 *)str, 0, &codepoint );

    for( UINTN c = 0; c < chars; c++ )
    {
        if( (g = font_get_glyph( font, codepoint[c] )) == NULL )
            continue;

        str_len++;
        w += g->device_width;
        h  = MAX( h, (g->font ? g->font->ascent + g->font->descent : g->height) );
        l  = MAX( l, (g->font ? g->font->leading : 0 ) );
    }

    efi_free( codepoint );

    if( width )
        *width = w;

    if( height )
        *height = h + l;

    return str_len;
}

const CHAR8 *font_name   (FONT *f) { return f->name;   }
const CHAR8 *font_family (FONT *f) { return f->family; }

UINT16 font_ascent     (FONT *f) { return f->ascent;     }
UINT16 font_descent    (FONT *f) { return f->descent;    }
UINT16 font_max_width  (FONT *f) { return f->max.width;  }
UINT16 font_max_height (FONT *f) { return f->max.height; }
UINT16 font_leading    (FONT *f) { return f->leading;    }
UINT16 font_height     (FONT *f) { return f->ascent + f->descent + f->leading; }

UINT16 font_xheight (FONT *f, UINT16 *ssize)
{
    GLYPH *g = NULL;
    UINT16 sc = 0;
    UINT16 ht = 0;
    UINT32 samples[] = { 'x', 'v', 'w', 'z', 0 };
    UINT16 xh = 0;

    for( UINT32 *cp = samples; cp && *cp; cp++ )
    {
        if( (g = f->lookup_glyph( f, *cp )) != NULL )
        {
            ht += g->height;
            sc++;
        }
    }

    if( sc > 0 )
        xh = ht / sc;
    else
        xh = f->ascent / 2;

    if( ssize )
        *ssize = sc;

    return xh;
}

#define FONTX(f,x) ((f) ? ((f)->x ?: (CHAR8 *)"-") : (CHAR8 *)"??")
VOID debug_glyph (UINT32 cp)
{
    GLYPH *glyph = font_get_glyph( NULL, cp );

    if( glyph )
    {
        DEBUG_LOG( "CP %04x: FONT %a.%a; %d x %d @ %d . %d px [dw: %d]",
                   cp,
                   FONTX(glyph->font,family),
                   FONTX(glyph->font,name),
                   glyph->width, glyph->height,
                   glyph->offset.x, glyph->offset.y,
                   glyph->device_width );
    }
    else
    {
        DEBUG_LOG( "No font has a glyph for CP %d", cp );
    }
}

static inline BOOLEAN
nth_bit_is_set (UINT8 *src, UINT32 offset)
{
    return (src[offset / 8] & (0x80 >> (offset % 8))) ? 1 : 0;
}

static VOID debug_1bpp_bitmap (UINT8 *src, UINT32 width, UINT32 height)
{
    CHAR8 *row = efi_alloc( width + 1 );

    row[ width ] = 0;

    for( UINT32 j = 0; j < height; j++ )
    {
        for( UINT32 i = 0; i < width; i ++ )
            row[ i ] = nth_bit_is_set( src, (j * width) + i ) ? '#' : ' ';
        DEBUG_LOG( "#- %02d %a", j, row );
    }

    efi_free( row );
}

static VOID debug_32bpp_bitmap (UINT32 *src, UINT32 width, UINT32 height)
{
    CHAR8 *row = efi_alloc( width + 1 );
    UINT32 colour = 0;
    row[ width ] = 0;

    for( UINT32 j = 0; j < height && !colour; j++ )
        for( UINT32 i = 0; i < width && !colour; i++ )
            colour = src[ (j * width) + i  ] & 0x00ffffff;

    DEBUG_LOG( "%d x %d 32bpp pixmap (#%08x)", width, height,
               colour );

    for( UINT32 j = 0; j < height; j++ )
    {
        UINT32 row_colour_sample = 0;
        for( UINT32 i = 0; i < width; i ++ )
        {
            UINT32 pixel = src[ (j * width) + i  ];
            BOOLEAN is_set = (pixel & 0x00ffffff) != 0;

            row[ i ] = is_set ? '#' : ' ';
            if( is_set && !row_colour_sample )
                row_colour_sample = pixel;
        }
        DEBUG_LOG( "#+ %03d #%08x |%a|", j, row_colour_sample, row );
    }

    efi_free( row );
}

// Draw glyph at x, y (0, 0 is at the _top_ left), in hex triplet colour rgb
// x, y are the _top_ left corner of the glyph:
EFI_STATUS
font_draw_glyph_at_xy (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx,
                       GLYPH *glyph,
                       UINT32 triplet,
                       UINT32 x,
                       UINT32 y)
{
    EFI_STATUS res;
    UINT16 cell_width  = 0;
    UINT16 cell_height = 0;

    // The device width is the actual display width of a char. This is
    // especially important for characters like SPC with 0-length bitmaps:
    if( glyph->device_width == 0 )
        return EFI_SUCCESS;

    // this does not reallocate if the blitbuffer is already the right size:
    font_alloc_blitbuffer( glyph->font, gfx, &cell_width, &cell_height );

    UINT16 baseline = cell_height - glyph->font->descent; // measured from top
    UINT16 y_offset = baseline - glyph->height - glyph->offset.y;

    res = gfx_convert_bitmap( gfx,
                              glyph->bitmap,
                              glyph->width,
                              glyph->height,
                              1,
                              triplet,
                              &glyph->font->blit_buffer,
                              cell_width,
                              cell_height,
                              glyph->offset.x,
                              y_offset );

    if( _debug_glyph )
    {
        debug_1bpp_bitmap( glyph->bitmap, glyph->width, glyph->height );
        debug_32bpp_bitmap( glyph->font->blit_buffer.data,
                            cell_width,
                            cell_height );
    }

    ERROR_RETURN( res, res, L"convert bitmap %d x %d",
                  glyph->width, glyph->height );

    return gfx_blit_out( gfx,
                         &glyph->font->blit_buffer,
                         cell_width,
                         cell_height,
                         x, y );
}

UINT16
font_output_text (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx, FONT *font,
                  CHAR16 *txt, UINT16 c_limit,
                  UINT16 x, UINT16 y,
                  UINT32 triplet,
                  FONT_DECORATION decor,
                  UINT16 *dx, UINT16 *dy)
{
    UINT16 o_count = 0;
    UINT16 x_limit;
    UINT16 y_limit;
    GLYPH *g;
    UINT16 width  = 0;
    UINT16 height = 0;
    UINT32 *codepoint = NULL;
    UINT32 chars = 0;

    gfx_current_resolution( gfx, &x_limit, &y_limit );

    // offscreen
    if( x >= x_limit || y >= y_limit )
        return 0;

    if( dx )
        *dx = 0;

    if( dy )
        *dy = 0;

    chars = utf16_decode( (const CHAR8 *)txt, 0, &codepoint );

    for( UINTN n = 0; (n < chars) && ((c_limit == 0) || (o_count < c_limit)); n++ )
    {
        UINT16 w = 0;
        UINT16 h = 0;

        // no glyph, try next char
        if( (g = font_get_glyph( font, codepoint[n] )) == NULL )
        {
            // counts towards output even if we cannot find a glyph:
            DEBUG_LOG("No glyph for cp %04x", codepoint[n]);
            o_count++;
            continue;
        }

        w = g->device_width;
        h = g->font->max.height;

        // run off the right edge of the screen. stop.
        if( (x + w) >= x_limit )
            break;

        o_count++;

        // char too tall, but a later char might be ok, so count it:
        if( (y + h) >= y_limit )
            continue;

        if( font_draw_glyph_at_xy( gfx, g, triplet, x + width, y ) == EFI_SUCCESS )
        {
            width += w;

            if( height < h )
                height = h;
        }
    }

    if( dx )
        *dx = width;

    if( dy )
        *dy = height;

    if( decor != DECOR_NONE && o_count > 0 )
    {
        if( decor & DECOR_BOXED )
        {
            gfx_draw_box( gfx, x, y, width - 1, height - 1, triplet, 0 );
        }
        else
        {
            if( decor & DECOR_OVERLINE )
                gfx_draw_box( gfx, x, y, width - 1, 1, triplet, 0 );

            if( decor & DECOR_UNDERLINE )
                gfx_draw_box( gfx, x, y + height - 1, width - 1, 1, triplet, 0 );
        }
    }

    return o_count;
}

#define CENTRAL_XxY(mode,w,h)                                   \
    w,                                                          \
    h,                                                          \
    (mode->Mode->Info->HorizontalResolution / 2) - (w / 2),     \
    (mode->Mode->Info->VerticalResolution   / 2) - (h / 2)
VOID font_demo_text_display (EFI_GRAPHICS_OUTPUT_PROTOCOL *gfx)
{
    BLIT_BUFFER logo = { 0 };
    EFI_STATUS res = EFI_INVALID_PARAMETER;

    if( !gfx )
        gfx = gfx_get_interface();

    if( gfx )
        res = gfx_blit_in( gfx, &logo, CENTRAL_XxY(gfx, 200, 200) );

    if( res == EFI_SUCCESS )
        res = gfx_set_mode( gfx, 3 );

    if( res == EFI_SUCCESS )
        res = gfx_blit_out( gfx, &logo, CENTRAL_XxY(gfx, 200, 200 ) );

    gfx_dealloc_blitbuffer( &logo );
    gfx_dump_modes();

    UINT32 colour[] =
      { 0xff0000, 0xcc9900, 0xcccc00, 0x00cc00,
        0x0066ff, 0x330066, 0x660066, 0xffffff };
    CHAR8 c[] = "RAINBOW!";
    UINT32 x_offset = 128;
    UINT32 y_offset = 128;

    DEBUG_CHAR(L'y');
    DEBUG_CHAR(L'a');

    for( UINT32 x = 0; x < sizeof(c); x++ )
    {
        EFI_STATUS opt res;
        GLYPH *g = font_get_glyph( NULL, c[x] );
        UINT32 rgb = colour[ x % ARRAY_SIZE(colour) ];

        res = font_draw_glyph_at_xy( gfx, g, rgb, x_offset, y_offset );

        x_offset += g->font->max.width;
        y_offset += g->font->max.height;
    }

    UINT16 *scribbles[] =
      {
          L"Time flies like an arrow,",
          L"  fruit flies like a banana.",
          L"Såy sømething in a fünñy accent.",
          L" ",
          L" — Groucho Marx.",
          L"This text is going to be truncated ->|<-",
          NULL
      };

    UINT16 ox = 1;
    UINT16 oy = 16;
    UINT16 ow = 0;
    UINT16 oh = 0;

    for( UINTN i = 0; scribbles[i]; i++)
    {
        UINT16 oc =
          font_output_text( gfx, NULL, scribbles[i], 38,
                            ox, oy,
                            colour[ i % ARRAY_SIZE(colour) ], DECOR_NONE,
                            &ow, &oh );
        DEBUG_LOG( "wrote %d chars (%d x %d) from: %s",
                   oc, ow, oh, scribbles[i] );
        oy += oh;
        uefi_call_wrapper( BS->Stall, 1, 1 * 1000000 );
    }

    uefi_call_wrapper( BS->Stall, 1, 1 * 1000000 );
}
