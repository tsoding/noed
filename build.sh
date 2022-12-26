#!/bin/sh

set -xe

mkdir -p ./build/
clang -Wall -Wextra -ggdb -o ./build/noed ./src/main.c
clang -Wall -Wextra -ggdb -o ./build/escape ./src/escape.c
