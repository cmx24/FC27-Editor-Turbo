#!/usr/bin/env bash
# Overlay test of the real Turbo.dll inside a running Direct3D 12 program, under Wine (vkd3d) with software Vulkan
# (Mesa lavapipe) on a virtual X display. The program is tests/win/game_stub.cpp, a stand-in, NOT FC 27.
#
# Timeline: the stand-in game starts presenting frames; after 1 s it loads the stub FCLiveEditor.DLL and turbo\Turbo.dll
# the way Turbo's lua\autorun does while the game starts (launch mode) WHILE it keeps presenting; at 1.5 s it writes Live
# Editor's "Initial setup done" for this process into Logs\live_editor_<date>.log; after 6 s it presses F8 (SendInput, from
# another thread); after 11 s it resizes its swap chain to 1280x720. Screenshots: hidden (5 s), shown (10 s), resized (14 s).
#
# MODE=probe (default): TurboProbe.exe finds the hook targets in a separate process (nothing created in the game).
# MODE=fallback: no TurboProbe.exe, Turbo probes inside the game; the stand-in pauses presenting for 4 s after loading
#   Turbo (a loading screen), because Wine + lavapipe deadlock when a DXGI factory is created while another thread
#   presents (reproduced without any Turbo code).
#
# Checks: the game runs to the end with no D3D12 error; Turbo waits for the game, installs its hooks, picks the queue,
# initialises Dear ImGui, survives the resize, shows its window on F8 (pixels on screen), proves its start and drawing
# phases (crash flag cleared) and logs no error.
#
# Needs: mingw-w64, wine64, Xvfb, mesa-vulkan-drivers (lavapipe), ImageMagick (import/convert). Run build_win.sh first.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CXX="${CXX:-x86_64-w64-mingw32-g++-posix}"
command -v "$CXX" >/dev/null || CXX=x86_64-w64-mingw32-g++
WINE="${WINE:-$(command -v wine64 || echo /usr/lib/wine/wine64)}"
LVP="${LVP:-/usr/share/vulkan/icd.d/lvp_icd.json}"
for need in "$WINE" "$LVP" "$(command -v Xvfb || true)" "$(command -v import || true)"; do
    [ -n "$need" ] && [ -e "$need" ] || { echo "missing tool (wine64 / lavapipe ICD / Xvfb / ImageMagick)"; exit 2; }
done
[ -f "$ROOT/build/win/Turbo.dll" ] || { echo "build/win/Turbo.dll missing: run scripts/build_win.sh first"; exit 2; }

