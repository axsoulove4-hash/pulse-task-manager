#!/usr/bin/env bash
set -e
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
QT_QPA_PLATFORM=offscreen ./build/pulse --self-test
