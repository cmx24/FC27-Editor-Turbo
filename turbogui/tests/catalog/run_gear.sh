#!/usr/bin/env bash
# Builds and runs the gear gallery tests (core/gear_ids.h, WP-08) with g++ only, in seconds. No game files are read:
# the only input is tests/fixtures/asset_catalog_v1_small.json (synthetic).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/catalog
g++ -std=c++17 -O0 -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party tests/catalog/test_gear_ids.cpp \
    src/core/asset_catalog.cpp -o build/catalog/test_gear_ids.exe
./build/catalog/test_gear_ids.exe tests/fixtures/asset_catalog_v1_small.json
