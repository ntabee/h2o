#include <mapnik/version.hpp>
 
#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/font_engine_freetype.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/expression.hpp>
#include <mapnik/color_factory.hpp>
#if MAPNIK_MAJOR_VERSION >= 3
 #include <mapnik/image.hpp>
 #define image_32 image_rgba8
#else
 #include <mapnik/graphics.hpp>
#endif
#include <mapnik/image_util.hpp>
#include <mapnik/image_view.hpp>
#include <mapnik/image_view_any.hpp>
#include <mapnik/config_error.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/box2d.hpp>

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "proj.hpp"
#include "path-mapper.h"

// cf. http://stackoverflow.com/a/2336245
// This routine is not tested with general cases:
// inputs containing path-metacharacters might cause
// unexpected behaviors, including security-critical ones.
// So, don't try to reuse it elsewhere, consult boost::filesystem::create_directories() or
// other quality-assured alternatives.
//
// The reason I still stick to this *here* is just I don't want
// *this specific* compile unit to be boost-dependent.
static inline void mkdir_p(char *tile_path) {
        size_t len;

        len = strlen(tile_path);
        if (len <= 0) {
            return;
        }
        char* end = tile_path + len - 1;
        // Find the last /
        while (*end != '/' && end != tile_path) {
            --end;
        }
        // Now either (1) *end = '/' && end is the last '/', or (2) end == tile_path
        // When (1), chop the trailing "/..."
        if (*end == '/') {
            *end = '\0';
        }
        for (char* p = tile_path; p != end; ++p) {
            if(*p == '/') {
                *p = 0;
                mkdir(tile_path, 0755);
                *p = '/';
            }
        }
        mkdir(tile_path, 0755);
        if (*end == '\0') {
            *end = '/';
        }
}
extern "C" void render_tile(const char* tile_path, uint32_t zoom, uint32_t x, uint32_t y) {
    mkdir_p(const_cast<char*>(tile_path));
    fprintf(stderr, tile_path);
}

extern "C" void* alloc_mapnik(const char* style_path) {
    mapnik::Map* m = new mapnik::Map(TILE_SIZE*2, TILE_SIZE*2);    // To avoid label scattering, we render a 2x2 larger area and then clip the center.
    mapnik::load_map(*m, style_path);
    return m;
}

extern "C" void dispose_mapnik(void* m) {
    mapnik::Map* map = (mapnik::Map*)m;
    delete map;
}

extern "C" void init_mapnik_datasource(const char* datasource) {
    if (datasource == NULL) {
        datasource = "/usr/local/lib/mapnik/input";
    }    
    mapnik::datasource_cache::instance().register_datasources(datasource);
}

extern "C" void load_fonts(const char *font_dir) {
    DIR *fonts = opendir(font_dir);
    struct dirent *entry;
    char path[PATH_MAX]; // FIXME: Eats lots of stack space when recursive

    if (!fonts) {
        fprintf(stderr, "Unable to open font directory: %s", font_dir);
        return;
    }

    while ((entry = readdir(fonts))) {
        struct stat b;
        char *p;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;
        snprintf(path, sizeof(path), "%s/%s", font_dir, entry->d_name);
        if (stat(path, &b))
            continue;
        if (S_ISDIR(b.st_mode)) {
            load_fonts(path);
            continue;
        }
        p = strrchr(path, '.');
        if (p && !strcmp(p, ".ttf")) {
            fprintf(stderr, "DEBUG: Loading font: %s", path);
            mapnik::freetype_engine::register_font(path);
        }
    }
    closedir(fonts);
}
