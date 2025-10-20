#!/bin/bash
gcc patchelf.c -lelf -o my_patchelf
g++ example.cpp -Wl,-rpath=lonflonflonfrunpath -o example
./example