#!/usr/bin/env bash
# Builds and runs the faces3d tests (core/faces3d.h): players.json model, verified ids, validation, the player_kit.lua generator
# on a synthetic stock + template, refusals, header, apply/remove; plus the real-stock byte compare against the F2 override when
# turbo_dev/research and the evidence stock are on this machine (TURBO_RESEARCH_DIR overrides; SKIP otherwise).
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/catalog/faces3d
mkdir -p "$out"
flags=(-std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party)
pids=()
for s in src/core/faces3d.cpp src/core/lua_loader.cpp src/core/sigscan.cpp src/core/devops.cpp tests/catalog/test_faces3d.cpp; do
  g++ "${flags[@]}" -c "$s" -o "$out/$(basename "$s" .cpp).o" & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
g++ "$out"/*.o -o "$out/test_faces3d.exe" -pthread
"./$out/test_faces3d.exe" "$@"
