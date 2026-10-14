#!/usr/bin/env bash
# Builds and runs the 3D-kit swap tests (core/kit_swap.h): resolver, plan, block table and runtime on a synthetic game; no game file is
# read. With a real install: run_kitswap.sh --game "C:\Program Files\EA Games\EA SPORTS FC 27" --team 95 --slot 0 (read only).
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/catalog/kitswap
mkdir -p "$out"
flags=(-std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party)
g++ "${flags[@]}" -c src/core/kit_swap.cpp -o "$out/kit_swap.o" &
p1=$!
g++ "${flags[@]}" -c tests/catalog/test_kit_swap.cpp -o "$out/test_kit_swap.o" &
p2=$!
wait "$p1"; wait "$p2"
g++ "$out"/*.o -o "$out/test_kit_swap.exe" -pthread
"./$out/test_kit_swap.exe" "$@"
