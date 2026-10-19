#!/usr/bin/env bash
# 2.19 kit-table-cache: offline timing, cold build vs table cache (kit_table_timing.cpp). Copies the live turbo_output\kits (read only:
# swap_state.json, kit_index.cache and the staged folders the state names, timestamps kept) into SCRATCH, then runs the tool there with the
# game's real Oodle and tocs (read only). Run inside MSYS2 at below-normal priority (compile_lock.ps1):
#   bash scripts/msys.sh 'bash turbogui/tests/catalog/run_kit_table_timing.sh <scratch dir>'
set -euo pipefail
cd "$(dirname "$0")/../.."
scratch="${1:?scratch dir}"
live="/c/FC 27 Live Editor/turbo_output/kits"
game="${GAME:-C:/Program Files/EA Games/EA SPORTS FC 27}"
out=build/catalog/kittiming
mkdir -p "$out"
flags=(-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party)
g++ "${flags[@]}" tests/catalog/kit_table_timing.cpp src/core/kit_swap.cpp -o "$out/kit_table_timing.exe" -pthread
le="$scratch/le"
rm -rf "$le"
mkdir -p "$le/turbo_output/kits"
cp -p "$live/swap_state.json" "$live/kit_index.cache" "$le/turbo_output/kits/"
# the team folders the state names (each team folder whole: its slot folders)
for t in $(grep -o '"team": *[0-9]*' "$live/swap_state.json" | grep -o '[0-9]*$' | sort -u); do
  cp -rp "$live/$t" "$le/turbo_output/kits/"
done
echo "scratch copy: $(du -sh "$le" | cut -f1), rows $(grep -c '"team"' "$live/swap_state.json")"
"./$out/kit_table_timing.exe" "$(cygpath -w "$le")" "$game" "${TURBO_VERSION:-2.18.0}" "${GAME_BUILD:-6AC07E31-2145C000}"
