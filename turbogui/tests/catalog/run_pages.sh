#!/usr/bin/env bash
# Builds and runs the asset browser tests (core/asset_pages.h: filter, 200-tile pages, names, item lists, sources) with
# g++ only, in seconds. No game files are read: the only input is tests/fixtures/asset_catalog_v1_small.json (synthetic).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/catalog
g++ -std=c++17 -O0 -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party tests/catalog/test_asset_pages.cpp \
    src/core/asset_catalog.cpp -o build/catalog/test_asset_pages.exe
./build/catalog/test_asset_pages.exe tests/fixtures/asset_catalog_v1_small.json build/catalog "$@"
