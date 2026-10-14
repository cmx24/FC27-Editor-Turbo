"""sheet_copies.py - READ-ONLY resolver of every team-sheet copy of the user's club in the CachedTeamSheetService (sweep T3, 2026-10-13;
layout of build 6AC07E31 = the 6AB9813C layout decoded in T3.md s.2). Library for x3_readback.py, x3_sentinels.py, x2_sentinels.py.

  svc+0x40/+0x2C8    inline sheets C[0..1]          svc+0x550/+0x7D8  inline sheets A[0..1] (Team-Management working sheets)
  svc+0xA60/+0xCE8   inline sheets B[0..1] (backup) svc+0xF70/+0xF78  PlayerLineupData pointers
  svc+0xF80/+0x1078  inline records X[0..1] (what Edit Tactic displays)   svc+0x1170/+0x1268 inline records Y[0..1]
  the map record of the club: header +0x13B8 buckets, +0x13C0 bucket count, +0x13C4 node count; node key +0 / vector +8..+0x10 /
  next +0x28; sheet 0x290 bytes, record vector at sheet+0x1B0..+0x1B8, record 0xF8 bytes.
  record: +0x68 code (12 printable chars + NUL, the identity; never written), +0x80 active, +0xA4 Line Height, +0xB0 Build-Up,
          +0xB4 Defensive Approach (i32 each).
Service address: Turbo's log line "channel E: service located ... at 0x..." (latest since the last "Turbo GUI loading in process");
team: "channel E: team N record ..." or --team. Header checks (loaded flag +0x1404 == 1, bucket / node counts sane) refuse a wrong
or freed copy. mock_world() builds a synthetic service (no dump, no real ids or codes) for the offline tests.
"""
import os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402

REC, SHEET = 0xF8, 0x290
SHEETS = {"C0": 0x40, "C1": 0x2C8, "A0": 0x550, "A1": 0x7D8, "B0": 0xA60, "B1": 0xCE8}
RECS = {"X0": 0xF80, "X1": 0x1078, "Y0": 0x1170, "Y1": 0x1268}
F_CODE, F_ACTIVE, F_LH, F_BU, F_DA = 0x68, 0x80, 0xA4, 0xB0, 0xB4
APPROACH = {0: "Deep", 1: "Balanced", 2: "High", 3: "Aggressive"}
BUILDUP = {0: "Balanced", 1: "Counter", 2: "value 2 (Short Passing? Q5)", 3: "value 3 (unknown)"}
LOG = os.path.join(harness.TO, "turbo_gui.log")


def H(a):
    return "0x%X" % a


def band(lh):
    return 0 if lh <= 30 else 1 if lh <= 60 else 2 if lh <= 90 else 3


def rec_fields(b):
    i = lambda o: struct.unpack_from("<i", b, o)[0]  # noqa: E731
    code = b[F_CODE:F_CODE + 12]
    ok = all(0x21 <= c <= 0x7E for c in code) and b[F_CODE + 12] == 0
    return {"code": code.decode("latin-1") if ok else "", "active": b[F_ACTIVE], "line": i(F_LH), "buildup": i(F_BU),
            "approach": i(F_DA), "preset": i(0x84), "ffn": i(0xC0), "sf": i(0xC4)}


def show(f):
    return "code %s, LH %d, BU %s (%d), DA %s (%d), active %d" % (
        "set" if f["code"] else "none", f["line"], BUILDUP.get(f["buildup"], "?"), f["buildup"], APPROACH.get(f["approach"], "?"),
        f["approach"], f["active"])


def sheet_info(b, label):
    i = lambda o: struct.unpack_from("<i", b, o)[0]  # noqa: E731
    q = lambda o: struct.unpack_from("<Q", b, o)[0]  # noqa: E731
    rb, re_ = q(0x1B0), q(0x1B8)
    n = (re_ - rb) // REC if rb and re_ > rb and (re_ - rb) % REC == 0 and re_ - rb <= 16 * REC else 0
    return {"label": label, "tsid": i(0), "team": i(4), "current": b[0xAC], "vec": (rb, re_), "n": n, "slots": i(0x280),
            "dirty": b[0x288], "mode": i(0x28C) if len(b) >= 0x290 else None}


def from_log(path=None):
    """-> (svc or None, team or None) from Turbo's log: the latest values since the last start of the GUI"""
    try:
        lines = open(path or LOG, encoding="utf-8", errors="replace").read().splitlines()
    except OSError:
        return None, None
    start = 0
    for i, l in enumerate(lines):
        if "Turbo GUI loading in process" in l:
            start = i
    svc = team = None
    for l in lines[start:]:
        m = re.search(r"channel E: service located .* at (0x[0-9A-Fa-f]+)", l)
        if m:
            svc = int(m.group(1), 16)
        m = re.search(r"channel E: team (\d+) record", l)
        if m:
            team = int(m.group(1))
    return svc, team


