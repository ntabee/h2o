
// define before any includes
#define BOOST_SPIRIT_THREADSAFE

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
#include <dirent.h>
#include <syslog.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <climits>
#include <cassert>
#include <strings.h>
#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <tuple>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/thread/thread.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/atomic.hpp>
#include <boost/timer/timer.hpp>
#include "proj.hpp"
#include "path-mapper.h"

#include "git-revision.h"
#define VERSION "0.0.0"

#if __GNUC__ >= 3
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

#define CANVAS_SCALE 8
#define RENDER_SIZE (TILE_SIZE*(CANVAS_SCALE+1))

void mkdir_p(const boost::filesystem::path& base_path) {
    if (likely(boost::filesystem::exists(base_path))) {
        boost::filesystem::path canonical = boost::filesystem::canonical(base_path);
        if (unlikely(!boost::filesystem::is_directory(canonical))) {
            std::string message = str(boost::format("Error: %1% exists, but not a directory.") % base_path);
            throw std::runtime_error( message );

        }
    } else {
        boost::filesystem::create_directories(base_path);
    }
}

// typedef std::tuple<uint32_t, uint32_t, uint32_t> triplet;
// Since boost::lockfree::queue supports only a type that "has_trivial_assign", 
// we encode a triple (zoom, x, y) into a single 64bit value divided as (16, 24, 24)-bits.
typedef uint64_t triplet;   
static inline triplet pack(uint32_t z, uint32_t x, uint32_t y) {
    z &= 0xFFFF;
    x &= 0xFFFFFF;
    y &= 0xFFFFFF;

    triplet val = z;
    val <<= 24;
    val |= x;
    val <<= 24;
    val |= y;
    return val;
}
static inline void unpack(triplet val, uint32_t& z, uint32_t& x, uint32_t& y) {
    y = (uint32_t)(val & 0xFFFFFF);
    val >>= 24;
    x = (uint32_t)(val & 0xFFFFFF);
    val >>= 24;
    z = (uint32_t)(val & 0xFFFF);
}
boost::lockfree::queue<triplet, boost::lockfree::capacity<1024>> queue;
boost::atomic<bool> done (false);
boost::atomic<uint64_t> rendered(0);    // # of tiles already done (including those skipped).
boost::atomic<uint64_t> skipped(0);     // # of tiles skipped.
uint64_t total_tiles;   // # of tiles to render, set in main() and invariant during the execution
bool skip_existing;     // If true, avoid overwriting existing tiles; set in main() and invariant.
boost::timer::cpu_timer* timer;

void renderer(const boost::filesystem::path& xml, const boost::filesystem::path& base_path) {
    using namespace mapnik;

    Map m(CANVAS_SCALE*TILE_SIZE, CANVAS_SCALE*TILE_SIZE);
    mapnik::load_map(m, xml.string());

    const std::string& base_as_string = base_path.string();
    size_t base_len  = base_as_string.length();

    // tile_path holds the full path base_path/nnn/.../nnn.png to render
    char* tile_path = (char*)alloca(base_len + 28);
    strncpy(tile_path, base_as_string.c_str(), base_len);

    // tp_head points to the end of base_path in tile_path
    char* tp_head = tile_path + base_len;
    if (tile_path[base_len-1] != '/') {
        tile_path[base_len] = '/';
        ++tp_head;
    }


    /* (left, top)-(right, bottom) in Mercator projection. */ 
    double l, t, r, b; 

#define INC_COUNT() { \
    uint64_t v = ++rendered; \
    if (unlikely(v % 100 == 0)) { \
        uint64_t s = skipped; \
        /* cout << XX << ... is not atomic */ \
        double sec = (double)timer->elapsed().wall/(1000UL*1000UL*1000UL); \
        printf("%ld/%ld (%.3lf%) done. (%ld skipped) %s %.3lf tiles/s.\n", v, total_tiles, (100.0*(double)v / total_tiles), s, timer->format(6, "%ws").c_str(), (double)v/sec); \
    } \
}

#if MAPNIK_MAJOR_VERSION >= 3
// #define VIEW(vw, image) image_view_rgba8 vw(TILE_SIZE/2, TILE_SIZE/2, TILE_SIZE, TILE_SIZE, image); 
 #define VIEW(vw, image) image_view_rgba8 vw(x1*TILE_SIZE, y1*TILE_SIZE, TILE_SIZE, TILE_SIZE, image); 
 #define SAVE(vw, to) { save_to_file(image_view_any(vw), to, "png256:e=miniz"); }
