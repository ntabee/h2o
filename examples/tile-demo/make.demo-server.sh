#!/bin/bash

######
## Description:
##  This script setups a tiny tile server with h2o-tile, which only serves a small region around Tokyo, Japan.
##
## Assumed Platform: Debian 8.0 (jessie) 64bit; >=50GB storage, >=2GB RAM
##  Ubuntu latest may also work by fixing apt package names
##  (in particular, font packages tend to be named quite differently)
##
##  RHEL/CentOS can be problematic, mainly due to their outdated gcc compiler (but not actually tested myself)
##
## Notice: Before you run the script:
##  - Do NOT open ports other than 22 and 8080
##  - Do NOT mix-place your important data and codes
##  Be aware, this is NOT a production-level, security-assured setup!!
##
## Prerequisites
##
##  Some steps are performed as a user named "osm" (useradd-ed at the first step of the script), so first
##    $ visudo
##  and add:
##    Defaults        timestamp_timeout=-1
##    osm   ALL=(ALL:ALL) ALL
##  This super-permissive privilege may be discarded after the setup.
##
##  Major data files are placed as follows:
##  - PostGIS database is under /var/lib/postgresql/9.4/main, this is the default of the apt package
##  - tiles are under /opt/osm/tiles:
##      + So, your /opt must have an enough space (several tens GBs), AND
##      + I **STRONGLY RECOMMEND** to mount /opt to a **Btrfs-formatted** partition, because...
##          the tiles/ dir will contain huge, really huge number of files:
##          even with our tiny region, we will potentially have **multi-millions** of .png iamges at the maximum zoom level.
##          Good old ext4 is, certainly, not suitable to such a usecase (what if # of files gets multi-billions?)
##          On the other hand, btrfs does not limit inodes a priori.
##        Mounting a btrfs partition, in a simplest form, is:
##          $ sudo sudo apt-get -qq install btrfs-tools
##          $ sudo mkfs.btrfs -O^extref -m single /dev/part/for/tiles
##          $ sudo vi /etc/fstab # and add the following line
##          /dev/part/for/tiles  /opt       btrfs   auto,noexec,noatime,autodefrag,compress=lzo,space_cache      0       2
##          # "compress=lzo" may not be very effective, however.

sudo apt-get -qq update
sudo useradd -m -s /bin/bash -d /opt/osm osm

sudo apt-get -qq install sudo passwd
sudo apt-get -qq install apt-utils
sudo apt-get -qq install ntp

# All packages required are installed here.
sudo apt-get -qq install btrfs-tools
sudo apt-get -qq install lvm2
sudo apt-get -qq install g++
sudo apt-get -qq install python
sudo apt-get -qq install python-dev
sudo apt-get -qq install python-pip
sudo apt-get -qq install libicu-dev
sudo apt-get -qq install libicu52
sudo apt-get -qq install libxml2
sudo apt-get -qq install libxml2-dev
sudo apt-get -qq install libbz2-dev
sudo apt-get -qq install libfreetype6
sudo apt-get -qq install libfreetype6-dev
sudo apt-get -qq install libjpeg-dev
sudo apt-get -qq install libpng-dev
sudo apt-get -qq install libtiff-dev
sudo apt-get -qq install libltdl-dev
sudo apt-get -qq install libproj-dev
sudo apt-get -qq install libcairo-dev
sudo apt-get -qq install libyaml-dev
sudo apt-get -qq install liblua5.2-dev
sudo apt-get -qq install libcairomm-1.0-dev
sudo apt-get -qq install libgdal-dev
sudo apt-get -qq install libsqlite-dev
sudo apt-get -qq install libharfbuzz-dev
sudo apt-get -qq install python-cairo-dev
sudo apt-get -qq install postgis
sudo apt-get -qq install python-gdal
sudo apt-get -qq install build-essential
sudo apt-get -qq install python-nose
sudo apt-get -qq install autotools-dev
sudo apt-get -qq install autogen
sudo apt-get -qq install osm2pgsql
sudo apt-get -qq install git
sudo apt-get -qq install autoconf
sudo apt-get -qq install automake
sudo apt-get -qq install node-carto
sudo apt-get -qq install unzip
sudo apt-get -qq install curl
sudo apt-get -qq install gdal-bin
sudo apt-get -qq install cmake
sudo apt-get -qq install wget

