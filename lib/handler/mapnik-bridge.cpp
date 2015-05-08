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

#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include <fstream>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "h2o.h"

#include "proj.hpp"
#include "path-mapper.h"
#include "mapnik-bridge.h"

#if __GNUC__ >= 3
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif
void mkdir_p(const boost::filesystem::path& base_path) {
    if (likely(boost::filesystem::exists(base_path))) {
        boost::filesystem::path canonical = boost::filesystem::canonical(base_path);
        if (unlikely(!boost::filesystem::is_directory(canonical))) {
            std::string message = str(boost::format("Error: %1% exists, but not a directory.\n") % base_path);
            throw std::runtime_error( message );
        }
    } else {
        boost::filesystem::create_directories(base_path);
    }
}

extern "C" void render_tile(h2o_req_t* req, void* map_ptr, const char* tile_path, uint32_t zoom, uint32_t x, uint32_t y, const char* mime_type, size_t mime_type_len, int flags, tile_rendered_callback callback) {

    try {
        using namespace mapnik;
        boost::filesystem::path path(tile_path);

        const Map _map = *(Map*)map_ptr;
        Map m(_map);    // clone

        /* (left, top)-(right, bottom) in Mercator projection. */ 
        double l, t, r, b; 
        image_32 image(m.width(),m.height()); 
        image_view_rgba8 vw(TILE_SIZE/2, TILE_SIZE/2, TILE_SIZE, TILE_SIZE, image); 

        tile_to_merc_box(zoom, x, y, l, t, r, b); 
        box2d<double> bbox(l, t, r, b); 
        /* Double the bbox */ 
        bbox.width(bbox.width() * 2); 
        bbox.height(bbox.height() * 2); 
        /* Render */ 
        m.zoom_to_box(bbox); 
        agg_renderer<image_32> ren(m,image); 
        ren.apply(); 
        /* Clip the center 256x256 into vw */ 
        std::string buf = save_to_string(image_view_any(vw), "png256:e=miniz");
        callback(req, buf.c_str(), buf.length(), tile_path, mime_type, mime_type_len, flags);

        /* write to the filesystem */
        /* 
        As the rendered image was already h2o_send_inline'ed in the callback, 
        any errors in saving it to a file will be just error-logged: 
        response to the client is NOT affected: no such thing as "500 Internal Server Error."
        */
        try {
            mkdir_p(path.parent_path());

            std::ofstream ofs(tile_path, std::ios_base::out | std::ios_base::binary);
            ofs.write(buf.c_str(), buf.length());
        } catch (std::exception& e) {
            h2o_req_log_error(req, "lib/handler/mapnik-bridge.cpp", "%s", e.what());
        }
    } catch (std::exception& e) {
        h2o_req_log_error(req, "lib/handler/mapnik-bridge.cpp", "%s", e.what());
        req->res.status = 500;
        req->res.reason = "internal server error";
        h2o_send_inline(req, NULL, 0);
    }

}

extern "C" void* alloc_mapnik(const char* style_path) {
    // To avoid label scattering, we render a 2x2 larger area and then clip the center.
    mapnik::Map* m = new mapnik::Map(TILE_SIZE*2, TILE_SIZE*2);    
    // Any failure in parsing the style file will immediately cause abortion by an unhandled exception, this is intended.
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

    // Absence of datasource dir will immediately cause abortion by an unhandled exception, this is intended.
    boost::filesystem::path bpath(datasource);
    if (unlikely(!boost::filesystem::exists(bpath))) {
        std::string message = str(boost::format("Error: Mapnik datasource %1% does not exist, check the \"mapnik-datasource\" configuration in your .conf.") % bpath);
        throw std::runtime_error( message );
    }
    boost::filesystem::path canonical = boost::filesystem::canonical(bpath);
    if (unlikely(!boost::filesystem::is_directory(canonical))) {
        std::string message = str(boost::format("Error: Mapnik datasource %1% is not a directory, check the \"mapnik-datasource\" configuration in your .conf.") % bpath);
        throw std::runtime_error( message );
    }

    mapnik::datasource_cache::instance().register_datasources(datasource);
}

extern "C" void load_fonts(const char *font_dir) {
    DIR *fonts = opendir(font_dir);
    struct dirent *entry;
    char path[PATH_MAX]; // FIXME: Eats lots of stack space when recursive

    if (!fonts) {
        fprintf(stderr, "Unable to open font directory: %s\n", font_dir);
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
#if DEBUG            
            fprintf(stderr, "DEBUG: Loading font: %s\n", path);
#endif
            mapnik::freetype_engine::register_font(path);
        }
    }
    closedir(fonts);
}
