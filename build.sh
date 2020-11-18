#!/bin/sh
set -e
mkdir -p bin
gcc -lgc -Wall src/nl.c -o bin/nl
