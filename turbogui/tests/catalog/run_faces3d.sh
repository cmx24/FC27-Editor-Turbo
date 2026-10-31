#!/usr/bin/env bash
# Builds and runs the faces3d tests (core/faces3d.h): players.json model, verified ids, validation, the player_kit.lua generator
# on a synthetic stock + template, refusals, header, apply/remove; plus the real-stock byte compare against the F2 override when
# turbo_dev/research and the evidence stock are on this machine (TURBO_RESEARCH_DIR overrides; SKIP otherwise).
# 5.0c E13: also test_faces3d_v5 (template v5, hair / beard replace); the last line sums both binaries (check_all reads it).
set -euo pipefail
cd "$(dirname "$0")/../.."
out=build/catalog/faces3d
mkdir -p "$out"
flags=(-std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party)
pids=()
for s in src/core/faces3d.cpp src/core/lua_loader.cpp src/core/sigscan.cpp src/core/devops.cpp tests/catalog/test_faces3d.cpp tests/catalog/test_faces3d_v5.cpp; do
  g++ "${flags[@]}" -c "$s" -o "$out/$(basename "$s" .cpp).o" & pids+=($!)
done
for p in "${pids[@]}"; do wait "$p"; done
lib=("$out/faces3d.o" "$out/lua_loader.o" "$out/sigscan.o" "$out/devops.o")
g++ "${lib[@]}" "$out/test_faces3d.o" -o "$out/test_faces3d.exe" -pthread
g++ "${lib[@]}" "$out/test_faces3d_v5.o" -o "$out/test_faces3d_v5.exe" -pthread
rc=0
"./$out/test_faces3d.exe" "$@" | tee "$out/faces3d.out" || rc=1
"./$out/test_faces3d_v5.exe" | tee "$out/faces3d_v5.out" || rc=1
p=0; f=0
for o in "$out/faces3d.out" "$out/faces3d_v5.out"; do
  l=$(grep -E -o '[0-9]+ passed, [0-9]+ failed' "$o" | tail -1 || true)
  [ -z "$l" ] && { echo "no result line in $o"; rc=1; continue; }
  p=$((p + $(echo "$l" | cut -d' ' -f1))); f=$((f + $(echo "$l" | cut -d' ' -f3)))
done
echo "faces3d + v5: $p passed, $f failed"
exit $rc
