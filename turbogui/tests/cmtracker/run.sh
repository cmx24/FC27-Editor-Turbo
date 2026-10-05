#!/usr/bin/env bash
# Builds and runs the CMTracker library test with g++ only. Optional argument: a PNG to run through the miniface conversion.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/cmt
g++ -std=c++17 -O1 -Wall -Wextra -Isrc -Ithird_party -Ithird_party/stb tests/cmtracker/test_cmtracker.cpp src/core/cmtracker.cpp src/core/image.cpp \
    src/core/model.cpp src/core/t3db.cpp src/core/teamnames.cpp -o build/cmt/test_cmtracker.exe
./build/cmt/test_cmtracker.exe "$@"
