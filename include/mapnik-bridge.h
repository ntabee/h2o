#ifndef MAPNIK_BRIDGE_H
#define MAPNIK_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#define MAPNIK_MAP_PTR void*
void init_mapnik_datasource(const char* datasource);
MAPNIK_MAP_PTR alloc_mapnik(const char* style_path);
void dispose_mapnik(void* m);
void load_fonts(const char *font_dir);

typedef void (*tile_rendered_callback)(h2o_req_t *req, const char* content, size_t content_length, const char* physical_tile_path, const char* mime_type, size_t mime_type_len, int flags);
void render_tile(h2o_req_t* req, MAPNIK_MAP_PTR map, const char* tile_path, uint32_t zoom, uint32_t x, uint32_t y, const char* mime_type, size_t mime_type_len, int flags, tile_rendered_callback callback);

#ifdef __cplusplus
}
#endif

#endif