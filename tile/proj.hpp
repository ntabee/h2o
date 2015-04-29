#ifndef PROJ_HPP
#define PROJ_HPP

#include <cstdint>

constexpr int TILE_SIZE = 256;

/*
Direct conversion from tile path z/x/y.png to Spherical Mercator: 
    http://www.maptiler.org/google-maps-coordinates-tile-bounds-projection/
Given: (x, y, z) a tile identifier
let TILE_SIZE = 256
let INITIAL_RESOLUTION = 2 * Math.PI * 6378137 / TILE_SIZE
assert (INITIAL_RESOLUTION = 156543.03392804097)
let ORIGIN_SHIFT = 2*Math.PI * 6378137/2.0
assert (ORIGIN_SHIFT = 20037508.342789244)

let res(z) = INITIAL_RESOLUTION / (1 << z)

let proj(x, y, z) = 
    let mx =   TILE_SIZE * x * res(z) - ORIGIN_SHIFT
    let my = -(TILE_SIZE * y * res(z) - ORIGIN_SHIFT)
    (mx, my)

e.g. proj(3638, 1612, 12) == (15556463.996599074, 4265797.674539117)

let proj_lonlat(mx, my) = 
    let lon = (mx / ORIGIN_SHIFT) * 180.0
    let lat = (my / ORIGIN_SHIFT) * 180.0

    lat <- 180 / math.pi * (2 * math.atan( math.exp( lat * math.pi / 180.0)) - math.pi / 2.0)
    (lat, lon)

e.g. proj_lonlat(proj(3638, 1612, 12)) = (139.74609375000003, 35.74651225991853)
*/
static inline void tile_to_merc(uint32_t zoom, uint32_t x, uint32_t y, double& mx, double& my) {
    constexpr double INITIAL_RESOLUTION = 2*M_PI * 6378137 / TILE_SIZE;
    constexpr double ORIGIN_SHIFT = 2*M_PI * 6378137/2.0;

    const double res = INITIAL_RESOLUTION / (1<<zoom);
    mx =   TILE_SIZE*x*res - ORIGIN_SHIFT;
    my = -(TILE_SIZE*y*res - ORIGIN_SHIFT);
}

static inline void tile_to_merc_box(uint32_t zoom, uint32_t x, uint32_t y, double& left, double& top, double& right, double& bot) {
    constexpr double INITIAL_RESOLUTION = 2*M_PI * 6378137 / TILE_SIZE;
    constexpr double ORIGIN_SHIFT = 2*M_PI * 6378137/2.0;

    const double res = INITIAL_RESOLUTION / (1<<zoom);
    left  =   TILE_SIZE*x*res - ORIGIN_SHIFT;
    top   = -(TILE_SIZE*y*res - ORIGIN_SHIFT);
    right =   TILE_SIZE*(x+1)*res - ORIGIN_SHIFT;
    bot   = -(TILE_SIZE*(y+1)*res - ORIGIN_SHIFT);
}

/*
Direct conversion from lon/lat with a zoom level z to its logical tile path z/x/y.png: 
    http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames#Lon..2Flat._to_tile_numbers_4
Given: (lon, lat, z) a coordinate and a zoom

  my $xtile = int( ($lon+180)/360 * 2**$zoom ) ;
  v (1 - log(tan(deg2rad($lat)) + sec(deg2rad($lat)))/pi)
  my $ytile = int( v/2 * 2**$zoom ) ;
  return ($xtile, $ytile);

let res(z) = 2^z 
let tx = (lon+180)/360 * res
let rad = degree-to-radian(lat)
let secant = 1.0 / cos(rad)
let log_tan_sec = log(tan(rad) + secant)
let v = 1 - (log_tan_sec / Math.PI)
let ty = (v/2 * res).toInt
(tx, ty)
*/
#define deg2rad(v) (v*M_PI / 180.0)
static inline void lonlat_to_tile(double lon_x, double lat_y, uint32_t zoom, uint32_t& tx, uint32_t& ty) {
    const double res = (1 << zoom);
    tx = (uint32_t)( (lon_x+180.0)/360.0 * res );

    lat_y = deg2rad(lat_y);
    const double secant = 1.0 / cos(lat_y);
    const double log_tan_sec = log(tan(lat_y) + secant);
    const double v = 1.0 - (log_tan_sec / M_PI);
    ty = (uint32_t)( floor(v/2.0 * res) );
}
#undef deg2rad

#endif
