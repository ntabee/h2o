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
#include <stdint.h>
#include "h2o.h"
#include "path-mapper.h"

int tile_hook(const char* rpath, const char* base, size_t base_len, char *path_buf, size_t path_buf_len, uint32_t* pzoom, uint32_t* px, uint32_t* py) {
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
#define LEX_DIGIT(v) if (isdigit(*hd)) {\
    v = 10*v + (*hd - '0'); \
    ++hd; \
} else {\
    return 0; \
}

#define EXPECT(c) if (*hd == c) { ++hd; } else { return 0; }

/*
Let us exploit good old gotos to simulate state-transitions
*/
#define LEX_DIGIT_OR_GOTO_ON_DELIM(v, c, label) if (isdigit(*hd)) { \
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
    if (hd[0] == 'p' && hd[1] == 'n' && hd[2] == 'g') {
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