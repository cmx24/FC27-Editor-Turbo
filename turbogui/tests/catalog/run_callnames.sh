#!/usr/bin/env bash
# Builds and runs the callnames-from-the-asset-catalog tests (core/callname_bank.h, core/callnames_catalog.h and the
# catalog parts of core/callnames.cpp) with g++ only. Input: tests/fixtures/asset_catalog_v1_small.json (synthetic).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/catalog
SRCS="src/core/callnames.cpp src/core/asset_catalog.cpp src/core/callname_audio.cpp src/core/commentary_bank.cpp src/core/t3db.cpp src/core/model.cpp src/core/overall.cpp src/core/teamnames.cpp"
g++ -std=c++17 -O0 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party tests/catalog/test_callnames_catalog.cpp \
    $SRCS -o build/catalog/test_callnames_catalog.exe
./build/catalog/test_callnames_catalog.exe tests/fixtures/asset_catalog_v1_small.json "$@"
