#!/usr/bin/env bash
# Builds and runs the Turbo GUI native tests (Linux: g++ with AddressSanitizer + UBSan; Windows: MSYS2 UCRT64 g++).
# Needs: g++ (C++17), lua5.4, and the Turbo Lua package + test harness (TURBO_TESTS, default ../turbo/tests)
# with Live Editor's own Lua libs (copy <Live Editor>\lua\libs) in turbo/le27/libs.
#
# usage: run_native.sh [OUT_DIR]      (default: turbogui/build/native_test)
#   A relative OUT_DIR is taken from the current folder (from scripts/msys.sh: the repository root, so turbogui/build/run1).
#   Inside the repository OUT_DIR must be under turbogui/build (gitignored); the run empties it first.
# Runs may overlap: from several worktrees, or in one worktree with different OUT_DIRs. Each run keeps its fixtures, logs,
# test binary and temp files (TMP/TEMP/TMPDIR, TURBO_TEST_TMP for the Lua side) inside its own OUT_DIR; the object files a
# worktree shares are compiled under a lock (build/native_bin/.compile.lock). A second run that finds the default OUT_DIR
# busy moves to build/native_test.<pid>; a busy OUT_DIR given on the command line is refused.
# The last line is always either "RESULT n passed, m failed" from the tests or "run_native.sh: FAILED ..." saying what broke.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STEP="setup"
OUT=""
on_exit() {
  local rc=$?
  [ $rc -eq 0 ] && return 0
  local why=""
  case "$STEP" in
    "running the tests")
      case $rc in
        1) why=" (test failures: see the FAIL lines above)";;
        67) why=" (a C++ exception nobody caught: Windows code 0x20474343; the CRASH line above names the case)";;
        3) why=" (abort() or std::terminate in the test binary; the CRASH line above names the case)";;
        126) why=" (the test binary could not be started: is another run relinking it?)";;
        127) why=" (the test binary died on an unhandled exception: the CRASH line above names the case; with no CRASH line it could not start at all, e.g. a missing DLL)";;
        130) why=" (interrupted)";;
        134|139) why=" (the test binary crashed with a signal; the CRASH line above names the case)";;
        *) why=" (unexpected exit code: the binary was probably killed from outside)";;
      esac;;
  esac
  echo "run_native.sh: FAILED during \"$STEP\" with exit code $rc$why${OUT:+; output folder $OUT}"
}
trap on_exit EXIT

export TURBO_TESTS="${TURBO_TESTS:-$ROOT/../turbo/tests}"
[ -f "$TURBO_TESTS/../le27/libs/v1/live_editor.lua" ] || { echo "Live Editor's Lua libs are missing: copy <Live Editor>\\lua\\libs to turbo/le27/libs"; exit 2; }
BIN="$ROOT/build/native_bin"
mkdir -p "$ROOT/build" "$BIN/obj"

# ---- this run's output folder, held for the whole run (flock: released by the OS even when the run is killed)
HAVE_FLOCK=0; command -v flock >/dev/null 2>&1 && HAVE_FLOCK=1
take_out() {  # take_out <dir>: lock <dir>.lock on fd 9; fails when another run holds it
  [ $HAVE_FLOCK -eq 1 ] || return 0
  exec 9>"$1.lock"
  flock -n 9 || { exec 9>&-; return 1; }
}
if [ $# -ge 1 ] && [ -n "$1" ]; then
  # absolute from here on: the lock, the cleanup and everything after `cd "$ROOT"` (link, Lua world, temp files) must use
  # the same folder. A relative OUT_DIR is taken from the caller's folder (scripts/msys.sh starts in the repository root).
  ARG="$1"; new_top=""; d="${ARG%/}"; [ -n "$d" ] || d="/"
  while [ ! -e "$d" ]; do new_top="$d"; d="$(dirname "$d")"; done   # the topmost folder this run creates
  mkdir -p "$ARG"
  OUT="$(cd "$ARG" && pwd)"
  REPO="$(cd "$ROOT/.." && pwd)"
  refuse_out() {  # leaves no folder behind that this run created
    if [ -n "$new_top" ]; then d="${ARG%/}"; while rmdir -- "$d" 2>/dev/null && [ "$d" != "$new_top" ]; do d="$(dirname "$d")"; done; fi
    echo "run_native.sh: not using $OUT as OUT_DIR (the run empties it): $1"; OUT=""; exit 3
  }
  case "$ROOT/" in "${OUT%/}/"*) refuse_out "it holds the sources: pick a folder under turbogui/build";; esac
  case "$OUT/" in "$BIN/"*) refuse_out "it holds the compiled objects: pick another folder under turbogui/build";; esac
  case "$OUT/" in "$REPO/"*) case "$OUT/" in "$ROOT/build/"?*) ;; *) refuse_out "it is inside the repository: use turbogui/build/<name> (gitignored)";; esac;; esac
  take_out "$OUT" || { echo "run_native.sh: another run is using $OUT: give this run its own folder (run_native.sh <OUT_DIR>)"; exit 3; }
