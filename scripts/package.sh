#!/usr/bin/env bash
# Builds dist/FC27_LE_Turbo_<version>.zip: exactly the files to copy into the Live Editor folder
# (the folder with FCLiveEditor.DLL). Needs the built binaries (turbogui/scripts/build_win.sh) and zip.
# `bash scripts/package.sh --guard` runs only the name guard below (git + a staged copy of turbo/package, about 1 s).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Name guard (2.1): the game asset catalog (turbo_output\asset_catalog: catalog_<build>.json, its summary, the Lua gate),
# its name sources and the callname masters hold real player names. They stay on the user's PC: never in git, never in
# the zip. Prints the offending paths; tests/fixtures/asset_catalog_v1_small.json is synthetic and allowed.
NAME_FILES='(^|/)(catalog_[^/]*\.(json|summary\.txt)|lua_gate\.txt|asset_writes\.log|asset_gate_allow\.txt|extra_player_names\.json|names_sheets\.json|[a-z]{3}_[a-z]{2}_bank(_now)?\.json)$|(^|/)asset_catalog/cache/|(^|/)callnames/masters/[^/]*\.json$'
git_leaks() { git -C "$ROOT" ls-files | grep -E "$NAME_FILES" || true; }
staged_leaks() {  # $1 = staged folder: names by path, and any file that is an asset catalog by content
  (cd "$1" && { find . -type f | sed 's#^\./##' | grep -E "$NAME_FILES"; grep -rlI 'turbo-asset-catalog/' . | sed 's#^\./##'; } | LC_ALL=C sort -u) || true
}
name_guard() {  # $1 = staged folder
  local g s
  g="$(git_leaks)"; s="$(staged_leaks "$1")"
  [ -z "$g" ] || { echo "name guard: files with real names are tracked in git:"; echo "$g"; }
  [ -z "$s" ] || { echo "name guard: files with real names would be packaged:"; echo "$s"; }
  [ -z "$g$s" ]
}
# Knobs guard (2.4, scripts/knobs_guard.py): a gameplay tuning knob is a slider only with its in-game proof; a knobs file with a
# slider (or a live knob) without a proof, or a proof without its evidence, is refused
knobs_guard() {  # $1 = staged folder
  # the coordinator's proof-session switch must never ship (it makes the profile write knobs whose effect is not proven)
  if [ -n "$(find "$1" -name tuning_test_unproven_on.txt -print -quit)" ]; then
    echo "knobs guard: refused: tuning_test_unproven_on.txt is in the package"; return 1
  fi
  # 2.7: tuning_live_off.txt is the user's kill switch of every gameplay tuning write in a match; a package that carried it would switch live apply off for everyone
  if [ -n "$(find "$1" -name tuning_live_off.txt -print -quit)" ]; then
    echo "knobs guard: refused: tuning_live_off.txt is in the package"; return 1
  fi
  local py=""
  for c in python3 python "py -3"; do $c -c "import json" >/dev/null 2>&1 && { py="$c"; break; }; done
  [ -n "$py" ] || { echo "knobs guard: no Python found (python3 / python / py -3)"; return 1; }
  $py -I "$ROOT/scripts/knobs_guard.py" "$1/turbo/tuning"
}
if [ "${1:-}" = "--guard" ]; then
  G="$(mktemp -d)"; trap 'rm -rf "$G"' EXIT
  cp -r "$ROOT/turbo/package/." "$G/"; rm -rf "$G/turbo/callnames/masters"
  r=0
  name_guard "$G" && echo "name guard: ok (git and turbo/package hold no catalog, gate, bank or name files)" || r=1
  knobs_guard "$G" || r=1
  exit $r
fi
VER="$(sed -n 's/.*version = "\([0-9][0-9A-Za-z.-]*\)".*/\1/p' "$ROOT/turbo/package/lua/libs/v2/imports/turbo/core/version.lua" | head -1)"
[ -n "$VER" ] || { echo "cannot read the version"; exit 1; }
GUI_VER="$(sed -n 's/.*kGuiVersion = "\([0-9][0-9A-Za-z.-]*\)".*/\1/p' "$ROOT"/turbogui/src/ui/app.h "$ROOT"/turbogui/src/ui/app.cpp | head -1)"
[ "$GUI_VER" = "$VER" ] || { echo "version mismatch: Lua $VER, GUI $GUI_VER"; exit 1; }
for f in Turbo.dll TurboInjector.exe TurboProbe.exe turbo_settings_apply.exe; do
  [ -s "$ROOT/turbogui/build/win/$f" ] || { echo "turbogui/build/win/$f missing: run turbogui/scripts/build_win.sh"; exit 1; }
