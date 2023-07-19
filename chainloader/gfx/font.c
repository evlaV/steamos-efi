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


#include "font.h"
#include "../util.h"
#include "../fileio.h"
#include "../err.h"

#if GNUC_PREREQ(5, 1)
#define SAFE_ADD(a,b,r) \
    (__builtin_add_overflow( a, b, r ) ? EFI_BUFFER_TOO_SMALL : EFI_SUCCESS)
#else
#error "Need GCC 5.1"
#endif

#if GNUC_PREREQ(4, 3)
#define BE_TO_LE32(x) __builtin_bswap32(x)
#else
#error "Need GCC 4.3"
#endif

#define BE_TO_LE16(x) ( (UINT16) (((x << 8) | (x >> 8))) )

#define UNKNOWN_FONT_NAME "Unknown"
#define PFF2_MAGIC      "PFF2"
#define SECT_FILE       "FILE"
#define SECT_FONT_NAME  "NAME"
#define SECT_POINT_SIZE "PTSZ"
#define SECT_WEIGHT     "WEIG"
#define SECT_MAX_WIDTH  "MAXW"
#define SECT_MAX_HEIGHT "MAXH"
#define SECT_ASCENT     "ASCE"
#define SECT_DESCENT    "DESC"
#define SECT_CHAR_INDEX "CHIX"
#define SECT_DATA       "DATA"
#define SECT_FAMILY     "FAMI"
#define SECT_SLAN       "SLAN"

#define WEIGHT_NORM 100
#define WEIGHT_BOLD 200

typedef enum _PFF2_SECTION
{
    PFF2_UNKNOWN,
    PFF2_FILE,
    PFF2_FONT_NAME,
    PFF2_POINT_SIZE,
    PFF2_WEIGHT,
    PFF2_MAX_WIDTH,
    PFF2_MAX_HEIGHT,
    PFF2_ASCENT,
    PFF2_DESCENT,
    PFF2_CHAR_INDEX,
    PFF2_DATA,
    PFF2_FAMILY,
    PFF2_SLAN
} PFF2_SECTION;

typedef struct _font_section
{
    EFI_FILE_PROTOCOL *file;
    CHAR8  name[4];
    UINT32 len;
    INT8   eof;
} FONT_SECTION;

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

static FONT null_font;

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
  font->chr_index  = 0;
  font->bmp_index  = 0;
}

static FONT *
font_alloc (VOID)
{
    FONT *font = efi_alloc( sizeof(FONT) );

    font_init( font );

    return font;
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
    efi_free( font->chr_index );
    efi_free( font->bmp_index );
    // for( i = 0; i < font->chars; i++ ) free each char glyph?
    mem_set( font, 0, sizeof(FONT) );
    efi_free( font );
}

VOID font_system_init (VOID)
{
    if( unknown_glyph )
        return;

    unknown_glyph = efi_alloc( sizeof(GLYPH) + sizeof(unknown_bitmap) );
    unknown_glyph->width        = 8;
    unknown_glyph->height       = 16;
    unknown_glyph->offset.x     = 0;
    unknown_glyph->offset.y     = -3;
    unknown_glyph->device_width = 8;
    unknown_glyph->font         = &null_font;
    mem_copy( unknown_glyph->bitmap,
              unknown_bitmap, sizeof(unknown_bitmap) );

    font_init (&null_font);

    null_font.name       = (CHAR8 *) "<No Font>";
    null_font.ascent     = unknown_glyph->height - 3;
    null_font.descent    = 3;
    null_font.max.width  = unknown_glyph->width;
    null_font.max.height = unknown_glyph->height;
    null_font.chars      = 1;
}

static VOID add_font (FONT *font)
{
    FONT_NODE *node = efi_alloc( sizeof(FONT_NODE) );

    node->value = font;
    node->next  = font_list;
    font_list   = node;
}

static VOID del_font (FONT *font)
{
    FONT_NODE **nextp = &font_list;
    FONT_NODE *cur    = *nextp;

    while( cur )
    {
      if (cur->value == font)
	{
	  *nextp = cur->next;
	  efi_free (cur);
	  return;
	}

      nextp = &cur->next;
      cur   = cur->next;
    }
}

static EFI_STATUS readsect (FONT_SECTION *sect, CHAR8 *buf, UINTN size)
{
    UINTN wanted = size;
    EFI_STATUS res = efi_file_read( sect->file, buf, &size );

    if( wanted > size )
    {
        sect->eof = 1;

        if( res == EFI_SUCCESS )
            res = EFI_END_OF_FILE;
    }

    return res;
}

static EFI_STATUS readsect_be32 (FONT_SECTION *sect, UINT32 *buf)
{
    UINT32 raw = 0;
    EFI_STATUS res = readsect( sect, (CHAR8 *)&raw, sizeof(UINT32) );

    if( res == EFI_SUCCESS )
        *buf = BE_TO_LE32( raw );
    else
        *buf = 0;

    return res;
}

