#!/usr/bin/bash

output=./build/heightmaptomesh
sources="./*.cpp"
includes="-I ../_headers -I ../_headers/nfd"
libs="-L ../_lib -lnfd"

g++ -Wall -Wextra -o $output $sources $includes $libs `pkg-config --cflags --libs gtk+-3.0`
