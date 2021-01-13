#include "efi.h"
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/param.h>

#include "config-extra.h"

#define S(x) ((const char *)(x).value.string.bytes ?: (const char *)"<NULL>")

void dump_config (cfg_entry *c)
{
    for( uint i = 0; c[ i ].type != cfg_end; i++ )
        switch( c[ i ].type )
        {
          case cfg_bool:
          case cfg_uint:
            fprintf( stderr, "#%u <%s>%s = '%s'→%lu\n",
                     i, _cts( c[ i ].type ), c[ i ].name,
                     S(c[ i ]) , c[ i ].value.number.u );
            break;

          case cfg_stamp:
            fprintf( stderr, "#%u <%s>%s = '%s' [%lu]\n",
                     i, _cts( c[ i ].type ),
                     c[ i ].name, S(c[ i ]), c[ i ].value.number.u );
            break;

          case cfg_path:
          case cfg_string:
          default:
            fprintf( stderr, "#%u <%s>%s = '%s' [%lu bytes]\n",
                     i, _cts( c[ i ].type ),
                     c[ i ].name, S(c[ i ]), c[ i ].value.string.size );
            break;
        }
}

uint64_t set_conf_uint (const cfg_entry *cfg, const char *name, uint64_t val)
{
    cfg_entry *c = (cfg_entry *) get_conf_item (cfg, (unsigned char *)name);

    if( !c )
        return 0;

    switch( c->type )
    {
      case cfg_uint:
      case cfg_stamp:
        c->value.number.u = val;
        break;

      case cfg_bool:
        c->value.number.u = val ? 1 : 0;
        break;

      default:
        return 0;
    }

    return 1;
}

uint64_t set_conf_string (const cfg_entry *cfg, const char *name, const char *val)
{
    cfg_entry *c = (cfg_entry *) get_conf_item (cfg, (unsigned char *)name);

    if( !c )
        return 0;

    size_t l = strlen( val );

    switch( c->type )
    {
      case cfg_string:
      case cfg_path:
        // .size does NOT include the terminating NULL of the initial contents:
        // this may not hold true if a shorter string has been assigned since
        // but that's not a case that need concern us here:
        if( c->value.string.size < l)
        {
            c->value.string.bytes = realloc( c->value.string.bytes, l + 1 );
            c->value.string.size  = l;
        }

        memset ( c->value.string.bytes, 0, c->value.string.size + 1 );
        strncpy( (char *)c->value.string.bytes, val, l + 1 );
        break;

      default:
        return 0;
    }

    return 1;
}

uint64_t set_conf_stamp (const cfg_entry *cfg, const char *name, uint64_t val)
{
    if( (val != 0) && (val < 19700101000000) )
        return 0;

    return set_conf_uint (cfg, name, val);
}

uint64_t structtm_to_stamp (const struct tm *when)
{
    return ( when->tm_sec                     +
             when->tm_min           * 100         +
             when->tm_hour          * 10000       +
             when->tm_mday          * 1000000     +
             (when->tm_mon  + 1)    * 100000000   +
             (when->tm_year + 1900) * 10000000000 );
}

uint64_t set_conf_stamp_time(const cfg_entry *cfg, const char *name, time_t when)
{
    const struct tm *now = gmtime( &when );

    uint64_t stamp = structtm_to_stamp( now );

    return set_conf_stamp( cfg, name, stamp );
}

uint64_t del_conf_item (const cfg_entry *cfg, const char *name)
{
    cfg_entry *c = (cfg_entry *) get_conf_item (cfg, (unsigned char *)name);

    if( !c )
        return 1;

    free( c->value.string.bytes );
    c->value.string.bytes = NULL;
    c->value.string.size  = 0;
    c->value.number.u     = 0;
    // this will make the write-out iterator skip this item
    // currently once deleted it cannot be undeleted or set
    c->name               = NULL;

    return 1;
}

ssize_t
snprint_item (const char *buf, size_t space, const cfg_entry *c)
{
    char *str = (char *)buf;
    switch( c->type )
    {
      case cfg_uint:
      case cfg_bool:
      case cfg_stamp:
        return snprintf( str, space, "%s: %lu\n", c->name, c->value.number.u );
        break;

      case cfg_string:
      case cfg_path:
        if( c->value.string.bytes )
            return snprintf( str, space, "%s: %s\n", c->name, c->value.string.bytes );
        else
            return snprintf( str, space, "%s: \n", c->name );
        break;

      default:
        return -1;
    }
}

static ssize_t
write_item (char **buf, size_t *size, size_t offset, const cfg_entry *cfg)
{
    ssize_t w;;
    ssize_t left;

    if( !*buf )
    {
        *buf  = calloc( 1, 4096 );
        *size = 4096;
    }

    left = *size - offset;
    w = snprint_item( *buf + offset, left, cfg );

    if( (w > 0) && (w >= left) )
    {
        *size = *size + MAX( 4096, w + 1 );
        *buf  = realloc( *buf, *size );
        left  = *size - offset;
        w     = snprint_item( *buf + offset, left, cfg );
    }

    return w;
}

size_t write_config (int fd, const cfg_entry *cfg)
{
    int flags = 0;
    size_t written = 0;
    char *buf = NULL;
    size_t bufsiz = 0;

    if( !cfg )
        return 0;

    if( (flags = fcntl( fd, F_GETFL )) == -1 )
        return 0;

    if( !(flags & (O_WRONLY|O_RDWR)) )
        return 0;

    for( uint i = 0; cfg[ i ].type != cfg_end; i++ )
    {
        if( !cfg[ i ].name || !*(cfg[ i ].name) )
            continue;

        int w = write_item( &buf, &bufsiz, written, &cfg[ i ] );

        if( w < 0 )
            goto fail;

        written += w;
    }

    for( size_t out = written; out > 0; )
    {
        ssize_t o = write( fd, buf + (written - out), out );

        if( o < 0 )
            goto fail;

        out -= o;
    }

    free( buf );
    return written;

fail:
    free( buf );
    return -1;
}