done
OUT="$ROOT/dist/FC27_LE_Turbo_$VER.zip"
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp -r "$ROOT/turbo/package/." "$STAGE/"
# the callname master JSONs hold real player names: they stay on the user's PC (built locally with turbo/tools/import_callname_masters.py)
rm -rf "$STAGE/turbo/callnames/masters"
mkdir -p "$STAGE/turbo"
cp "$ROOT/turbogui/build/win/Turbo.dll" "$ROOT/turbogui/build/win/TurboInjector.exe" "$ROOT/turbogui/build/win/TurboProbe.exe" \
  "$ROOT/turbogui/build/win/turbo_settings_apply.exe" "$STAGE/turbo/"
name_guard "$STAGE" || { echo "package refused: remove those files first"; exit 1; }
knobs_guard "$STAGE" || { echo "package refused: a tuning knob is offered as a slider without its in-game proof"; exit 1; }
find "$STAGE" -exec touch -d "2026-01-01 00:00:00" {} +
mkdir -p "$ROOT/dist"; rm -f "$OUT"
make_zip() {  # $1 = staged folder, $2 = output zip; `zip` when present, else python zipfile (same sorted order, 2026-01-01 stamp, deflate)
  if command -v zip >/dev/null 2>&1; then
    (cd "$1" && find . -type f | LC_ALL=C sort | sed 's#^\./##' | zip -X -q "$2" -@)
    return
  fi
  echo "zip not found: building the package with python zipfile"
  local py="" c s="$1" o="$2"
  for c in python3 python "py -3"; do $c -c "import zipfile" >/dev/null 2>&1 && { py="$c"; break; }; done
  [ -n "$py" ] || { echo "no zip and no Python (python3 / python / py -3): cannot build the package"; return 1; }
  if command -v cygpath >/dev/null 2>&1; then s="$(cygpath -w "$1")"; o="$(cygpath -w "$2")"; fi
  $py -I - "$s" "$o" <<'PYZIP'
import os, sys, zipfile
src, out = sys.argv[1], sys.argv[2]
names = []
for d, _, fs in os.walk(src):
    for f in fs:
        names.append(os.path.relpath(os.path.join(d, f), src).replace(os.sep, "/"))
names.sort(key=lambda n: n.encode("utf-8"))
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for n in names:
        zi = zipfile.ZipInfo(n, (2026, 1, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.external_attr = 0o644 << 16
        with open(os.path.join(src, n.replace("/", os.sep)), "rb") as fh:
            z.writestr(zi, fh.read())
PYZIP
}
unzip_to() {  # $1 = zip, $2 = target folder; `unzip` when present, else python zipfile
  if command -v unzip >/dev/null 2>&1; then unzip -q "$1" -d "$2"; return; fi
  local py="" c a="$1" b="$2"
  for c in python3 python "py -3"; do $c -c "import zipfile" >/dev/null 2>&1 && { py="$c"; break; }; done
  [ -n "$py" ] || { echo "no unzip and no Python: cannot verify the package"; return 1; }
  if command -v cygpath >/dev/null 2>&1; then a="$(cygpath -w "$1")"; b="$(cygpath -w "$2")"; fi
  $py -I -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$a" "$b"
}
make_zip "$STAGE" "$OUT"
# verify: the zip holds exactly the staged files
CHK="$(mktemp -d)"; trap 'rm -rf "$STAGE" "$CHK"' EXIT
unzip_to "$OUT" "$CHK"; diff -r "$STAGE" "$CHK" >/dev/null && echo "zip content == staged files"
(cd "$ROOT/dist" && sha256sum "$(basename "$OUT")" > "$(basename "$OUT").sha256" && cat "$(basename "$OUT").sha256")
if command -v unzip >/dev/null 2>&1; then unzip -l "$OUT" | tail -1; else ls -l "$OUT"; fi
