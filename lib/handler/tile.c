
//#include "tile-hook.h"
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include "path-mapper.h"
#include "mapnik-bridge.h"

struct st_h2o_tile_handler_t {
    h2o_file_handler_t super;
    h2o_iovec_t style_file_path;    /* path to a Mapnik's style file */
    MAPNIK_MAP_PTR map; /* mapnik::Map* related to style_file_path */
};

#if __GNUC__ >= 3
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

static inline int tile_rewrite_path(const char* rpath, const char* base, size_t base_len, char *path_buf, size_t path_buf_len, uint32_t* pzoom, uint32_t* px, uint32_t* py) {
    /*
    rpath: abs. path to the tile, as resolved by h2o.
    */
    const char* tile_path_head = rpath + base_len;
    /*
    match tile_path_head with ($zoom)/($x)/($y).(png|jpg), where:
        $zoom  = \d{1,2}
        $x, $y = \d{1,9}
    non-match will immediately return 0 (failure).

    No "overflow check" is performed, so $x, $y in [2^32 ... 9,999,999,999] will
    result in an unexpected (but still "defined") behavior.

    The suffix (png|jpg) is case-sensitive & accepts lowers only.
    I don't think any common tile clients intentionally query capital filenames.
    */
    const char* hd = tile_path_head;  // points to the next char to scan.
    uint32_t zoom = 0, x = 0, y = 0;
    enum TILE_SUFFIX suffix = PNG;

    /*
    Follows a hard-coded matcher for (\d{1,2})/(\d{1,9})/(\d{1,9}).(png|jpg)
    */
#define LEX_DIGIT(v) if (likely(isdigit(*hd))) {\
    v = 10*v + (*hd - '0'); \
    ++hd; \
} else {\
    return 0; \
}

#define EXPECT(c) if (likely(*hd == c)) { ++hd; } else { return 0; }

/*
Let us exploit good old gotos to simulate state-transitions
*/
#define LEX_DIGIT_OR_GOTO_ON_DELIM(v, c, label) if (likely(isdigit(*hd))) { \
    v = 10*v + (*hd - '0'); \
    ++hd; \
} else if (*hd == c) { \
    ++hd; \
    goto label; \
} else { \
    return 0; \
}

/* 
LetsParse: // <- the "initial state" 
*/
    /* zoom = hd.match \d{1,2} */
    LEX_DIGIT(zoom)
    LEX_DIGIT_OR_GOTO_ON_DELIM(zoom, '/', ParseX)
    EXPECT('/')
ParseX:
    /* x = hd.match \d{1,9} */
    LEX_DIGIT(x)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    LEX_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    EXPECT('/')
ParseY:
    /* y = hd.match \d{1,9} */
    LEX_DIGIT(y)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    LEX_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    EXPECT('.')
ParseSuffix:
    if (likely(hd[0] == 'p' && hd[1] == 'n' && hd[2] == 'g')) {
        suffix = PNG; 
    } else if (hd[0] == 'j' && hd[1] == 'p' && hd[2] == 'g') {
        suffix = JPG;
    } else {
        return 0;
    }
