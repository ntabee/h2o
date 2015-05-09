
// define before any includes
#define BOOST_SPIRIT_THREADSAFE

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
#include <boost/program_options.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include "proj.hpp"
#include "path-mapper.h"

#include "git-revision.h"
#define VERSION "0.0.0"

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


/*
yield-tile-urls -p base-url [-z z1-z2] [-b x1,y1,x2,y2]
generates URLs for tiles bounded by the box (x1, y1)-(x2, y2) for zoom levels in [z1, z2], prefixed by base-url.
Options:
    -p,--prefix base-url: a URL (or, can be a local path), to which paths for tiles are appended
    -z,--zoom z1-z2: zoom levels in [0, ..., 20] to generate URLs
        + values less/greater than 0/20 are fit to 0/20 resp. 
        + defaults to 0-16
    -b,--bbox x1,y1,x2,y2: (x1, y1) and (x2, y2) are two pairs of lon/lat
        + the pairs define the bounding box of URLs
        + defaults to (-180, 90)-(180, -90), i.e. the entire planet
    -s,--suffix (png|jpg|...)
        + suffix of the URLs
    -v,--version
    -h,--help
*/
int main(int ac, char** av) {
    try { 
        std::string config;
        std::string base;
        std::string suffix;
        argv::zoom::pair_t zoom_levels;
        argv::bbox::box_t  bbox;
        bool emit_physical = false; // If true, print in the physical form: /base/z/nnn/nnn/nnn/nnn/nnn.png

        /** Define and parse the program options 
        */ 
        namespace po = boost::program_options; 
        po::options_description desc(
            "yield-tile-urls -p base-url [-z z1-z2] [-b x1,y1,x2,y2]\n"
            "generates URLs for tiles bounded by the box (x1, y1)-(x2, y2) for zoom levels in [z1, z2], prefixed by base-url.\n\n"
            "Options"
        ); 
        desc.add_options() 
            ("help,h", "Prints help messages")
            ("version,v", "Prints version info.")
            ("prefix,p", po::value<std::string>(&base)->value_name("base-url")->required(), "Specifies the URL (or, can be a local path) to which tile-paths are appended") 
            ("physical,P", "Emits tile-paths in the physical form: base/z/nnn/nnn/nnn/nnn/nnn.png") 
            ("suffix,s", po::value<std::string>(&suffix)->value_name("suffix")->default_value("png"), "Specifies the suffix of the URLs") 
            ("zoom,z", 
                po::value<argv::zoom::pair_t>(&zoom_levels)->value_name("z1-z2")->default_value(argv::zoom::pair_t{0, 16}, "0-16"), 
                "Specifies zoom levels to generate URLs, between 0-20\n"
                "  + values less/greater than 0/20 are fit to 0/20 resp.\n"
                "  + defaults to 0-16") 
            ("bbox,b", 
                po::value<argv::bbox::box_t>(&bbox)->value_name("x1,y1,x2,y2")->default_value(argv::bbox::box_t{-180, 90, 180, -90}, "-180,90,180,-90"), 
                "Specifies the bounding box of URLs in lon/lat\n"
                "  + defaults to (-180,90)-(180,-90), i.e. the entire planet.") 
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

            if ( vm.count("physical") ) {
                emit_physical = true;
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
        // Tweak lat/lon values so that they fit in their "actual" range.
        // To be strict, the range of lon. values is [-180, 180), so exact 180.0 should be "shifted just a little"
        x1 = std::min(x1, 180.0 - 0.0000001);
        x2 = std::min(x2, 180.0 - 0.0000001);
        // and lat. values are limited to +-85.0511 rather than 90.0, as explained.
        y1 = std::max(-LAT_LIMIT, std::min(y1, LAT_LIMIT));
        y2 = std::max(-LAT_LIMIT, std::min(y2, LAT_LIMIT));

        // Trim the trailing '/' if any.
        
        char* prefix = const_cast<char*>(base.c_str());
        if (prefix[base.length()-1] == '/') {
            prefix[base.length()-1] = '\0';
        }
        // Emit!
        uint32_t tx_left, ty_top, tx_right, ty_bottom;
        if (emit_physical) {
            char* tile_path = (char*)alloca(base.length() + 28);
            strcpy(tile_path, prefix);
            char* tp_head = tile_path + strlen(prefix);
            *tp_head = '/'; 
            tp_head++;
            boost::algorithm::to_lower(suffix);
            TILE_SUFFIX ts = (suffix == "png") ? PNG : JPG;

            for (uint32_t z = (uint32_t)z1; z <= (uint32_t)z2; ++z) {
                lonlat_to_tile(x1, y1, z, tx_left, ty_top);
                lonlat_to_tile(x2, y2, z, tx_right, ty_bottom);
                for (uint32_t y = ty_top; y <= ty_bottom; ++y) {
                    for (uint32_t x = tx_left; x <= tx_right; ++x) {
                        to_physical_path(tp_head, z, x, y, ts);
                        puts(tile_path);
                    }
                }
            }
        } else {
            for (uint32_t z = (uint32_t)z1; z <= (uint32_t)z2; ++z) {
                lonlat_to_tile(x1, y1, z, tx_left, ty_top);
                lonlat_to_tile(x2, y2, z, tx_right, ty_bottom);
                for (uint32_t y = ty_top; y <= ty_bottom; ++y) {
                    for (uint32_t x = tx_left; x <= tx_right; ++x) {
                        printf("%s/%d/%d/%d.%s\n", prefix, z, x, y, suffix.c_str());
                    }
                }
            }
        }
    } catch(std::exception& e) { 
        std::cerr << "Unhandled Exception reached the top of main: " 
              << e.what() << ", application will now exit" << std::endl; 
        return -1; 

    } 
    return 0; 
}
