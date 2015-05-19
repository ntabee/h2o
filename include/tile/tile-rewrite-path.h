#pragma once
#include <ctype.h>
#include "path-mapper.h"

#if __GNUC__ >= 3
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline int tile_parse_path(const char* path, size_t base_len, uint32_t* pzoom, uint32_t* px, uint32_t* py, enum TILE_SUFFIX* psuffix) {
    /*
    rpath: abs. path to the tile, as resolved by h2o.
    */
    const char* tile_path_head = path + base_len;
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

    *pzoom = zoom;
    *px    = x;
    *py    = y;
    *psuffix = suffix;
    /* fprintf(stderr, "***%d %d %d %d ==> %s ***\n", zoom, x, y, suffix, path_buf); */

    return 1;
}

static inline int tile_rewrite_path(const char* rpath, const char* base, size_t base_len, char *path_buf, size_t path_buf_len, uint32_t* pzoom, uint32_t* px, uint32_t* py) {
    enum TILE_SUFFIX suffix = PNG;
    assert(path_buf_len >= base_len + 27);
    if (unlikely( !tile_parse_path(rpath, base_len, pzoom, px, py, &suffix) )) {
        return 0;
    } 
    memcpy(path_buf, base, base_len);
    to_physical_path(path_buf + base_len, *pzoom, *px, *py, suffix);

    return 1;

}

#ifdef __cplusplus
}
#endif
