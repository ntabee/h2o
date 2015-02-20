/*
 * Copyright (c) N. Tabuchi (@n_tabee)
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

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include "h2o.h"

enum TILE_SUFFIX {
    PNG, JPG
};

static int hook_and_generate(h2o_req_t *req, const char *path, size_t path_len) {
    size_t req_path_prefix = req->pathconf->path.len;
    size_t tile_path_len = req->path_normalized.len - req_path_prefix;
    char* tile_path_head = req->path_normalized.base + req_path_prefix;

    /*
    match tile_path_head with ($zoom)/($x)/($y).(png|jpg), where:
    	   $zoom  = \d{1,2}
        $x, $y = \d{1,9}
    non-match will immediately return -1
    */
    char* hd = tile_path_head;
    unsigned int zoom = 0, x = 0, y = 0;
    enum TILE_SUFFIX suffix = PNG;

#define IF_DIGIT_STORE(v) if (isdigit(*hd)) {\
    v = 10*v + (*hd - '0'); \
    ++hd; \
} else {\
    return -1; \
}

#define EXPECT(c) if (*hd == c) { ++hd; } else { return -1; }

#define STORE_DIGIT_OR_GOTO_ON_DELIM(v, c, label) if (isdigit(*hd)) { \
    v = 10*v + (*hd - '0'); \
    ++hd; \
} else if (*hd == c) { \
    ++hd; \
    goto label; \
} else { \
    return -1; \
}

    // zoom ~= \d{1,2}
    IF_DIGIT_STORE(zoom)
    STORE_DIGIT_OR_GOTO_ON_DELIM(zoom, '/', ParseX)
    EXPECT('/')
ParseX:
    // x ~= \d{1,9}
    IF_DIGIT_STORE(x)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    STORE_DIGIT_OR_GOTO_ON_DELIM(x, '/', ParseY)
    EXPECT('/')
ParseY:
    // y ~= \d{1,9}
    IF_DIGIT_STORE(y)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    STORE_DIGIT_OR_GOTO_ON_DELIM(y, '.', ParseSuffix)
    EXPECT('.')
ParseSuffix:
    if (hd[0] == 'p' && hd[1] == 'n' && hd[2] == 'g') {
       suffix = PNG; 
    } else if (hd[0] == 'j' && hd[1] == 'p' && hd[2] == 'g') {
        suffix = JPG;
    } else {
        return -1;
    }
    fprintf(stderr, "***%d %d %d %d***\n", zoom, x, y, suffix);

    return -1;
}