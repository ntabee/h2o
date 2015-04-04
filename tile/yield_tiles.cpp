
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <climits>
#include <cassert>
#include <strings.h>
#include <algorithm>
#include <exception>
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
#include "proj.hpp"
#include "path-mapper.h"

static double strtod_wrapper(char* str) {
    // cf. http://stackoverflow.com/a/5581058
    char *err;
    double d = strtod(str, &err);
    if (*err == 0) { 
        /* very probably ok */ 
        return d;
    }
    if (!isspace((unsigned char)*err)) { 
        /* error */ 
        return HUGE_VAL;
    }
    return d;
}

static long strtol_wrapper(char* str) {
    char *err;
    long v = strtol(str, &err, 10);
    if (*err == 0) { 
        /* very probably ok */ 
        return v;
    }
    if (!isspace((unsigned char)*err)) { 
        /* error */ 
        return LONG_MAX;
    }
    return v;
}

void mkdir_p(const boost::filesystem::path& base_path) {
    if (boost::filesystem::exists(base_path)) {
        boost::filesystem::path canonical = boost::filesystem::canonical(base_path);
        if (!boost::filesystem::is_directory(canonical)) {
            std::string message = str(boost::format("Error: %1% exists, but not a directory.") % base_path);
            throw std::runtime_error( message );

        }
    } else {
        boost::filesystem::create_directories(base_path);
    }
}

/*

*/
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
boost::lockfree::queue<triplet> queue(1024);
boost::atomic<uint64_t> rendered(0);
boost::atomic<bool> done (false);

void renderer(const boost::filesystem::path& xml, const boost::filesystem::path& base_path) {
    boost::filesystem::path tile_path;
    triplet tile_id;
    while (!done) {
        while (queue.pop(tile_id)) {

        }
    }

    while (queue.pop(tile_id)) {

    }
}

namespace argv {
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

            static boost::regex r("(\\d{1,2})(-|,)(\\d{1,2})");

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

