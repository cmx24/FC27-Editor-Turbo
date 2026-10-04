#!/usr/bin/env bash
# Runs a command inside MSYS2 UCRT64 (g++, objdump, strip, lua5.4, zip) from Git Bash or PowerShell-launched bash.
# Usage: bash scripts/msys.sh "bash turbogui/scripts/build_win.sh"
# Needs MSYS2 in C:\msys64 with mingw-w64-ucrt-x86_64-gcc/binutils, zip, and lua5.4 in /usr/local/bin (see docs/BUILD_WINDOWS.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MSYS_BASH="${MSYS_BASH:-/c/msys64/usr/bin/bash.exe}"
[ -x "$MSYS_BASH" ] || { echo "MSYS2 not found at $MSYS_BASH"; exit 2; }
exec env MSYSTEM=UCRT64 CHERE_INVOKING=1 "$MSYS_BASH" -lc "cd \"\$(cygpath -u '$ROOT')\" && export PATH=/ucrt64/bin:/usr/local/bin:\$PATH && $*"
