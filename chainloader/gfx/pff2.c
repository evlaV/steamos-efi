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
#include "../util.h"
#include "../fileio.h"
#include "../err.h"

#if GNUC_PREREQ(5, 1)
#define SAFE_ADD(a,b,r) \
    (__builtin_add_overflow( a, b, r ) ? EFI_BUFFER_TOO_SMALL : EFI_SUCCESS)
#define SAFE_MUL(a,b,r) \
    (__builtin_mul_overflow( a, b, r ) ? EFI_BUFFER_TOO_SMALL : EFI_SUCCESS)
#define CAST(a, r) SAFE_ADD((a), 0, (r))
#else
#error "Need GCC 5.1"
#endif

#if GNUC_PREREQ(4, 3)
#define BE_TO_LE32(x) __builtin_bswap32(x)
#else
#error "Need GCC 4.3"
#endif

#define BE_TO_LE16(x) ( (UINT16) (((x << 8) | (x >> 8))) )

#define BITMAP_1BPP_BUFSIZE(w,h,r) \
    ({ UINT64 _p;                                      \
       (SAFE_MUL((w), (h), &_p) != EFI_SUCCESS) ?      \
          EFI_BUFFER_TOO_SMALL  :                      \
          CAST(_p / 8 + !!(_p % 8), (r));  })

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
    PFF2_FAMILY,
    PFF2_POINT_SIZE,
    PFF2_WEIGHT,
    PFF2_MAX_WIDTH,
    PFF2_MAX_HEIGHT,
    PFF2_ASCENT,
    PFF2_DESCENT,
    PFF2_CHAR_INDEX,
    PFF2_DATA,
    PFF2_SLAN
} PFF2_SECTION;

typedef struct _font_section
{
    EFI_FILE_PROTOCOL *file;
    UINT8  name[4];
    UINT32 len;
    INT8   eof;
} FONT_SECTION;

static EFI_STATUS readbuf (EFI_FILE_PROTOCOL *file, UINT8 *buf, UINTN size)
{
    UINTN wanted = size;
    EFI_STATUS res = efi_file_read( file, (CHAR8 *)buf, &size );

    if( (wanted > size) && (res == EFI_SUCCESS) )
        res = EFI_END_OF_FILE;

    return res;
}

static EFI_STATUS readsect (FONT_SECTION *sect, UINT8 *buf, UINTN size)
{
    EFI_STATUS res;

    res = readbuf( sect->file, buf, size );

    if( res == EFI_END_OF_FILE )
        sect->eof = 1;

    return res;
}

static EFI_STATUS readsect_be32 (FONT_SECTION *sect, UINT32 *buf)
{
    UINT32 raw = 0;
    EFI_STATUS res = readsect( sect, (UINT8 *)&raw, sizeof(UINT32) );

    if( res == EFI_SUCCESS )
        *buf = BE_TO_LE32( raw );
    else
        *buf = 0;

    return res;
}

static const CHAR8 *SN(UINT8 *x)
{
    static CHAR8 z[5]; mem_copy( z, x, 4 ); z[ 4 ] = 0; return z;
}

static EFI_STATUS
open_font_section (EFI_FILE_PROTOCOL *file, FONT_SECTION *sect)
{
    EFI_STATUS res;

    if( !sect )
        return EFI_INVALID_PARAMETER;

    if( file )
        sect->file = file;

    sect->eof = 0;

    res = readsect( sect, sect->name, 4 );
    ERROR_RETURN( res, res,
                  L"EOF while reading font section %a", SN(sect->name) );

    res = readsect_be32( sect, &sect->len );
    ERROR_RETURN( res, res, L"Invalid section size in %a", SN(sect->name) );

    // The DATA section's length is just "rest of file", it doesn't actually
    // hold the real length of the section:
    if( mem_cmp( sect->name, SECT_DATA, 4 ) == 0 )
    {
        if( sect->len == 0xffffffff )
        {
            UINT64 fpos = 0;
            UINT64 epos = 0;

            efi_file_tell( sect->file, &fpos );
            efi_file_seek( sect->file, SEEK_TO_EOF );
            efi_file_tell( sect->file, &epos );
            efi_file_seek( sect->file, fpos );

            sect->len = epos - fpos;
        }
    }

    DEBUG_LOG("%a %ld bytes", SN(sect->name), sect->len );

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
        return PFF2_SLAN;

    if ( IS_SECTION( sect, SECT_DATA ) )
        return PFF2_DATA;

    return PFF2_UNKNOWN;

}

