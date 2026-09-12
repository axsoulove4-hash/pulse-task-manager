#!/usr/bin/env bash
set -e
printf '%s\n' 'Build AppImage: run in Podman or Docker with Qt6.'
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
