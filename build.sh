#!/bin/sh

set -xe

clang -Wall -Wextra -ggdb -o noed main.c
rm -rf *.dSYM
