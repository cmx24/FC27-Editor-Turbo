#!/usr/bin/env bash
# 2.20 K1 kitplan: the plan writer (kit_plan_timing.cpp --write-plans). Plans every row of <LE root>\turbo_output\kits\swap_state.json with
# the game's real Oodle and tocs (read only) and writes each kit's swap_plan_4.bin, then starts a Runtime the way the game does and checks
# that the table comes "from plans" with every row ready (last line "PLAN WRITER: PASS|FAIL ...").
#   bash scripts/msys.sh 'bash turbogui/tests/catalog/run_kit_plan_writer.sh <LE root> [game build]'
# <LE root> = a scratch copy (a dry run) or, for the Tester ONLY, the live Live Editor folder before a boot. It writes only under
# <LE root>\turbo_output: kits\<team>\<slot>\swap_plan_4.bin, kits\kit_index.cache and kits\kit_blocks.cache (when missing or stale)
# and kit_swap_log.txt. 4.0: PASS also needs the start's line "kit swap: non-kit list ready (kit_blocks.cache loaded ...); 0 kits queued".
# Builders never run it on the live folder.
set -euo pipefail
cd "$(dirname "$0")/../.."
le="${1:?LE root}"
game="${GAME:-C:/Program Files/EA Games/EA SPORTS FC 27}"
out=build/catalog/kitplanwriter
mkdir -p "$out"
g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -Isrc/core -Ithird_party -DKITPLAN_RUNTIME tests/catalog/kit_plan_timing.cpp src/core/kit_swap.cpp \
    -o "$out/kit_plan_writer.exe" -pthread
le_w="$le"; command -v cygpath > /dev/null 2>&1 && le_w="$(cygpath -w "$le")"
"./$out/kit_plan_writer.exe" --write-plans "$le_w" "$game" ${2:+"$2"}
