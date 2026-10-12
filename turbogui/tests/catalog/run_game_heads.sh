#!/usr/bin/env bash
# Builds and runs the real-face chooser's "Game heads" tests (core/game_heads.h) and its 5-choices-per-menu rule
# (core/face_filter.cpp) with g++ only, in seconds; the translation units compile in parallel. Input: the synthetic
# tests/fixtures/asset_catalog_v1_small.json; no game file is read.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/catalog/game_heads
mkdir -p "$out"
flags=(-std=c++17 -O0 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party)
pids=()
for src in tests/catalog/test_game_heads.cpp src/core/asset_catalog.cpp src/core/face_filter.cpp src/core/face3d_looks.cpp \
           src/core/hair_catalog.cpp; do
    obj="$out/$(basename "${src%.cpp}").o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ -n "$(find src/core tests/catalog -name '*.h' -newer "$obj" -print -quit)" ]; then
        g++ "${flags[@]}" -c "$src" -o "$obj" &
        pids+=($!)
    fi
done
for p in ${pids[@]+"${pids[@]}"}; do wait "$p"; done
g++ "$out"/*.o -o "$out/test_game_heads.exe"
"./$out/test_game_heads.exe" tests/fixtures/asset_catalog_v1_small.json