            static boost::regex r("(\\d{1,2})(-|,)(\\d{1,2})");

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
        + defaults to boost::thread::hardware_concurrency() - 1
    -s,--skip-existing yes|no: 
        + if yes, existing tiles are not re-rendered; 
        + if no, every tile within the specified region (by -z and -b) is unconditionally overwritten
        + defaults to "no"


*/
int main(int ac, char** av) {
    try { 
        std::string config;
        std::string base;
//        argv::zoom zoom_levels(std::tuple<uint32_t, uint32_t>(0, 16));
        argv::zoom::pair_t zoom_levels;
        argv::bbox::box_t  bbox;
        unsigned int nthreads;
        bool skip_existing;
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
                po::value<unsigned int>(&nthreads)->value_name("n")->default_value(boost::thread::hardware_concurrency() - 1), 
                "Specifies the number of threas to render\n"
                "  + defaults to boost::thread::hardware_concurrency()-1") 
            ("skip-existing,s", 
                po::value<bool>(&skip_existing)->value_name("yes|no")->default_value(false, "no"), 
                "Specifies an overwriting policy:\n"
                "  + if yes, existing tiles are not re-rendered\n"
                "  + if no, every tile within the specified region (by -z and -b) is unconditionally overwritten\n"
                "  + defaults to 'no'") 
        ;   // add_options();
        po::variables_map vm; 

        try { 
            po::store(po::command_line_parser(ac, av).options(desc).run(), vm); // throws on error 

            /** --help option 
            */ 
            if ( vm.count("help")  ) { 
                std::cerr << desc;
                return 0; 
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

    } catch(std::exception& e) { 
        std::cerr << "Unhandled Exception reached the top of main: " 
              << e.what() << ", application will now exit" << std::endl; 
        return -1; 

    } 
    return 0; 
#if 0
#define PRINT_USAGE() fprintf(stderr, "Usage: yield-tiles render-def.xml base-path z1 z2 x1 y1 x2 y2\n")
    if (ac < 9) {
        PRINT_USAGE();
        return -1;
    }

    const boost::filesystem::path xml(av[1]);
    if (!boost::filesystem::exists(xml)) {
        fprintf(stderr, "Error: %s does not exist.", av[1]);
        return -1;
    } else {
        boost::filesystem::path canonical = boost::filesystem::canonical(xml);
        if (!boost::filesystem::is_regular_file(canonical)) {
            fprintf(stderr, "Error: %s exists, but not a regular file.", av[1]);
            return -1;
        }
    }
    // mkdir -p base-path
    const boost::filesystem::path base_path(av[2]);
    try {
        mkdir_p(base_path);
    } catch (const boost::filesystem::filesystem_error& ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
        return -1;
    } catch (const std::exception& ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
        return -1;
    }

    // Let's parse zoom levels...
#define CHECK_RANGE_L(val, v1, v2, orig_in) if (val < v1 || val > v2) { \
    PRINT_USAGE(); \
    fprintf(stderr, "Error: %s must be in the range [%ld, %ld]. (Given: %s = %s, parsed as %ld.)\n", #val, v1, v2, #val, orig_in, val); \
    return -1; \
}
#define PARSE_Z(var, str) long var = strtol_wrapper(str); CHECK_RANGE_L(var, 0L, 20L, str)

    PARSE_Z(z1, av[3])
    PARSE_Z(z2, av[4])

    if (z1 > z2) {
        std::swap(z1, z2);
    }

    // then, coordinates.
#define CHECK_RANGE_D(val, v1, v2, orig_in) if (val < v1 || val > v2) { \
    PRINT_USAGE(); \
    fprintf(stderr, "Error: %s must be in the range [%f, %f]. (Given: %s = %s, parsed as %f.)\n", #val, v1, v2, #val, orig_in, val); \
    return -1; \
}
#define PARSE_LON(var, str) double var = strtod_wrapper(str); CHECK_RANGE_D(var, -180.0, 180.0, str)

// Mercator projection limits the lat value to this, cf. http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames#X_and_Y
#define LAT_LIMIT 85.0511
#define PARSE_LAT(var, str) double var = strtod_wrapper(str); CHECK_RANGE_D(var, -LAT_LIMIT, LAT_LIMIT, str)

    PARSE_LON(x1, av[5]);
    PARSE_LAT(y1, av[6]);
    PARSE_LON(x2, av[7]);
    PARSE_LAT(y2, av[8]);

    if (x1 > x2) {
        std::swap(x1, x2);
    }

    // The y-axis of the Spherical Mercator spans north-to-south, hence smaller lat values (southern points) are mapped to larger tile IDs.
    // To assure the output to be in ascending order, we force y1 >= y2 (not y1 <= y2.)
    if (y1 < y2) {
        std::swap(y1, y2);
    }
    return 0;
    // Emit!
    char num_buf_z[18];
    char num_buf_x[18];
    char num_buf_y[18];

    uint32_t tx_left, ty_top, tx_right, ty_bottom;
    int t;
    for (uint32_t z = (uint32_t)z1; z <= (uint32_t)z2; ++z) {
        lonlat_to_tile(x1, y1, z, tx_left, ty_top);
        lonlat_to_tile(x2, y2, z, tx_right, ty_bottom);
        // assert(tx_left <= tx_right);
        // assert(ty_top <= ty_bottom);
        for (uint32_t y = ty_top; y <= ty_bottom; ++y) {
            for (uint32_t x = tx_left; x <= tx_right; ++x) {

//              A faster alternative to printf("%d %d %d\n", z, x, y);
                t = jiaendu::ufast_utoa10(z, num_buf_z);
                num_buf_z[t] = ' ';
                num_buf_z[t+1] = '\0';
                fputs(num_buf_z, stdout);

                t = jiaendu::ufast_utoa10(x, num_buf_x);
                num_buf_x[t] = ' ';
                num_buf_x[t+1] = '\0';
                fputs(num_buf_x, stdout);

                t = jiaendu::ufast_utoa10(y, num_buf_y);
                num_buf_y[t] = '\n';
                num_buf_y[t+1] = '\0';
                fputs(num_buf_y, stdout);
            }
        }
    }

    return 0;
#endif
}