static EFI_STATUS
open_font_section (EFI_FILE_PROTOCOL *file, FONT_SECTION *sect)
{
    EFI_STATUS res;

    if( !file ) return EFI_NOT_FOUND;
    if( !sect ) return EFI_INVALID_PARAMETER;

    sect->file = file;
    sect->eof  = 0;

    res = readsect( sect, sect->name, 4 );
    ERROR_RETURN( res, res, L"EOF while reading font section" );

    res = readsect_be32( sect, &sect->len );
    ERROR_RETURN( res, res, L"Invalid section size in font file");

    return EFI_SUCCESS;
}

#define IS_SECTION(s,label) \
    ( mem_cmp( s->name, label, sizeof(label) - 1 ) == 0 )

static PFF2_SECTION
pff2_section_type( FONT_SECTION *sect )
{
    if( IS_SECTION( sect, SECT_FILE ) )
        return PFF2_FILE;

    if( IS_SECTION( sect, SECT_FONT_NAME ) )
        return PFF2_FONT_NAME;

    if( IS_SECTION( sect, SECT_POINT_SIZE ) )
        return PFF2_POINT_SIZE;

    if( IS_SECTION( sect, SECT_WEIGHT ) )
        return PFF2_WEIGHT;

    if( IS_SECTION( sect, SECT_MAX_WIDTH ) )
        return PFF2_MAX_WIDTH;

    if( IS_SECTION( sect, SECT_MAX_HEIGHT ) )
        return PFF2_MAX_HEIGHT;

    if( IS_SECTION( sect, SECT_ASCENT ) )
        return PFF2_ASCENT;

    if( IS_SECTION( sect, SECT_DESCENT ) )
        return PFF2_DESCENT;

    if( IS_SECTION( sect, SECT_CHAR_INDEX ) )
        return PFF2_CHAR_INDEX;

    if( IS_SECTION( sect, SECT_FAMILY ) )
        return PFF2_FAMILY;

    if( IS_SECTION( sect, SECT_SLAN ) )
        return PFF2_FAMILY;

    if ( IS_SECTION( sect, SECT_DATA ) )
        return PFF2_DATA;

    return PFF2_UNKNOWN;

}

#define CHECK_SECTION(s,label,l) \
    if( mem_cmp( (s)->name, label, sizeof(label) -  1) != 0 )           \
    {                                                                   \
        res = EFI_INVALID_PARAMETER;                                    \
        ERROR_JUMP( res, cleanup,                                       \
                    L"Section is %a, expected %a", (s)->name, label );  \
    }                                                                   \
    if( l > 0 && l != (s)->len )                                        \
    {                                                                   \
        res = EFI_INVALID_PARAMETER;                                    \
        ERROR_JUMP( res, cleanup,                                       \
                    L"Section %a length is %d, expected %d",            \
                    (s)->name, (s)->len, l );                           \
    }

static EFI_STATUS
section_to_string (FONT_SECTION *sect, CHAR8 **buf)
{
    EFI_STATUS res;
    UINTN size;

    if( *buf != NULL )
        res = EFI_INVALID_PARAMETER;

    ERROR_RETURN( res, res,
                  L"section %a: buffer already allocated", sect->name );

    res = SAFE_ADD( sect->len, 1, &size );
    ERROR_RETURN( res, res,
                  L"Integer overflow reading font section %a", sect->name );

    *buf = efi_alloc( size );
    res = readsect( sect, *buf, sect->len );
    (*buf)[ sect->len ] = 0;
    ERROR_JUMP( res, cleanup,
                L"IO error reading font section %a", sect->name );

    return EFI_SUCCESS;

cleanup:
    efi_free( *buf );
    *buf = NULL;
    return res;
}

static EFI_STATUS
section_to_short (FONT_SECTION *sect, UINT16 *value)
{
    EFI_STATUS res;
    UINT16 raw;

    if( sect->len != 2 )
    {
        res = EFI_INVALID_PARAMETER;
        ERROR_RETURN( res, res,
                      L"section %a is wrong size for a UINT16", sect->name );
    }

    res = readsect( sect, (CHAR8 *)&raw, 2 );
    ERROR_RETURN( res, res, L"error reading uint from secion %a", sect->name );

    *value = BE_TO_LE16(raw);

    return EFI_SUCCESS;
}

