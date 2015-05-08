#!/bin/bash

if [ "$1" = "" ]; then
    echo 'Usage: run.sh base-url'
    echo 'Example: run.sh "http://localhost:8080"'
    exit 1
fi

./gen-paths.sh "$1" > paths.txt

wrk -c 1000 -t 4 -d 30s -s traverse.lua http://localhost:8080/
