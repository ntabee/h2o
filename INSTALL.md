This instruction is of **h2o-tile**, a special-purpose fork of [h2o](https://github.com/h2o/h2o).
While original h2o is a general-purpose HTTPD, this h2o-tile puts particular forcus on serving <a href="http://openstreetmap.org">OpenStreetMap</a>-based maps. 

For general introduction, installation and configuration of the "plain-vanilla" h2o, see the [documentation in the original repo.](https://h2o.github.io/);  h2o-tile does *NOT* (should not have) tweak(-ed) its documented behavior.

#Installation

## Dependency

+ Boost (>= 1.52.0): `sudo apt-get install libboost-all-dev`
+ Mapnik (>= 2.2.0): `sudo apt-get install libmapnik-dev`

## A. `$ make` it easy

```bash
$ git clone https://github.com/ntabee/h2o-tile
$ cd h2o-tile
$ git checkout tile
$ git submodule update --init --recursive
$ cmake .
$ make
$ sudo make install
```

## B. Speed-mania build

```bash
# Install qrintf, a printf-optimizing gcc wrapper
$ git clone https://github.com/h2o/qrintf.git
$ cd qrintf
# Current HEAD (as of 10, May, 2015) doesn't work well with gcc
$ git checkout v0.9.1 
$ git submodule update --init
$ make test
$ sudo make install
...
$ cd /path/to/h2o-tile
$ git checkout tile
$ cmake . -DCMAKE_C_COMPILER=/usr/local/bin/qrintf-gcc -DCMAKE_C_FLAGS='-mtune=native -march=native -O3' -DCMAKE_CXX_FLAGS='-mtune=native -march=native -O3'
...
```

## Installed binaries

`make install` will place *three distinct* binaries named `h2o*` in `/usr/local/bin`, confused? Don't worry, let me explain one by one:

- `/usr/local/bin/h2o` is the "**plain-vanilla**" version: no "map-relevant" extensions, functionally equivalent to the original
- `/usr/local/bin/h2o-tile` is the "**renderer-bundled**" version: each request to a tile potentially invokes the Mapnik rendering engine to draw a non-existent map fragment.  You need to correctly setup a PostGIS database and Mapnik's style file.
- `/usr/local/bin/h2o-tile-proxy` is the "**caching proxy**" version: tiles are fetched from another server, typically OSM's official ones and cached locally; you don't need to struggle with your own PostGIS dbs.  However, note that excessive access to upstream servers **can be regarded abuse** and can be access-blocked, see the [OSM's tile usage policy](http://wiki.openstreetmap.org/wiki/Tile_usage_policy).

# Preparing storages

The nature of web-based map systems poses some consideration on your local storage setup.

A "map" is a collection of raster images called *tiles*, stored in your local storage.
Each tile is placed under a directory of your choice, with the following hierarchy/naming convention: `${tile.dir}/z/nnn/nnn/nnn/nnn/nnn.png`, where:

- "z" is a number between 0-20 and represents the zoom level of the tile, and
- each "nnn" is a 3-digit number between 0-255 representing a 8-bit value; the 5-tuple of such 8-bit values identifies a specific region on the earth at the given zoom level. See:
    + [OSM wiki](http://wiki.openstreetmap.org/wiki/Slippy_map_tilenames) for the basics of tile-naming conventions, and
    + the following [fragment of Apache mod_tile](https://github.com/openstreetmap/mod_tile/blob/master/src/store_file_utils.c#L86-L104) for the concrete directory hierarchy implementation, to which our h2o-tile follows.

A problem arises here is, the "collection" of tiles can be enormously large:

- **one million** of images under the `${tile.dir}` is **indeed ordinary**,
- **one billion** of tiles under the directory is **quite realistic**,
- **one trillion** (1,000 billion) is **theoretically possible**.

Not many filesystems, like our good friend `ext4` and `xfs`, can handle such a huge directory tree effectively.
You must choose some alternatives and mount your `${tile.dir}` as a separate partition.

My particular recommendation is [Btrfs](https://btrfs.wiki.kernel.org/index.php/Main_Page), which

- does not limit the number of inodes a priori, and
- sotres small files (so are many tiles) into the metadata space.

Mounting a btrfs partition (to `/opt`), in a simplest form, is:

```bash
$ sudo apt-get install btrfs-tools
$ sudo mkfs.btrfs -O^extref -m single /dev/your/partition/for/tiles
$ sudo vi /etc/fstab # and add the following line
/dev/part/for/tiles  /opt       btrfs   auto,noexec,noatime,autodefrag,space_cache      0       2
```

# Configuration

h2o-tile adds several directives to be written in `*.conf` files.

## 1. Directive(s) common to `h2o-tile` and `h2o-tile-proxy`

- `tile.dir`: specifies the base directory of a tile storage.
    + Level: path. A path-level configuration **may** have **at most one** `tile.dir` directive.
    A path with this directive is processed by the [tile handler](https://github.com/ntabee/h2o-tile/blob/tile/lib/handler/tile.c), instead of the normal file handler.

Example:

```
    paths:
      /tiles:
        tile.dir: /opt/osm/tiles
```

## 2. Directives for `h2o-tile`

- `mapnik-datasource`: specifies the directory where Mapnik's datasource plugins are located.
    + Level: global. There **must** be **exaxtly one** `mapnik-datasource` at the global level.
    The value is typically `/usr/lib/mapnik/input` or `/usr/local/lib/mapnik/input`; a valid datasource directory
    is typically named `input` and contains several shared libraries suffixed as `.input`:

Example:

```
mapnik-datasource: /usr/local/lib/mapnik/input
```

Example of a "datasource" directory:

 
```bash
me@debian:~ $ ls /usr/local/lib/mapnik/input/
csv.input   geojson.input  pgraster.input  raster.input  sqlite.input
gdal.input  ogr.input      postgis.input   shape.input   topojson.input
me@debian:~ $ file /usr/local/lib/mapnik/input/*
/usr/local/lib/mapnik/input/csv.input:      ELF 64-bit LSB shared object, x86-64, version 1 (GNU/Linux), dynamically linked, BuildID[sha1]=e87af8b883b701bec11292578ec7c4a887bab816, not stripped
/usr/local/lib/mapnik/input/gdal.input:     ELF 64-bit LSB shared object, x86-64, version 1 (SYSV), dynamically linked, BuildID[sha1]=c4d222fc0c20b7a9bc183107a7bbd1a44e10120f, not stripped
```

- `mapnik-fonts`: specifies directories to search fonts.
    + Level: global. There **may** be **one ore more** `mapnik-fonts` at the global level.
    A common combination is `/usr/share/fonts` + `/usr/lib/mapnik/fonts` (or `/usr/local/lib/...`.)

Example:

```
mapnik-fonts: /usr/local/lib/mapnik/fonts
mapnik-fonts: /usr/share/fonts
```

- `tile.style`: specifies a Mapniks' style file used for rendering.
    + Level: path. There **must** be **exactly one** `tile.style` at each `tile.dir`-ed path.
    This instruction will not dive into a "how to prepare style files" guide, but
    [openstreetmap-carto](https://github.com/gravitystorm/openstreetmap-carto), for example, is 
    the "de-facto" OSM style; [This shell snippet](https://github.com/ntabee/h2o-tile/blob/tile/examples/tile-demo/make.demo-server.sh#L237-L241) illustrates how to setup it.

Example:

```
    paths:
      /tiles:
        tile.style: /opt/osm/openstreetmap-carto/osm.xml
```


## 3. Directives for `h2o-tile-proxy`

- `tile.upstream`: specifies the upstream server to fetch tiles.
    + Level: path. There **must** be **exactly one** `tile.upstream` at each `tile.dir`-ed path.
    The primary candidate would be `http://(a|b|c).tile.openstreetmap.org`, the OSM's official tile servers.
    However, again, excessive access **can be regarded abuse** and the [OSM's tile usage policy](http://wiki.openstreetmap.org/wiki/Tile_usage_policy) should be obeyed. [OSM wiki contains a list of other candidates](http://wiki.openstreetmap.org/wiki/Tile_servers).

Example:

```
    paths:
      /tiles:
          tile.upstream: http://c.tile.openstreetmap.org
```
