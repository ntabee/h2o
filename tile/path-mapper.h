#ifndef PATH_MAPPER_H
#define PATH_MAPPER_H

enum TILE_SUFFIX {
    PNG, JPG
};

/*
Map a 4-tuple (zoom, x, y, suffix) to the physical path for zoom/x/y.suffix.
The mapping is a "DESIGN-DECISION OF NO RETURN", so let me present a verbose sketch:

- The domain is:
    + zoom in [0 ...   20]
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
static inline void to_physical_path(char *buf, uint32_t zoom, uint32_t x, uint32_t y, enum TILE_SUFFIX suffix) {
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
    so that tiles are clustered into separate directories.
    */

    for (i=0; i<5; i++) {
        hash[i] = ((x & 0x0f) << 4) | (y & 0x0f);
        x >>= 4;
        y >>= 4;
    }

    /* Assure "zoom" fits within 2 digits. Valid values (0-20) are not affected */
    zoom = zoom % 100;

#define PHYSPATH_LEN 26
    snprintf(buf, PHYSPATH_LEN, "%d/%u/%u/%u/%u/%u.%s", zoom, hash[4], hash[3], hash[2], hash[1], hash[0], suffix_str);    
#undef  PHYSPATH_LEN
}
/*
A quick trick for callers
*/
#define ALLOCA_PATH_BUF(x) char x[27]


#endif