#else
// #define VIEW(vw, image) image_view<mapnik::image_data_32> vw(TILE_SIZE/2, TILE_SIZE/2, TILE_SIZE, TILE_SIZE, image.data()); 
 #define VIEW(vw, image) image_view<mapnik::image_data_32> vw(x1*TILE_SIZE, y1*TILE_SIZE, TILE_SIZE, TILE_SIZE, image.data()); 
 #define SAVE(vw, to) { save_to_file(vw, to, "png256"); }
#endif

#define EXISTS(p) boost::filesystem::exists(p)
#define DO_RENDER(tile_id) { \
    unpack(tile_id, z, x, y); \
    uint32_t bound = 1 << z; \
    if (bound - x < CANVAS_SCALE) { x = 0; } \
    if (bound - y < CANVAS_SCALE) { y = 0; } \
    uint32_t render_size_tx = std::min(CANVAS_SCALE, (1 << z)); \
    uint32_t render_size_ty = std::min(CANVAS_SCALE, (1 << z)); \
    uint32_t x2, y2; \
    x2 = std::min(render_size_tx, bound); \
    y2 = std::min(render_size_ty, bound); \
    m.resize(render_size_tx*TILE_SIZE, render_size_ty*TILE_SIZE); \
    image_32 image(render_size_tx*TILE_SIZE, render_size_ty*TILE_SIZE); \
    tile_to_merc(z, x, y, l, t); \
    tile_to_merc(z, x+render_size_tx, y+render_size_ty, r, b); \
    box2d<double> bbox(l, t, r, b); \
    m.zoom_to_box(bbox); \
    agg_renderer<image_32> ren(m,image); \
    ren.apply(); \
    for (int x1=0; x1<x2; ++x1) { \
        for (int y1=0; y1<y2; ++y1) { \
            to_physical_path(tp_head, z, x1, y1, PNG); \
            boost::filesystem::path boost_tile_path = boost::filesystem::path(tile_path); \
            if ( likely(!(skip_existing && EXISTS(boost_tile_path))) )  { \
                mkdir_p(boost_tile_path.parent_path()); \
                                                        \
                /* Clip the center 256x256 into vw */ \
                VIEW(vw, image) \
                SAVE(vw, tile_path); \
            } else { \
                ++skipped; \
            } \
            INC_COUNT(); \
        } \
    } \
} // DO_RENDER()

    triplet tile_id;
    uint32_t z, x, y;
    while (!done) {
        while (queue.pop(tile_id)) {
            DO_RENDER(tile_id);
        }
    }

    while (queue.pop(tile_id)) {
        DO_RENDER(tile_id)
    }
}

namespace argv {
    // argv parser for --zoom=z1-z2
    namespace zoom {
        struct pair_t {
        public:
            uint32_t z1, z2;
        };

        void validate(boost::any& v,
                      const std::vector<std::string>& values,
                      pair_t*, int)
        {
            namespace po = boost::program_options;

            static boost::regex r("(\\d+)(-|,)(\\d+)");

            // Make sure no previous assignment to 'v' was made.
            po::validators::check_first_occurrence(v);
            // Extract the first string from 'values'. If there is more than
            // one string, it's an error, and exception will be thrown.
            const std::string& s = po::validators::get_single_string(values);

            // Do regex match and convert the interesting part to 
            // int.
            boost::smatch match;
            if (regex_match(s, match, r)) {
                v = boost::any(pair_t{ boost::lexical_cast<uint32_t>(match[1]), boost::lexical_cast<uint32_t>(match[3]) });
            } else {
                throw po::validation_error(po::validation_error::invalid_option_value);
            }       
        }
    }

    // ... and for --bbox=x1,y1,x2,y2
    namespace bbox {
        struct box_t {
        public:
            double x1, y1, x2, y2;
        };