## Install fonts
cd /tmp
wget -O Gargi-2.0.tar.xz http://sourceforge.net/projects/indlinux/files/Fonts/Gargi-2.0.tar.xz/download
tar xf Gargi-2.0.tar.xz 
sudo cp gargi.ttf /usr/share/fonts/truetype/

cd /tmp
wget -O kanotf.zip http://prdownloads.sourceforge.net/brahmi/kanotf.zip?download
unzip kanotf.zip
cd kanotf/fonts
for f in *; do mv $f "`basename $f .TTF`.ttf"; done
cd ../..
sudo cp -ar kanotf /usr/share/fonts/truetype/
cd /tmp

sudo apt-get -qq install ttf-dejavu
sudo apt-get -qq install fonts-droid
sudo apt-get -qq install ttf-unifont
sudo apt-get -qq install fonts-sipa-arundina
sudo apt-get -qq install fonts-sil-padauk
sudo apt-get -qq install fonts-khmeros
sudo apt-get -qq install fonts-indic
sudo apt-get -qq install fonts-taml-tscu
sudo apt-get -qq install fonts-gubbi
sudo apt-get -qq install fonts-knda
sudo apt-get -qq install fonts-lohit-knda
sudo apt-get -qq install fonts-navilu
sudo apt-get -qq install fonts-gargi
sudo apt-get -qq install fonts-sarai
sudo apt-get -qq install fonts-deva
sudo apt-get -qq install fonts-tibetan-machine

# Tune the CPU governor, optional, but useful at least until setup is done.
sudo apt-get -qq install cpufrequtils
sudo sh -c "echo 'GOVERNOR=\"performance\"' > /etc/default/cpufrequtils"  

# Install qrintf, a printf-optimizing gcc wrapper
git clone https://github.com/h2o/qrintf.git
cd qrintf
# Current HEAD (as of 10, May, 2015) doesn't work well with gcc
git checkout v0.9.1 
git submodule init
git submodule update
make test
sudo make install
cd ..

# Install boost and mapnik, will taka an hour or so.
# If you don't insist on native-tuned version, just
# $ sudo apt-get -qq install libmapnik-dev libboost-all-dev
# **MIGHT** go, but I haven't tested with apt-supplied version of mapnik.
wget -O boost_1_58_0.tar.bz2 http://sourceforge.net/projects/boost/files/boost/1.58.0/boost_1_58_0.tar.bz2/download
tar xf boost_1_58_0.tar.bz2
cd boost_1_58_0
./bootstrap.sh --with-icu=/usr/lib/x86_64-linux-gnu/ --prefix=/usr 
sudo ./b2 install link=shared,static threading=multi variant=release --address-model=64 --architecture=x86 cflags='-O3 -mtune=native -march=native' cxxflags='-O3 -mtune=native -march=native' -j 4
cd ..

git clone https://github.com/mapnik/mapnik.git
cd mapnik
./configure CUSTOM_CFLAGS='-mtune=native -march=native' CUSTOM_CXXFLAGS='-mtune=native -march=native'
JOBS=4 make
make check
sudo make install
make clean
cd ..

# Install wslay and libuv
sudo pip install -U Sphinx
git clone https://github.com/tatsuhiro-t/wslay.git
cd wslay
autoreconf -i
autoconf
automake
./configure --prefix=/usr CFLAGS='-mtune=native -march=native' CXXFLAGS='-mtune=native -march=native'
make
sudo make install
make clean
cd ..

git clone https://github.com/libuv/libuv.git
cd libuv
./autogen.sh
./configure --prefix=/usr CFLAGS='-mtune=native -march=native' CXXFLAGS='-mtune=native -march=native'
make -j 4
sudo make install
make clean
cd ..


# Create the PostGIS database

# Trust all local access
cat > /tmp/pg_hba.conf <<EOF
# DO NOT DISABLE!
# If you change this first entry you will need to make sure that the
# database superuser can access the database using some other method.
# Noninteractive access to all databases is required during automatic
# maintenance (custom daily cronjobs, replication, and similar tasks).
#
# Database administrative login by Unix domain socket
local   all             postgres                                peer

# TYPE  DATABASE        USER            ADDRESS                 METHOD

# "local" is for Unix domain socket connections only
local   all             all                                     trust
# IPv4 local connections:
host    all             all             127.0.0.1/32            trust
# IPv6 local connections:
host    all             all             ::1/128                 trust
EOF
sudo mv /tmp/pg_hba.conf /etc/postgresql/9.4/main/pg_hba.conf

