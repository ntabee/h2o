
// define before any includes
#define BOOST_SPIRIT_THREADSAFE

#include <dirent.h>
#include <syslog.h>
#include <unistd.h>
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
#include <sstream>
#include <string>
#include <iostream>
#include <tuple>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
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
boost::atomic<uint64_t> removed(0);     // # of tiles removed.
boost::atomic<uint64_t> skipped(0);     // # of tiles that are non-existent or failed to unlink(), thus skipped.
bool echo_back = false;                 // If true, print each processed line to stdout, invariant during the execution
boost::timer::cpu_timer* timer;

void worker(const boost::filesystem::path& base_path) {

    const std::string& base_as_string = base_path.string();
    size_t base_len  = base_as_string.length();

    // tile_path holds the full path base_path/nnn/.../nnn.png to remove
    char* tile_path = (char*)alloca(base_len + 28);
    strncpy(tile_path, base_as_string.c_str(), base_len);

    // tp_head points to the end of base_path in tile_path
    char* tp_head = tile_path + base_len;
    if (tile_path[base_len-1] != '/') {
        tile_path[base_len] = '/';
        ++tp_head;
    }

#define EXISTS(p) boost::filesystem::exists(p)
#define DO_REMOVE(tile_id) { \
    unpack(tile_id, z, x, y); \
    to_physical_path(tp_head, z, x, y, PNG); \
    if (!EXISTS( (boost::filesystem::path(tile_path)) )) { \
        ++skipped; \
    } else { \
        if ( likely(unlink(tile_path) == 0) ) { \
            ++removed; \
            if (echo_back) { \
                printf("%u/%u/%u\n", z, x, y); \
            } \
        } else { \
            ++skipped; \
        } \
    } \
}
    triplet tile_id;
    uint32_t z, x, y;
    while (!done) {
        while (queue.pop(tile_id)) {
            DO_REMOVE(tile_id);
        }
    }

    while (queue.pop(tile_id)) {
        DO_REMOVE(tile_id)
    }
}

void dry_worker(const boost::filesystem::path& base_path) {

    const std::string& base_as_string = base_path.string();
    size_t base_len  = base_as_string.length();

    // tile_path holds the full path base_path/nnn/.../nnn.png to remove
    char* tile_path = (char*)alloca(base_len + 28);
    strncpy(tile_path, base_as_string.c_str(), base_len);

    // tp_head points to the end of base_path in tile_path
    char* tp_head = tile_path + base_len;
    if (tile_path[base_len-1] != '/') {
        tile_path[base_len] = '/';
        ++tp_head;
    }

#define EXISTS(p) boost::filesystem::exists(p)
#define DO_DRY_RUN(tile_id) { \
    unpack(tile_id, z, x, y); \
    to_physical_path(tp_head, z, x, y, PNG); \
    if (!EXISTS( (boost::filesystem::path(tile_path)) )) { \
        ++skipped; \
    } else { \
        printf("%s will be removed\n", tile_path); \
        ++removed; \
    } \
}
    triplet tile_id;
    uint32_t z, x, y;
    while (!done) {
        while (queue.pop(tile_id)) {
            DO_DRY_RUN(tile_id);
        }
    }

    while (queue.pop(tile_id)) {
        DO_DRY_RUN(tile_id)
    }
}

