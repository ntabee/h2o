#!/bin/bash

if [ "$1" = "" ]; then
    echo 'Usage: run.sh base-url'
    echo 'Example: run.sh "http://localhost:8080"'
    exit 1
fi

bash ./gen-paths.sh > paths.txt

wrk -c 1000 -t 4 -d 10 -s traverse.lua "$1"