class Unusable(Exception):
    pass


def resolve(read, svc, team, say=print):
    """read(pairs) -> [bytes|None]. -> dict(svc, header{...}, copies{label: (addr, fields)}, map_code). Raises Unusable when the
    service fails its header checks."""
    pairs = [(svc, 0x10), (svc + 0xF70, 0x1418 - 0xF70)]
    pairs += [(svc + o, SHEET) for o in SHEETS.values()] + [(svc + o, REC) for o in RECS.values()]
    res = read(pairs)
    if res[0] is None or res[1] is None:
        raise Unusable("svc %s not readable" % H(svc))
    vt1, vt2 = struct.unpack_from("<QQ", res[0], 0)
    hdr = res[1]
    hq = lambda o: struct.unpack_from("<Q", hdr, o - 0xF70)[0]  # noqa: E731
    hi = lambda o: struct.unpack_from("<i", hdr, o - 0xF70)[0]  # noqa: E731
    loaded = hdr[0x1404 - 0xF70]
    buckets, nb, nn = hq(0x13B8), hi(0x13C0), hi(0x13C4)
    info = {"vt1": vt1, "vt2": vt2, "loaded": loaded, "buckets": buckets, "nb": nb, "nn": nn, "last_team": hi(0x13A0)}
    say("svc %s: VT1 %s VT2 %s loaded %d, last team %d, map buckets %s x%d nodes %d" % (
        H(svc), H(vt1), H(vt2), loaded, hi(0x13A0), H(buckets), nb, nn))
    img = lambda v: harness.IMAGE_BASE <= v < harness.IMAGE_BASE + harness.BUILDS["6AC07E31"]["size"]  # noqa: E731
    if loaded != 1 or not (1 <= nb <= 100000) or not (1 <= nn <= 4 * nb) or not (img(vt1) and img(vt2)):
        raise Unusable("service header checks failed (VT pair in the image, loaded flag, bucket / node counts): wrong svc or a freed copy")
    copies, pairs2 = {}, []
    for k, (lab, off) in enumerate(SHEETS.items()):
        b = res[2 + k]
        if b is None:
            say("  sheet %s svc+%s: NOT readable" % (lab, H(off)))
            continue
        s = sheet_info(b, lab)
        say("  sheet %s svc+%s: team %d current %d records %d mode %s" % (lab, H(off), s["team"], s["current"], s["n"], s["mode"]))
        for r in range(s["n"]):
            pairs2.append(("%s.rec%d" % (lab, r), s["vec"][0] + REC * r))
    for k, (lab, off) in enumerate(RECS.items()):
        b = res[2 + len(SHEETS) + k]
        if b is not None:
            copies[lab] = (svc + off, rec_fields(b))
    rb = read([(ad, REC) for _, ad in pairs2]) if pairs2 else []
    for k, (lab, ad) in enumerate(pairs2):
        if k < len(rb) and rb[k]:
            copies[lab] = (ad, rec_fields(rb[k]))
    node = read([(buckets + 8 * (team % nb), 8)])[0]
    node = struct.unpack_from("<Q", node, 0)[0] if node else 0
    hops = 0
    while node and hops < 64:
        nbk = read([(node, 0x30)])[0]
        if not nbk:
            break
        if struct.unpack_from("<i", nbk, 0)[0] == team:
            vb, ve = struct.unpack_from("<QQ", nbk, 8)
            n = (ve - vb) // SHEET if vb and ve > vb and (ve - vb) % SHEET == 0 else 0
            for sidx in range(min(n, 4)):
                sb = read([(vb + SHEET * sidx, SHEET)])[0]
                if not sb:
                    continue
                s = sheet_info(sb, "map.sheet%d" % sidx)
                say("  map sheet %d at %s: team %d current %d records %d mode %s" % (sidx, H(vb + SHEET * sidx), s["team"], s["current"],
                                                                                    s["n"], s["mode"]))
                recs = read([(s["vec"][0] + REC * r, REC) for r in range(s["n"])])
                for r, b in enumerate(recs):
                    if b:
                        copies["map.s%d.rec%d" % (sidx, r)] = (s["vec"][0] + REC * r, rec_fields(b))
            break
        node = struct.unpack_from("<Q", nbk, 0x28)[0]
        hops += 1
    m = active(copies, "map.s0")
    return {"svc": svc, "header": info, "copies": copies, "map_code": m[1][1]["code"] if m else ""}


def active(copies, prefix):
    """the active record (+0x80 == 1) of a sheet copy (label prefix, e.g. 'map.s0' or 'A0')"""
    c = sorted((lab, v) for lab, v in copies.items() if lab.startswith(prefix + ".") and v[1]["active"] == 1)
    return c[0] if c else None


def target(copies, copy):
    """-> (label, (addr, fields)) of the record a sentinel for `copy` goes to, or None (copy absent / holds no tactic)"""
    if copy in RECS:
        v = copies.get(copy)
        return (copy, v) if v and v[1]["code"] else None
    return active(copies, "map.s0" if copy == "map" else copy)