        void validate(boost::any& v,
                      const std::vector<std::string>& values,
                      box_t*, int)
        {
            namespace po = boost::program_options;

            // Make sure no previous assignment to 'v' was made.
            po::validators::check_first_occurrence(v);
            // Extract the first string from 'values'. If there is more than
            // one string, it's an error, and exception will be thrown.
            const std::string& s = po::validators::get_single_string(values);

            try {
                std::vector<std::string> tokens;
                boost::split(tokens, s, boost::is_any_of(","));
                // boost::char_separator<char> sep(",");
                // boost::tokenizer< boost::char_separator<char> > tok(s, sep);
                // auto it = tok.begin();
                double x1 = boost::lexical_cast<double>(tokens[0]);
                double y1 = boost::lexical_cast<double>(tokens[1]);
                double x2 = boost::lexical_cast<double>(tokens[2]);
                double y2 = boost::lexical_cast<double>(tokens[3]);
                v = boost::any(box_t { x1, y1, x2, y2 });
            } catch (...) {
                throw po::validation_error(po::validation_error::invalid_option_value);
            }
        }
    }
}

static void load_fonts(const char *font_dir) {
    DIR *fonts = opendir(font_dir);
    struct dirent *entry;
    char path[PATH_MAX]; // FIXME: Eats lots of stack space when recursive

    if (!fonts) {
        syslog(LOG_CRIT, "Unable to open font directory: %s", font_dir);
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
            syslog(LOG_DEBUG, "DEBUG: Loading font: %s", path);
            mapnik::freetype_engine::register_font(path);
        }
    }
    closedir(fonts);
}


