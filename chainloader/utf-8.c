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
#include "utf-8.h"

// A utf-8 byte sequence of n bytes (where n > 1) starts with a byte that has
// n high bits set + 1 bit unset, followed by n-1 bytes holding six bits each
// with 1 high bit set followed by an unset bit.
//
// The number of payload bits held by an (n > 1)-byte sequence is therefore:
//  ( (n-1) * 6 )       +  ( 8 - ( n + 1 ) )
//  ^^^^^^^^^^^^           ^^^^^^^^^^^^^^^^^
//  continuation bytes     lead byte
//  ( (n-1) * 6 ) + ( 8 - ( n + 1 ) )
//   6n - 6       +  7 - n
//   5n + 1
// A single byte utf-8 sequence can encode 7 bits of payload
// The minimum codepoint which require N bytes are therefore:
#define UTF8_NBIT_MINIMUM(n) (0x1 << n)
#define UTF8_2BYTE_CP UTF8_NBIT_MINIMUM(7)
#define UTF8_3BYTE_CP UTF8_NBIT_MINIMUM(11)
#define UTF8_4BYTE_CP UTF8_NBIT_MINIMUM(16)
// this one is artificially limited - it should be 21 bits but utf-8
// is constrained to the more limited set that utf-16 can handle
#define UTF8_4BYTE_CP_MAX UTF8_NBIT_MINIMUM(20) + UTF8_NBIT_MINIMUM(16)

#define UTF8_0TH_HDR(n)         ((~((0x1 << (8 - (n))) - 1)) & 0xff)
#define UTF8_XTH_ENC(n,x,cp)    (cp >> (((n) - (x) - 1) * 6) & 0x3f)
#define UTF8_ENCODE_XTH(n,x,cp) ((0x80 | UTF8_XTH_ENC((n),(x),cp)) & 0xff)
#define UTF8_ENCODE_0TH(n,cp)   (UTF8_0TH_HDR(n) | UTF8_XTH_ENC(n,0,cp))

#define UTF8_6BYTE_HDR UTF8_0TH_HDR(6)
#define UTF8_5BYTE_HDR UTF8_0TH_HDR(5)
#define UTF8_4BYTE_MAX_HDR UTF8_0TH_HDR(4) + 4
#define UTF8_4BYTE_HDR UTF8_0TH_HDR(4)
#define UTF8_3BYTE_HDR UTF8_0TH_HDR(3)
#define UTF8_2BYTE_HDR UTF8_0TH_HDR(2)

#define UTF8_DECODE_XTH(n,x,u8) (((u8) & 0x3f) << (((n) - (x) - 1) * 6))
#define UTF8_DECODE_0TH(n,u8)   \
    (((u8) & (~UTF8_0TH_HDR((n)+1) & 0xff)) << (((n) - 1) * 6))

#define UTF8_CONT_MIN 0x80
#define UTF8_CONT_MAX 0xbf
#define UTF8_CONT_3_1_MIN(b) (((b) == 0xe0) ? 0xa0 : UTF8_CONT_MIN)
#define UTF8_CONT_3_1_MAX(b) (((b) == 0xed) ? 0x9f : UTF8_CONT_MAX)
#define UTF8_CONT_4_1_MIN(b) (((b) == UTF8_4BYTE_HDR)     ? 0x90 : UTF8_CONT_MIN)
#define UTF8_CONT_4_1_MAX(b) (((b) == UTF8_4BYTE_MAX_HDR) ? 0x8f : UTF8_CONT_MAX)

#define UTF8_CONT_OK(b) (((b) & 0xc0) == UTF8_CONT_MIN)

UINT8 utf8_encode_cp (UINT32 cp, UINT8 buf[6])
{
    if( cp < UTF8_2BYTE_CP )
    {
        buf[0] = (uint8_t)cp;
        return 1;
    }

    if( cp < UTF8_3BYTE_CP )
    {
        buf[0] = UTF8_ENCODE_0TH(2,cp);
        buf[1] = UTF8_ENCODE_XTH(2,1,cp);
        return 2;
    }

    if( cp < UTF8_4BYTE_CP )
    {
        buf[0] = UTF8_ENCODE_0TH(3,cp);
        buf[1] = UTF8_ENCODE_XTH(3,1,cp);
        buf[2] = UTF8_ENCODE_XTH(3,2,cp);
        return 3;
    }

    if( cp <= UTF8_4BYTE_CP_MAX )
    {
        buf[0] = UTF8_ENCODE_0TH(4,cp);
        buf[1] = UTF8_ENCODE_XTH(4,1,cp);
        buf[2] = UTF8_ENCODE_XTH(4,2,cp);
        buf[3] = UTF8_ENCODE_XTH(4,3,cp);
        return 4;
    }

    return 0;
}

UINT8 *utf8_encode (const UINT32 *codepoints, INTN len)
{
    if( len < 1 )
        return NULL;

    UINTN esize = (len * 4);
    UINT8 *encoded = efi_alloc( esize + 1 ); // max space required + NUL
    UINT8 *buf = encoded;
    const UINT8 *end = encoded + esize - 4; // need 4 bytes to safely encode

    for( INTN n = 0; (n < len) && (buf <= end); n++ )
        buf += utf8_encode_cp( codepoints[n], buf );

    *buf = '\0';

    return encoded;
}

typedef enum
{
    U8_SEQ_1,
    U8_SEQ_2,
    U8_SEQ_3,
    U8_SEQ_4,
    U8_SEQ_X, // handle algorithmically allowed sequences foribidden by spec
} utf8_decode_state;

