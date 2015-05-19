#include <mapnik/version.hpp>
 
#include <mapnik/map.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/font_engine_freetype.hpp>
#include <mapnik/agg_renderer.hpp>
#include <mapnik/expression.hpp>
#include <mapnik/color_factory.hpp>
#if MAPNIK_MAJOR_VERSION >= 3
 #include <mapnik/image.hpp>
 #include <mapnik/image_view_any.hpp>
 #define image_32 image_rgba8
#else
 #include <mapnik/image_data.hpp>
 #include <mapnik/graphics.hpp>
#endif
#include <mapnik/image_util.hpp>
#include <mapnik/image_view.hpp>
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
#include "tile/mapnik-bridge.h"
#include "tile/mkdir-p.h"

extern "C" {

void save_tile(h2o_req_t* req, const char* tile_path, const char* data, size_t len, const char* mime_type, size_t mime_type_len, int flags, tile_rendered_callback callback) {
    /* write to the filesystem */
    /* 
    As the rendered image was already h2o_send_inline'ed in the callback, 
    any errors in saving it to a file will be just error-logged: 
    response to the client is NOT affected: no such thing as "500 Internal Server Error."
    */
    callback(req, data, len, tile_path, mime_type, mime_type_len, flags);

    size_t tmp_path_len = strlen(tile_path)+18;
    char *tmp_tile_path = static_cast<char*>(alloca(tmp_path_len));
    snprintf(tmp_tile_path, tmp_path_len, "%s.%x", tile_path, pthread_self());
    mkdir_p_parent(tmp_tile_path);
    int fd = open(tmp_tile_path, O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (fd < 0) {
        h2o_req_log_error(req, "lib/handler/mapnik-bridge.cpp", "Could not open file %s: %s\n", tmp_tile_path, strerror(errno));
    }
    int v = write(fd, data, len);
    if (v != len) {
        h2o_req_log_error(req, "lib/handler/mapnik-bridge.cpp", "Failed to write to file %s: %s\n", tmp_tile_path, strerror(errno));
        close(fd);
        unlink(tmp_tile_path);
    }
    close(fd);
    rename(tmp_tile_path, tile_path);
}

void render_tile(h2o_req_t* req, void* map_ptr, const char* tile_path, uint32_t zoom, uint32_t x, uint32_t y, const char* mime_type, size_t mime_type_len, int flags, tile_rendered_callback callback) {

    try {
        using namespace mapnik;
//        boost::filesystem::path path(tile_path);

        const Map _map = *(Map*)map_ptr;
        Map m(_map);    // clone

        /* (left, top)-(right, bottom) in Mercator projection. */ 
        double l, t, r, b; 
        image_32 image(m.width(),m.height()); 
#if MAPNIK_MAJOR_VERSION >= 3
        image_view_rgba8 vw(TILE_SIZE/2, TILE_SIZE/2, TILE_SIZE, TILE_SIZE, image); 
#else
        image_view<mapnik::image_data_32> vw(TILE_SIZE/2, TILE_SIZE/2, TILE_SIZE, TILE_SIZE, image.data()); 
#endif
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
#if MAPNIK_MAJOR_VERSION >= 3
        std::string buf = save_to_string(image_view_any(vw), "png256:e=miniz");
#else
        std::string buf = save_to_string(vw, "png256");
#endif
        save_tile(req, tile_path, buf.c_str(), buf.length(), H2O_STRLIT("image/png"), 0, callback);
    } catch (std::exception& e) {
        h2o_req_log_error(req, "lib/handler/mapnik-bridge.cpp", "%s", e.what());
        req->res.status = 500;
        req->res.reason = "internal server error";
        h2o_send_inline(req, NULL, 0);
    }

}

void* alloc_mapnik(const char* style_path) {
    // To avoid label scattering, we render a 2x2 larger area and then clip the center.
    mapnik::Map* m = new mapnik::Map(TILE_SIZE*2, TILE_SIZE*2);    
    // Any failure in parsing the style file will immediately cause abortion by an unhandled exception, this is intended.
    mapnik::load_map(*m, style_path);
    return m;
}

void dispose_mapnik(void* m) {
    mapnik::Map* map = (mapnik::Map*)m;
    delete map;
}

void init_mapnik_datasource(const char* datasource) {
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

void load_fonts(const char *font_dir) {
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

}
