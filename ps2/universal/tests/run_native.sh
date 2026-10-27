#!/bin/sh
# Builds and runs the native unit tests. Run inside the MSYS2 MINGW32 shell (or any shell with gcc).
set -e
cd "$(dirname "$0")/.."
mkdir -p build
gcc -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g -Isrc -o build/test_native.exe \
    tests/test_native.c src/bb_core.c src/vusb.c
./build/test_native.exe
