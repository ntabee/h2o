#ifndef TILE_HOOK_H
#define TILE_HOOK_H
int tile_hook(const char* tile_path, const char* base, size_t base_len, char *path_buf, size_t path_buf_len, uint32_t* pzoom, uint32_t* px, uint32_t* py);
void render_tile(const char* tile_path, uint32_t zoom, uint32_t x, uint32_t y); // defined in render-tile.cpp
#endif