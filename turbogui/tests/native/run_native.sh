#!/usr/bin/env bash
# Builds and runs the Turbo GUI native tests on Linux (g++, AddressSanitizer + UBSan).
# Needs: g++ (C++17), lua5.4, and the Turbo Lua package + test harness (TURBO_TESTS, default ../turbo/tests)
# with Live Editor's own Lua libs (copy <Live Editor>\lua\libs) in turbo/le27/libs.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
export TURBO_TESTS="${TURBO_TESTS:-$ROOT/../turbo/tests}"
[ -f "$TURBO_TESTS/../le27/libs/v1/live_editor.lua" ] || { echo "Live Editor's Lua libs are missing: copy <Live Editor>\\lua\\libs to turbo/le27/libs"; exit 2; }
OUT="${1:-$ROOT/build/native_test}"
BIN="$ROOT/build/native_bin"
mkdir -p "$OUT" "$BIN/obj"
rm -rf "$OUT"/*

DEFS="-DIMGUI_ENABLE_TEST_ENGINE -DIMGUI_USER_CONFIG=\"turbo_imconfig.h\""
cd "$ROOT"   # relative include paths: ROOT may contain spaces
INC="-Isrc -Isrc/ui -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SANITIZE="${SANITIZE-}";; *) SANITIZE="${SANITIZE--fsanitize=address,undefined}";; esac
LUA="${LUA:-lua5.4}"
FLAGS="-std=c++17 -O1 -g -fno-omit-frame-pointer $SANITIZE -Wall -Wextra -Wno-unused-parameter $DEFS $INC"

objs=()
for s in third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
         third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_null.cpp \
         src/core/t3db.cpp src/core/model.cpp src/core/bridge.cpp src/core/le_log.cpp src/core/memmap.cpp src/core/image.cpp src/core/legacy.cpp src/core/devops.cpp src/core/teamnames.cpp \
         src/ui/app.cpp src/ui/widgets.cpp src/ui/ui_players.cpp src/ui/ui_teams.cpp src/ui/ui_database.cpp src/ui/ui_tools.cpp src/ui/textures.cpp src/ui/ui_images.cpp src/ui/ui_competitions.cpp src/ui/ui_identity.cpp \
         tests/native/test_main.cpp; do
  o="$BIN/obj/$(echo "$s" | sed 's#[/.]#_#g').o"
  if [ ! -f "$o" ] || [ "$ROOT/$s" -nt "$o" ] || [ "$0" -nt "$o" ] || [ -n "$(find "$ROOT/src" -name '*.h' -newer "$o" -print -quit)" ]; then
    g++ $FLAGS -c "$ROOT/$s" -o "$o"
  fi
  objs+=("$o")
done
g++ $SANITIZE -o "$BIN/turbo_native_tests" "${objs[@]}"

"$LUA" "$ROOT/tests/native/gui_world.lua" build "$OUT" > "$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; exit 1; }
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  "$BIN/turbo_native_tests" "$OUT" "$ROOT/tests/native/gui_world.lua"