OUT="${1:-$ROOT/build/overlay_test}"
RUN_S="${RUN_S:-16}"
MODE="${MODE:-probe}"
PAUSE_MS=0
[ "$MODE" = "fallback" ] && PAUSE_MS=4000
DISP="${DISP:-:97}"
mkdir -p "$OUT/bin"
rm -rf "$OUT/le" "$OUT"/*.png "$OUT/status.txt" "$OUT/game.log"
"$CXX" -std=c++17 -O1 -municode -DWIDL_EXPLICIT_AGGREGATE_RETURNS -static -static-libgcc -static-libstdc++ \
    "$ROOT/tests/win/game_stub.cpp" -o "$OUT/bin/game_stub.exe" -ld3d12 -ldxgi -ldinput8 -ldxguid
"$CXX" -shared -static -static-libgcc -static-libstdc++ "$ROOT/tests/win/stub_le.cpp" -o "$OUT/bin/FCLiveEditor.DLL"

LE="$OUT/le"
mkdir -p "$LE/turbo" "$LE/turbo_output"
cp "$ROOT/build/win/Turbo.dll" "$LE/turbo/Turbo.dll"
[ "$MODE" = "probe" ] && cp "$ROOT/build/win/TurboProbe.exe" "$LE/turbo/TurboProbe.exe"
cp "$OUT/bin/FCLiveEditor.DLL" "$LE/FCLiveEditor.DLL"
echo '{}' > "$LE/turbo_config.json"
winpath() { printf 'Z:%s' "$(echo "$1" | sed 's#/#\\#g')"; }

Xvfb "$DISP" -screen 0 1920x1080x24 -nolisten tcp >/dev/null 2>&1 &
XPID=$!
trap 'kill $XPID 2>/dev/null || true' EXIT
sleep 2
export DISPLAY="$DISP" WINEPREFIX="$OUT/prefix" WINEDEBUG=-all VK_ICD_FILENAMES="$LVP" TURBO_GUI_SETTLE_MS=500
"$WINE" wineboot -i >/dev/null 2>&1 || true

"$WINE" "$OUT/bin/game_stub.exe" "$(winpath "$LE")" "$RUN_S" 1000 6000 11000 "$(winpath "$OUT/status.txt")" "$PAUSE_MS" 1500 > "$OUT/game.log" 2>&1 &
GPID=$!
sleep 5
import -window root -display "$DISP" "$OUT/hidden.png" 2>/dev/null || true
sleep 5
import -window root -display "$DISP" "$OUT/shown.png" 2>/dev/null || true
sleep 4
import -window root -display "$DISP" "$OUT/resized.png" 2>/dev/null || true
# a hung game is a failure, not a wait: give it its run time plus 30 s
( sleep $((RUN_S + 30)); kill $GPID 2>/dev/null ) &
WD=$!
wait $GPID && GRC=0 || GRC=$?
kill $WD 2>/dev/null || true
LOG="$LE/turbo_output/turbo_gui.log"

fails=0
check() { if eval "$2"; then echo "  PASS $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }
has() { grep -q -- "$1" "$LOG" 2>/dev/null; }
# pixels in the game window area that are not the stand-in game's own clear colour (rgb 26,77,51)
foreign() { convert "$1" -crop 1000x700+12+40 +repage -fill black -fuzz 4% -opaque "rgb(26,77,51)" -fill white +opaque black \
            -format "%[fx:round(mean*w*h)]" info: 2>/dev/null || echo -1; }

echo "== overlay in a running Direct3D 12 program (Wine + vkd3d + lavapipe), mode $MODE"
sed 's/^/   game: /' "$OUT/game.log"
check "the game ran to the end without D3D12 errors" "[ $GRC -eq 0 ] && grep -q 'errors=0' '$OUT/status.txt'"
check "the game resized its swap chain with Turbo hooked" "grep -q 'resized=1' '$OUT/status.txt'"
check "loaded in launch mode, Turbo waited for Live Editor's 'Initial setup done'" "has 'loaded while the game is starting' && has 'Live Editor has finished setting up the game'"
check "nothing probed or hooked before Live Editor finished setting up" "[ \$(grep -n 'finished setting up' '$LOG' | head -1 | cut -d: -f1) -lt \$(grep -n 'hook targets\\|probing\\|TurboProbe' '$LOG' | head -1 | cut -d: -f1) ]"
check "Turbo waited for the game window and Direct3D 12" "has 'game window found'"
if [ "$MODE" = "probe" ]; then
    check "hook targets found by TurboProbe.exe, nothing created inside the game" "has 'verified in the game'"
    check "no in-game probe" "! has 'falling back'"
else
    check "without TurboProbe.exe Turbo falls back to probing inside the game" "has 'falling back'"
fi
check "hooks installed" "has 'hooks installed'"
check "input shield hooked the game's DirectInput 8 mouse and raw input (4 hooks)" "has 'input shield: 4 input hooks installed'"
check "the game's DirectInput mouse still works with the shield (polled every frame, no errors)" "grep -qE 'dinput_polls=[1-9][0-9]* dinput_ok=[1-9]' '$OUT/status.txt'"
check "overlay picked the game's direct queue" "has 'overlay queue:'"
check "Dear ImGui initialised on the game's swap chain" "has 'overlay ready'"
check "start phase proven (300 frames)" "has 'overlay proven'"
check "F8 showed the window: first frame drawn on screen" "has 'first frame drawn on screen'"
check "drawing proven (120 frames submitted)" "has 'drawing proven'"
check "no overlay error logged" "! grep -qiE 'error|failed|disabled for this session' '$LOG'"
check "crash flag cleared" "[ ! -e '$LE/turbo_output/turbo_gui_start.flag' ]"
H=$(foreign "$OUT/hidden.png"); S=$(foreign "$OUT/shown.png"); Z=$(foreign "$OUT/resized.png")
echo "   pixels not drawn by the game: hidden=$H shown=$S after-resize=$Z"
check "nothing of Turbo on screen while hidden" "[ $H -ge 0 ] && [ $H -lt 500 ]"
check "Turbo's window on screen after F8" "[ $S -gt 20000 ]"
check "Turbo's window still on screen after the swap chain resize" "[ $Z -gt 20000 ]"
echo "   turbo_gui.log:"; sed 's/^/     /' "$LOG" 2>/dev/null || true
[ $fails -eq 0 ] && echo "OVERLAY TEST PASSED" || echo "OVERLAY TEST FAILED ($fails)"
exit $fails
