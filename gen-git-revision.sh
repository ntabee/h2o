#!/bin/sh
echo '#define GIT_REVISION "'`git rev-parse HEAD`'"'
echo '#define GIT_REVISION_SHORT "'`git rev-parse --short HEAD`'"'


