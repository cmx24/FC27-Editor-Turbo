#!/usr/bin/env bash
# Installs the current build into the Live Editor folder (game must be closed).
# Usage: bash scripts/deploy.sh [LE folder]   (default: C:/FC 27 Live Editor)
# - refuses while FC27.exe runs (Turbo.dll is loaded and locked then)
# - backs up every file it replaces to <LE>/turbo_dev/backups/installs/installed_<timestamp>/ first
# - copies turbogui/build/win/{Turbo.dll,TurboProbe.exe,TurboInjector.exe} to <LE>/turbo/ and turbo/package/** to <LE>/
#   (turbo_config.json is only copied when the LE folder has none: the user's settings are kept)
# - prints sha256 of the installed binaries
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LE="${1:-/c/FC 27 Live Editor}"
[ -f "$LE/FCLiveEditor.DLL" ] || { echo "not a Live Editor folder: $LE"; exit 2; }
if tasklist 2>/dev/null | grep -qi '^FC27.exe'; then echo "FC27.exe is running: close the game first"; exit 3; fi
for f in Turbo.dll TurboProbe.exe TurboInjector.exe; do
  [ -s "$ROOT/turbogui/build/win/$f" ] || { echo "missing build/win/$f: run turbogui/scripts/build_win.sh"; exit 4; }
done
STAMP="$(date +%Y%m%d_%H%M%S)"
BK="$LE/turbo_dev/backups/installs/installed_$STAMP"
mkdir -p "$BK"
# file list: binaries + every package file except turbo_config.json (kept) and the turbo_output README
mapfile -t PKG < <(cd "$ROOT/turbo/package" && find . -type f ! -path './turbo_config.json' | sed 's#^\./##' | LC_ALL=C sort)
backup() { local rel="$1"; if [ -f "$LE/$rel" ]; then mkdir -p "$BK/$(dirname "$rel")"; cp -p "$LE/$rel" "$BK/$rel"; fi; }
for f in Turbo.dll TurboProbe.exe TurboInjector.exe; do backup "turbo/$f"; done
for rel in "${PKG[@]}"; do backup "$rel"; done
mkdir -p "$LE/turbo"
cp "$ROOT/turbogui/build/win/Turbo.dll" "$ROOT/turbogui/build/win/TurboProbe.exe" "$ROOT/turbogui/build/win/TurboInjector.exe" "$LE/turbo/"
for rel in "${PKG[@]}"; do mkdir -p "$LE/$(dirname "$rel")"; cp "$ROOT/turbo/package/$rel" "$LE/$rel"; done
[ -f "$LE/turbo_config.json" ] || cp "$ROOT/turbo/package/turbo_config.json" "$LE/turbo_config.json"
echo "backup: $BK ($(find "$BK" -type f | wc -l) files)"
echo "installed: 3 binaries + ${#PKG[@]} package files, from $(cd "$ROOT" && git log --oneline -1)"
(cd "$LE/turbo" && sha256sum Turbo.dll TurboProbe.exe TurboInjector.exe)
