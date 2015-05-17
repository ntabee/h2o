H2O-tile: an H2O-based special-purpose HTTPD for OpenStreetMap tile server.
===

This is
--
- aimed at serving *already-prepared* tiles very quick
- *NOT* aimed at *on-demand rendering* non-existent tiles very quick

As such, the implementation does not adopt so-called *meta-tiles* as the storage format:
tiles are stored just as individual .png files.
(pros./cons. of this approach will be explained in near future... when?)

Status
--
At the very moment, a quick setup script is placed at `examples/tile-demo`, 
it builds a tiny demo-purpose tile server, covering a small region around Tokyo, Japan.

A brief instruction of the setup is in [INSTALL.md](INSTALL.md).

A [live demo](http://h2o-tile-demo.ddns.net:8080/) is running on Amazon EC2


Author
--
Naoshi Tabuchi [@n_tabee](https://twitter.com/n_tabee)

License
--
MIT License, unless otherwise explicitly stated.

upstream's original credits is left below:
--

H2O - an optimized HTTP server with support for HTTP/1.x and HTTP/2
===

[![Build Status](https://travis-ci.org/h2o/h2o.svg?branch=master)](https://travis-ci.org/h2o/h2o)

Copyright (c) 2014,2015 [DeNA Co., Ltd.](http://dena.com/), [Kazuho Oku](https://github.com/kazuho/), [Tatsuhiko Kubo](https://github.com/cubicdaiya/), [Domingo Alvarez Duarte](https://github.com/mingodad/), [Nick Desaulniers](https://github.com/nickdesaulniers/), [Marc Hörsken](https://github.com/mback2k), [Masahiro Nagano](https://github.com/kazeburo/), Jeff Marrison, [Daisuke Maki](https://github.com/lestrrat/)

H2O is a very fast HTTP server written in C, licensed under [the MIT License](http://opensource.org/licenses/MIT).  It can also be used as a library.

For more information, please refer to the documentation at [h2o.github.io](http://h2o.github.io).
