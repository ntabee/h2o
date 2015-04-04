#ifndef PROJ_HPP
#define PROJ_HPP

#include <cstdint>

void tile_to_merc(uint32_t zoom, uint32_t x, uint32_t y, double& mx, double& my);
void tile_to_merc_box(uint32_t zoom, uint32_t x, uint32_t y, double& left, double& top, double& right, double& bot);
void lonlat_to_tile(double lon_x, double lat_y, uint32_t zoom, uint32_t& tx, uint32_t& ty);
constexpr int TILE_SIZE = 256;

#endif
