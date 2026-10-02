#!/usr/bin/env bash
# Builds dist/FC27_LE_Turbo_<version>.zip: exactly the files to copy into the Live Editor folder
# (the folder with FCLiveEditor.DLL). Needs the built binaries (turbogui/scripts/build_win.sh) and zip.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VER="$(sed -n 's/.*version = "\([0-9.]*\)".*/\1/p' "$ROOT/turbo/package/lua/libs/v2/imports/turbo/core/version.lua" | head -1)"
[ -n "$VER" ] || { echo "cannot read the version"; exit 1; }
GUI_VER="$(sed -n 's/.*kGuiVersion = "\([0-9.]*\)".*/\1/p' "$ROOT"/turbogui/src/ui/app.h "$ROOT"/turbogui/src/ui/app.cpp | head -1)"
[ "$GUI_VER" = "$VER" ] || { echo "version mismatch: Lua $VER, GUI $GUI_VER"; exit 1; }
for f in Turbo.dll TurboInjector.exe TurboProbe.exe; do
  [ -s "$ROOT/turbogui/build/win/$f" ] || { echo "turbogui/build/win/$f missing: run turbogui/scripts/build_win.sh"; exit 1; }
done
OUT="$ROOT/dist/FC27_LE_Turbo_$VER.zip"
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp -r "$ROOT/turbo/package/." "$STAGE/"
mkdir -p "$STAGE/turbo"
cp "$ROOT/turbogui/build/win/Turbo.dll" "$ROOT/turbogui/build/win/TurboInjector.exe" "$ROOT/turbogui/build/win/TurboProbe.exe" "$STAGE/turbo/"
find "$STAGE" -exec touch -d "2026-01-01 00:00:00" {} +
mkdir -p "$ROOT/dist"; rm -f "$OUT"
(cd "$STAGE" && find . -type f | LC_ALL=C sort | sed 's#^\./##' | zip -X -q "$OUT" -@)
# verify: the zip holds exactly the staged files
CHK="$(mktemp -d)"; trap 'rm -rf "$STAGE" "$CHK"' EXIT
unzip -q "$OUT" -d "$CHK"; diff -r "$STAGE" "$CHK" >/dev/null && echo "zip content == staged files"
(cd "$ROOT/dist" && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256" && cat "$(basename "$OUT").sha256")
unzip -l "$OUT" | tail -1