/*
expire-tiles -p base-path [-f expire-list-file] [-t num-threads]
removes tiles listed in expire-list-file under base-path.
Options:
    -p,--prefix base-path: a valid directory name, into which tiles are rendered
    -f,--file expire-list-file: the list of tiles to expire: 
                                a tile list consists of lines of the form slash-delimited triple of integers
                                    z/x/y
                                each line denotes the logical path of a tile, without suffix.
                                Example:
                                    15/5252/11331
                                    15/5253/11331
                                    15/7540/11128
                                    ...
                                When this option is omitted, or '-' is given, stdin is used.
    -t,--threads n: the number of threads to render
        + defaults to boost::thread::hardware_concurrency()
    -d,--dry-run
        + only echoes the tile paths to be expired, without actual removing
    -e,--echo-back
        + prints each successfully processed line to stdout, this is useful when pipelining another process such as re-rendering
    -v,--version
    -h,--help
*/
int main(int ac, char** av) {
    try { 
        std::string base;
        std::string list_file;
        unsigned int nthreads;
        bool dry_run = false;
        /** Define and parse the program options 
        */ 
        namespace po = boost::program_options; 
        po::options_description desc(
            "expire-tiles -p base-path [-f expire-list-file] [-t num-threads]\n"
            "removes tiles listed in expire-list-file under base-path.\n\n"
            "Options"
        ); 
        desc.add_options() 
            ("help,h", "Prints help messages")
            ("version,v", "Prints version info.")
            ("prefix,p", po::value<std::string>(&base)->value_name("base-path")->required(), "Specifies the base directory into which tile are rendered") 
            ("file,f", po::value<std::string>(&list_file)->value_name("expire-list-file")->default_value("(stdin)"), "Specifies the list of tiles to expire") 
            ("echo-back,e", "prints each successfully processed line to stdout, this is useful when pipelining another process such as re-rendering")
            ("threads,t", 
                po::value<unsigned int>(&nthreads)->value_name("n")->default_value(boost::thread::hardware_concurrency()), 
                "Specifies the number of threas to render\n"
                "  + defaults to boost::thread::hardware_concurrency()") 
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

            // --dry-run
            if ( vm.count("dry-run") ) {
                dry_run = true;
            }

            // --echo-back
            if ( vm.count("echo-back") ) {
                echo_back = true;
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

        // Check if base-path is really there.
        const boost::filesystem::path base_path(base);
        if (!boost::filesystem::exists(base)) {
            std::cerr << "ERROR: The base path " << base << " does not exist." << std::endl << std::endl; 
            return -1;
        }

        // Open the list file
        std::istream* in;
        if (base == "-") {
            in = &std::cin;
        } else {
            try {
                in = new std::ifstream(list_file);
            } catch (std::runtime_error& e) {
                std::cerr << "ERROR: " << e.what() << std::endl << std::endl; 
                return -1; 
            }
        }
        std::cout 
            << "Now expiration begins:" << std::endl
            << "  Base path : " << base_path.string() << std::endl
            << "  List file : " << (base == "-" ? "(stdin)" : list_file) << std::endl
        ;
        timer = new boost::timer::cpu_timer;

        // Awake renderer threads
        boost::thread_group consumer_threads;
        for (int i = 0; i != nthreads; ++i) {
            consumer_threads.create_thread(boost::bind( dry_run ? dry_worker : worker, base_path));
        }

        // Let it run!
        try {
            std::string line;
            uint64_t lineno = 0;
            uint32_t z, x, y;
            while (std::getline(*in, line)) {
                ++lineno;
                std::vector<std::string> tokens;
                boost::split(tokens, line, boost::is_from_range('/','/'));
                if (tokens.size() != 3 ||
                    !boost::conversion::try_lexical_convert<uint32_t>(tokens[0], z) ||
                    !boost::conversion::try_lexical_convert<uint32_t>(tokens[1], x) ||
                    !boost::conversion::try_lexical_convert<uint32_t>(tokens[2], y) ) {
                    std::cerr << "WARN: skipped illfomed line: " << line << " (line at " << lineno << ")" << std::endl;
                }
                uint64_t tile_id = pack(z, x, y);
                while (!queue.push(tile_id)) {}
            }
        } catch (std::runtime_error& e) {
            std::cerr << "ERROR: " << e.what() << std::endl << std::endl; 
            return -1; 
        }
        done = true;
        consumer_threads.join_all();

        if (in != &std::cin) {
            delete in;
        }     

        std::cout 
            << timer->format() << std::endl
            << removed << " tiles removed." << std::endl
            << skipped << " tiles did not exist or could not unlink(), thus skipped." << std::endl
            << "Completed!" << std::endl;
        ;
    } catch(std::exception& e) { 
        std::cerr << "Unhandled Exception reached the top of main: " 
              << e.what() << ", application will now exit" << std::endl; 
        return -1; 

    } 
    return 0; 
}
