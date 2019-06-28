#!/bin/bash

# script that generates a python bindings of libcaer, it uses swig and gcc
# check that your python version and adjust accordingly.

swig -python -I$(pkg-config --variable=includedir libcaer) pyflags.i
gcc -std=c11 -O3 -fPIC -c pyflags_wrap.c $(pkg-config --cflags python3)
ld -shared -lcaer pyflags_wrap.o -o _libcaer_wrap.so
