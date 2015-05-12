#!/bin/bash

if [ "$1" = "" ]; then
    echo 'Usage: run.sh base-url'
    echo 'Example: run.sh "http://localhost:8080"'
    exit 1
fi

if [ -e paths.txt.bz2 -a ! -e paths.txt ]; then
    echo 'Decompres the path list to fetch...'
    bunzip2 -k paths.txt.bz2
    echo 'done.'
fi

if [ ! -e paths.txt ]; then
    echo 'Generate the path list to fetch...'
    bash ./gen-paths.sh > paths.txt
    echo 'done.'
fi

wrk --latency -c 1000 -t 4 -d 10 -s traverse.lua "$1"
