"""Commentary-bank selection tables in the running game, through Turbo's dev service (docs/callnames.md section 6).

    python scripts/re/callnames_bank.py scan [from_hex to_hex]      list the selection-row tables (runs) in a range
    python scripts/re/callnames_bank.py chain                       walk SpeechSystem -> registry -> event -> handler
    python scripts/re/callnames_bank.py events                      names of the commentary events (base CommentaryDb)

Read-only: only read / ptrs / find / region requests. The game must be running with Turbo.dll loaded (the dev service
answers turbo_output\\turbo_dev_request.json); wait 1.3 s between requests (the service polls the file). The scan
splits its range until every window has fewer than 1000 hits (the service caps a find at 1000), so a dense area costs
many requests: 0x3D4000000..0x3DC000000 took about 25 minutes on 2026-10-04. Turbo.dll itself scans in-process
(core/commentary_bank.cpp) in a few seconds; this script is for checking its results and for RE.
"""
import json
import os
import struct
import sys
import time

OUT = r"C:\FC 27 Live Editor\turbo_output"
REQ = os.path.join(OUT, "turbo_dev_request.json")
RES = os.path.join(OUT, "turbo_dev_result.json")
IMAGE_BASE = 0x140000000
SPEECH_SYSTEM_PTR = 0x14C27D590  # build 6AB9813C-211EF000 (signature "speech_system_ptr" in core/sigscan.cpp)
_last = 0.0


def call(req, timeout=120.0):
    global _last
    assert req.get("op") in ("read", "ptrs", "find", "region", "regions", "ping", "multi"), "read-only ops only"
    wait = 1.3 - (time.time() - _last)
    if wait > 0:
        time.sleep(wait)
    rid = int(time.time() * 1000) % 1000000000
    req = dict(req, id=rid)
    tmp = REQ + ".tmp"
    with open(tmp, "w") as f:
        json.dump(req, f)
    for _ in range(20):
        try:
            os.replace(tmp, REQ)
            break
        except PermissionError:
            time.sleep(0.3)
    _last = time.time()
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.25)
        try:
            r = json.load(open(RES))
        except Exception:
            continue
        if r.get("id") == rid:
            return r
    raise SystemExit("dev service: no answer")


def ptrs(a, n):
    r = call({"op": "ptrs", "addr": "0x%X" % a, "count": n})
    return [int(v, 16) if v else 0 for v in r.get("values", [])]


def read(a, n):
    r = call({"op": "read", "addr": "0x%X" % a, "len": n})
    return bytes.fromhex(r["hex"]) if r.get("ok") else None


ROW = "?? ?? ?? 00 00 00 00 00 ?? ?? ?? ?? ?? 00 00 00 02 00 00 00 00 00 00 00 ?? ?? ?? ?? ?? ?? ?? 88"


def scan(lo, hi):
    r = call({"op": "find", "pattern": ROW, "max": 1000, "from": "0x%X" % lo, "to": "0x%X" % hi})
    h = [int(x, 16) for x in r.get("hits", [])]
    if len(h) >= 1000 and hi - lo > 0x10000:
        mid = (lo + hi) // 2
        return scan(lo, mid) + scan(mid, hi)
    return h


def cmd_scan(lo, hi):
    hits = sorted(set(scan(lo, hi)))
    runs = []
    for h in hits:
        if runs and h - runs[-1][-1] == 0x40:
            runs[-1].append(h)
        else:
            runs.append([h])
    print("rows %d, runs %d" % (len(hits), len(runs)))
    for run in sorted(runs, key=len, reverse=True):
        if len(run) < 8:
            continue
        s = run[0]
        hdr = read(s - 4, 4)
        count = struct.unpack("<I", hdr)[0] & 0x7FFFFFFF if hdr else -1
        first = read(s, 64)
        last = read(run[-1], 64)
        v0 = struct.unpack_from("<I", first, 0)[0] if first else -1
        v1 = struct.unpack_from("<I", last, 0)[0] if last else -1
        print("  %x-%x rows %d header %d first value %d last value %d" % (s, run[-1] + 0x40, len(run), count, v0, v1))


def cmd_chain():
    r = ptrs(SPEECH_SYSTEM_PTR, 1)[0]
    print("SpeechSystem r = [%x] = %x" % (SPEECH_SYSTEM_PTR, r))
    s = ptrs(r + 0x50, 1)[0]
    sel = ptrs(r + 0x58, 1)[0]
    print("  registry s = [r+0x50] = %x   selector = [r+0x58] = %x" % (s, sel))
    hdr = ptrs(s + 0x28, 3)
    buckets, count = hdr[1], hdr[2] & 0xFFFFFFFF
    print("  event map: buckets %x count %d" % (buckets, count))
    nonempty = 0
    nodes = 0
    for i in range(0, count * 8, 65536):
        b = read(buckets + i, min(65536, count * 8 - i))
        for k in range(0, len(b), 8):
            head = struct.unpack_from("<Q", b, k)[0]
            if head:
                nonempty += 1
    print("  non-empty buckets %d" % nonempty)
    # one node: PLAYER_NAME_FE (key 0xad0cbbb2) -> handler -> owner -> language db
    key = 0xAD0CBBB2
    node = ptrs(buckets + (key % count) * 8, 1)[0]
    while node:
        nv = ptrs(node, 11)
        if nv[0] & 0xFFFFFFFF == key:
            break
        node = nv[10]
    if not node:
        print("  PLAYER_NAME_FE not registered")
        return
    nv = ptrs(node, 11)
    ctx = nv[1]
    name = read(ptrs(ctx + 0x38, 1)[0], 32).split(b"\0")[0]
    handlers = ptrs(nv[6], 1)
    print("  PLAYER_NAME_FE node %x ctx %x name %s handler %x" % (node, ctx, name, handlers[0]))
    owner = handlers[0] - 0x40
    y = ptrs(owner + 0x48, 1)[0]
    langdb = ptrs(y, 1)[0]
    m = ptrs(langdb + 0xD8, 2)
    print("  owner %x language db %x: event map buckets %x count %d (count 1 = nothing bound: no bank loaded)" % (owner, langdb, m[0], m[1] & 0xFFFFFFFF))


def cmd_events():
    r = ptrs(SPEECH_SYSTEM_PTR, 1)[0]
    s = ptrs(r + 0x50, 1)[0]
    hdr = ptrs(s + 0x28, 3)
    buckets, count = hdr[1], hdr[2] & 0xFFFFFFFF
    names = []
    for i in range(0, count * 8, 65536):
        b = read(buckets + i, min(65536, count * 8 - i))
        for k in range(0, len(b), 8):
            node = struct.unpack_from("<Q", b, k)[0]
            while node:
                nv = ptrs(node, 11)
                ctx = nv[1]
                sp = ptrs(ctx + 0x38, 1)[0]
                nm = read(sp, 48)
                names.append(nm.split(b"\0")[0].decode("latin-1") if nm else "?")
                node = nv[10]
    print("\n".join(sorted(names)))


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "scan"
    if what == "scan":
        lo = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x3D4000000
        hi = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x3DC000000
        cmd_scan(lo, hi)
    elif what == "chain":
        cmd_chain()
    elif what == "events":
        cmd_events()
    else:
        print(__doc__)