INTN utf8_decode (const CHAR8 *encoded, UINTN bytes, UINT32 **codepoints)
{
    INTN cp_count = 0;
    UINT8 needed   = 0;
    UINT32 *cp  = NULL;
    UINTN bcount = bytes ?: strlen_a( encoded );

    if( codepoints )
    {
        cp = efi_alloc( bcount * sizeof(UINT32) );

        if( cp == NULL )
        {
            // NOTE: Cannot use ERROR_RETURN et al here
            // strnarrow/widen are built on these decoders:
            DEBUG_LOG("Could not allocate codepoint storage");
            return 0;
        }

        *codepoints = cp;
    }

    utf8_decode_state state = U8_SEQ_1;
    UINT8 u8_byte_min = UTF8_CONT_MIN;
    UINT8 u8_byte_max = UTF8_CONT_MAX;
    UINT8 malformed   = 0;
    UINT8 truncated   = 0;
    const CHAR8 *end = encoded + bcount;

    for( CHAR8 *b = (CHAR8 *)encoded; b && b < end; b++ )
    {
        switch (state)
        {
          case U8_SEQ_1:
            // 7-bit ASCII chars are self-encoding in UTF-8:
            if( *b < UTF8_2BYTE_CP )
            {
                if( cp )
                    cp[cp_count] = *b;
                needed = 0;
            }
            // utf-8 is capped at the highest codepoint handled by utf-16
            // This is why UTF8_4BYTE_CP_MAX < UTF8_5BYTE_HDR and why
            // there are no valid 5 and 6 byte sequences:
            else if( *b > UTF8_4BYTE_MAX_HDR )
            {
                malformed = 1;
                state     = U8_SEQ_X;

                if( *b >= UTF8_6BYTE_HDR )
                    needed = 5;
                else if ( *b >= UTF8_5BYTE_HDR )
                    needed = 4;
                else
                    needed = 3;
            }
            else if( *b >= UTF8_4BYTE_HDR )
            {
                u8_byte_min  = UTF8_CONT_4_1_MIN(*b);
                u8_byte_max  = UTF8_CONT_4_1_MAX(*b);

                if( cp )
                    cp[cp_count] |= UTF8_DECODE_0TH(4,*b);

                needed = 3;
                state  = U8_SEQ_4;
            }
            else if( *b >= UTF8_3BYTE_HDR )
            {
                u8_byte_min  = UTF8_CONT_3_1_MIN(*b);
                u8_byte_max  = UTF8_CONT_3_1_MAX(*b);

                if( cp )
                    cp[cp_count] |= UTF8_DECODE_0TH(3,*b);

                needed = 2;
                state  = U8_SEQ_3;
            }
            // c0 and c1 are algorithmically valid but would only occur
            // here in an overlong sequence, which the spec disallows:
            else if( *b >= UTF8_2BYTE_HDR + 2 )
            {
                if( cp )
                    cp[cp_count] |= UTF8_DECODE_0TH(2,*b);

                needed = 1;
                state  = U8_SEQ_2;
            }
            else if( *b >= UTF8_2BYTE_HDR )
            {
                malformed = 1;
                needed    = 1;
                state     = U8_SEQ_2;
            }
            else
            {
                malformed = 1;
            }
            break;

          case U8_SEQ_2:
            if( UTF8_CONT_OK(*b) )
            {
                if( !malformed && cp )
                    cp[cp_count] |= UTF8_DECODE_XTH(2,1,*b);

                needed--;
            }
            else
            {
                truncated = 1;
            }
            break;

          case U8_SEQ_3:
            if( UTF8_CONT_OK(*b) )
            {
                if( (*b < u8_byte_min) || (*b > u8_byte_max) )
                    malformed = 1;

                if( !malformed && cp )
                    cp[cp_count] |= UTF8_DECODE_XTH(3,(3 - needed),*b);

                needed--;
            }
            else
            {
                truncated = 1;
            }

            // only the 1st continuation byte can have special requirements
            u8_byte_min = UTF8_CONT_MIN;
            u8_byte_max = UTF8_CONT_MAX;
            break;

          case U8_SEQ_4:
            if( UTF8_CONT_OK(*b) )
            {
                if( (*b < u8_byte_min) || (*b > u8_byte_max) )
                    malformed = 1;

                if( !malformed && cp )
                    cp[cp_count] |= UTF8_DECODE_XTH(4,(4 - needed),*b);

                needed--;
            }
            else
            {
                truncated = 1;
            }

            // only the 1st continuation byte can have special requirements
            u8_byte_min = UTF8_CONT_MIN;
            u8_byte_max = UTF8_CONT_MAX;
            break;

          case U8_SEQ_X:
            // These sequences are forbidden so we don't process them, we
            // just eat their bytes as long as they are algorithmically ok.
            if( UTF8_CONT_OK(*b) && needed )
                needed--;
            else
                truncated = 1;
            break;
        }

        // we have an unprocessed byte here:
        // NB: we can be in a malformed sequence that was _also_ truncated:
        if( truncated )
        {
            if( cp )
                cp[cp_count] = REPLACEMENT_CHAR;

            malformed = 0;
            truncated = 0;
            needed    = 0;

            state     = U8_SEQ_1;
            cp_count++;
            b--;
        }
        // end of sequence reached, but may be overlong or forbidden:
        else if( needed == 0 )
        {
            if( cp && ( malformed              ||
                        cp[cp_count] == 0xfffe ||
                        cp[cp_count] == 0xffff ) )
                cp[cp_count] = REPLACEMENT_CHAR;

            malformed = 0;
            state     = U8_SEQ_1;
            cp_count++;
        }
    }

    return cp_count;
}
