#!/usr/bin/env bash
# Builds and runs the Lua loader hook tests (core/lua_loader.h): md5, signatures + install decision on synthetic code, rows,
# override checks, log lines, and the detour logic with a fake lua_load and fake readers. Nothing of the game is read.
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/catalog/lualoader
mkdir -p "$out"
flags=(-std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party)
pids=()
for s in src/core/lua_loader.cpp src/core/sigscan.cpp src/core/devops.cpp tests/catalog/test_lua_loader.cpp; do
  g++ "${flags[@]}" -c "$s" -o "$out/$(basename "$s" .cpp).o" & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
g++ "$out"/*.o -o "$out/test_lua_loader.exe" -pthread
"./$out/test_lua_loader.exe" "$@"