# The db name "gis" and username "osm" are mandatory:
# some parts of the toolchain almost hard-code these names.
sudo su postgres -c "createdb gis"
sudo su postgres -c "psql -c 'create extension postgis' gis"
sudo su postgres -c "psql -c 'create extension hstore' gis"
sudo su postgres -c "createuser osm"

# cf. http://wiki.openstreetmap.org/wiki/User:Species/PostGIS_Tuning
# Values should vary on your environment.
# Below is a rather "conservative" config. assuming small instances with <=2G RAM
cat > /tmp/postgresql.conf <<EOF
  shared_buffers = 64MB
  work_mem = 16MB
  maintenance_work_mem = 128MB
  checkpoint_segments = 1600
  autovacuum = off
  fsync = off
  synchronous_commit = off
  effective_io_concurrency = 30
  effective_cache_size = 1GB
  max_connections = 300
  wal_buffers = 16MB
EOF
sudo sh -c "cat /tmp/postgresql.conf >> /etc/postgresql/9.4/main/postgresql.conf"

sudo /etc/init.d/postgresql restart

# Make the style file for rendering, openstreetmap-carto is the "de-facto" of OSM
cd /opt/osm
sudo git clone https://github.com/gravitystorm/openstreetmap-carto.git
cd openstreetmap-carto
# N.B. get-shapefiles.sh, which populates coastline shapes, downloads quite huge a dataset (>= 1GB)
sudo bash ./get-shapefiles.sh
sudo sh -c "carto project.mml > osm.xml"

# Download the OMS map data for our region
cd /opt/osm
sudo wget https://www.dropbox.com/s/uuw16ub3fgqd8be/tokyo_japan.osm.pbf
# ... and import it to the db
PBF=tokyo_japan.osm.pbf
sudo osm2pgsql -U osm --create --slim -C 1024 --number-processes 4 --hstore --style /opt/osm/openstreetmap-carto/openstreetmap-carto.style --multi-geometry /opt/osm/$PBF

# Span indices to gain query performance
cat > /tmp/index.sql <<EOF
-- http://wiki.openstreetmap.org/wiki/User:Species/PostGIS_Tuning#Indices
-- Not all indices are effective (at least for a small region)
-- Only those I found performant on tokyo_japan.pbf are uncommented

CREATE INDEX "planet_osm_polygon_nobuilding_index" 
  ON "planet_osm_polygon"
  USING gist ("way")
    WHERE "building" IS NULL; 

CREATE INDEX "idx_line_waterway" on planet_osm_line  USING gist (way) WHERE "waterway" IS NOT NULL ; 
CREATE INDEX "idx_line_name" on planet_osm_line  USING gist (way) WHERE "name" IS NOT NULL ; 
CREATE INDEX "idx_line_ref" on planet_osm_line  USING gist (way) WHERE "ref" IS NOT NULL ;
EOF
sudo su postgres -c "psql gis -f /tmp/index.sql"

# ... and then, YES! THIS is OUR tile server!
cd /tmp
git clone https://github.com/ntabee/h2o-tile.git
cd h2o-tile
git checkout tile
cmake . -DCMAKE_C_COMPILER=/usr/local/bin/qrintf-gcc -DCMAKE_C_FLAGS='-mtune=native -march=native -O3' -DCMAKE_CXX_FLAGS='-mtune=native -march=native -O3'
make -j 4
sudo make install
sudo cp examples/h2o/tiles.conf /opt/osm
sudo mkdir /opt/osm/www
sudo cp examples/doc_root.leaflet/index.html /opt/osm/www
# Finally, pre-render the tiles up to zoom 16 (1:15,000 scale)
# "138.779000334176,34.8709816591244,140.868432008203,36.5579448557204" is the bounding-box of our imported region
# N.B. this is a task of half a day or so to yield ~160k files.
# If you are inpatient, you can stop by 14 or 15 (-z 0-14, etc.); in that case, 
# larger zoom levels are rendered on-demand and slower to display at the first access.
#
# I don't recommend pre-rendering everything beyond zoom 17, unless you are running AWS c4.8xlarge or alike.
sudo su osm -c "yield-tiles -s -z 0-16 -b 138.779000334176,34.8709816591244,140.868432008203,36.5579448557204 -c /opt/osm/openstreetmap-carto/osm.xml -p /opt/osm/tiles"

# Running the dameon is as easy as:
sudo su osm -c "h2o-tile -c /opt/osm/tiles.conf"

# Now open your browser and access to http://your-ip-addr:8080/ to see if it works.
