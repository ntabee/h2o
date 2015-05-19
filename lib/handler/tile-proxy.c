#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "h2o.h"
#include "tile/tile-rewrite-path.h"
#include "tile/mkdir-p.h"

struct st_h2o_tile_proxy_handler_t {
    struct rp_handler_t super;
    h2o_iovec_t local_base_path; /* has "/" appended at last */
    int (*on_req_delegate)(struct st_h2o_handler_t *self, h2o_req_t *req);
};

struct st_h2o_tile_store_filter_t {
    h2o_filter_t super;
    h2o_iovec_t local_base_path; /* has "/" appended at last */
};

struct st_store_tile_t {
    h2o_ostream_t super;
    h2o_iovec_t local_tile_path; 
    h2o_iovec_t tmp_tile_path;
    int fd;
};

static void store_chunk(h2o_ostream_t *_self, h2o_req_t *req, h2o_iovec_t *inbufs, size_t inbufcnt, int is_final)
{
    struct st_store_tile_t *self = (void *)_self;
    int i;

    int fd = self->fd;
    if (fd < 0) {
        goto Cont;
    }

    for (i=0; i != inbufcnt; ++i) {
        int v = write(fd, inbufs[i].base, inbufs[i].len);
        if (v != inbufs[i].len) {
            h2o_req_log_error(req, "lib/handler/tile-proxy.c", "Failed to write to file %s: %s\n", self->tmp_tile_path.base, strerror(errno));
            close(fd);
            unlink(self->tmp_tile_path.base);
            goto Cont;
        }
    }

    if (is_final) {
        close(fd);
        if (rename(self->tmp_tile_path.base, self->local_tile_path.base) != 0) {
            h2o_ostream_send_next(&self->super, req, inbufs, inbufcnt, is_final);
            for (i=0; i<32; ++i) {
                usleep(10);
                if (rename(self->tmp_tile_path.base, self->local_tile_path.base) != 0) {
                    return;
                }
            }
            return;
        }
    }
Cont:
    h2o_ostream_send_next(&self->super, req, inbufs, inbufcnt, is_final);
}

static void on_setup_ostream(h2o_filter_t *_self, h2o_req_t *req, h2o_ostream_t **slot)
{
    struct st_h2o_tile_store_filter_t *self = (void *)_self;
    uint32_t x = 0, y = 0, z = 0;
    char* physical_tile_path = alloca(28);

    if (req->res.status != 200) {
        h2o_req_log_error(req, "lib/handler/tile-proxy.c", "Upstream returned %d: %s\n", req->res.status, req->res.reason);
    }
    /*
    req->path_normalized.base is of the form "/z/x/y.png"
    */
    if ( likely(
            req->res.status == 200 && 
            tile_rewrite_path(req->path_normalized.base, "/", 1, physical_tile_path, 28, &z, &x, &y)) ) {
        struct st_store_tile_t *store_tile;
        char thread_id[18];
        /*
        Now, physical_path is of the form /z/nnn/nnn/nnn/nnn/nnn.png
        */
        /* 
        Remove the leading '/'
        */
        ++physical_tile_path;
        h2o_iovec_t full_path = h2o_concat(&req->pool, self->local_base_path, h2o_iovec_init(physical_tile_path, strlen(physical_tile_path)));

        store_tile = (void *)h2o_add_ostream(req, sizeof(struct st_store_tile_t), slot);
        store_tile->local_tile_path = full_path;
        store_tile->super.do_send = store_chunk;
        snprintf(thread_id, 22, ".%x.png", pthread_self());
        store_tile->tmp_tile_path = h2o_concat(&req->pool, full_path, h2o_iovec_init(thread_id, strlen(thread_id)));
        mkdir_p_parent(store_tile->tmp_tile_path.base);
        store_tile->fd = open(store_tile->tmp_tile_path.base, O_WRONLY | O_TRUNC | O_CREAT, 0666);
        if (store_tile->fd < 0) {
            h2o_req_log_error(req, "lib/handler/tile-proxy.c", "Could not open file %s: %s\n", store_tile->tmp_tile_path.base, strerror(errno));
        }
    } 

    h2o_setup_next_ostream(&self->super, req, slot);
}

static int on_req_tile(struct st_h2o_handler_t *_self, h2o_req_t *req) {
    h2o_tile_proxy_handler_t *self = (void*)_self;
    uint32_t x = 0, y = 0, z = 0;
    char* physical_tile_path = alloca(28);
    char* tile_path = req->path_normalized.base + req->pathconf->path.len; /* is of the form z/x/y.png */
    if (likely(tile_rewrite_path(tile_path, "", 0, physical_tile_path, 28, &z, &x, &y))) {
        h2o_iovec_t full_path = h2o_concat(&req->pool, self->local_base_path, h2o_iovec_init(physical_tile_path, strlen(physical_tile_path)));
        if (likely(access(full_path.base, F_OK) == 0)) {
            return h2o_file_send(req, 200, "OK", full_path.base, h2o_iovec_init(H2O_STRLIT("image/png")), 0);
        }
    }
    return self->on_req_delegate(_self, req);
}

h2o_tile_proxy_handler_t *h2o_tile_proxy_register(h2o_pathconf_t *pathconf, const char *local_base_path, const char *proxy) {
    h2o_iovec_t local_base_path_v = h2o_strdup_slashed(NULL, local_base_path, SIZE_MAX);
    h2o_tile_proxy_handler_t *self;

    do { /* scoping */
        /* Initialize the handler */
        h2o_proxy_config_vars_t proxy_config = { 10*1000, 0, 2000 };

        self = (void*)h2o_create_handler(pathconf, sizeof(*self));
        h2o_url_parse(proxy, strlen(proxy), &self->super.upstream);

        self->super = *(h2o_proxy_register_reverse_proxy(pathconf, &self->super.upstream, &proxy_config));
        self->local_base_path = local_base_path_v;
        self->on_req_delegate = self->super.super.on_req;
        self->super.super.on_req = on_req_tile;
    } while (0);

    do { /* scoping */
        struct st_h2o_tile_store_filter_t *self = (void *)h2o_create_filter(pathconf, sizeof(*self));
        self->local_base_path = local_base_path_v;
        self->super.on_setup_ostream = on_setup_ostream;
    } while (0);

    return self;
}

// h2o_tile_proxy_handler_t *h2o_tile_register(h2o_pathconf_t *pathconf, const char* base_path, const char *upstream)
// {
//     h2o_tile_proxy_register(pathconf, base_path, upstream);

//     return self;
// }
