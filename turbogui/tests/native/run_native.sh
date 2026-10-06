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
FLAGS="-std=c++17 -O1 -g -fno-omit-frame-pointer -pthread $SANITIZE -Wall -Wextra -Wno-unused-parameter $DEFS $INC"

objs=()
for s in third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
         third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_null.cpp \
         src/core/t3db.cpp src/core/model.cpp src/core/bridge.cpp src/core/le_log.cpp src/core/memmap.cpp src/core/image.cpp src/core/legacy.cpp src/core/devops.cpp src/core/callnames.cpp src/core/teamnames.cpp src/core/sigscan.cpp src/core/gamethread.cpp src/core/fce_standings.cpp src/core/player_capture.cpp src/core/game_calls.cpp src/core/standings_refresh.cpp src/core/transfer_list.cpp src/core/player_move.cpp src/core/player_create.cpp src/core/player_morale.cpp src/core/commentary_bank.cpp src/core/commentary_audio.cpp src/core/reveal.cpp src/core/manager_rules.cpp src/core/match_setup.cpp src/core/reapply.cpp \
         src/core/face_filter.cpp src/core/hair_catalog.cpp src/ui/ui_faces.cpp \
         src/ui/app.cpp src/ui/widgets.cpp src/ui/ui_players.cpp src/ui/ui_teams.cpp src/ui/ui_database.cpp src/ui/ui_tools.cpp src/ui/textures.cpp src/ui/ui_images.cpp src/ui/ui_competitions.cpp src/ui/ui_callnames.cpp src/ui/ui_names.cpp src/ui/ui_presets.cpp src/ui/ui_cmtracker.cpp src/core/cmtracker.cpp src/ui/geo.cpp src/core/overall.cpp src/core/archetypes.cpp src/ui/ui_identity.cpp src/ui/ui_standings.cpp src/ui/ui_match.cpp src/ui/ui_reapply.cpp \
         src/ui/file_picker.cpp \
         src/core/hub_customise.cpp src/ui/ui_club_tools.cpp \
         src/core/callname_audio.cpp src/ui/ui_callname_play.cpp src/core/callname_voice.cpp src/core/callname_voice_host.cpp \
         src/ui/ui_zoom.cpp src/ui/ui_team_filter.cpp \
         src/core/edit_unlock.cpp src/ui/ui_edit_unlock.cpp \
         src/core/edit_unlock_rules.cpp src/core/edit_unlock_hook.cpp tests/native/test_edit_unlock_hook.cpp tests/native/test_http_stub.cpp \
         src/core/teamname_override.cpp \
         tests/native/test_main.cpp; do
  o="$BIN/obj/$(echo "$s" | sed 's#[/.]#_#g').o"
  if [ ! -f "$o" ] || [ "$ROOT/$s" -nt "$o" ] || [ "$0" -nt "$o" ] || [ -n "$(find "$ROOT/src" "$ROOT/tests/native" -name '*.h' -newer "$o" -print -quit)" ]; then
    g++ $FLAGS -c "$ROOT/$s" -o "$o"
  fi
  objs+=("$o")
done
g++ -pthread $SANITIZE -o "$BIN/turbo_native_tests" "${objs[@]}"

"$LUA" "$ROOT/tests/native/gui_world.lua" build "$OUT" > "$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; exit 1; }
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  "$BIN/turbo_native_tests" "$OUT" "$ROOT/tests/native/gui_world.lua"