else
  OUT="$ROOT/build/native_test"
  if ! take_out "$OUT"; then
    OUT="$ROOT/build/native_test.$$"
    echo "run_native.sh: build/native_test is in use by another run: this run uses $OUT"
    take_out "$OUT" || { echo "run_native.sh: could not lock $OUT"; exit 3; }
  fi
fi
[ $HAVE_FLOCK -eq 1 ] || echo "run_native.sh: flock not found: overlapping runs in this worktree are not protected"
mkdir -p "$OUT"
rm -rf "${OUT:?}"/*
mkdir -p "$OUT/tmp"

DEFS="-DIMGUI_ENABLE_TEST_ENGINE -DIMGUI_USER_CONFIG=\"turbo_imconfig.h\""
cd "$ROOT"   # relative include paths: ROOT may contain spaces
INC="-Isrc -Isrc/ui -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends"
# 2.15 hooks/minhook-reloc: the MinHook relocation test needs the MinHook headers and its C sources (Windows only).
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) INC="$INC -Ithird_party/minhook/include";; esac
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SANITIZE="${SANITIZE-}";; *) SANITIZE="${SANITIZE--fsanitize=address,undefined}";; esac
LUA="${LUA:-lua5.4}"
FLAGS="-std=c++17 -O1 -g -fno-omit-frame-pointer -pthread $SANITIZE -Wall -Wextra -Wno-unused-parameter $DEFS $INC"

# ---- compile (the worktree's shared objects: one run at a time) and link this run's own binary
STEP="waiting for the compile lock"
if [ $HAVE_FLOCK -eq 1 ]; then
  exec 8>"$BIN/.compile.lock"
  flock -n 8 || { echo "run_native.sh: another run is compiling in this worktree: waiting"; flock 8; }
fi
STEP="compiling"
objs=()
# parallel, incremental compiles (only changed sources and the objects whose headers changed)
# (absolute: "$0" is relative when started from the repository root, and this script has changed folder since)
PC_SCRIPT="$ROOT/tests/native/run_native.sh"; . "$ROOT/scripts/parallel_compile.sh"
for s in third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
         third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_null.cpp \
         src/core/t3db.cpp src/core/squad_role.cpp src/core/model.cpp src/core/bridge.cpp src/core/le_log.cpp src/core/memmap.cpp src/core/image.cpp src/core/legacy.cpp src/core/devops.cpp src/core/callnames.cpp src/core/teamnames.cpp src/core/sigscan.cpp src/core/gamethread.cpp src/core/fce_standings.cpp src/core/player_capture.cpp src/core/game_calls.cpp src/core/standings_refresh.cpp src/core/transfer_list.cpp src/core/player_move.cpp src/core/player_create.cpp src/core/player_morale.cpp src/core/commentary_bank.cpp src/core/commentary_audio.cpp src/core/reveal.cpp src/core/manager_rules.cpp src/core/match_setup.cpp src/core/reapply.cpp \
         src/core/face_filter.cpp src/core/face3d_looks.cpp src/core/hair_catalog.cpp src/core/user_text.cpp src/ui/ui_faces.cpp src/core/bodytype_catalog.cpp src/ui/ui_bodytypes.cpp \
         src/ui/app.cpp src/ui/widgets.cpp src/ui/ui_players.cpp src/ui/ui_teams.cpp src/ui/ui_database.cpp src/ui/ui_tools.cpp src/ui/textures.cpp src/ui/ui_images.cpp src/ui/ui_competitions.cpp src/ui/ui_callnames.cpp src/ui/ui_names.cpp src/ui/ui_presets.cpp src/ui/ui_cmtracker.cpp src/core/cmtracker.cpp src/core/season_pack.cpp src/core/kit_fonts.cpp src/ui/geo.cpp src/core/overall.cpp src/core/archetypes.cpp src/ui/ui_identity.cpp src/core/kit_import.cpp src/core/kit_swap.cpp src/core/attrib_ebx.cpp src/core/oodle_dispatch.cpp src/ui/ui_kits.cpp src/ui/ui_standings.cpp src/ui/ui_match.cpp src/ui/ui_reapply.cpp \
         src/core/lua_loader.cpp src/core/faces3d.cpp src/ui/ui_faces3d_gate.cpp \
         src/ui/file_picker.cpp \
         src/ui/ui_career_page.cpp src/ui/ui_diagnostics_page.cpp src/ui/ui_kits_page.cpp \
         src/core/asset_catalog.cpp src/ui/ui_asset_browser.cpp \
         src/core/hub_customise.cpp src/ui/ui_club_tools.cpp \
         src/core/transfer_stats.cpp src/core/scouts.cpp src/ui/ui_parity.cpp \
         src/core/callname_audio.cpp src/ui/ui_callname_play.cpp src/core/callname_voice.cpp src/core/callname_voice_host.cpp \
         src/ui/ui_zoom.cpp src/ui/ui_shell.cpp src/ui/ui_team_filter.cpp src/ui/ui_role_rules.cpp src/ui/ui_club_search.cpp src/ui/ui_settings.cpp src/ui/ui_club_page.cpp src/ui/ui_nav_pages.cpp src/ui/ui_pages_216.cpp \
         src/ui/ui_tactics_page.cpp \
         src/core/edit_unlock.cpp src/ui/ui_edit_unlock.cpp \
         src/core/edit_unlock_rules.cpp src/core/edit_unlock_hook.cpp tests/native/test_edit_unlock_hook.cpp tests/native/test_http_stub.cpp \
         src/ui/pitch_view.cpp src/ui/ui_tactics.cpp src/ui/ui_live_tactic.cpp src/ui/ui_tactic_presets.cpp src/core/tactic_presets.cpp src/core/team_sheet.cpp src/core/bench_probe.cpp src/core/sliders.cpp src/core/tactic_profiles.cpp src/core/tactics.cpp src/core/tactics_db.cpp src/core/opp_rules.cpp src/core/opposition.cpp src/core/teamname_override.cpp \
         src/core/tuning.cpp src/ui/ui_tuning.cpp \
         src/ui/ui_changes.cpp \
         src/core/game_settings.cpp src/ui/ui_game_settings.cpp \
         tests/native/test_crash_report.cpp tests/native/test_main.cpp; do
  o="$BIN/obj/$(echo "$s" | sed 's#[/.]#_#g').o"
  # test_main.cpp (13k lines of test code) at -O0: about 50 s instead of 95 s; relative source paths keep the .d files valid
  # in a copy of build/native_bin made for a new worktree
  f="$FLAGS"; [ "$s" = tests/native/test_main.cpp ] && f="${FLAGS/-O1/-O0}"
  pc_add "$o" "$s" g++ $f
  objs+=("$o")
done
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*)
  # 2.16 gui/perf: the input shield's detours, timed in the test process (test_gui_perf.h; host stubs in test_input_shield_stubs.cpp)
  for s in src/win/input_shield.cpp tests/native/test_input_shield_stubs.cpp; do
    o="$BIN/obj/$(echo "$s" | sed 's#[/.]#_#g').o"
    pc_add "$o" "$s" g++ $FLAGS
    objs+=("$o")
  done
  for s in third_party/minhook/src/trampoline.c third_party/minhook/src/buffer.c third_party/minhook/src/hook.c third_party/minhook/src/hde/hde64.c; do
    o="$BIN/obj/$(echo "$s" | sed 's#[/.]#_#g').o"
    pc_add "$o" "$s" gcc -O2 -Ithird_party/minhook/include
    objs+=("$o")
  done ;;
esac
pc_wait
STEP="linking"
TEST_BIN="$OUT/turbo_native_tests"
g++ -pthread $SANITIZE -o "$TEST_BIN" "${objs[@]}"
[ $HAVE_FLOCK -eq 1 ] && exec 8>&-   # objects done: the next run in this worktree may compile

# ---- run: every temp file of this run (C++ cases, the Lua side's Live Editor folders) inside OUT
export TMP="$OUT/tmp" TEMP="$OUT/tmp" TMPDIR="$OUT/tmp" TURBO_TEST_TMP="$OUT/tmp"
STEP="building the Lua test world"
"$LUA" "$ROOT/tests/native/gui_world.lua" build "$OUT" > "$OUT/build.log" 2>&1 || { cat "$OUT/build.log"; exit 1; }
STEP="running the tests"
rc=0
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  "$TEST_BIN" "$OUT" "$ROOT/tests/native/gui_world.lua" || rc=$?
# a green run in a fallback folder (build/native_test.<pid>) leaves nothing behind
if [ $rc -eq 0 ] && [ "$OUT" = "$ROOT/build/native_test.$$" ]; then rm -rf "$OUT" "$OUT.lock"; fi
exit $rc