# ------------------------------------------------------------------------------------------------------------------ mock world
MOCK_CODE = "SYNTH0CODE01"  # 12 printable chars (synthetic; a real share code never goes into git)
MOCK_TEAM = 7


def record(code, lh, bu, da, act=1):
    b = bytearray(REC)
    if code:
        b[F_CODE:F_CODE + 12] = code.encode()
    b[F_ACTIVE] = act
    struct.pack_into("<i", b, 0x84, 1)
    struct.pack_into("<i", b, F_LH, lh)
    struct.pack_into("<ii", b, 0xA8, 2, 2)
    struct.pack_into("<ii", b, F_BU, bu, da)
    return bytes(b)


def sheet(team, vec, n):
    b = bytearray(SHEET)
    struct.pack_into("<ii", b, 0, -1, team)
    b[0xAC] = 1
    struct.pack_into("<QQ", b, 0x1B0, vec, vec + REC * n)
    struct.pack_into("<i", b, 0x280, 11)
    struct.pack_into("<i", b, 0x28C, -1)
    return bytes(b)


def mock_world(path, log_path=None, x0=None, load=(25, 0, 0), copies=("C0", "A0", "B0"), loaded=1):
    """synthetic CachedTeamSheetService in a harness.MockMem file: inline sheets `copies` with one active heap record each holding
    MOCK_CODE and the career-load values `load` (LH, BU, DA), empty X/Y records (x0 = (LH, BU, DA) fills X0 as an open Edit Tactic),
    the map record of MOCK_TEAM. Writes Turbo-style log lines naming the svc and the team when log_path is given. -> (MockMem, svc)"""
    svc = harness.MOCK_HEAP + 0x100000
    heap = harness.MOCK_HEAP + 0x200000
    blob = bytearray(0x1418)
    struct.pack_into("<QQ", blob, 0, harness.IMAGE_BASE + 0x0D000000, harness.IMAGE_BASE + 0x0D000100)  # synthetic VT pair (image)
    regions = []
    for lab in copies:
        off = SHEETS[lab]
        vec = heap
        heap += 0x1000
        regions.append((vec, record(MOCK_CODE, *load) + record("", 0, 0, 0, 0)))
        blob[off:off + 0x288] = sheet(MOCK_TEAM, vec, 2)[:0x288]
    if x0:
        blob[RECS["X0"]:RECS["X0"] + REC] = record(MOCK_CODE, *x0)
    buckets, nb = heap, 7
    heap += 0x100
    node, sheetv, recv = heap, heap + 0x100, heap + 0x1000
    struct.pack_into("<ii", blob, 0x13A0, MOCK_TEAM, -1)
    struct.pack_into("<Q", blob, 0x13B8, buckets)
    struct.pack_into("<ii", blob, 0x13C0, nb, 3)
    blob[0x1404] = loaded
    bk = bytearray(8 * nb)
    struct.pack_into("<Q", bk, 8 * (MOCK_TEAM % nb), node)
    nd = bytearray(0x30)
    struct.pack_into("<i", nd, 0, MOCK_TEAM)
    struct.pack_into("<QQ", nd, 8, sheetv, sheetv + SHEET)
    regions += [(svc, bytes(blob)), (buckets, bytes(bk)), (node, bytes(nd)), (sheetv, sheet(MOCK_TEAM, recv, 2)),
                (recv, record(MOCK_CODE, *load) + record("", 0, 0, 0, 0))]
    mods = [{"name": "FC27.exe", "base": "0x%X" % harness.IMAGE_BASE, "size": harness.BUILDS["6AC07E31"]["size"]}]
    regions.append((harness.IMAGE_BASE, harness.pe_header(0x6AC07E31, harness.BUILDS["6AC07E31"]["size"])))
    mem = harness.MockMem.create(path, regions, mods)
    if log_path:
        with open(log_path, "a", encoding="utf-8") as f:
            f.write("2026-10-13 21:00:00.000  Turbo GUI loading in process\n")
            f.write("2026-10-13 21:00:01.000  channel E: service located by the registry Find at 0x%X (Find returned 0x%X)\n" % (svc, svc))
            f.write("2026-10-13 21:00:02.000  channel E: team %d record 0 approach 0->0 line height 25->25 build-up 0->0 (read back ok)\n"
                    % MOCK_TEAM)
    return mem, svc


def mock_reader(path):
    """read(pairs) over a MockMem file"""
    mem = harness.MockMem(path)

    def read(pairs):
        if not pairs:
            return []
        r = mem.call({"op": "multi", "ops": [{"op": "bytes", "addr": H(a), "len": n} for a, n in pairs]})
        return [bytes.fromhex(x["hex"]) if x.get("ok") else None for x in r.get("results", [])]
    return read
