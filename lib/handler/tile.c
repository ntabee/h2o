
//#include "tile-hook.h"
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <strings.h>
#include "path-mapper.h"
#include "tile/tile-rewrite-path.h"
#include "tile/mapnik-bridge.h"
#include "tile/tile-proxy.h"

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
    req->res.content_length = content_length;
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
    h2o_mimemap_type_t *mime_type;
    char *rpath;
    size_t rpath_len, req_path_prefix;
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
    rpath = alloca(super->real_path.len + (req->path_normalized.len - req_path_prefix) + super->max_index_file_len + 1);
    rpath_len = 0;
    memcpy(rpath + rpath_len, super->real_path.base, super->real_path.len);
    rpath_len += super->real_path.len;
    memcpy(rpath + rpath_len, req->path_normalized.base + req_path_prefix, req->path_normalized.len - req_path_prefix);
    rpath_len += req->path_normalized.len - req_path_prefix;

    rpath[rpath_len] = '\0';
    do { /* scoping */
        uint32_t x, y, z;
        size_t tile_path_buf_len = super->real_path.len + (req->path_normalized.len - req_path_prefix) + 28;
        char* tile_path = alloca(tile_path_buf_len);
        /* Try to convert rpath (base/z/x/y.png) to the tiles' scheme: base/z/nnn/nnn/nnn/nnn/nnn.png */
        if (likely(tile_rewrite_path(rpath, super->real_path.base, super->real_path.len, tile_path, tile_path_buf_len, &z, &x, &y))) {
            rpath = tile_path;
            rpath_len = strlen(rpath) + 1;  /* The actual length of rpath */
            /* If successful, try to send it back as-is */
            if ((generator = create_generator(req, tile_path, rpath_len, &is_dir, super->flags)) != NULL) {
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
#ifdef H2O_TILE_PROXY
                assert(!"unreachable");
#else
                /* 
                This function is already a "custom handler", associating yet another to ".png" files
                would be highly probably a misconfiguration.
                */
                mime_type = h2o_mimemap_get_type(self->super.mimemap, h2o_get_filext(rpath, rpath_len));
                switch (mime_type->type) {
                case H2O_MIMEMAP_TYPE_MIMETYPE:
                    render_tile(req, self->map, rpath, z, x, y, mime_type->data.mimetype.base, mime_type->data.mimetype.len, super->flags, on_tile_rendered);
                    break;
                case H2O_MIMEMAP_TYPE_DYNAMIC:
                    h2o_send_error(req, 500, "Internal Server Error", "MIME type for .png is declared as 'dynamic.'", 0);
                }
#endif
            } else {
                h2o_send_error(req, 403, "Access Forbidden", "access forbidden", 0);
            }
            return 0;
        } else {
            // If path conversion failed, fallback to on_req()
            return on_req((h2o_handler_t*)super, req);
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
    mime_type = h2o_mimemap_get_type(self->super.mimemap, h2o_get_filext(rpath, rpath_len));

    /* return file */
    switch (mime_type->type) {
    case H2O_MIMEMAP_TYPE_MIMETYPE:
        do_send_file(generator, req, 200, "OK", mime_type->data.mimetype, is_get);
        return 0;
    case H2O_MIMEMAP_TYPE_DYNAMIC:
        h2o_send_error(req, 500, "Internal Server Error", "MIME type for .png is declared as 'dynamic.'", 0);
        return 0;
    }    

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


#define on_dispose_tile on_dispose;
h2o_tile_handler_t *h2o_tile_register(h2o_pathconf_t *pathconf, const char *base_path, const char* style_file_path)
{
    h2o_tile_handler_t *self;

    self = (void *)h2o_create_handler(pathconf, sizeof(*self));

    /* super() */
    /* Don't let super try to respond the default index.html */
    const char* NO_INDEX_FILES[1];
    NO_INDEX_FILES[0] = NULL;
    self->super = *(h2o_file_register(pathconf, base_path, NO_INDEX_FILES, NULL, 0));
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