#define CHARIDX_ENTRY_SIZE 9
#define MAX_BITMAP_IDX 0x10000
#define BITMAP_ALLOCATION (MAX_BITMAP_IDX * sizeof(UINT16))
static EFI_STATUS
section_to_index (FONT_SECTION *sect, FONT *font)
{
    EFI_STATUS res;
    UINTN i;

    if( sect->len % CHARIDX_ENTRY_SIZE != 0 )
        ERROR_RETURN( EFI_LOAD_ERROR, EFI_LOAD_ERROR,
                      L"Invalid PFF2 char index section size %d is not "
                      L"a multiple of %d ", sect->len, CHARIDX_ENTRY_SIZE );

    font->chars = sect->len / CHARIDX_ENTRY_SIZE;

    font->chr_index = efi_alloc( font->chars * sizeof(CHAR_INDEX_ENTRY) );
    res = font->chr_index ? EFI_SUCCESS: EFI_OUT_OF_RESOURCES;
    ERROR_RETURN( res, res, L"alloc for for %d chars", font->chars );

    font->bmp_index = efi_alloc( BITMAP_ALLOCATION );
    res = font->bmp_index ? EFI_SUCCESS: EFI_OUT_OF_RESOURCES;
    ERROR_RETURN( res, res, L"alloc for bitmaps %d", BITMAP_ALLOCATION );

    mem_set( font->bmp_index, 0xff, BITMAP_ALLOCATION );

    DEBUG_LOG("Loading %d chars into %d bytes of bitmap from %a",
              font->chars, BITMAP_ALLOCATION, font->name );

    UINT32 last_code = 0;

    for( i = 0; i < font->chars; i++ )
    {
        CHAR_INDEX_ENTRY *chr = &font->chr_index[i];

        res = readsect_be32( sect, &chr->code );
        ERROR_RETURN( res, res, L"Reading code #%d from %a", 1, font->name );

        res = (i > 0 && last_code >= chr->code) ? EFI_LOAD_ERROR : EFI_SUCCESS;
        ERROR_RETURN( res, res,
                      L"Character %d in %a out of sequence", i, font->name );

        if (chr->code < MAX_BITMAP_IDX && i < 0xffff)
            font->bmp_index[ chr->code ] = i;

        res = readsect( sect, &chr->storage_flags, 1 );
        ERROR_RETURN( res, res, L"Flags for char %d from %a", i, font->name );

        res = readsect_be32( sect, &chr->offset );
        ERROR_RETURN( res, res, L"Offset for char %d from %a", 1, font->name );

        chr->glyph = NULL;

        last_code = chr->code;
    }

    return EFI_SUCCESS;
}

FONT *
font_load (EFI_FILE_PROTOCOL *dir, CHAR16 *path)
{
    FONT *font = NULL;
    EFI_FILE_PROTOCOL *src = NULL;
    EFI_STATUS res;
    FONT_SECTION section;
    CHAR8 magic[4];

    res = efi_file_open( dir, &src, path, EFI_FILE_MODE_READ, 0 );
    ERROR_JUMP( res, cleanup, L"Open font %s failed: %r", path );

    res = open_font_section( src, &section );
    ERROR_JUMP( res, cleanup, L"Open font section failed" );
    CHECK_SECTION( &section, SECT_FILE, 4 );

    res = readsect( &section, magic, 4 );
    if( res == EFI_SUCCESS )
        if( mem_cmp( magic, PFF2_MAGIC, 4 ) != 0 )
            res = EFI_INVALID_PARAMETER;
    ERROR_JUMP( res, cleanup, L"Invalid PFF2 magic %x %x %x %x",
                magic[0], magic[1], magic[2], magic[3] );

    font = font_alloc();
    font->file = src;

    UINT8 go = 1;
    while( go )
    {
        PFF2_SECTION stype;
        res = open_font_section( src, &section );

        // font file finished:
        if( section.eof )
            break;

        // some other termination of file read:
        ERROR_JUMP( res, cleanup, L"Font file read error");

        stype = pff2_section_type( &section );

        switch( stype )
        {
          case PFF2_FONT_NAME:
            res = section_to_string( &section, &font->name );
            break;

          case PFF2_POINT_SIZE:
            res = section_to_short( &section, &font->point );
            break;

          case PFF2_MAX_WIDTH:
            res = section_to_short( &section, &font->max.width );
            break;

          case PFF2_MAX_HEIGHT:
            res = section_to_short( &section, &font->max.height );
            break;

          case PFF2_ASCENT:
            res = section_to_short( &section, &font->ascent );
            break;

          case PFF2_DESCENT:
            res = section_to_short( &section, &font->descent );
            break;

          case PFF2_CHAR_INDEX:
            res = section_to_index( &section, font );
            break;

          case PFF2_WEIGHT:
            ({
                CHAR8 *weight = NULL;
                res = section_to_string( &section, &weight );

                if( res == EFI_SUCCESS )
                {
                    if( !weight || !*weight || strcmpa( weight, (CHAR8 *)"normal" ) )
                        font->weight = WEIGHT_NORM;
                    else if( strcmpa( weight, (CHAR8 *)"bold" ) )
                        font->weight = WEIGHT_BOLD;
                }

                efi_free( weight );
            });
            break;

          case PFF2_DATA:
            res = EFI_SUCCESS;
            go  = 0;
            break;

          default:
            res = EFI_SUCCESS;
            // seek to end of extion here:
            break;
        }

        ERROR_JUMP( res, cleanup,
                    L"Error reading font section %a", section.name );
    }

    if( !font->name )
    {
        font->name = efi_alloc( sizeof(UNKNOWN_FONT_NAME) );
        mem_copy( font->name, UNKNOWN_FONT_NAME, sizeof(UNKNOWN_FONT_NAME) );
    }

    DEBUG_LOG( "Loaded font: '%a'", font->name );
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