/* 
Done: // <- the "final state" 
*/

    /*
    Here, tile_path_head is guaranteed to match the pattern & zoom, x, y are accordingly stored,
    o.w. the function must have already returned 0.
    */

    assert(path_buf_len >= base_len + 27);
    memcpy(path_buf, base, base_len);
    to_physical_path(path_buf + base_len, zoom, x, y, suffix);
    *pzoom = zoom;
    *px    = x;
    *py    = y;
    /* fprintf(stderr, "***%d %d %d %d ==> %s ***\n", zoom, x, y, suffix, path_buf); */

    return 1;

}   
#if 0
static inline h2o_buffer_t tile_body(uint8_t* content) {
    h2o_buffer_t *_;
    h2o_buffer_init(&_, &h2o_socket_buffer_prototype);

    {
        h2o_iovec_t _s = (h2o_iovec_init(H2O_STRLIT("<!DOCTYPE html>\n<TITLE>Index of ")));
        if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
            --_s.len;
        h2o_buffer_reserve(&_, _s.len);
        memcpy(_->bytes + _->size, _s.base, _s.len);
        _->size += _s.len;
    }
    {
        h2o_iovec_t _s = (path_normalized_escaped);
        if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
            --_s.len;
        h2o_buffer_reserve(&_, _s.len);
        memcpy(_->bytes + _->size, _s.base, _s.len);
        _->size += _s.len;
    }
    {
        h2o_iovec_t _s = (h2o_iovec_init(H2O_STRLIT("</TITLE>\n<H2>Index of ")));
        if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
            --_s.len;
        h2o_buffer_reserve(&_, _s.len);
        memcpy(_->bytes + _->size, _s.base, _s.len);
        _->size += _s.len;
    }
    {
        h2o_iovec_t _s = (path_normalized_escaped);
        if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
            --_s.len;
        h2o_buffer_reserve(&_, _s.len);
        memcpy(_->bytes + _->size, _s.base, _s.len);
        _->size += _s.len;
    }
    {
        h2o_iovec_t _s = (h2o_iovec_init(H2O_STRLIT("</H2>\n<UL>\n<LI><A HREF=\"..\">Parent Directory</A>\n")));
        if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
            --_s.len;
        h2o_buffer_reserve(&_, _s.len);
        memcpy(_->bytes + _->size, _s.base, _s.len);
        _->size += _s.len;
    }

    while ((ret = readdir_r(dp, &dent, &dentp)) == 0 && dentp != NULL) {
        h2o_iovec_t fn_escaped;
        if (strcmp(dent.d_name, ".") == 0 || strcmp(dent.d_name, "..") == 0)
            continue;
        fn_escaped = h2o_htmlescape(pool, dent.d_name, strlen(dent.d_name));
        {
            h2o_iovec_t _s = (h2o_iovec_init(H2O_STRLIT("<LI><A HREF=\"")));
            if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
                --_s.len;
            h2o_buffer_reserve(&_, _s.len);
            memcpy(_->bytes + _->size, _s.base, _s.len);
            _->size += _s.len;
        }
        {
            h2o_iovec_t _s = (fn_escaped);
            if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
                --_s.len;
            h2o_buffer_reserve(&_, _s.len);
            memcpy(_->bytes + _->size, _s.base, _s.len);
            _->size += _s.len;
        }
        {
            h2o_iovec_t _s = (h2o_iovec_init(H2O_STRLIT("\">")));
            if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
                --_s.len;
            h2o_buffer_reserve(&_, _s.len);
            memcpy(_->bytes + _->size, _s.base, _s.len);
            _->size += _s.len;
        }
        {
            h2o_iovec_t _s = (fn_escaped);
            if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
                --_s.len;
            h2o_buffer_reserve(&_, _s.len);
            memcpy(_->bytes + _->size, _s.base, _s.len);
            _->size += _s.len;
        }
        {
            h2o_iovec_t _s = (h2o_iovec_init(H2O_STRLIT("</A>\n")));
            if (_s.len != 0 && _s.base[_s.len - 1] == '\n')
                --_s.len;
            h2o_buffer_reserve(&_, _s.len);
            memcpy(_->bytes + _->size, _s.base, _s.len);
            _->size += _s.len;
        }
    }

    return _;
}
#endif
static void on_tile_rendered(h2o_req_t *req, const char* content, size_t content_length, const char* physical_tile_path, const char* mime_type, size_t mime_type_len, int flags) {

    struct tm last_modified_gmt;
    char last_modified[H2O_TIMESTR_RFC1123_LEN + 1];
    char etag_buf[sizeof("\"deadbeef-deadbeefdeadbeef\"")];
    size_t etag_len;
    /* send response */
    req->res.status = 200;
    req->res.reason = "OK";
    req->res.content_length = content_length;
    h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_CONTENT_TYPE, mime_type, mime_type_len);

    time_t now;
    time(&now);
    gmtime_r(&now, &last_modified_gmt);
    time2packed(&last_modified_gmt);
    h2o_time2str_rfc1123(last_modified, &last_modified_gmt);
    if ((flags & H2O_FILE_FLAG_NO_ETAG) != 0) {
        etag_len = 0;
    } else {
        etag_len = sprintf(etag_buf, "\"%08x-%zx\"", (unsigned)now, (size_t)content_length);
    }
    h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_LAST_MODIFIED, last_modified, H2O_TIMESTR_RFC1123_LEN);
    if (etag_len != 0) {
        h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ETAG, etag_buf, etag_len);
    }
    h2o_send_inline(req, content, content_length);

}

