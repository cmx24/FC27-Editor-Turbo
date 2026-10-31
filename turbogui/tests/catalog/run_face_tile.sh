#!/usr/bin/env bash
# Builds and runs the real-face chooser tile tests (ui/ui_face_tile.h, pure part; 5.0c E7-realface): the picture ladder
# (choose_source), the badge layout (layout_badges: no overlap, inside the cell, 4.5:1), the hidden-head render breaker and the
# placeholder initials. g++ only, seconds.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/catalog/face_tile
mkdir -p "$out"
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Isrc tests/catalog/test_face_tile.cpp -o "$out/test_face_tile.exe"
"./$out/test_face_tile.exe" "$@"
