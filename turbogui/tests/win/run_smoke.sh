#!/usr/bin/env bash
# Windows smoke test for Turbo.dll, run under Wine (no game, no GPU needed).
# Cross-compiles smoke_loader.exe and a stub FCLiveEditor.DLL (NOT Live Editor) with mingw, builds a fake
# Live Editor folder around build/win/Turbo.dll and runs every mode (refuse, start, lua, nowindow, disabled, guard, guardretry, guardexit, guardkill, launch, launchlua).
# Run scripts/build_win.sh first. On Windows, run smoke_loader.exe <folder> <mode> yourself instead.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CXX="${CXX:-x86_64-w64-mingw32-g++-posix}"
command -v "$CXX" >/dev/null || CXX=x86_64-w64-mingw32-g++
WINE="${WINE:-$(command -v wine64 || echo /usr/lib/wine/wine64)}"
[ -x "$WINE" ] || { echo "wine64 not found (set WINE=...)"; exit 2; }
[ -f "$ROOT/build/win/Turbo.dll" ] || { echo "build/win/Turbo.dll missing: run scripts/build_win.sh first"; exit 2; }

OUT="${1:-$ROOT/build/smoke}"
rm -rf "$OUT"; mkdir -p "$OUT/bin"
export WINEPREFIX="$OUT/prefix" WINEDEBUG=-all WINEDLLOVERRIDES="winemenubuilder.exe=d"
unset DISPLAY || true

"$CXX" -std=c++17 -O1 -municode -static -static-libgcc -static-libstdc++ \
    "$ROOT/tests/win/smoke_loader.cpp" -o "$OUT/bin/smoke_loader.exe"
"$CXX" -shared -static -static-libgcc -static-libstdc++ \
    "$ROOT/tests/win/stub_le.cpp" -o "$OUT/bin/FCLiveEditor.DLL"

make_folder() {  # <name> -> prints the folder path; a fresh fake Live Editor folder per mode
    local d="$OUT/$1"
    mkdir -p "$d/turbo" "$d/turbo_output"
    cp "$ROOT/build/win/Turbo.dll" "$d/turbo/Turbo.dll"
    cp "$ROOT/build/win/TurboProbe.exe" "$d/turbo/TurboProbe.exe"   # as installed
    cp "$OUT/bin/FCLiveEditor.DLL" "$d/FCLiveEditor.DLL"
    echo '{}' > "$d/turbo_config.json"
    echo "$d"
}
winpath() { printf 'Z:%s' "$(echo "$1" | sed 's#/#\\#g')"; }

"$WINE" wineboot -i >/dev/null 2>&1 || true
rc=0
for mode in refuse start lua nowindow disabled guard guardretry guardexit guardkill launch launchlua; do
    d="$(make_folder "$mode")"
    echo "== smoke: $mode"
    if timeout 120 "$WINE" "$OUT/bin/smoke_loader.exe" "$(winpath "$d")" "$mode"; then
        echo "   exit 0"
    else
        echo "   exit $? (FAIL); log:"; sed 's/^/     /' "$d/turbo_output/turbo_gui.log" 2>/dev/null || true
        rc=1
    fi
    # the crash flag after the process is gone: a clean exit removes it, a killed process leaves it
    case "$mode" in
        guardexit) if [ -e "$d/turbo_output/turbo_gui_start.flag" ]; then echo "   FAIL flag still there after a clean exit"; rc=1; else echo "   PASS flag removed by the clean exit"; fi ;;
        guardkill) if [ -e "$d/turbo_output/turbo_gui_start.flag" ]; then echo "   PASS flag kept after the process was killed"; else echo "   FAIL flag missing after a kill"; rc=1; fi ;;
    esac
done
[ $rc -eq 0 ] && echo "ALL SMOKE MODES PASSED" || echo "SMOKE FAILED"
exit $rc
