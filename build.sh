#!/bin/bash
# CTR Native Build Script (Linux)
# Builds the 64-bit engine (docs/64BIT.md), as Linux releases ship.
# Requires gcc, g++ and the X11/GL/ALSA dev packages.

set -e

if ! command -v gcc &>/dev/null || ! command -v g++ &>/dev/null; then
    echo "ERROR: gcc and g++ are required"
    echo "Install: sudo apt install gcc g++ cmake ninja-build"
    echo "         sudo apt install libx11-dev libxext-dev libgl1-mesa-dev libasound2-dev libudev-dev libdbus-1-dev"
    exit 1
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCTR_64BIT=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

echo ""
echo "Build succeeded: build/ctr_native"
