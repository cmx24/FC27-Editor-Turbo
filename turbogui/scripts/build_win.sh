#!/usr/bin/env bash
# Cross-compiles Turbo.dll and TurboInjector.exe for Windows x64 with MinGW-w64 (posix threads).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/win"
OBJ="$OUT/obj"
mkdir -p "$OBJ"
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
DEFS="-DWIDL_EXPLICIT_AGGREGATE_RETURNS -D_WIN32_WINNT=0x0A00 -DNOMINMAX -DNDEBUG -DIMGUI_USER_CONFIG=\"turbo_imconfig.h\""
INC="-I$ROOT/src -I$ROOT/src/ui -I$ROOT/third_party -I$ROOT/third_party/imgui -I$ROOT/third_party/imgui/backends -I$ROOT/third_party/minhook/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type $DEFS $INC"
CFLAGS="-O2 $DEFS -I$ROOT/third_party/minhook/include"

objs=()
# GCC 13 reports "array subscript [0, 4] is outside array bounds of 'bool [5]'" inside imgui.cpp:
# the range it prints is inside the array (known false positive), so it is silenced for Dear ImGui only.
compile_cxx() {
  local src="$1"; local o="$OBJ/$(echo "$src" | sed 's#[/.]#_#g').o"; local extra=""
  case "$src" in third_party/*) extra="-Wno-array-bounds";; esac
  $CXX $CXXFLAGS $extra -c "$ROOT/$src" -o "$o"; objs+=("$o")
}
compile_c()   { local src="$1"; local o="$OBJ/$(echo "$src" | sed 's#[/.]#_#g').o"; $CC $CFLAGS -c "$ROOT/$src" -o "$o"; objs+=("$o"); }

for s in third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp \
         third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_win32.cpp \
         third_party/imgui/backends/imgui_impl_dx12.cpp \
         src/core/t3db.cpp src/core/model.cpp src/core/bridge.cpp \
         src/ui/app.cpp src/ui/widgets.cpp src/ui/ui_players.cpp src/ui/ui_teams.cpp src/ui/ui_database.cpp src/ui/ui_tools.cpp \
         src/win/dllmain.cpp src/win/overlay_dx12.cpp; do
  compile_cxx "$s"
done
for s in third_party/minhook/src/hook.c third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c \
         third_party/minhook/src/hde/hde64.c; do
  compile_c "$s"
done

$CXX -shared -o "$OUT/Turbo.dll" "${objs[@]}" -static -static-libgcc -static-libstdc++ \
  -ld3d12 -ldxgi -ld3dcompiler_47 -ldwmapi -lgdi32 -luser32 -limm32 -lole32 -Wl,--subsystem,windows
$CXX -std=c++17 -O2 -municode -o "$OUT/TurboInjector.exe" "$ROOT/src/injector/main.cpp" -static -static-libgcc -static-libstdc++
$CXX -std=c++17 -O2 -Wall -Wextra -municode $DEFS -o "$OUT/TurboProbe.exe" "$ROOT/src/probe/main.cpp" -static -static-libgcc -static-libstdc++ \
  -ld3d12 -ldxgi -Wl,--subsystem,windows
x86_64-w64-mingw32-strip "$OUT/Turbo.dll" "$OUT/TurboInjector.exe" "$OUT/TurboProbe.exe"
ls -la "$OUT/Turbo.dll" "$OUT/TurboInjector.exe" "$OUT/TurboProbe.exe"
