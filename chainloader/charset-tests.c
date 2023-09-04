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
#include "utf-16.h"
#include "fileio.h"
#include "charset-tests.h"

#ifdef CHARSET_TESTS

#define UNICODE_EN_CP  L"tests/ucp-in.bin"
#define UNICODE_EN_U8  L"tests/ucp-in-u8-sample.txt"
#define UNICODE_EN_U16 L"tests/ucp-in-u16-sample.txt"

#define UNICODE_DE_U8_CP   L"tests/ucp-out-u8.bin"
#define UNICODE_DE_U8_ENC  L"tests/ucp-out-u8-sample.txt"

#define UNICODE_DE_U16_CP  L"tests/ucp-out-u16.bin"
#define UNICODE_DE_U16_ENC L"tests/ucp-out-u16-sample.txt"

// note: can't use ERROR_… here, they rely on strnarrow which relies
// on a working encode layer (since console logging wants UTF-16 and
// the logfile wants UTF-8).
#define CHARSET_ERROR(s, l, fmt, ...) \
    ({ if( s != EFI_SUCCESS ) { DEBUG_LOG( fmt, ##__VA_ARGS__ ); goto l; } });

typedef CHAR8 *(*encoder) (const UINT32 *codepoints, INTN len);
typedef INTN   (*decoder) (const CHAR8 *encoded, UINTN bytes, UINT32 **cp);

typedef struct
{
    encoder encode;
    decoder decode;
} codec;

static EFI_STATUS
path_to_mem (EFI_FILE_PROTOCOL *root,
             const CHAR16 *wdir, const CHAR16 *path,
             CHAR8 **data, UINTN *size)
{
    EFI_STATUS rc;
    CHAR16 *abs_path = resolve_path( path, wdir, FALSE );
    EFI_FILE_PROTOCOL *fh = NULL;
    UINTN alloc = 0;

    if( abs_path == NULL )
        return EFI_NOT_FOUND;

    rc = efi_file_open( root, &fh, abs_path, EFI_FILE_MODE_READ, 0 );
    if( rc != EFI_SUCCESS )
        goto cleanup;

    rc = efi_file_to_mem( fh, data, size, &alloc );

cleanup:
    efi_free( abs_path );
    efi_file_close( fh );

    return rc;
}

static EFI_STATUS
utfx_coding_test (EFI_FILE_PROTOCOL *root,
                  const CHAR16 *orig_path,
                  const CHAR8 *coding,
                  const CHAR8 *test,
                  const CHAR16 *cp_file,
                  const CHAR16 *enc_file,
                  codec *xcode,
                  UINT8 width)
{
    UINT32 *codepoints = NULL;
    UINT32 *decoded    = NULL;
    CHAR8  *enc_sample = NULL;
    CHAR8  *enc_result = NULL;
    UINTN cp_bytes = 0;
    UINTN en_bytes = 0;
    UINTN cp_count = 0;
    UINTN de_count = 0;
    EFI_STATUS rc;

    const CHAR8 *a = NULL;
    const CHAR8 *b = NULL;
    UINTN xcount = 0;

    rc = path_to_mem( root, orig_path, cp_file,
                      (CHAR8 **)&codepoints, &cp_bytes );
    CHARSET_ERROR( rc, cleanup,
                   "%a.%a: Loading sample %s", coding, test, cp_file );
    cp_count = cp_bytes / sizeof(UINT32);
    DEBUG_LOG( "%a.%a: %d bytes input data (%d codepoints)",
               coding, test, cp_bytes, cp_count );

    rc = path_to_mem( root, orig_path, enc_file, &enc_sample, &en_bytes );
    CHARSET_ERROR( rc, cleanup,
                   "%a.%a: Loading sample %s", coding, test, enc_file );
    DEBUG_LOG( "%a.%a: %d bytes of sample data", coding, test, en_bytes );

    if( xcode->encode )
    {
        UINTN enc_len  = 0;

        enc_result = xcode->encode( codepoints, cp_count );

        switch( width )
        {
          case 1:
            enc_len = strlen_a( enc_result );
            break;

          case 2:
            enc_len = strlen_w( (CHAR16 *)enc_result ) * 2;
            break;

          default:
            rc = EFI_INVALID_PARAMETER;
            CHARSET_ERROR( rc, cleanup, "%a.%a: Unsupported encoding width %d",
                           coding, test, width );
        }

        rc = ( enc_len != en_bytes ) ? EFI_BAD_BUFFER_SIZE : EFI_SUCCESS;
        CHARSET_ERROR( rc, cleanup,
                       "%a.%a: error: %d encoded vs %d sample bytes",
                       coding, test, enc_len, en_bytes );
        DEBUG_LOG( "%a.%a: encoded input to %d bytes", coding, test, enc_len);

        a = enc_result;
        b = enc_sample;
        xcount = enc_len;
    }
    else if( xcode->decode )
    {
        de_count = (UINTN)xcode->decode( enc_sample, en_bytes, &decoded );
        rc = ( de_count != cp_count ) ? EFI_BAD_BUFFER_SIZE : EFI_SUCCESS;
        CHARSET_ERROR( rc, cleanup,
                       "%a.%a: error: %d decoded vs %d sample codepoints",
                       coding, test, de_count, cp_count );
        DEBUG_LOG( "%a.%a: encoded %d bytes to %d codepoints",
                   coding, test, en_bytes, de_count );

        a = (const CHAR8 *)decoded;
        b = (const CHAR8 *)codepoints;
        xcount = de_count * sizeof(UINT32);
    }
    else
    {
        CHARSET_ERROR( EFI_INVALID_PARAMETER, cleanup,
                       "%a.%a Invalid encode/decode test - no codec supplied",
                       coding, test );
    }

    for( UINTN o = 0; o < xcount; o++ )
    {
        if( a[o] == b[o] )
            continue;

        rc = EFI_COMPROMISED_DATA;
        CHARSET_ERROR( rc, cleanup,
                       "%a.%a: output and sample differ at offset %d",
                       coding, test, o );
    }

cleanup:
    efi_free( codepoints );
    efi_free( decoded    );
    efi_free( enc_sample );
    efi_free( enc_result );

    return rc;
}

static EFI_STATUS
utf8_encoding_test (EFI_FILE_PROTOCOL *root, CHAR16 *orig_path)
{
    codec xcode = { .encode = utf8_encode, .decode = NULL };
    return utfx_coding_test( root, orig_path,
                             (CHAR8 *)"utf-8", (CHAR8 *)"encoding",
                             UNICODE_EN_CP, UNICODE_EN_U8,
                             &xcode, 1 );
}

static EFI_STATUS
utf16_encoding_test (EFI_FILE_PROTOCOL *root, CHAR16 *orig_path)
{
    codec xcode = { .encode = utf16_encode, .decode = NULL };
    return utfx_coding_test( root, orig_path,
                             (CHAR8 *)"utf-16", (CHAR8 *)"encoding",
                             UNICODE_EN_CP, UNICODE_EN_U16,
                             &xcode, 2 );
}

static EFI_STATUS
utf8_decoding_test (EFI_FILE_PROTOCOL *root, CHAR16 *orig_path)
{
    codec xcode = { .encode = NULL, .decode = utf8_decode };
    return utfx_coding_test( root, orig_path,
                             (CHAR8 *)"utf-8", (CHAR8 *)"decoding",
                             UNICODE_DE_U8_CP, UNICODE_DE_U8_ENC,
                             &xcode, 1 );
}

static EFI_STATUS
utf16_decoding_test (EFI_FILE_PROTOCOL *root, CHAR16 *orig_path)
{
    codec xcode = { .encode = NULL, .decode = utf16_decode };
    return utfx_coding_test( root, orig_path,
                             (CHAR8 *)"utf-16", (CHAR8 *)"decoding",
                             UNICODE_DE_U16_CP, UNICODE_DE_U16_ENC,
                             &xcode, 1 );
}

EFI_STATUS charset_tests (EFI_FILE_PROTOCOL *root, CHAR16 *path)
{
    EFI_STATUS rc[4];

    rc[0] = utf8_encoding_test ( root, path );
    rc[1] = utf8_decoding_test ( root, path );
    rc[2] = utf16_encoding_test( root, path );
    rc[3] = utf16_decoding_test( root, path );

    for( UINT8 i = 0; i < ARRAY_SIZE(rc); i++ )
        if( rc[i] != EFI_SUCCESS )
            return rc[ i ];

    return EFI_SUCCESS;
}

#endif
