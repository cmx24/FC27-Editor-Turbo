#!/usr/bin/env bash
# Test of the GUI preview host (tools/gui-preview). usage: test_gui_preview.sh <gui_preview.exe> [world dir]
# Cases: an unknown page id is refused (exit 2, message names the valid ids, no file and no scratch folder is created); --list and
# --selftest work; with a world, Home renders to a real PNG of the asked size. Exit 0 = all pass.
set -u
EXE="${1:?usage: test_gui_preview.sh <gui_preview.exe> [world dir]}"
WORLD="${2:-}"
T="$(mktemp -d)"
trap 'rm -rf "$T"' EXIT
fail=0
ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=$((fail + 1)); }

# 1. unknown id
"$EXE" --page no/such-page --out "$T/x.png" > "$T/o1.txt" 2> "$T/e1.txt"; rc=$?
[ $rc -eq 2 ] && ok "unknown id: exit code 2" || bad "unknown id: exit code $rc (want 2)"
[ ! -e "$T/x.png" ] && ok "unknown id: no file written" || bad "unknown id: wrote $T/x.png"
grep -q "unknown page id 'no/such-page'" "$T/e1.txt" && ok "unknown id: message names the id" || bad "unknown id: message: $(cat "$T/e1.txt")"
grep -q "players/player" "$T/e1.txt" && ok "unknown id: message lists the valid ids" || bad "unknown id: no id list"
# a typo of a real id is refused too, in either part
"$EXE" --page players/playr --out "$T/y.png" > /dev/null 2>&1; [ $? -eq 2 ] && [ ! -e "$T/y.png" ] && ok "typo in the page part refused" || bad "typo in the page part accepted"
"$EXE" --page playerz/player --out "$T/y.png" > /dev/null 2>&1; [ $? -eq 2 ] && [ ! -e "$T/y.png" ] && ok "typo in the area part refused" || bad "typo in the area part accepted"
# 2. nothing to do
"$EXE" --out "$T/z.png" > /dev/null 2>&1; [ $? -eq 2 ] && ok "no --page/--all: usage error" || bad "no --page/--all accepted"
# 3. list + selftest
"$EXE" --list > "$T/list.txt" 2>&1; rc=$?
[ $rc -eq 0 ] && grep -qx "home/home" "$T/list.txt" && grep -qx "players/player" "$T/list.txt" && ok "--list names home/home and players/player" || bad "--list ($rc)"
"$EXE" --selftest > "$T/self.txt" 2>&1 && ok "--selftest" || { bad "--selftest"; cat "$T/self.txt"; }
# 4. a real render
if [ -n "$WORLD" ] && [ -f "$WORLD/world.img" ]; then
  "$EXE" --page home/home --size 1280x720 --out "$T/home.png" --world "$WORLD" --scratch "$T/scratch" > "$T/r.txt" 2>&1; rc=$?
  [ $rc -eq 0 ] && ok "home/home renders (exit 0)" || { bad "home/home render exit $rc"; cat "$T/r.txt"; }
  if [ -f "$T/home.png" ]; then
    sig="$(head -c 8 "$T/home.png" | od -An -tx1 | tr -d ' \n')"
    [ "$sig" = "89504e470d0a1a0a" ] && ok "PNG signature" || bad "not a PNG ($sig)"
    # IHDR width/height are big-endian 32-bit at offsets 16 and 20
    dim="$(dd if="$T/home.png" bs=1 skip=16 count=8 2>/dev/null | od -An -tu1 | tr -s ' ' | sed 's/^ //')"
    [ "$dim" = "0 0 5 0 0 0 2 208" ] && ok "PNG is 1280x720" || bad "PNG size bytes: $dim"
    [ "$(stat -c %s "$T/home.png")" -gt 20000 ] && ok "PNG has content (> 20 KB)" || bad "PNG is nearly empty"
  else
    bad "home.png missing"
  fi
  [ ! -e "$T/scratch" ] && ok "scratch folder removed" || bad "scratch folder left behind"
else
  echo "  skip render case (no world dir given)"
fi
echo "RESULT $([ $fail -eq 0 ] && echo PASS || echo "FAIL ($fail)")"
exit $((fail > 0))
