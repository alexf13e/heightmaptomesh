#!/usr/bin/bash

./build.sh

if [ $? -eq 0 ]; then
    ./build/heightmaptomesh
fi
