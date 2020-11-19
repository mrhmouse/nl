#!/bin/sh
set -e
mkdir -p bin
gcc -lgc -ldl -Wall src/libnl.c src/nl.c -o bin/nl
