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

#include "util.h"
#include "utf-16.h"

#define UTF16_BMP_A_MIN 0x0000
#define UTF16_BMP_A_MAX 0xd7ff
#define UTF16_BMP_B_MIN 0xe000
#define UTF16_BMP_B_MAX 0xffff
#define UTF16_HIS_MIN   0xd800
#define UTF16_HIS_MAX   0xdbff
#define UTF16_LOS_MIN   0xdc00
#define UTF16_LOS_MAX   0xdfff
#define UTF16_XXP_MIN   0x010000
#define UTF16_XXP_MAX   0x10ffff
#define UTF16_S_MASK    ((0x1 << 10) - 1)
#define UTF16_HI_WORD(cp) \
    (UTF16_HIS_MIN + ((((cp) - UTF16_XXP_MIN) >> 10) & UTF16_S_MASK))
#define UTF16_LO_WORD(cp) \
    (UTF16_LOS_MIN + (((cp) - UTF16_XXP_MIN) & UTF16_S_MASK))

#define UTF16_DECODE_HI(u16) (((u16) - UTF16_HIS_MIN) << 10)
#define UTF16_DECODE_LO(u16) ((u16) - UTF16_LOS_MIN)
#define UTF16_DECODE_HILO(hi,lo) \
    ((UTF16_DECODE_HI(hi) | UTF16_DECODE_LO(lo)) + UTF16_XXP_MIN)

// the fffe/ffff pairs every 0x10000 codepoints are reserved for
// non-character encoding use:
// 0x00fffe, 0x00ffff … 0x01fffe, 0x01ffff … 0x10fffe, 0x10ffff
#define UTF16_NONCHARACTER(cp) \
    ((((cp) & 0xfffe) == 0xfffe) && (((cp) >> 16) <= 0x10))

UINT8 utf16_encode_cp (UINT32 cp, UINT8 buf[4])
{
    if( cp <= UTF16_BMP_A_MAX ||
        (cp >= UTF16_BMP_B_MIN && cp <= UTF16_BMP_B_MAX) )
    {
        ((UINT16 *)buf)[0] = cp;

        return 2;
    }

    if( cp >= UTF16_XXP_MIN && cp <= UTF16_XXP_MAX )
    {
        ((UINT16 *)buf)[0] = UTF16_HI_WORD(cp);
        ((UINT16 *)buf)[1] = UTF16_LO_WORD(cp);

        return 4;
    }

    return 0;
}

CHAR8 *utf16_encode (const UINT32 *codepoints, INTN len)
{
    if( len < 1 )
        return NULL;

    UINTN esize = (len * 4);
    CHAR8 *encoded = efi_alloc( esize + 2 ); // max space required + L'\0'
    CHAR8 *buf = encoded;
    const CHAR8 *end = encoded + esize - 4; // need 4 bytes to safely encode

    for( INTN n = 0; (n < len) && (buf <= end); n++ )
        buf += utf16_encode_cp( codepoints[n], buf );

    *(CHAR16 *)buf = L'\0';

    return encoded;
}


INTN utf16_decode (const CHAR8 *encoded, UINTN bytes, UINT32 **codepoints)
{
    INTN  cp_count = 0;

    UINT32 *cp  = NULL;
    UINTN bcount = bytes ?: (strlen_w( (CHAR16 *)encoded ) * 2);
    // if we got an odd # of bytes ensure we have room for a trailing 0xfffd
    UINTN ccount = (bcount + 1) / 2;

    if( codepoints )
    {
        cp = efi_alloc( ccount * sizeof(UINT32) );

        if( cp == NULL )
        {
            // NOTE: Cannot use ERROR_RETURN et al here:
            // strnarrow/widen are built on these decoders:
            DEBUG_LOG("Could not allocate codepoint storage");
            return 0;
        }

        *codepoints = cp;
    }

    CHAR8 *b;
    UINT32 hi = 0;
    const CHAR8 *end = encoded + bcount;

    for( b = (CHAR8 *)encoded; b && b < end; b += 2 )
    {
        UINT16 lo = *(UINT16 *)b;

        if( hi == 0 )
        {
            if( lo <= UTF16_BMP_A_MAX ||
                ((lo >= UTF16_BMP_B_MIN) && (lo < UTF16_BMP_B_MAX - 1)) )
            {
                if( cp )
                    cp[cp_count] = lo;
                cp_count++;
            }
            else if ( lo >= UTF16_HIS_MIN && lo <= UTF16_HIS_MAX )
            {
                hi = lo;
            }
            else
            {   // solo low surrogate - or otherwise not allowed
                if( cp )
                    cp[cp_count] = REPLACEMENT_CHAR;
                cp_count++;
            }
        }
        else
        {
            if( lo >= UTF16_LOS_MIN && lo <= UTF16_LOS_MAX )
            {
                UINT32 decoded = UTF16_DECODE_HILO(hi,lo);

                if( cp )
                {
                    if( UTF16_NONCHARACTER(decoded) )
                        cp[cp_count] = REPLACEMENT_CHAR;
                    else
                        cp[cp_count] = decoded;
                }

                cp_count++;
            }
            else
            {
                // we have an unused u16 that still needs processing:
                if( cp )
                    cp[cp_count] = REPLACEMENT_CHAR;
                cp_count++;
                b -= 2;
            }

            hi = 0;
        }
    }

    // if we have a half-wchar left or ended on a high surrogate:
    if( b < end || hi )
    {
        if( cp )
            cp[cp_count] = REPLACEMENT_CHAR;
        cp_count++;
    }

    return cp_count;
}