/*
yield-tiles -c render-def.xml -p base-path [-z z1-z2] [-b x1,y1,x2,y2] [-t num-threads] [-s]
generates tiles bounded by the box (x1, y1)-(x2, y2) for zoom levels in [z1, z2] into base-path.
Options:
    -c,--config render-def.xml: a mapnik conf. file, typically of openstreetmap-carto or alike. REQUIRED
    -p,--prefix base-path: a valid directory name, into which tiles are rendered
    -z,--zoom z1-z2: zoom levels in [0, ..., 20] to render
        + values less/greater than 0/20 are fit to 0/20 resp. 
        + defaults to 0-16
    -b,--bbox x1,y1,x2,y2: (x1, y1) and (x2, y2) are two pairs of lon/lat
        + the pairs define the bounding box of rendering
        + defaults to (-180, 90)-(180, -90), i.e. the entire planet
    -t,--threads n: the number of threads to render
        + defaults to boost::thread::hardware_concurrency()
    -s,--skip-existing yes|no: 
        + if yes, existing tiles are not re-rendered; 
        + if no, every tile within the specified region (by -z and -b) is unconditionally overwritten
        + defaults to "no"
    -d,--dry-run
        + only estimates the number of tiles, avoid actual rendering
    -v,--version
    -h,--help
*/
int main(int ac, char** av) {
    try { 
        std::string config;
        std::string base;
        argv::zoom::pair_t zoom_levels;
        argv::bbox::box_t  bbox;
        unsigned int nthreads;
        bool dry_run = false;
        /** Define and parse the program options 
        */ 
        namespace po = boost::program_options; 
        po::options_description desc(
            "yield-tiles -c render-def.xml -p base-path [-z z1-z2] [-b x1,y1,x2,y2] [-t num-threads] [-s]\n"
            "generates tiles bounded by the box (x1, y1)-(x2, y2) for zoom levels in [z1, z2] into base-path.\n\n"
            "Options"
        ); 
        desc.add_options() 
            ("help,h", "Prints help messages")
            ("version,v", "Prints version info.")
            ("config,c", po::value<std::string>(&config)->value_name("render-def.xml")->required(), "Specifies a mapnik config file (such as of openstreetmap-carto)") 
            ("prefix,p", po::value<std::string>(&base)->value_name("base-path")->required(), "Specifies the base directory into which tile are rendered") 
            ("zoom,z", 
                po::value<argv::zoom::pair_t>(&zoom_levels)->value_name("z1-z2")->default_value(argv::zoom::pair_t{0, 16}, "0-16"), 
                "Specifies zoom levels to render, between 0-20\n"
                "  + values less/greater than 0/20 are fit to 0/20 resp.\n"
                "  + defaults to 0-16") 
            ("bbox,b", 
                po::value<argv::bbox::box_t>(&bbox)->value_name("x1,y1,x2,y2")->default_value(argv::bbox::box_t{-180, 90, 180, -90}, "-180,90,180,-90"), 
                "Specifies the bounding box of rendering in lon/lat\n"
                "  + defaults to (-180,90)-(180,-90), i.e. the entire planet.") 
            ("threads,t", 
                po::value<unsigned int>(&nthreads)->value_name("n")->default_value(boost::thread::hardware_concurrency()), 
                "Specifies the number of threas to render\n"
                "  + defaults to boost::thread::hardware_concurrency()") 
            ("skip-existing,s", 
                "Avoids re-rendering existing tiles") 
            ("dry-run,d", 
                "Only estimates the number of tiles, does not actually render\n") 
        ;   // add_options();
        if (ac <= 1) {
            // No options are given.
            // Just print the usage w.o. error messages.
            std::cerr << desc;
            return 0;
        }
        po::variables_map vm; 

        try { 
            po::store(po::command_line_parser(ac, av).options(desc).run(), vm); // throws on error 

            // --help
            if ( vm.count("help")  ) { 
                std::cerr << desc;
                return 0; 
            } 

            if ( vm.count("version") ) {
                std::cout << (
                    VERSION " (commit: " GIT_REVISION_SHORT ")" 
                ) << std::endl;
                return 0;
            }

            // --skip-existing
            if ( vm.count("skip-existing") ) {
                skip_existing = true;
            }
            // --dry-run
            if ( vm.count("dry-run") ) {
                dry_run = true;
            }
            po::notify(vm); // throws on error, so do after help in case 
                            // there are any problems 
        } catch(po::required_option& e) { 
            std::cerr << desc << std::endl;
            std::cerr << "ERROR: " << e.what() << std::endl << std::endl; 
            return -1; 
        } catch(po::error& e) { 
            std::cerr << desc << std::endl;
            std::cerr << "ERROR: " << e.what() << std::endl << std::endl; 
            return -1; 
        }

        // Ensure -c XX.xml is a regular file
        const boost::filesystem::path xml(config);
        if (!boost::filesystem::exists(xml)) {
            std::cerr << "Error: " << config << " does not exist." << std::endl;
            return -1;
        } else {
            boost::filesystem::path canonical = boost::filesystem::canonical(xml);
            if (!boost::filesystem::is_regular_file(canonical)) {
                std::cerr << "Error: " << config << " exists, but is not a regular file." << std::endl;
                return -1;
            }
        }
        // mkdir -p base-path
        const boost::filesystem::path base_path(base);
        try {
            mkdir_p(base_path);
        } catch (const boost::filesystem::filesystem_error& e) {
            std::cerr << "ERROR: " << e.what() << std::endl << std::endl; 
            return -1;
        }

#define CHECK_RANGE(val, v1, v2) if (val < v1 || val > v2) { \
    std::cerr << "Error: " << (#val) << " = " << (val) << " is out of the range, must be in [" << (v1) << ", " << (v2) << "]" << std::endl; \
    return -1; \
}

        // Ensure zoom levels are in the valid range
        uint32_t z1 = zoom_levels.z1, z2 = zoom_levels.z2;
        CHECK_RANGE(z1, 0, 20)
        CHECK_RANGE(z2, 0, 20)

        if (z1 > z2) {
            std::swap(z1, z2);
        }

        // and so are bbox coordinates...

        // Mercator projection limits the lat value to this, cf. http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames#X_and_Y
        constexpr double LAT_LIMIT = 85.0511;
        double x1 = bbox.x1,
               y1 = bbox.y1,
               x2 = bbox.x2,
               y2 = bbox.y2;
        CHECK_RANGE(x1, -180.0, 180.0)        
        CHECK_RANGE(x2, -180.0, 180.0)        
        // The magic number 85.0511 is quite counter-intuitive, let us accept any deg. as inputs and then fit them below
        CHECK_RANGE(y1, -90.0, 90.0)        
        CHECK_RANGE(y2, -90.0, 90.0)

        // Ensure values are in ascening order.
        if (x1 > x2) {
            std::swap(x1, x2);
        }
        // The y-axis of the Spherical Mercator spans north-to-south, hence smaller lat values (southern points) are mapped to larger tile IDs.
        // To assure the output to be in ascending order, we force y1 >= y2 (not y1 <= y2.)
        if (y1 < y2) {
            std::swap(y1, y2);
        }
        std::cout 
            << "Now rendering begins:" << std::endl
            << "  Base path   : " << base_path.string() << std::endl
            << "  Mapnik conf.: " << xml.string() << std::endl
            << "  Zoom levels : " << (boost::format("%1% - %2%") % z1 % z2) << std::endl
            << "  Bounding box: " << (boost::format("(%1%,%2%)-(%3%,%4%)") % x1 % y1 % x2 % y2) << std::endl
        ;
        // Tweak lat/lon values so that they fit in their "actual" range.
        // To be strict, the range of lon. values is [-180, 180), so exact 180.0 should be "shifted just a little"
        x1 = std::min(x1, 180.0 - 0.0000001);
        x2 = std::min(x2, 180.0 - 0.0000001);
        // and lat. values are limited to +-85.0511 rather than 90.0, as explained.
        y1 = std::max(-LAT_LIMIT, std::min(y1, LAT_LIMIT));
        y2 = std::max(-LAT_LIMIT, std::min(y2, LAT_LIMIT));

        timer = new boost::timer::cpu_timer;
        // Count # of tiles to render
        std::cout 
            << "  # of tiles  : " << std::endl
        ;

        for (uint32_t z = (uint32_t)z1; z <= (uint32_t)z2; ++z) {
            uint32_t tx_left, ty_top, tx_right, ty_bottom;
            lonlat_to_tile(x1, y1, z, tx_left, ty_top);
            lonlat_to_tile(x2, y2, z, tx_right, ty_bottom);
            uint64_t tiles = 0;
            for (uint32_t y = ty_top; y <= ty_bottom; y+=CANVAS_SCALE) {
                for (uint32_t x = tx_left; x <= tx_right; x+=CANVAS_SCALE) {
                    uint32_t render_size_tx = std::min(CANVAS_SCALE, (1 << z)); \
                    uint32_t render_size_ty = std::min(CANVAS_SCALE, (1 << z)); \
                    tiles += render_size_tx*render_size_ty;
                }
            }
            std::cout 
                << "    Zoom " << (boost::format("%|2|") % z) << ": " << tiles << std::endl;

            total_tiles+=tiles;
        }
        std::cout 
            << "    Total  : " << total_tiles << std::endl
        ;
        // for (uint32_t z = (uint32_t)z1; z <= (uint32_t)z2; ++z) {
        //     uint32_t tx_left, ty_top, tx_right, ty_bottom;
        //     lonlat_to_tile(x1, y1, z, tx_left, ty_top);
        //     lonlat_to_tile(x2, y2, z, tx_right, ty_bottom);

        //     uint64_t rows = (uint64_t)(ty_bottom - ty_top + 1);
        //     uint64_t cols = (uint64_t)(tx_right - tx_left + 1);
        //     uint64_t tiles = rows * cols;
        //     total_tiles += tiles;
        //     std::cout 
        //         << "    Zoom " << (boost::format("%|2|") % z) << ": " << tiles << std::endl;
        // }

        if (dry_run) {
            return 0;
        }

        // FIXME: Paths should be coufigurable.
        using namespace mapnik;
        datasource_cache::instance().register_datasources("/usr/local/lib/mapnik/input");
        load_fonts("/usr/local/lib/mapnik/fonts");
        load_fonts("/usr/share/fonts");

        // Awake renderer threads
        boost::thread_group consumer_threads;
        for (int i = 0; i != nthreads; ++i)
            consumer_threads.create_thread(boost::bind(renderer, xml, base_path));

        // Emit!
        for (uint32_t z = (uint32_t)z1; z <= (uint32_t)z2; ++z) {
            uint32_t tx_left, ty_top, tx_right, ty_bottom;
            lonlat_to_tile(x1, y1, z, tx_left, ty_top);
            lonlat_to_tile(x2, y2, z, tx_right, ty_bottom);
            for (uint32_t y = ty_top; y <= ty_bottom; y+=CANVAS_SCALE) {
                for (uint32_t x = tx_left; x <= tx_right; x+=CANVAS_SCALE) {
                    uint64_t tile_id = pack(z, x, y);
                    while (!queue.push(tile_id)) {}
                }
            }
        }
        done = true;
        consumer_threads.join_all();        

        std::cout 
            << timer->format()
            << "Completed!" << std::endl
        ;
    } catch(std::exception& e) { 
        std::cerr << "Unhandled Exception reached the top of main: " 
              << e.what() << ", application will now exit" << std::endl; 
        return -1; 

    } 
    return 0; 
}