static EFI_STATUS
section_to_string (FONT_SECTION *sect, CHAR8 **buf)
{
    EFI_STATUS res = EFI_SUCCESS;
    UINTN size;

    if( *buf != NULL )
        res = EFI_INVALID_PARAMETER;

    ERROR_RETURN( res, res,
                  L"section %a: buffer already allocated", SN(sect->name) );

    res = SAFE_ADD( sect->len, 1, &size );
    ERROR_RETURN( res, res,
                  L"Integer overflow reading font section %a", SN(sect->name) );

    *buf = efi_alloc( size );
    res = readsect( sect, (UINT8 *)*buf, sect->len );
    (*buf)[ sect->len ] = 0;
    ERROR_JUMP( res, cleanup,
                L"IO error reading font section %a", SN(sect->name) );

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
                      L"%a wrong size for a UINT16", SN(sect->name) );
    }

    res = readsect( sect, (UINT8 *)&raw, 2 );
    ERROR_RETURN( res, res, L"error reading uint from %a", SN(sect->name) );

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

static EFI_STATUS
section_to_void ( FONT_SECTION *sect )
{
    EFI_STATUS res = EFI_SUCCESS;
    UINT64 fpos = 0;
    UINT64 npos = 0;

    res = efi_file_tell( sect->file, &fpos );
    ERROR_RETURN( res, res, L"tell() on file failed" );

    res = SAFE_ADD( fpos, sect->len, &npos );
    ERROR_RETURN( res, res, L"overflow while seeking %ld bytes from %ld",
                  sect->len, fpos );

    res = efi_file_seek( sect->file, npos );
    ERROR_RETURN( res, res, L"seek error to pos %ld", npos );

    return res;
}

static EFI_STATUS
read_be_uint16 (EFI_FILE_PROTOCOL *file, UINT16 *val)
{
    EFI_STATUS res = readbuf( file, (UINT8 *)val, sizeof(*val) );

    *val = BE_TO_LE16(*val);

    return res;
}

static EFI_STATUS
read_be_int16 (EFI_FILE_PROTOCOL *file, INT16 *val)
{
    return read_be_uint16( file, (UINT16 *)val );
}

static inline CHAR_INDEX_ENTRY *
lookup_codepoint (const FONT *font, UINT32 cp)
{
    CHAR_INDEX_ENTRY *table, *first, *end;
    UINTN len;

    if( !(table = font->chr_index) )
        return NULL;

    if( cp < MAX_BITMAP_IDX && font->bmp_index )
        if( font->bmp_index[ cp ] < 0xffff )
            return &table[ font->bmp_index[ cp ] ];

    // Didn't find anything in the index? time to search for real…
    // char index is ordered by codepoint (this is a requirement of
    // the PF2 format, and we enforce this while loading the font index)
    first = table;
    len   = font->chars;
    end   = first + len;

    while( len > 0 )
    {
        UINTN half = len >> 1;
        CHAR_INDEX_ENTRY *mid = first + half;

        if( mid->code == cp )
        {
            first = mid;
            len = 0;
        }
        else if( mid->code < cp )
        {
            first = mid + 1;
            len = len - half - 1;
        }
        else
        {
            len = half;
        }
    }

    return (first < end && first->code == cp) ? first : NULL;
}

