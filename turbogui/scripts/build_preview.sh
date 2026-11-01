#!/usr/bin/env bash
# Builds the GUI preview host (tools/gui-preview): build/preview/gui_preview.exe. Windows only (MSYS2 UCRT64 g++ through scripts/msys.sh).
#   bash scripts/msys.sh "bash turbogui/scripts/build_preview.sh"
# What it links: the SAME non-win source set as the native tests (the data layer without the game: tests/native/run_native.sh is the only
# list, read from there so a page added to the tests is in the preview), with Dear ImGui's DX11 backend instead of the null backend.
# The test-only files (test_main.cpp, test_crash_report.cpp) and the null backend are dropped; the two host stubs (the edit-unlock hook and
# the HTTP client, tests/native/test_*_stub*.cpp) stay, because there is no game process to hook and no network to call.
# Flags are the release build's (build_win.sh: -O2 -Wall -Wextra, no sanitizer, no test engine): a warning here is a failure.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/preview"
OBJ="$OUT/obj"
mkdir -p "$OBJ"
cd "$ROOT"
CXX="${CXX:-g++}"
DEFS="-DWIDL_EXPLICIT_AGGREGATE_RETURNS -D_WIN32_WINNT=0x0A00 -DNOMINMAX -DNDEBUG -DIMGUI_USER_CONFIG=\"turbo_imconfig.h\""
INC="-Isrc -Isrc/ui -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -pthread $DEFS $INC"
PC_SCRIPT="$ROOT/scripts/build_preview.sh"; . "$ROOT/scripts/parallel_compile.sh"

# the test link set: the words of the `for s in ... ; do` block of run_native.sh that end in .cpp
list="$(awk '/^for s in third_party\/imgui\/imgui.cpp/{on=1} on{print} on && /; do$/{exit}' tests/native/run_native.sh | tr -d '[:cntrl:]\134' | tr ' ' '\n' | grep -E '\.cpp$' || true)"
[ -n "$list" ] || { echo "build_preview.sh: could not read the source list from tests/native/run_native.sh"; exit 1; }
srcs=()
while IFS= read -r s; do
  case "$s" in
    tests/native/test_main.cpp|tests/native/test_crash_report.cpp|third_party/imgui/backends/imgui_impl_null.cpp) continue;;
  esac
  srcs+=("$s")
done <<< "$list"
srcs+=(third_party/imgui/backends/imgui_impl_dx11.cpp src/preview/page_ids.cpp src/preview/registry.cpp src/preview/main.cpp)
# 5.0 pages (src/preview/registry.h): every src/preview/pages/*.cpp registers one page, and the 5.0 kit's src/ui/ui5_*.cpp come along
# when the tree has them (a source the test list already holds is not added twice)
for s in src/preview/pages/*.cpp src/ui/ui5_*.cpp; do
  [ -f "$s" ] || continue
  dup=0; for t in "${srcs[@]}"; do [ "$t" = "$s" ] && dup=1; done
  [ "$dup" = 1 ] || srcs+=("$s")
done

objs=()
for s in "${srcs[@]}"; do
  o="$OBJ/$(echo "$s" | sed 's#[/.]#_#g').o"
  extra=""; case "$s" in third_party/*) extra="-Wno-array-bounds";; esac
  pc_add "$o" "$s" $CXX $CXXFLAGS $extra
  objs+=("$o")
done
pc_wait
echo "sources: ${#srcs[@]}"
$CXX -pthread -o "$OUT/gui_preview.exe" "${objs[@]}" -static -static-libgcc -static-libstdc++ \
  -ld3d11 -ldxgi -ld3dcompiler -lwindowscodecs -lole32 -luuid -lgdi32 -luser32 -limm32
ls -l "$OUT/gui_preview.exe"
