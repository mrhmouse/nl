#!/bin/sh
set -e
mkdir -p bin
gcc -lgc -ldl -Wall src/nl.c src/main.c -o bin/nl
