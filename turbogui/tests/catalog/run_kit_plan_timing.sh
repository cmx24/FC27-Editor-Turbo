#!/usr/bin/env bash
# 2.20 K1 kitplan: offline timing of the write-once kit plan (kit_plan_timing.cpp). Copies the live turbo_output\kits (read only:
# swap_state.json, kit_index.cache and the staged folders the state names, timestamps kept) into SCRATCH, then runs the tool there with the
# game's real Oodle and tocs (read only). Run inside MSYS2 at below-normal priority (compile_lock.ps1):
#   bash scripts/msys.sh 'bash turbogui/tests/catalog/run_kit_plan_timing.sh <scratch dir> [team slot] [--runtime]'
set -euo pipefail
cd "$(dirname "$0")/../.."
scratch="${1:?scratch dir}"
team="${2:-2056}"
slot="${3:-0}"
extra=()
[ "${4:-}" = "--runtime" ] && extra=(-DKITPLAN_RUNTIME)
live="/c/FC 27 Live Editor/turbo_output/kits"
game="${GAME:-C:/Program Files/EA Games/EA SPORTS FC 27}"
out=build/catalog/kitplantiming
mkdir -p "$out"
flags=(-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party "${extra[@]}")
g++ "${flags[@]}" tests/catalog/kit_plan_timing.cpp src/core/kit_swap.cpp -o "$out/kit_plan_timing.exe" -pthread
le="$scratch/le"
rm -rf "$le"
mkdir -p "$le/turbo_output/kits"
cp -p "$live/swap_state.json" "$live/kit_index.cache" "$le/turbo_output/kits/"
for t in $(grep -o '"team": *[0-9]*' "$live/swap_state.json" | grep -o '[0-9]*$' | sort -u); do
  cp -rp "$live/$t" "$le/turbo_output/kits/"
done
# no plan or table cache from the live folder: the copy starts with none (a plan is written only by this run, inside the scratch root)
find "$le" -name swap_plan.bin -delete
rm -f "$le/turbo_output/kits/kit_table.cache"
echo "scratch copy: $(du -sh "$le" | cut -f1), rows $(grep -c '"team"' "$live/swap_state.json")"
"./$out/kit_plan_timing.exe" "$(cygpath -w "$le")" "$game" "$team" "$slot" "${GAME_BUILD:-6AC07E31-2145C000}" --boot-teams "${BOOT_TEAMS:-2056,112558}"
