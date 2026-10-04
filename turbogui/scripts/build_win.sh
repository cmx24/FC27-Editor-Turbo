#!/usr/bin/env bash
# Cross-compiles Turbo.dll and TurboInjector.exe for Windows x64 with MinGW-w64 (posix threads).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/win"
OBJ="$OUT/obj"
mkdir -p "$OBJ"
cd "$ROOT"   # include paths below are relative, so a ROOT with spaces (e.g. "C:/FC 27 Live Editor/...") works
# Tool names: Debian-style cross compiler by default; on Windows (MSYS2 UCRT64/MINGW64) the native g++/gcc are used.
if [ -z "${CXX:-}" ]; then
  if command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1; then CXX=x86_64-w64-mingw32-g++-posix; else CXX=g++; fi
fi
if [ -z "${CC:-}" ]; then
  if command -v x86_64-w64-mingw32-gcc-posix >/dev/null 2>&1; then CC=x86_64-w64-mingw32-gcc-posix; else CC=gcc; fi
fi
if [ -z "${OBJDUMP:-}" ]; then
  if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then OBJDUMP=x86_64-w64-mingw32-objdump; else OBJDUMP=objdump; fi
fi
if [ -z "${STRIP:-}" ]; then
  if command -v x86_64-w64-mingw32-strip >/dev/null 2>&1; then STRIP=x86_64-w64-mingw32-strip; else STRIP=strip; fi
fi
DEFS="-DWIDL_EXPLICIT_AGGREGATE_RETURNS -D_WIN32_WINNT=0x0A00 -DNOMINMAX -DNDEBUG -DIMGUI_USER_CONFIG=\"turbo_imconfig.h\""
INC="-Isrc -Isrc/ui -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends -Ithird_party/minhook/include"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type $DEFS $INC"
CFLAGS="-O2 $DEFS -Ithird_party/minhook/include"

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
         src/core/t3db.cpp src/core/model.cpp src/core/bridge.cpp src/core/le_log.cpp src/core/memmap.cpp src/core/image.cpp src/core/legacy.cpp src/core/devops.cpp src/core/teamnames.cpp \
         src/ui/app.cpp src/ui/widgets.cpp src/ui/ui_players.cpp src/ui/ui_teams.cpp src/ui/ui_database.cpp src/ui/ui_tools.cpp src/ui/textures.cpp src/ui/ui_images.cpp src/ui/ui_competitions.cpp src/ui/ui_identity.cpp \
         src/win/dllmain.cpp src/win/overlay_dx12.cpp src/win/lazy_imports.cpp src/win/input_shield.cpp src/win/memmap_win.cpp src/win/devtools_win.cpp; do
  compile_cxx "$s"
done
for s in third_party/minhook/src/hook.c third_party/minhook/src/buffer.c third_party/minhook/src/trampoline.c \
         third_party/minhook/src/hde/hde64.c; do
  compile_c "$s"
done

$CXX -shared -o "$OUT/Turbo.dll" "${objs[@]}" -static -static-libgcc -static-libstdc++ \
  -lgdi32 -luser32 -limm32 -lole32 -Wl,--subsystem,windows
# Turbo.dll may be loaded while the game starts: it must not import Direct3D 12, DXGI, the shader compiler, DWM or the shell
# (src/win/lazy_imports.cpp loads those on first use). Fail the build if any of them is in its import table.
if $OBJDUMP -p "$OUT/Turbo.dll" | grep -iE "DLL Name: (d3d12|dxgi|d3dcompiler|dwmapi|shell32|dinput8)" ; then
  echo "Turbo.dll must not import the DLLs above"; exit 1
fi
$CXX -std=c++17 -O2 -municode -o "$OUT/TurboInjector.exe" "$ROOT/src/injector/main.cpp" -static -static-libgcc -static-libstdc++
$CXX -std=c++17 -O2 -Wall -Wextra -municode $DEFS -o "$OUT/TurboProbe.exe" "$ROOT/src/probe/main.cpp" -static -static-libgcc -static-libstdc++ \
  -ld3d12 -ldxgi -Wl,--subsystem,windows
$STRIP "$OUT/Turbo.dll" "$OUT/TurboInjector.exe" "$OUT/TurboProbe.exe"
ls -la "$OUT/Turbo.dll" "$OUT/TurboInjector.exe" "$OUT/TurboProbe.exe"
