#!/usr/bin/env bash
# Builds and runs the asset catalog tests (core/asset_catalog + the schema v1 fixture) with g++ only, in seconds.
# No game files are read: the only input is tests/fixtures/asset_catalog_v1_small.json (synthetic).
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p build/catalog
g++ -std=c++17 -O0 -Wall -Wextra -Wno-unused-parameter -Isrc -Ithird_party tests/catalog/test_asset_catalog.cpp \
    src/core/asset_catalog.cpp -o build/catalog/test_asset_catalog.exe
./build/catalog/test_asset_catalog.exe tests/fixtures/asset_catalog_v1_small.json build/catalog "$@"