static GLYPH *
lookup_glyph (FONT *font, UINT32 cp)
{
    EFI_STATUS res;
    GLYPH *glyph = NULL;
    CHAR_INDEX_ENTRY *indexed = NULL;
    UINT16 width, height;
    INT16  xoff, yoff, dwidth;
    INTN   len;
    UINTN  size;

    if( !(indexed = lookup_codepoint( font, cp )) )
        return NULL;

    if( indexed->glyph )
        return indexed->glyph;

    if( font->bad )
        return NULL;

    if( !font->file )
        return NULL;

    res = efi_file_seek( font->file,  indexed->offset );
    ERROR_JUMP( res, bad_font, L"CP %d offset %lu", cp, indexed->offset );

    res = read_be_uint16( font->file, &width  );
    if( res == EFI_SUCCESS && width > font->max.width )
        res = EFI_INVALID_PARAMETER;
    ERROR_JUMP( res, bad_font, L"CP %d bad width", cp );

    res = read_be_uint16( font->file, &height );
    if( res == EFI_SUCCESS && height > font->max.height )
        res = EFI_INVALID_PARAMETER;
    ERROR_JUMP( res, bad_font, L"CP %d bad height", cp );

    res = read_be_int16 ( font->file, &xoff   );
    ERROR_JUMP( res, bad_font, L"CP %d bad x-offset", cp );

    res = read_be_int16 ( font->file, &yoff   );
    ERROR_JUMP( res, bad_font, L"CP %d bad y-offset", cp );

    res = read_be_int16 ( font->file, &dwidth );
    ERROR_JUMP( res, bad_font, L"CP %d bad dwidth", cp );

    res = BITMAP_1BPP_BUFSIZE(width, height, &len);
    ERROR_JUMP( res, bad_font,
                L"CP %d bitmap buffer overflow %d x %d", cp, width, height );

    res = SAFE_ADD( sizeof(GLYPH), len, &size );
    ERROR_JUMP( res, bad_font,
                L"CP %d bitmap buffer overflow (%d pixels)", cp, len );

    glyph = efi_alloc( size );
    ERROR_JUMP( res, bad_font, L"CP %d Allocating %d bitmap bytes", cp, len );

    glyph->font     = font;
    glyph->width    = width;
    glyph->height   = height;
    glyph->offset.x = xoff;
    glyph->offset.y = yoff;
    glyph->device_width = dwidth;

    if( len != 0 )
    {
        res = readbuf( font->file, glyph->bitmap, len );
        ERROR_JUMP( res, bad_font, L"CP %d reading glyph bitmap", cp );
        indexed->glyph = glyph;
    }

    return glyph;

bad_font:
    efi_free( glyph );
    font->bad = TRUE;
    return NULL;
}

#define CHECK_SECTION(s,label,l)                                         \
    if( mem_cmp( (s)->name, label, sizeof(label) -  1) != 0 )            \
        ERROR_RETURN( EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER,      \
                      L"Section is %a, expected %a", (s)->name, label ); \
    if( l > 0 && l != (s)->len )                                         \
        ERROR_RETURN( EFI_INVALID_PARAMETER, EFI_INVALID_PARAMETER,      \
                      L"Section %a length is %d, expected %d",           \
                      (s)->name, (s)->len, l );

EFI_STATUS
pff2_load_file (EFI_FILE_PROTOCOL *src, FONT *font)
{
    EFI_STATUS res;
    FONT_SECTION section;
    UINT8 magic[4];

    res = open_font_section( src, &section );
    ERROR_RETURN( res, res, L"Open font section failed" );
    CHECK_SECTION( &section, SECT_FILE, 4 );

    res = readsect( &section, magic, 4 );
    if( res == EFI_SUCCESS )
        if( mem_cmp( magic, PFF2_MAGIC, 4 ) != 0 )
            res = EFI_INVALID_PARAMETER;

    ERROR_RETURN( res, res, L"Invalid PFF2 magic %x %x %x %x",
                  magic[0], magic[1], magic[2], magic[3] );

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
        ERROR_RETURN( res, res, L"Font file read error");

        stype = pff2_section_type( &section );

        switch( stype )
        {
          case PFF2_FONT_NAME:
            res = section_to_string( &section, &font->name );
            break;

          case PFF2_FAMILY:
            res = section_to_string( &section, &font->family );
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
            res = section_to_void( &section );
            break;
        }

        ERROR_RETURN( res, res,
                      L"Error reading font section %a", section.name );
    }

    if( !font->name )
    {
        font->name = efi_alloc( sizeof(UNKNOWN_FONT_NAME) );
        mem_copy( font->name, UNKNOWN_FONT_NAME, sizeof(UNKNOWN_FONT_NAME) );
    }

    font->lookup_glyph = lookup_glyph;

    return EFI_SUCCESS;
}
