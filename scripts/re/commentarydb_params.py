"""List the events of the base CommentaryDb (the EBX partition the speech system loads, 10,201 named events on build
6AB9813C-211EF000) with the parameters each event declares, from a raw dump of that partition.

    bash scripts/re/py.sh scripts/re/commentarydb_params.py <dump.bin> [--base 0x309250000] [--db 0x309250010]
                         [--all] [--out events.json]

The dump is a byte copy of the game's memory from --base (the 2026-10-03 23:04 dump of [0x309250000, +0x340000) is kept
in C:\\FC 27 Live Editor\\turbo_dev\\masters\\re_inmatch\\commentarydb_base_0x309250000.bin; the partition's address
changes every session, read it again with the dev service when needed: db = the event table the speech registry's
handler searches, docs/callnames.md 5.1). Layout (docs/re/inmatch-callnames.md section 1):

    db      +0x50  event array (pointer to the first element; u32 count at -4)
    event   +0x18  name (char*), +0x20 candidate array (empty in the base db), +0x28 parameter array, +0x30 u32 id
                   (= djb2-xor of the name)
    param   +0x18  name (char*), +0x20 u32 hash (= djb2-xor of the name), +0x24 u32 position

Without --all only the events that take a player parameter are written: surname_ID, or any name containing "_pID"
(the game derives a "_gID" twin of every "_pID" parameter, 0x1414A90C8). Every id and hash is re-checked against
djb2-xor; the exit code is 1 when one does not match.
"""
import argparse
import json
import struct
import sys


def djb2x(s):
    h = 5381
    for c in s.encode("latin-1"):
        h = ((h * 33) ^ c) & 0xFFFFFFFF
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--base", default="0x309250000")
    ap.add_argument("--db", default=None, help="address of the CommentaryDb object (default: base + 0x10)")
    ap.add_argument("--all", action="store_true", help="every event, not only the ones with a player parameter")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    b = open(a.dump, "rb").read()
    base = int(a.base, 16)
    db = int(a.db, 16) if a.db else base + 0x10

    def inb(x):
        return base <= x < base + len(b)

    def q(x):
        return struct.unpack_from("<Q", b, x - base)[0]

    def d(x):
        return struct.unpack_from("<I", b, x - base)[0]

    def cs(x):
        o = x - base
        e = b.find(b"\0", o, o + 256)
        return b[o:e if e >= 0 else o + 256].decode("latin-1")

    arr = q(db + 0x50)
    count = d(arr - 4) & 0x7FFFFFFF
    bad = 0
    events = []
    for i in range(count):
        e = q(arr + 8 * i)
        if not inb(e):
            bad += 1
            continue
        name = cs(q(e + 0x18))
        eid = d(e + 0x30)
        bad += eid != djb2x(name)
        params = []
        pa = q(e + 0x28)
        if pa and inb(pa):
            for k in range(d(pa - 4) & 0x7FFFFFFF):
                p = q(pa + 8 * k)
                if not inb(p):
                    bad += 1
                    continue
                pn = cs(q(p + 0x18))
                ph = d(p + 0x20)
                bad += ph != djb2x(pn)
                params.append((d(p + 0x24), pn))
        params = [pn for (_, pn) in sorted(params)]
        events.append({"event": name, "id": "0x%08X" % eid, "params": params})

    def is_player(p):
        return p == "surname_ID" or "_pID" in p

    sel = events if a.all else [e for e in events if any(is_player(p) for p in e["params"])]
    print("%d events in the db, %d written, %d id/hash mismatches" % (len(events), len(sel), bad))
    for key in ("surname_ID", "player_db_pID"):
        print("  %-14s %d events" % (key, sum(1 for e in events if key in e["params"])))
    if a.out:
        with open(a.out, "w") as f:
            json.dump(sel, f, indent=0)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