/*
FIXME:
This is nearly identical to do_req(); not DRY, workarounds are expected.
What we need is:
  + a mechanism to delegate requests to on_req(), with
    - req->path_normalized.base is rewritten to the physical tile path: /base/z/x/y.png => /base/z/nnn/nnn/nnn/nnn/nnn.png, and
    - a custom 404 handler s.t. a non-existent tile is rendered & sent-back with 200, where
      * rendering respects the style given by h2o_tile_handler_t.style_file_path
*/
static int on_req_tile(h2o_handler_t *_self, h2o_req_t *req)
{
    h2o_tile_handler_t *self = (void *)_self;
    h2o_file_handler_t *super = &(self->super);
    h2o_iovec_t mime_type;
    char *rpath;
    size_t rpath_len, req_path_prefix, tile_path_len;
    struct st_h2o_sendfile_generator_t *generator = NULL;
    size_t if_modified_since_header_index, if_none_match_header_index;
    int is_dir, is_get;

     /* only accept GET and HEAD */
    if (h2o_memis(req->method.base, req->method.len, H2O_STRLIT("GET"))) {
        is_get = 1;
    } else if (h2o_memis(req->method.base, req->method.len, H2O_STRLIT("HEAD"))) {
        is_get = 0;
    } else {
        h2o_add_header(&req->pool, &req->res.headers, H2O_TOKEN_ALLOW, H2O_STRLIT("GET, HEAD"));
        h2o_send_error(req, 405, "Method Not Allowed", "method not allowed", H2O_SEND_ERROR_KEEP_HEADERS);
        return 0;
    }

    /* build path (still unterminated at the end of the block) */
    req_path_prefix = req->pathconf->path.len;
    tile_path_len = super->real_path.len + (req->path_normalized.len - req_path_prefix) + 28;
    rpath = alloca(tile_path_len);
    rpath_len = 0;
    memcpy(rpath + rpath_len, super->real_path.base, super->real_path.len);
    rpath_len += super->real_path.len;
    memcpy(rpath + rpath_len, req->path_normalized.base + req_path_prefix, req->path_normalized.len - req_path_prefix);
    rpath_len += req->path_normalized.len - req_path_prefix;

    rpath[rpath_len] = '\0';
    do { /* scoping */
        uint32_t x, y, z;
        /* Try to convert rpath (base/z/x/y.png) to the tiles' scheme: base/z/nnn/nnn/nnn/nnn/nnn.png */
        if (likely(tile_rewrite_path(rpath, super->real_path.base, super->real_path.len, rpath, tile_path_len, &z, &x, &y))) {
            rpath_len = tile_path_len;  // Now, rpath_len was changed.
            /* If successful, try to send it back as-is */
            if ((generator = create_generator(req, rpath, tile_path_len, &is_dir, super->flags)) != NULL) {
                goto Opened;
            }
            if (is_dir) {
                /* Tile directories shouldn't have index files. */
                h2o_send_error(req, 404, "File Not Found", "file not found", 0);
                return 0;
            }
            /* failed to open */
            if (errno == ENOENT) {
                /* If create_generator() failed (i.e. tile_path is non-existent), invoke renderer */
                mime_type = h2o_mimemap_get_type(self->super.mimemap, h2o_get_filext(rpath, tile_path_len));
                render_tile(req, self->map, rpath, z, x, y, mime_type.base, mime_type.len, super->flags, on_tile_rendered);
            } else {
                h2o_send_error(req, 403, "Access Forbidden", "access forbidden", 0);
            }
            return 0;
        } else {
            // If path conversion failed, fallback to on_req()
            return on_req(_self, req);
        }
    } while (0);

Opened:
    if ((if_none_match_header_index = h2o_find_header(&req->headers, H2O_TOKEN_IF_NONE_MATCH, SIZE_MAX)) != -1) {
        h2o_iovec_t *if_none_match = &req->headers.entries[if_none_match_header_index].value;
        if (h2o_memis(if_none_match->base, if_none_match->len, generator->etag_buf, generator->etag_len))
            goto NotModified;
    } else if ((if_modified_since_header_index = h2o_find_header(&req->headers, H2O_TOKEN_IF_MODIFIED_SINCE, SIZE_MAX)) != -1) {
        h2o_iovec_t *ims_vec = &req->headers.entries[if_modified_since_header_index].value;
        struct tm ims_tm;
        if (h2o_time_parse_rfc1123(ims_vec->base, ims_vec->len, &ims_tm) == 0 &&
            generator->last_modified.packed <= time2packed(&ims_tm))
            goto NotModified;
    }

    /* obtain mime type */
    mime_type = h2o_mimemap_get_type(self->super.mimemap, h2o_get_filext(rpath, tile_path_len));

    /* return file */
    do_send_file(generator, req, 200, "OK", mime_type, is_get);
    return 0;

NotModified:
    req->res.status = 304;
    req->res.reason = "Not Modified";
    h2o_send_inline(req, NULL, 0);
    do_close(&generator->super, req);
    return 0;
}

static void on_dispose_tile_context(h2o_handler_t* _self, h2o_context_t* ctx) {
    h2o_tile_handler_t *self = (void *)_self;
    dispose_mapnik(self->map);
}

/* Just an alias for now */
#define on_dispose_tile on_dispose

h2o_tile_handler_t *h2o_tile_register(h2o_pathconf_t *pathconf, const char *base_path, const char *style_file_path)
{
    h2o_tile_handler_t *self;

    self = (void *)h2o_create_handler(pathconf, sizeof(*self));
    self->map = alloc_mapnik(style_file_path);

    /* super() */
    self->super = *(h2o_file_register(pathconf, base_path, NULL, NULL, 0));
    /* */

    /* overload callbacks */
    self->super.super.dispose = on_dispose_tile;
    self->super.super.on_req = on_req_tile;
    self->super.super.on_context_dispose = on_dispose_tile_context;

    /* setup attributes */
    self->style_file_path = h2o_strdup(NULL, style_file_path, SIZE_MAX);
    self->map = alloc_mapnik(style_file_path);

    return self;
}
/*--------------------*/
