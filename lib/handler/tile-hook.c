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

enum TILE_SUFFIX {
    PNG, JPG
};

/*
Map a 4-tuple (zoom, x, y, suffix) to the physical path for zoom/x/y.suffix.
The mapping is a "DESIGN-DECISION OF NO RETURN", so let me present a verbose sketch:

- The domain is:
    + zoom in [1 ...   20]
    + x, y in [0 ... 2^20 - 1]
    + suffix in { PNG, JPG }
    The "zoom level 20" means the scale of 1/500 where 1 pixel roughly covers 15cm square on the earth.
    It's the finest resolution which "standard" raster tile servers support.
  Violation to these will result in a "garbage" tile path that renders an unexpected place,
  but this function itself still behaves memory-safe. 
N.B.
  x, y in [0 ... 2^20-1] means we need 2^20 * 2^20 = 1T *files* to serve the entire planet, solely at zoom 20,
  which would call for some petabyte-scale storage. I can't afford to test it literally.

- The buffer "buf" for the result must:
    + be caller-alloc'ed, and
    + guarantee 27-byte length at least (the rationale for this magic number is explained below.)

- The resultant path is of the form:
    zz/nnn/nnn/nnn/nnn/nnn.(png|jpg)
      zz:  1 - 2 digits
      nnn: 1 - 3 digits,
        a combination of five such nnn's represents the 40-bit pair (x, y) divided into five 8-bits
      So, the max len of a path sums-up to 26 bytes; adding the NUL terminator calls for a buffer of at least 27 bytes.
  The path is relative to "somewhere, defined by the caller" and no "leading slash" is included.

  N.B.
    + The path is SLASH-DELIMITED, meaning may NOT work on Windows as expected.
*/
static void to_physical_path(char *buf, uint32_t zoom, uint32_t x, uint32_t y, enum TILE_SUFFIX suffix) {
/*
No such a param as "buflen"; malicious/vulnerable codes will anyway pass a wrong value.
The only rule to follow instead is the magic 27-byte convention.
*/
    unsigned char i, hash[5];

    /* Determine the suffix */
    const char* suffix_str;
    switch (suffix) {
    case PNG:
        suffix_str = "png";
        break;
    case JPG:
        suffix_str = "jpg";
        break;
    default:
        /* failover */
        suffix_str = "png";
        break;
    }
    /*
    Divide (x, y) into a 5-tuple of bytes.
    We follow mod_tile's "4bit-wise pairing" scheme:
      https://github.com/openstreetmap/mod_tile/blob/master/src/store_file_utils.c#L86
    so that adjacent tiles are mapped to separate directories.
    */

    for (i=0; i<5; i++) {
        hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
        x >>= 4;
        y >>= 4;
    }

    /* Assure "zoom" fits within 2 digits. Valid values (1-20) are not affected */
    zoom = zoom % 100;

#define PHYSPATH_LEN 26
    snprintf(buf, PHYSPATH_LEN, "%d/%u/%u/%u/%u/%u.%s", zoom, hash[4], hash[3], hash[2], hash[1], hash[0], suffix_str);    
#undef  PHYSPATH_LEN;
}

static int hook_and_generate(h2o_req_t *req, const char *path, size_t path_len) {
    size_t req_path_prefix = req->pathconf->path.len;
    /* size_t tile_path_len = req->path_normalized.len - req_path_prefix; // is not used */
    char* tile_path_head = req->path_normalized.base + req_path_prefix;

    /*
    match tile_path_head with ($zoom)/($x)/($y).(png|jpg), where:
        $zoom  = \d{1,2}
        $x, $y = \d{1,9}
    non-match will immediately return -1.

    No "overflow check" is performed, so $x, $y in [2^32 ... 9,999,999,999] will
    result in an unexpected (but still "defined") behavior.

    The suffix (png|jpg) is case-sensitive & accepts lowers only.
    I don't think any common tile clients intentionally query capital filenames.
    */
    char* hd = tile_path_head;  // points to the next char to scan.
    uint32_t zoom = 0, x = 0, y = 0;
    enum TILE_SUFFIX suffix = PNG;

    /*
    Follows a hard-coded matcher for (\d{1,2})/(\d{1,9})/(\d{1,9}).(png|jpg)
    */
#define LEX_DIGIT(v) if (isdigit(*hd)) {\
    v = 10*v + (*hd - '0'); \
    ++hd; \
} else {\
    return -1; \
}

#define EXPECT(c) if (*hd == c) { ++hd; } else { return -1; }

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
    return -1; \
}

/* 
LetsParse: // <- the "initial state" 
*/
    // zoom = hd.match \d{1,2}
    LEX_DIGIT(zoom)
    LEX_DIGIT_OR_GOTO_ON_DELIM(zoom, '/', ParseX)
    EXPECT('/')
ParseX:
    // x = hd.match \d{1,9}
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
    // y = hd.match \d{1,9}
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
        return -1;
    }
/* 
Done: // <- the "final state" 
*/

    /*
    Here, tile_path_head is guaranteed to match the pattern & zoom, x, y are accordingly stored,
    o.w. the function must have already returned -1.
    */
    fprintf(stderr, "***%d %d %d %d***\n", zoom, x, y, suffix);

    return -1;
}   