/*
 * Copyright (c) 2014 DeNA Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include "./test.h"

static void loopback_on_send(h2o_ostream_t *self, h2o_req_t *req, h2o_iovec_t *inbufs, size_t inbufcnt, int is_final)
{
    h2o_loopback_conn_t *conn = H2O_STRUCT_FROM_MEMBER(h2o_loopback_conn_t, _ostr_final, self);
    size_t i;

    for (i = 0; i != inbufcnt; ++i) {
        h2o_buffer_reserve(&conn->body, inbufs[i].len);
        memcpy(conn->body->bytes + conn->body->size, inbufs[i].base, inbufs[i].len);
        conn->body->size += inbufs[i].len;
    }

    if (is_final)
        conn->_is_complete = 1;
    else
        h2o_proceed_response(&conn->req);
}

static socklen_t get_sockname(h2o_conn_t *conn, struct sockaddr *sa)
{
    struct sockaddr_in *sin = (void *)sa;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(0x7f000001);
    sin->sin_port = htons(80);
    return sizeof(*sin);
}

static socklen_t get_peername(h2o_conn_t *conn, struct sockaddr *sa)
{
    struct sockaddr_in *sin = (void *)sa;
    sin->sin_family = AF_INET;
    sin->sin_addr.s_addr = htonl(0x7f000001);
    sin->sin_port = htons(55555);
    return sizeof(*sin);
}

h2o_loopback_conn_t *h2o_loopback_create(h2o_context_t *ctx, h2o_hostconf_t **hosts)
{
    h2o_loopback_conn_t *conn = h2o_mem_alloc(sizeof(*conn));

    memset(conn, 0, offsetof(struct st_h2o_loopback_conn_t, req));
    conn->super.ctx = ctx;
    conn->super.hosts = hosts;
    conn->super.get_sockname = get_sockname;
    conn->super.get_peername = get_peername;
    h2o_init_request(&conn->req, &conn->super, NULL);
    h2o_buffer_init(&conn->body, &h2o_socket_buffer_prototype);
    conn->req._ostr_top = &conn->_ostr_final;
    conn->_ostr_final.do_send = loopback_on_send;

    return conn;
}

void h2o_loopback_destroy(h2o_loopback_conn_t *conn)
{
    h2o_buffer_dispose(&conn->body);
    h2o_dispose_request(&conn->req);
    free(conn);
}

void h2o_loopback_run_loop(h2o_loopback_conn_t *conn)
{
    if (conn->req.input.scheme == NULL)
        conn->req.input.scheme = &H2O_URL_SCHEME_HTTP;
    if (conn->req.version == 0)
        conn->req.version = 0x100; /* HTTP/1.0 */

    h2o_process_request(&conn->req);

    while (!conn->_is_complete) {
#if H2O_USE_LIBUV
        uv_run(conn->super.ctx->loop, UV_RUN_ONCE);
#else
        h2o_evloop_run(conn->super.ctx->loop);
#endif
    }
}

char *sha1sum(const void *src, size_t len)
{
    SHA_CTX ctx;
    unsigned char bin[SHA_DIGEST_LENGTH];
    static char hexbuf[SHA_DIGEST_LENGTH * 2 + 1];
    size_t i;

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, src, len);
    SHA1_Final(bin, &ctx);

    for (i = 0; i != SHA_DIGEST_LENGTH; ++i) {
        hexbuf[i * 2] = ("0123456789abcdef")[bin[i] >> 4];
        hexbuf[i * 2 + 1] = ("0123456789abcdef")[bin[i] & 0xf];
    }
    hexbuf[i * 2] = '\0';

    return hexbuf;
}

h2o_loop_t *test_loop;

static void test_loopback(void)
{
    h2o_globalconf_t conf;
    h2o_context_t ctx;
    h2o_loopback_conn_t *conn;

    h2o_config_init(&conf);
    h2o_config_register_host(&conf, h2o_iovec_init(H2O_STRLIT("default")), 65535);
    h2o_context_init(&ctx, test_loop, &conf);

    conn = h2o_loopback_create(&ctx, ctx.globalconf->hosts);
    conn->req.input.method = h2o_iovec_init(H2O_STRLIT("GET"));
    conn->req.input.path = h2o_iovec_init(H2O_STRLIT("/"));
    h2o_loopback_run_loop(conn);

    ok(conn->req.res.status == 404);

    h2o_context_dispose(&ctx);
    h2o_config_dispose(&conf);
}

int main(int argc, char **argv)
{
    { /* library tests */
        subtest("lib/common/multithread.c", test_lib__common__multithread_c);
        subtest("lib/common/hostinfo.c", test_lib__common__hostinfo_c);
        subtest("lib/common/serverutil.c", test_lib__common__serverutil_c);
        subtest("lib/common/serverutil.c", test_lib__common__socket_c);
        subtest("lib/common/string.c", test_lib__common__string_c);
        subtest("lib/common/url.c", test_lib__common__url_c);
        subtest("lib/common/time.c", test_lib__common__time_c);
        subtest("lib/core/headers.c", test_lib__core__headers_c);
        subtest("lib/core/proxy.c", test_lib__core__proxy_c);
        subtest("lib/handler/headers.c", test_lib__handler__headers_c);
        subtest("lib/handler/mimemap.c", test_lib__handler__mimemap_c);
        subtest("lib/http2/hpack.c", test_lib__http2__hpack);
        subtest("lib/http2/scheduler.c", test_lib__http2__scheduler);
    }

    { /* tests that use the run loop */
#if H2O_USE_LIBUV
        test_loop = h2o_mem_alloc(sizeof(*test_loop));
        uv_loop_init(test_loop);
#else
        test_loop = h2o_evloop_create();
#endif

        subtest("lib/t/test.c/loopback", test_loopback);
        subtest("lib/fastcgi.c", test_lib__handler__fastcgi_c);
        subtest("lib/file.c", test_lib__handler__file_c);
        subtest("lib/redirect.c", test_lib__handler__redirect_c);
        subtest("issues/293.c", test_issues293);

#if H2O_USE_LIBUV
        uv_loop_close(test_loop);
        free(test_loop);
#else
// h2o_evloop_destroy(loop);
#endif
    }

    return done_testing();
}
