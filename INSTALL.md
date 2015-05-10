#tl;dr

`curl https://raw.githubusercontent.com/ntabee/h2o-tile/tile/examples/tile-demo/make.demo-server.sh | /bin/bash`

But... please have a look at the introduction in the script!

#Steps summarized

1. `apt-get install <compilers and libraries>`
2. `apt-get install <fonts>` and `wget` other non-apt fonts
3. `apt-get install postgresql postgis`
4. `createdb` a PostgreSQL database and populate PostGIS features
5. `wget` [the style file](https://github.com/gravitystorm/openstreetmap-carto) for rendering
6. `wget` the OSM data, clipped around Tokyo
7. import the data (6.) into the db (4.) with `osm2pgsql`
8. build our `h2o-tile`
9. pre-render tile images

For further details, please refer to the code comments in [the script](https://github.com/ntabee/h2o-tile/blob/tile/examples/tile-demo/make.demo-server.sh).

#Future work

This "plain-vanilla" shell script should be something more DevOps-friendly, like Ansible, Fabric, Docker, etc...
Such pull reqs. are really welcomed!
