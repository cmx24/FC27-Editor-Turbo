"""FC 27 Editor Turbo - MATCH MEASURE (proof harness, tools/; read-only). From the sweep's T4 v2 copy (2026-10-13) of
TOOLS/match_measure.py: --marker FILE (window label per sample -> column win), ball velocity columns, --clock-addr HEX (optional f32
read per sample -> column clock), achieved cadence and strata in summary.json, --replay DIR (offline re-summary, no game needed).
WP-C changes: dev2 from this folder (the gate-fixed client), paths through harness.py (env-overridable), GAME_STATE gate for every
live command, positions.csv written per sample (a killed run keeps its data), --run-dir (exact output folder), no player ids in the
code (--team-pids / MM_TEAM_PIDS, only route B needs them), --mock (synthetic 22-player engine over a harness.MockMem knob: the
offline proof of x1_run.sh), a VERDICT line for --check / --probe, and a time box (--budget).
Everything else is the original route A/B/C code.

During a running match it samples the pitch positions of all 22 players (+ ball and ball carrier) about once per second
and reports per team: back-line height, team length, width, compactness, pressing distance, possession / territory.
Memory access only through dev2 (the Turbo dev service, read-only ops: modules, find, bytes, multi).

Usage (game in a running match, not paused):
  python match_measure.py --check -v --label baseline   live verdict: locate + liveness + 20 s measure, PASS / FAIL
  python match_measure.py                       60 s at 1 Hz, label "run"
  python match_measure.py --label A --seconds 60
  python match_measure.py --locate              locate + validate only (prints the chain and the 22 players)
  python match_measure.py --compare DIR_A DIR_B  A/B deltas of two runs (or their summary.json files)
  python match_measure.py --probe               one short read-only run (<= ~60 s of dev calls) that prints every
                                                candidate root (C: G1, A: S0) with player counts + two position reads 1 s
                                                apart, and names the root that works (exit 0) or none (exit 1)
Output: turbo_output\\match_measure\\<yyyymmdd-hhmmss>_<label>\\ positions.csv (raw), summary.json, report.txt

Routes are tried in the order A (S0), C (G1), B (heap scan).
Route C (normal matches, static analysis 2026-10-13, turbo_output\\MATCH_POSITIONS_STATIC.txt): one image global G1
holds the match object M (made with the gameplay match, 0 outside it; ~310 gameplay code sites read it). From M:
  T = [M+0xA10] player registry: T+0x3D8..+0x3E0 vector<Player*> of the playing players (the game's own "closest
      player to a position" walks it), T+0x58 + handle*8 = Player* (handle < 0x38), T+0x38 -> team objects -> per-team
      player vectors (GetGoalkeeper walks them). Player layout = route A's (info +0x20, position via pos_getter).
  field F = [M+fptr] (route A), ball = [M+0x510 + i*8] (i < 4) -> same ring as route A, possession record R = [M+0xFB8]
  (the M field that the script context copies to P+pobj). All offsets come from code signatures (CSIGS), cached per build.
Route B (normal matches, 2026-10-13): the root slot S0 below is a shared_ptr set only by the gameplay-SCRIPT runner,
so it is empty in a normal match; then the 22 on-field FifaPlayerData objects (reflected HUD data DB type, size 0x260)
are found on the heap: a starter's PlayerId at +0x1BC -> object -> its vtable -> all instances (+0x50 Position,
+0xCC TeamId, +0x12C IsOnField, +0x1A8 DistanceToBall, +0x248 IsTeamOnOffense). Pitch size assumed 105 x 68 on route B.
Route A - how the match is found (docs: turbo_output\\MATCH_MEASURE.txt): the game's own gameplay Lua bindings
(lxGetPlayerPos / lxGetBallPos / lxGetBallPossessorId / lxGetFieldHalfLength) read everything from ONE static,
the gameplay root slot S0. The code of those bindings is located by signature in the running FC27.exe image,
the struct offsets are read out of the matched instructions (so a title update that moves fields is followed),
and the result is cached per build in turbo_output\\match_measure_layout.json.
  P = [S0]; W = [[P]]           (gameplay world)
  players: EASTL map<int handle, Player*> at W+map_off (anchor; root at +0x10; node right +0, left +8, key +0x20,
           value +0x28); handle = team * 25 + index (lxGetPlayerId)
  Player: +0x20 -> info I (I+0x0C team index 0/1, I+0x10 field position 0 = GK ..);
          position (vec4 x, y, z, w): flag byte Player+pl_flag == 0 -> [[[Player+pl_state]]] + st_pos, else Player+pl_altpos
  ball:   map<int, Ball*> at W+ball_map (key = ball id - 1000; ball 0 = the match ball);
          H = [[Ball+0x78]+0x238]: ring of 600 x 64-byte records at H (record +0x10 position, +0x30 velocity), frame
          counter at H+0x9610 (current record = frame % 600)
  carrier: R = [P+0x80]; buffer = [R+0x20974] (0/1); handle = [R + buffer*0x104B0 + 1000*0x7E0 - 0x1E1640]
  pitch:  F = [[P+0x40]+0xA50]; F+0x10 length, F+0x14 width (metres)
"""
import argparse, csv, json, math, os, random, statistics, struct, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)  # dev2.py (gate-fixed) and harness.py live next to this file; dev2 is imported lazily
import harness  # noqa: E402
dev2 = None


def _dev2():
    global dev2
    if dev2 is None:
        import dev2 as _d  # noqa
        dev2 = _d
    return dev2

OUT_ROOT = os.path.join(harness.TO, "match_measure")
CACHE = os.path.join(harness.TO, "match_measure_layout.json")
SCALE = 1.0  # --mock only: real seconds per simulated second (X1_TIME_SCALE); 1.0 live


def _now():
    return time.time() if SCALE == 1.0 else _T0 + (time.time() - _T0) / SCALE


def _sleep(sec):
    time.sleep(max(0.0, sec) * SCALE)


_T0 = time.time()

# ---------------------------------------------------------------- signatures (FC27.exe image, dev "find" syntax)
# fields: name -> (offset in the match, kind); kinds: rip = rip-relative disp32 (next ip = offset + 4), rel = call
# target (same), u8 / u32 / i32 = immediate or displacement read as is
SIGS = {
    # lxGetPlayerPos impl: mov rax,[S0]; mov rcx,[rax]; mov rcx,[rcx]; call map_lookup; ...; call pos_getter
    "player_pos": {
        "pattern": "48 83 EC 38 48 8B 05 ?? ?? ?? ?? 4C 8B D9 48 8B 08 48 8B 09 E8 ?? ?? ?? ?? 48 85 C0 74 ?? "
                   "48 8D 54 24 20 48 8B C8 E8 ?? ?? ?? ??",
        "fields": {"S0": (7, "rip"), "map_lookup": (21, "rel"), "pos_getter": (39, "rel")},
    },
    # lxGetBallPos impl: ball = ball_lookup(W, id - 1000); H = [[ball+bh1]+bh2]; frame = [H+frame] % ring; pos = H + rec*64 + rpos
    "ball_pos": {
        "pattern": "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 4C 8B D9 81 C2 18 FC FF FF 48 8B 08 48 8B 09 E8 ?? ?? ?? ?? "
                   "48 85 C0 74 ?? 48 8B 40 ?? 4C 8B 88 ?? ?? ?? ?? B8 ?? ?? ?? ?? 45 8B 81 ?? ?? ?? ?? 41 F7 E8 "
                   "C1 FA ?? 8B C2 C1 E8 1F 03 D0 69 C2 ?? ?? ?? ?? 44 2B C0 49 63 C0 48 C1 E0 ?? C4 A1 78 10 4C 08 ??",
        "fields": {"S0_ball": (7, "rip"), "ball_lookup": (27, "rel"), "bh1": (39, "u8"), "bh2": (43, "u32"),
                   "bframe": (55, "u32"), "ring": (74, "u32"), "rshift": (87, "u8"), "rpos": (94, "u8")},
    },
    # lxGetBallPossessorId impl: R = [P+pobj]; b = [R+pbuf]; out = [R + b*psize + id*pstride + poff]
    "possessor": {
        "pattern": "48 8B C1 48 8B 0D ?? ?? ?? ?? 4C 8B 89 ?? ?? ?? ?? 49 63 89 ?? ?? ?? ?? 4C 69 C1 ?? ?? ?? ?? "
                   "48 63 CA 48 69 D1 ?? ?? ?? ?? 4B 8D 0C 01 8B 94 0A ?? ?? ?? ?? 89 10 C3",
        "fields": {"S0_poss": (6, "rip"), "pobj": (13, "u32"), "pbuf": (20, "u32"), "psize": (27, "u32"),
                   "pstride": (37, "u32"), "poff": (48, "i32")},
    },
    # lxGetFieldHalfLength impl: F = [[P+fobj]+fptr]; length = F+flen (x 0.5)
    "field_len": {
        "pattern": "48 8B 05 ?? ?? ?? ?? 48 8B 50 ?? 48 8B 82 ?? ?? ?? ?? C5 FA 10 40 ?? 48 8B 41 10 C5 FA 59 05 ?? ?? ?? ?? "
                   "C5 FA 5A C8 C5 FB 11 08 C7 40 08 03 00 00 00 B8 01 00 00 00 48 83 41 10 10 C3",
        "fields": {"S0_field": (3, "rip"), "fobj": (10, "u8"), "fptr": (14, "u32"), "flen": (22, "u8")},
        "multi_ok": True,  # length and width getters share this shape (flen 0x10 / 0x14): the smallest flen wins
    },
}
# checked at the call targets of player_pos (not searched)
TARGET_SIGS = {
    "pos_getter": {
        "pattern": "48 83 EC 18 80 B9 ?? ?? ?? ?? 00 75 ?? 48 8B 81 ?? ?? ?? ?? 48 8B 08 48 8B 01 C5 F8 10 80 ?? ?? ?? ?? "
                   "C5 FA 7F 04 24 48 8D 04 24 C5 F8 10 00 C5 F9 7F 02 48 8B C2 48 83 C4 18 C3 C5 F8 10 81 ?? ?? ?? ??",
        "fields": {"pl_flag": (6, "u32"), "pl_state": (16, "u32"), "st_pos": (30, "u32"), "pl_altpos": (63, "u32")},
    },
    "map_lookup": {  # lea r9,[rcx+map_off] (disp8 form; disp32 form 4C 8D 89 handled below)
        "pattern": "4C 8D 49 ?? 4D 8B 51 10 4D 8B C1 4D 85 D2 74 ?? 41 39 52 20",
        "fields": {"map_off": (3, "u8")},
        "alt": {"pattern": "4C 8D 89 ?? ?? ?? ?? 4D 8B 51 10 4D 8B C1 4D 85 D2 74 ?? 41 39 52 20",
                "fields": {"map_off": (3, "u32")}},
    },
    "ball_lookup": {
        "pattern": "4C 8D 89 ?? ?? ?? ?? 4D 8B 51 10 4D 8B C1 4D 85 D2 74 ?? 41 39 52 20",
        "fields": {"ball_map": (3, "u32")},
        "alt": {"pattern": "4C 8D 49 ?? 4D 8B 51 10 4D 8B C1 4D 85 D2 74 ?? 41 39 52 20", "fields": {"ball_map": (3, "u8")}},
    },
}
# not taken from code (validated live instead: 11 + 11 players, one field position 0 per team)
FIXED = {"pl_info": 0x20, "info_team": 0x0C, "info_fpos": 0x10, "node_key": 0x20, "node_val": 0x28,
         "handles_per_team": 25, "pl_pid": None}


class DevError(RuntimeError):
    pass


def call(req, timeout=60):
    r = _dev2().call(req, timeout=timeout)
    if r.get("error") and not r.get("ok") and "results" not in r:
        raise DevError(str(r.get("error")))
    return r


def H(a):
    return "0x%X" % a


def mbytes(pairs, timeout=60):
    """[(addr, len)] -> [bytes | None] (one dev call per 200)"""
    out = []
    for i in range(0, len(pairs), 200):
        ch = pairs[i:i + 200]
        r = call({"op": "multi", "ops": [{"op": "bytes", "addr": H(a), "len": n} for a, n in ch]}, timeout)
        for x in r.get("results", []):
            out.append(bytes.fromhex(x["hex"]) if x.get("ok") else None)
    return out


def rd(addr, n):
    return mbytes([(addr, n)])[0]


def u64(b, o=0):
    return struct.unpack_from("<Q", b, o)[0]


def ptr_ok(v):
    return 0x10000 <= v < 0x7FFFFFFFFFFF


def pat_match(pattern, data):
    toks = pattern.split()
    if len(data) < len(toks):
        return False
    return all(t == "??" or int(t, 16) == data[i] for i, t in enumerate(toks))


def extract(fields, at, data):
    out = {}
    for name, (off, kind) in fields.items():
        if kind in ("rip", "rel"):
            out[name] = at + off + 4 + struct.unpack_from("<i", data, off)[0]
        elif kind == "u8":
            out[name] = data[off]
        elif kind == "u32":
            out[name] = struct.unpack_from("<I", data, off)[0]
        elif kind == "i32":
            out[name] = struct.unpack_from("<i", data, off)[0]
    return out


# ---------------------------------------------------------------- build + layout
def game_module():
    r = call({"op": "modules"}, 90)
    for m in r.get("modules", []):
        if m["name"].lower() == "fc27.exe":
            return int(m["base"], 16), int(m["size"])
    raise DevError("FC27.exe not in the module list")


def build_key(base, size):
    hdr = rd(base, 0x400)
    e = struct.unpack_from("<I", hdr, 0x3C)[0]
    ts = struct.unpack_from("<I", hdr, e + 8)[0]
    return "%08X-%08X" % (ts, size)


def resolve_layout(base, size, log=print):
    """signature scan of the running image -> layout dict (raises when something does not fit)"""
    lay = {}
    for name, s in SIGS.items():
        t0 = time.time()
        r = call({"op": "find", "pattern": s["pattern"], "max": 8, "from": H(base), "to": H(base + size)}, 120)
        hits = [int(h, 16) for h in r.get("hits", [])]
        log("  sig %-10s %d hit(s) %.1fs %s" % (name, len(hits), time.time() - t0, " ".join(H(h) for h in hits[:4])))
        if not hits:
            raise DevError("signature %s: no match" % name)
        if len(hits) > 1 and not s.get("multi_ok"):
            raise DevError("signature %s: %d matches" % (name, len(hits)))
        n = len(s["pattern"].split())
        cands = []
        for h, b in zip(hits, mbytes([(h, n) for h in hits])):
            cands.append((h, extract(s["fields"], h, b)))
        if name == "field_len":
            cands.sort(key=lambda c: c[1]["flen"])
        at, f = cands[0]
        lay[name + "_at"] = at
        lay.update(f)
    for name, s in TARGET_SIGS.items():
        at = lay[name]
        n = max(len(s["pattern"].split()), len(s.get("alt", {}).get("pattern", "").split()))
        b = rd(at, n + 8)
        if pat_match(s["pattern"], b):
            lay.update(extract(s["fields"], at, b))
        elif "alt" in s and pat_match(s["alt"]["pattern"], b):
            lay.update(extract(s["alt"]["fields"], at, b))
        else:
            raise DevError("code at %s (%s) is not the expected shape: %s" % (H(at), name, b[:24].hex(" ")))
    if not (lay["S0"] == lay["S0_ball"] == lay["S0_poss"] == lay["S0_field"]):
        raise DevError("the four bindings read different root slots: %s" % [H(lay[k]) for k in ("S0", "S0_ball", "S0_poss", "S0_field")])
    lay.update(FIXED)
    lay["S0_rva"] = lay["S0"] - base
    return lay


def load_layout(relocate=False, log=print):
    base, size = game_module()
    key = build_key(base, size)
    cache = {}
    try:
        cache = json.load(open(CACHE))
    except Exception:
        pass
    if not relocate and key in cache and cache[key].get("base") == base and "S0" in cache[key]:
        lay = dict(cache[key])
        lay["img_size"] = size
        return key, base, lay
    if not relocate and key in cache and "S0_rva" in cache[key]:
        lay = dict(cache[key])
        lay["S0"] = base + lay["S0_rva"]
        lay["base"] = base
        lay["img_size"] = size
        return key, base, lay
    log("locating the match code on build %s (signature scan, once per build)" % key)
    lay = resolve_layout(base, size, log)
    lay["base"] = base
    lay["img_size"] = size
    keep = dict(lay)
    keep["pl_pid"] = cache.get(key, {}).get("pl_pid", lay.get("pl_pid"))
    if cache.get(key, {}).get("fpd_vtable_rva"):
        keep["fpd_vtable_rva"] = cache[key]["fpd_vtable_rva"]
        keep["fpd_key_off"] = cache[key].get("fpd_key_off", 0)
    if not relocate:  # route C layout of this build (ensure_route_c re-scans it on --relocate)
        keep.update({k: v for k, v in cache.get(key, {}).items() if k.startswith("c_")})
    cache[key] = keep
    try:
        json.dump(cache, open(CACHE, "w"), indent=1)
    except OSError:
        pass
    return key, base, keep


# ---------------------------------------------------------------- match objects
def walk_map(anchor, limit=200):
    """EASTL rbtree at `anchor`: [(key, value)] (BFS, one dev call per tree level)"""
    root = rd(anchor + 0x10, 8)
    if root is None:
        raise DevError("map anchor unreadable")
    level = [u64(root)] if ptr_ok(u64(root)) else []
    out, seen = [], set()
    while level and len(out) < limit:
        level = [n for n in level if n not in seen and n != anchor]
        seen.update(level)
        nxt = []
        for n, b in zip(level, mbytes([(n, 0x30) for n in level])):
            if b is None:
                continue
            right, left = u64(b, 0), u64(b, 8)
            out.append((struct.unpack_from("<i", b, 0x20)[0], u64(b, 0x28)))
            for c in (right, left):
                if ptr_ok(c) and c != anchor:
                    nxt.append(c)
        level = nxt
    return sorted(out)


class Match:
    """resolved match: per player the addresses to sample"""

    def __init__(self, lay):
        self.lay = lay

    def locate(self, want_pid=True):
        L = self.lay
        P = u64(rd(L["S0"], 8))
        if not ptr_ok(P):
            raise DevError("gameplay root slot is empty (no match running?)")
        A = u64(rd(P, 8))
        W = u64(rd(A, 8)) if ptr_ok(A) else 0
        if not ptr_ok(W):
            raise DevError("gameplay world not set (no match running?)")
        self.P, self.W = P, W
        nodes = walk_map(W + L["map_off"])
        players = []
        objs = [v for k, v in nodes]
        hdr = mbytes([(o, 0x100) for o in objs])
        infos = mbytes([(u64(h, L["pl_info"]), 0x40) if h else (0, 1) for h in hdr])
        st1 = mbytes([(u64(h, L["pl_state"]), 8) if h else (0, 1) for h in hdr])
        st2 = mbytes([(u64(b), 8) if b else (0, 1) for b in st1])
        for (k, o), h, inf, b2 in zip(nodes, hdr, infos, st2):
            if h is None or inf is None or b2 is None:
                continue
            S = u64(b2)
            players.append({"handle": k, "obj": o, "info": u64(h, L["pl_info"]),
                            "team": struct.unpack_from("<i", inf, L["info_team"])[0],
                            "fpos": struct.unpack_from("<i", inf, L["info_fpos"])[0],
                            "state": S, "pid": None})
        if L.get("pl_pid") is not None:
            kind, off = L["pl_pid"]
            src = mbytes([((p["info"] if kind == "info" else p["obj"]) + off, 4) for p in players])
            for p, b in zip(players, src):
                if b:
                    p["pid"] = struct.unpack_from("<i", b)[0]
        self.players = players
        # pitch, ball, carrier
        fb = rd(u64(rd(P + L["fobj"], 8)) + L["fptr"], 8)
        F = u64(fb)
        dims = rd(F + L["flen"], 8)
        self.length, self.width = struct.unpack("<2f", dims)
        anchor = W + L["ball_map"]
        balls = walk_map(anchor, 16)
        self.ball = None
        for k, v in balls:
            if k == 0:
                b1 = u64(rd(v + L["bh1"], 8))
                self.ball = u64(rd(b1 + L["bh2"], 8))
        self.R = u64(rd(P + L["pobj"], 8))
        return self

    def sample_ops(self):
        L = self.lay
        ops = []
        self._clock_slot = None
        for p in self.players:
            ops.append((p["obj"] + L["pl_altpos"], (L["pl_flag"] - L["pl_altpos"]) + 1))
            ops.append((p["state"] + L["st_pos"], 16))
        if self.ball:
            ops.append((self.ball, L["bframe"] + 4))  # the whole ring of records + the frame counter after it
        if ptr_ok(self.R or 0):
            ops.append((self.R + L["pbuf"], 4))
            base_poss = L["pstride"] * 1000 + L["poff"]
            ops.append((self.R + base_poss, 4))
            ops.append((self.R + L["psize"] + base_poss, 4))
        if getattr(self, "clock_addr", None):
            self._clock_slot = len(ops)
            ops.append((self.clock_addr, 4))
        return ops

    def read_sample(self):
        L = self.lay
        ops = self.sample_ops()
        res = mbytes(ops, 30)
        t = time.time()
        out = []
        i = 0
        for p in self.players:
            alt, st = res[i], res[i + 1]
            i += 2
            pos = None
            if alt is not None and alt[-1] != 0:
                pos = struct.unpack_from("<3f", alt, 0)
            elif st is not None:
                pos = struct.unpack_from("<3f", st, 0)
            out.append(pos)
        ball = None
        if self.ball:
            rb = res[i]
            i += 1
            if rb:
                frame = struct.unpack_from("<i", rb, L["bframe"])[0]
                rec = ((frame % L["ring"]) << L["rshift"]) + L["rpos"]
                ball = struct.unpack_from("<3f", rb, rec) if 0 <= rec and rec + 12 <= L["bframe"] else None
                # v2: velocity record field (+0x30 in the 64-byte record, position is +0x10 = rpos)
                vrec = rec + 0x20
                self.last_ball_vel = struct.unpack_from("<3f", rb, vrec) if ball is not None and vrec + 12 <= L["bframe"] else None
        carrier = None
        if ptr_ok(self.R or 0):
            buf_b, p0, p1 = res[i], res[i + 1], res[i + 2]
            if buf_b and p0 and p1:
                bsel = struct.unpack("<i", buf_b)[0]
                carrier = struct.unpack("<i", p0 if bsel == 0 else p1)[0]
        self.last_clock = None
        if getattr(self, "_clock_slot", None) is not None and res[self._clock_slot]:
            self.last_clock = struct.unpack("<f", res[self._clock_slot])[0]
        k = getattr(self, "scale", 1.0)  # route C: the match engine works in feet (see MatchC.locate)
        if k != 1.0:
            out = [tuple(c * k for c in q) if q else q for q in out]
            ball = tuple(c * k for c in ball) if ball else ball
            if getattr(self, "last_ball_vel", None):
                self.last_ball_vel = tuple(c * k for c in self.last_ball_vel)
        return t, out, ball, carrier


# ---------------------------------------------------------------- route C: the match object global G1 (normal matches)
# Static analysis 2026-10-13 of fc27_image.bin (build 6AB9813C), see turbo_output\MATCH_POSITIONS_STATIC.txt.
# Signature syntax: dev "find" bytes, ?? = any byte, <name:kind> = a field read out of the match (kind rip / rel = absolute
# target of a disp32, u8 / u32 / i32 = immediate or displacement as is). All of them matched exactly once in the old image.
CSIGS = {
    # tiny T+0x38 reader: mov rax,[G1]; mov eax,ecx; mov rax,[rax+t_off]; mov rax,[rax+t_teams]; mov rcx,[rax+rcx*8]
    "c_team": "48 83 EC 28 48 8B 05 <c_G1:rip> 8B C9 48 8B 80 <c_t_off:u32> 48 8B 40 <c_t_teams:u8> 48 8B 0C C8 "
              "4C 8B 41 60 E8",
    # player-slot reader: rcx=[G1]; jz; rcx=[rcx+t_off]; jz; cmp r8d,cnt; jae; rax=[rcx+r8*8+arr]   (2nd source of G1)
    "c_slot": "48 8B 0D <c_G1b:rip> 44 8B F2 ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 48 8B 89 "
              "<c_t_off_b:u32> 48 85 C9 0F 84 ?? ?? ?? ?? 41 83 F8 <c_t_cnt_b:u8> 0F 83 ?? ?? ?? ?? 49 63 C0 48 8B 44 C1 "
              "<c_t_arr_b:u8>",
    # T::GetPlayer(handle): cmp edx,cnt; jae; movsxd rax,edx; mov rax,[rcx+rax*8+arr]; ret
    "c_get": "83 FA <c_t_cnt:u8> 73 09 48 63 C2 48 8B 44 C1 <c_t_arr:u8> C3",
    # Lua "closest player from pos" impl: M=[[S0]+fobj]; T=[M+t_off]; walks the Player* vector [T+vbeg] .. [T+vend]
    "c_vec": "48 8B 05 <c_S0:rip> 4C 8B D1 45 33 C9 48 8B 48 <c_fobj:u8> 48 8B 81 <c_t_off_c:u32> 48 8B 98 <c_t_vend:u32> "
             "4C 8B 98 <c_t_vbeg:u32> 4C 3B DB",
    # ball-ring user: i=[X+cur] (< nballs); Ball=[[X+0x2D0]+i*8+balls] ([X+0x2D0] = M); H=[[Ball+bh1]+bh2]; frame=[H+bframe]
    "c_ball": "8B 83 ?? ?? ?? ?? 83 F8 <c_b_cnt:u8> 0F 83 ?? ?? ?? ?? 8B C8 48 8B 83 ?? ?? ?? ?? 4C 8B B4 C8 "
              "<c_m_balls:u32> 49 8B 46 <c_bh1:u8> 41 83 CA FF 4C 8B 80 <c_bh2:u32> 45 8D 4A 02 B8 ?? ?? ?? ?? 41 8B 88 "
              "<c_bframe:u32>",
    # Lua team walk: team=[[T+t_teams]+i*8]; players = vector at [team+tm_cont] + [team+tm_idx]*stride + tm_vec
    "c_tvec": "48 8B 14 C8 48 63 42 <c_tm_idx:u8> 4C 8B 72 <c_tm_cont:u8> 4C 69 E8 <c_tm_stride:u32> E8 ?? ?? ?? ?? "
              "4B 8B BC 2E <c_tm_vec:u32>",
    # GetGoalkeeper(team): walks the same team vector, returns the player whose info+fpos == 0
    "c_gk": "48 63 C2 48 69 D0 <c_tm_stride_g:u32> 4C 8B 84 0A <c_tm_vec_g:u32> 4C 8B 8C 0A ?? ?? ?? ?? 4D 3B C1 74 ?? "
            "49 8B 08 48 8B 41 <c_pl_info:u8> 83 78 <c_info_fpos:u8> 00 74 ?? 49 83 C0 08 EB",
    # gameplay-script context ctor: P+X <- [M+pm_X] (stores at obj+X+0x10); P+pobj (route A carrier record) <- M+?
    "c_ctx": "4C 89 41 50 49 8B 80 <c_pm_48:u32> 48 89 41 58 49 8B 80 <c_pm_50:u32> 48 89 41 60 49 8B 80 <c_pm_58:u32> "
             "48 89 41 68 49 8B 80 <c_pm_60:u32> 48 89 41 70 49 8B 80 <c_pm_68:u32> 48 89 41 78 49 8B 80 <c_pm_70:u32> "
             "48 89 81 80 00 00 00 49 8B 80 <c_pm_78:u32> 48 89 81 88 00 00 00 49 8B 80 <c_pm_80:u32> "
             "48 89 81 90 00 00 00 49 8B 80 <c_pm_88:u32> 48 89 81 98 00 00 00",
}
C_KINDS = {"rip": 4, "rel": 4, "u8": 1, "u32": 4, "i32": 4}


def sig_expand(spec):
    """'.. <name:kind> ..' -> (dev find pattern, {name: (offset, kind)})"""
    out, fields = [], {}
    for t in spec.split():
        if t.startswith("<"):
            name, kind = t[1:-1].split(":")
            fields[name] = (len(out), kind)
            out += ["??"] * C_KINDS[kind]
        else:
            out.append(t)
    return " ".join(out), fields


def code_range(base, size):
    """the largest executable, non-writable section of FC27.exe (.text1 on 6AB9813C, where all the signatures live; the
    packer's writable+executable .data is skipped); the whole image if unreadable"""
    try:
        hdr = rd(base, 0x1000)
        e = struct.unpack_from("<I", hdr, 0x3C)[0]
        nsec, osz = struct.unpack_from("<H", hdr, e + 6)[0], struct.unpack_from("<H", hdr, e + 20)[0]
        best = None
        for k in range(nsec):
            s = e + 24 + osz + k * 40
            vsz, va = struct.unpack_from("<II", hdr, s + 8)
            ch = struct.unpack_from("<I", hdr, s + 36)[0]
            if ch & 0x20000000 and not ch & 0x80000000 and (best is None or vsz > best[1]):
                best = (va, vsz)
        if best and best[1] > 0x100000:
            return base + best[0], base + best[0] + best[1]
    except (DevError, TypeError, struct.error):
        pass
    return base, base + size


def scan_sig(spec, lo, hi, mx=8):
    """[(address, {field: value})] of the code matches of one CSIGS entry"""
    pat, fields = sig_expand(spec)
    r = call({"op": "find", "pattern": pat, "max": mx, "from": H(lo), "to": H(hi)}, 120)
    hits = [int(h, 16) for h in r.get("hits", [])]
    n = len(pat.split())
    out = []
    for h, b in zip(hits, mbytes([(h, n) for h in hits]) if hits else []):
        if b is not None and pat_match(pat, b):
            out.append((h, extract(fields, h, b)))
    return out


def resolve_route_c(base, size, lay, log=print):
    """scan CSIGS -> route C layout (rva of G1 + offsets). Raises DevError when G1 or the registry offset is not found."""
    lo, hi = code_range(base, size)
    got = {}
    for name, spec in CSIGS.items():
        t0 = time.time()
        res = scan_sig(spec, lo, hi)
        if not res and (lo, hi) != (base, base + size):
            res = scan_sig(spec, base, base + size)
        log("  sig %-7s %d hit(s) %.1fs %s" % (name, len(res), time.time() - t0, " ".join(H(h) for h, _ in res[:3])))
        got[name] = [f for _, f in res]

    def one(name):
        return got[name][0] if len(got.get(name) or []) == 1 else None
    team, slot, vec, get = one("c_team"), one("c_slot"), one("c_vec"), None
    g1s = sorted(set([x["c_G1"] for x in [team] if x] + [x["c_G1b"] for x in [slot] if x]))
    if not g1s:
        raise DevError("route C: no code signature for the match object global matched (c_team, c_slot)")
    if len(g1s) > 1:
        raise DevError("route C: the two G1 signatures disagree: %s" % [H(g) for g in g1s])
    votes = [x for x in (team and team["c_t_off"], slot and slot["c_t_off_b"], vec and vec["c_t_off_c"]) if x]
    t_off = max(set(votes), key=votes.count)
    c = {"c_G1_rva": g1s[0] - base, "c_t_off": t_off, "c_t_arr": None, "c_t_cnt": None, "c_t_vbeg": None,
         "c_t_vend": None, "c_t_teams": None, "c_tm_idx": None, "c_tm_cont": None, "c_tm_stride": None, "c_tm_vec": None,
         "c_m_balls": None, "c_b_cnt": None, "c_m_poss": None, "c_m_field": lay.get("fptr"),
         "c_pl_info": lay.get("pl_info", 0x20), "c_info_fpos": lay.get("info_fpos", 0x10), "c_sig_hits": {}}
    if slot:
        c["c_t_arr"], c["c_t_cnt"] = slot["c_t_arr_b"], slot["c_t_cnt_b"]
    else:
        gets = got.get("c_get") or []
        if len(gets) == 1:
            get = gets[0]
            c["c_t_arr"], c["c_t_cnt"] = get["c_t_arr"], get["c_t_cnt"]
    if vec and vec["c_t_off_c"] == t_off:
        c["c_t_vbeg"], c["c_t_vend"] = vec["c_t_vbeg"], vec["c_t_vend"]
    tv = one("c_tvec")
    if team and tv:
        c["c_t_teams"] = team["c_t_teams"]
        c.update({k: tv[k] for k in ("c_tm_idx", "c_tm_cont", "c_tm_stride", "c_tm_vec")})
    gk = one("c_gk")
    if gk:
        c["c_pl_info"], c["c_info_fpos"] = gk["c_pl_info"], gk["c_info_fpos"]
    ball = one("c_ball")
    if ball and 0 < ball["c_b_cnt"] <= 16:
        c["c_m_balls"], c["c_b_cnt"] = ball["c_m_balls"], ball["c_b_cnt"]
        c["c_bh1"], c["c_bh2"], c["c_bframe"] = ball["c_bh1"], ball["c_bh2"], ball["c_bframe"]
    ctx = one("c_ctx")
    if ctx and lay.get("pobj") is not None and ("c_pm_%02x" % lay["pobj"]) in ctx:
        c["c_m_poss"] = ctx["c_pm_%02x" % lay["pobj"]]
    if ctx and lay.get("fobj") is not None and lay["fobj"] != 0x40:
        log("  note: route A fobj is 0x%X (expected 0x40 = M); route C does not depend on it" % lay["fobj"])
    c["c_sig_hits"] = {k: len(v) for k, v in got.items()}
    return c


def ensure_route_c(key, base, size, lay, relocate=False, log=print):
    """route C layout into lay (cached per build in match_measure_layout.json)"""
    if lay.get("c_G1_rva") and not (relocate and not lay.get("_c_fresh")):
        lay["c_G1"] = base + lay["c_G1_rva"]
        return lay
    log("locating the match object global on build %s (route C signatures, once per build)" % key)
    c = resolve_route_c(base, size, lay, log)
    lay.update(c)
    lay["c_G1"] = base + c["c_G1_rva"]
    lay["_c_fresh"] = True
    try:
        cache = json.load(open(CACHE))
    except Exception:
        cache = {}
    cache.setdefault(key, {}).update(c)
    try:
        json.dump(cache, open(CACHE, "w"), indent=1)
    except OSError:
        pass
    return lay


def i32(b, o=0):
    return struct.unpack_from("<i", b, o)[0]


class MatchC(Match):
    """route C: G1 -> match object M -> player registry T (playing-players vector, team vectors or handle slots)"""

    def __init__(self, lay, log=print):
        Match.__init__(self, lay)
        self.log = log
        self.route = "C (match object global G1)"
        self.source, self.pitch_read, self.ball_index = None, False, None

    def m_read(self):
        L = self.lay
        M = u64(rd(L["c_G1"], 8))
        if not ptr_ok(M):
            raise DevError("route C: the match object global is empty (no match running?)")
        offs = [L["c_t_off"], L.get("c_m_field") or 0, L.get("c_m_poss") or 0,
                (L.get("c_m_balls") or 0) + 8 * (L.get("c_b_cnt") or 0)]
        mb = rd(M, max(offs) + 8)
        if mb is None:
            raise DevError("route C: match object %s unreadable" % H(M))
        return M, mb

    def t_span(self):
        L = self.lay
        offs = [0x40]
        if L.get("c_t_arr") is not None:
            offs.append(L["c_t_arr"] + 8 * L["c_t_cnt"])
        if L.get("c_t_vend") is not None:
            offs.append(max(L["c_t_vbeg"], L["c_t_vend"]) + 8)
        if L.get("c_t_teams") is not None:
            offs.append(L["c_t_teams"] + 8)
        return max(offs)

    def sources(self, T, tb):
        """{name: [Player*]}: every list of players the registry keeps"""
        L = self.lay
        out = {}

        def vec(b, e, cap=64):
            n = (e - b) // 8 if ptr_ok(b) and e >= b and (e - b) % 8 == 0 else -1
            if not 0 < n <= cap:
                return []
            v = rd(b, n * 8)
            return [u64(v, 8 * i) for i in range(n)] if v else []
        if L.get("c_t_vbeg") is not None:
            out["vector"] = vec(u64(tb, L["c_t_vbeg"]), u64(tb, L["c_t_vend"]))
        if L.get("c_t_teams") is not None and L.get("c_tm_vec") is not None:
            lst = []
            arr = u64(tb, L["c_t_teams"])
            ta = rd(arr, 16) if ptr_ok(arr) else None
            for k in range(2 if ta else 0):
                tm = u64(ta, 8 * k)
                tmb = rd(tm, max(L["c_tm_idx"], L["c_tm_cont"]) + 8) if ptr_ok(tm) else None
                if not tmb:
                    continue
                cont, idx = u64(tmb, L["c_tm_cont"]), i32(tmb, L["c_tm_idx"])
                if not ptr_ok(cont) or not 0 <= idx < 8:
                    continue
                be = rd(cont + idx * L["c_tm_stride"] + L["c_tm_vec"], 16)
                if be:
                    lst += vec(u64(be, 0), u64(be, 8), 32)
            out["teams"] = lst
        if L.get("c_t_arr") is not None:
            v = tb[L["c_t_arr"]: L["c_t_arr"] + 8 * L["c_t_cnt"]]
            out["slots"] = [u64(v, 8 * i) for i in range(len(v) // 8) if ptr_ok(u64(v, 8 * i))]
        return out

    def players_from(self, ptrs):
        L = self.lay
        pi, ft, fp = L.get("c_pl_info", L["pl_info"]), L["info_team"], L.get("c_info_fpos", L["info_fpos"])
        ptrs = [p for p in dict.fromkeys(ptrs) if ptr_ok(p)]
        if not ptrs:
            return []
        hdr = mbytes([(o, 0x100) for o in ptrs])
        ok = lambda h, off: h is not None and ptr_ok(u64(h, off))
        infos = mbytes([(u64(h, pi), max(ft, fp) + 4) if ok(h, pi) else (0, 1) for h in hdr])
        st1 = mbytes([(u64(h, L["pl_state"]), 8) if ok(h, L["pl_state"]) else (0, 1) for h in hdr])
        st2 = mbytes([(u64(b), 8) if ok(b, 0) else (0, 1) for b in st1])
        players = []
        for o, h, inf, b2 in zip(ptrs, hdr, infos, st2):
            if h is None or inf is None:
                continue
            players.append({"handle": i32(inf, 0), "obj": o, "info": u64(h, pi), "team": i32(inf, ft),
                            "fpos": i32(inf, fp), "state": u64(b2) if b2 else 0, "pid": None})
        return players

    def locate(self):
        L = self.lay
        M, mb = self.m_read()
        T = u64(mb, L["c_t_off"])
        if not ptr_ok(T):
            raise DevError("route C: player registry M+0x%X is empty (match not built yet?)" % L["c_t_off"])
        tb = rd(T, self.t_span())
        if tb is None:
            raise DevError("route C: player registry %s unreadable" % H(T))
        srcs = self.sources(T, tb)
        best = None
        for name in ("vector", "teams", "slots"):
            if name not in srcs:
                continue
            pl = [p for p in self.players_from(srcs[name]) if p["team"] in (0, 1)]
            per = [sum(1 for p in pl if p["team"] == t) for t in (0, 1)]
            score = (per == [11, 11], min(per) > 0, -abs(len(pl) - 22))
            if best is None or score > best[0]:
                best = (score, name, pl)
            if per == [11, 11]:
                break
        if not best or not best[2]:
            raise DevError("route C: the player registry lists no team-0/1 players (%s)" % {k: len(v) for k, v in srcs.items()})
        self.source, self.players = best[1], best[2]
        self.M = self.P = M
        self.T = self.W = T
        # pitch (route A's field offsets: F = [M+fptr], F+flen length, +4 width)
        self.length, self.width, self.pitch_read = 105.0, 68.0, False
        self.scale = 1.0
        F = u64(mb, L["c_m_field"]) if L.get("c_m_field") else 0
        dims = rd(F + L["flen"], 8) if ptr_ok(F) else None
        if dims:
            ln, wd = struct.unpack("<2f", dims)
            if 60 <= ln <= 140 and 30 <= wd <= 100:
                self.length, self.width, self.pitch_read = ln, wd, True
            elif 200 <= ln <= 460 and 100 <= wd <= 330:
                # the match engine works in FEET: live 2026-10-13 the pitch read 344.48 x 223.09 = 105 x 68 m and players stood
                # up to |x| 170 (half the length); positions and pitch are converted to metres (read_sample applies self.scale)
                self.scale = 0.3048
                self.length, self.width, self.pitch_read = ln * 0.3048, wd * 0.3048, True
        per = [sum(1 for p in self.players if p["team"] == t) for t in (0, 1)]
        if max(per) > 11:  # a list with bench players: keep the ones standing on the pitch
            keep = trim_on_pitch(L, self.players, self.length / self.scale, self.width / self.scale)
            kper = [sum(1 for p in keep if p["team"] == t) for t in (0, 1)]
            if 0 < max(kper) <= 11:
                self.log("  route C: %s list %s -> %s players on the pitch" % (self.source, per, kper))
                self.players = keep
                self.source += "+on-pitch"
        # ball: the slot whose ring frame counter runs (else the first one set)
        self.ball, self.ball_index = None, None
        rings = []
        for i in range(L.get("c_b_cnt") or 0):
            bp = u64(mb, L["c_m_balls"] + 8 * i)
            b1 = rd(bp + L.get("c_bh1", L["bh1"]), 8) if ptr_ok(bp) else None
            b2 = rd(u64(b1) + L.get("c_bh2", L["bh2"]), 8) if b1 and ptr_ok(u64(b1)) else None
            if b2 and ptr_ok(u64(b2)):
                rings.append((i, u64(b2)))
        if len(rings) > 1:
            f1 = mbytes([(h + L["bframe"], 4) for _, h in rings])
            time.sleep(0.12)
            f2 = mbytes([(h + L["bframe"], 4) for _, h in rings])
            run = [r for r, a, b in zip(rings, f1, f2) if a and b and a != b]
            rings = run + [r for r in rings if r not in run]
        if rings:
            self.ball_index, self.ball = rings[0]
        mp = L.get("c_m_poss")
        self.R = u64(mb, mp) if mp else 0
        return self


def trim_on_pitch(lay, players, length, width):
    """the players standing on the pitch (one position read). Along-pitch axis from the two keepers (fpos 0), else the
    axis with the larger median |coordinate|"""
    pos = read_positions(lay, players)
    pts = [q for q in pos if q]
    if not pts:
        return players
    gk = {p["team"]: q for p, q in zip(players, pos) if q and p["fpos"] == 0}
    if 0 in gk and 1 in gk:
        along = 0 if abs(gk[0][0] - gk[1][0]) >= abs(gk[0][2] - gk[1][2]) else 2
    else:
        along = 0 if statistics.median(abs(q[0]) for q in pts) >= statistics.median(abs(q[2]) for q in pts) else 2
    lat = 2 - along
    return [p for p, q in zip(players, pos) if q and abs(q[along]) <= length / 2 + 2 and abs(q[lat]) <= width / 2 + 2]


def read_positions(lay, players):
    """one multi read: [(x, y, z) | None] per player (same rule as the game's pos_getter)"""
    ops = []
    for p in players:
        ops.append((p["obj"] + lay["pl_altpos"], (lay["pl_flag"] - lay["pl_altpos"]) + 1))
        ops.append((p["state"] + lay["st_pos"], 16) if ptr_ok(p.get("state") or 0) else (0, 1))
    res = mbytes(ops, 30) if ops else []
    out = []
    for k in range(len(players)):
        alt, st = res[2 * k], res[2 * k + 1]
        if alt is not None and alt[-1] != 0:
            out.append(struct.unpack_from("<3f", alt, 0))
        elif st is not None:
            out.append(struct.unpack_from("<3f", st, 0))
        else:
            out.append(None)
    return out


# ---------------------------------------------------------------- route B: FifaPlayerData (HUD data DB), heap discovery
# Used when the Lua root slot S0 is empty (normal matches: S0 is a shared_ptr that only the gameplay-script runner sets,
# static analysis 2026-10-13, see MATCH_MEASURE.txt). Offsets = reflected FC 27 type FifaPlayerData (size 0x260),
# TACTICS_MOD_CATALOGUE section 9; validated live by the --check verdict, not before.
FPD = {"vel": 0x30, "pos": 0x50, "team": 0xCC, "pid_ig": 0xD0, "on_field": 0x12C, "role": 0x188, "dist_ball": 0x1A8,
       "jersey": 0x1B4, "pid": 0x1BC, "defense": 0x246, "offense": 0x248, "size": 0x260}
SPANS = [(0x10000, 0x40000000), (0x40000000, 0x80000000), (0x80000000, 0xC0000000), (0xC0000000, 0x100000000),
         (0x100000000, 0x180000000), (0x180000000, 0x200000000), (0x200000000, 0x280000000), (0x280000000, 0x300000000),
         (0x300000000, 0x400000000), (0x400000000, 0x800000000000)]


def qhex(v):
    return " ".join("%02X" % x for x in struct.pack("<Q", v))


def find_writable(pattern, spans, mx, budget, log=print):
    """writable-memory pattern search; a span that hits the service time budget is split; stops at mx hits or budget s"""
    hits, todo, t0 = [], list(spans), time.time()
    while todo and time.time() - t0 < budget:
        a, b = todo.pop(0)
        r = _dev2().call({"op": "find", "pattern": pattern, "max": mx, "from": H(a), "to": H(b), "writable": True}, timeout=30)
        if r.get("error") and not r.get("ok"):
            log("  find %s..%s: %s" % (H(a), H(b), r.get("error")))
            continue
        if r.get("timed_out") and b - a > 0x1000000:
            m = (a + (b - a) // 2) & ~0xFFF
            todo[:0] = [(a, m), (m, b)]
            continue
        hits += [int(h, 16) for h in r.get("hits", [])]
        if len(hits) >= mx:
            break
    return sorted(set(hits)), time.time() - t0


def fpd_plausible(b):
    if not b or len(b) < FPD["size"]:
        return False
    x, y, z = struct.unpack_from("<3f", b, FPD["pos"])
    return (all(math.isfinite(c) for c in (x, y, z)) and abs(x) < 90 and abs(y) < 30 and abs(z) < 90
            and b[FPD["on_field"]] in (0, 1) and 0 <= struct.unpack_from("<i", b, FPD["jersey"])[0] < 100)


class FgdbMatch:
    """route B: the 22 on-field FifaPlayerData objects (pid -> object -> its vtable -> all instances nearby)"""

    def __init__(self, lay, pids, log=print, budget=60.0):
        self.lay, self.pids, self.log, self.budget = lay, pids, log, budget
        self.ball, self.R, self.P, self.W = None, None, 0, 0
        self.length, self.width = 105.0, 68.0  # assumed (FIFA standard); route B has no pitch object
        self.route = "B (FifaPlayerData heap objects)"

    def locate(self):
        t0 = time.time()
        log = self.log
        base, size = self.lay["base"], self.lay.get("img_size", 0x30000000)
        img = lambda v: base <= v < base + size
        seed, vt = None, None
        cached = self.lay.get("fpd_vtable_rva")
        self.key_off = self.lay.get("fpd_key_off", 0)
        if cached:  # same build: the vtable is known, one near-less scan for it
            vt = base + cached
        else:
            for pid in self.pids[:3]:
                pat = "?? 00 00 00 ?? ?? ?? ?? " + " ".join("%02X" % x for x in struct.pack("<i", pid))
                hits, dt = find_writable(pat, SPANS, 24, max(5.0, self.budget * 0.6 - (time.time() - t0)), log)
                objs = [h - FPD["jersey"] for h in hits if (h - FPD["jersey"]) % 8 == 0]
                blobs = mbytes([(o, FPD["size"]) for o in objs]) if objs else []
                good = [(o, b) for o, b in zip(objs, blobs) if fpd_plausible(b) and (img(u64(b, 0)) or img(u64(b, 8)))]
                log("  pid %d: %d hit(s) in %.1fs, %d plausible FifaPlayerData" % (pid, len(hits), dt, len(good)))
                if good:
                    seed, sb = good[0]
                    self.key_off = 0 if img(u64(sb, 0)) else 8  # +0 vtable, else +8 TypeInfo* (attribulator-style)
                    vt = u64(sb, self.key_off)
                    break
            if vt is None:
                raise DevError("route B: no FifaPlayerData object with a starter's PlayerId at +0x1BC (%.0fs)" % (time.time() - t0))
        near = [(max(0x10000, seed - (1 << 28)), seed + (1 << 28))] if seed else SPANS
        kpat = ("?? " * self.key_off) + qhex(vt)
        hits, dt = find_writable(kpat, near, 400, max(5.0, self.budget - (time.time() - t0)), log)
        objs = [h for h in hits if h % 8 == 0]
        if seed and len(objs) < 22:  # instances may live outside the +-256 MB window
            more, dt2 = find_writable(kpat, SPANS, 400, max(5.0, self.budget - (time.time() - t0)), log)
            objs = sorted(set(objs) | set(h for h in more if h % 8 == 0))
        blobs = mbytes([(o, FPD["size"]) for o in objs])
        players = []
        for o, b in zip(objs, blobs):
            if not fpd_plausible(b) or b[FPD["on_field"]] != 1:
                continue
            players.append({"handle": struct.unpack_from("<i", b, FPD["pid_ig"])[0], "obj": o, "info": 0,
                            "team": struct.unpack_from("<i", b, FPD["team"])[0], "fpos": None, "state": 0,
                            "pid": struct.unpack_from("<i", b, FPD["pid"])[0],
                            "jersey": struct.unpack_from("<i", b, FPD["jersey"])[0],
                            "role": struct.unpack_from("<i", b, FPD["role"])[0]})
        # de-duplicate byte copies (same pid twice: keep the first, the dev service's own buffers show up in scans)
        seen, uniq = set(), []
        for p in players:
            if p["pid"] in seen:
                continue
            seen.add(p["pid"])
            uniq.append(p)
        players = uniq
        teams = sorted(set(p["team"] for p in players))
        tmap = {t: i for i, t in enumerate(teams)}
        for p in players:
            p["club_or_side"] = p["team"]
            p["team"] = tmap[p["team"]]
        # keeper: PlayerRole 0 if exactly one per team has it, else the deepest player is chosen per sample
        for t in range(len(teams)):
            r0 = [p for p in players if p["team"] == t and p["role"] == 0]
            if len(r0) == 1:
                r0[0]["fpos"] = 0
        for p in players:
            if p["fpos"] is None:
                p["fpos"] = 1
        self.players = players
        self.vtable = vt
        self.seconds = time.time() - t0
        self.lay["fpd_vtable_rva"] = vt - base
        self.lay["fpd_key_off"] = self.key_off
        log("  route B: vtable %s (rva 0x%X), %d instances, %d on field, teams %s, %.1fs"
            % (H(vt), vt - base, len(objs), len(players), teams, self.seconds))
        return self

    def read_sample(self):
        res = mbytes([(p["obj"], FPD["size"]) for p in self.players], 30)
        t = time.time()
        self.last_offense = {}
        out, carrier, best = [], None, None
        for p, b in zip(self.players, res):
            if not b:
                out.append(None)
                continue
            out.append(struct.unpack_from("<3f", b, FPD["pos"]))
            d = struct.unpack_from("<f", b, FPD["dist_ball"])[0]
            if b[FPD["offense"]]:
                self.last_offense[p["team"]] = True
            if b[FPD["offense"]] and math.isfinite(d) and (best is None or d < best):
                best, carrier = d, p["handle"]
        if best is not None and best > 2.5:  # nobody of the attacking side is on the ball
            carrier = None
        return t, out, None, carrier

# ---------------------------------------------------------------- metrics
def team_sample_metrics(rows, L, along, lat, carrier_pos, carrier_team, team):
    """rows: [(player dict, (x, y, z))] of one team. Returns dict or None"""
    gk = [q for q in rows if q[0]["fpos"] == 0]
    if not gk:  # deepest player towards the side of the team centroid
        side = 1.0 if statistics.fmean(q[1][along] for q in rows) > 0 else -1.0
        gk = [max(rows, key=lambda q: side * q[1][along])]
    s = 1.0 if gk[0][1][along] > 0 else -1.0  # own goal at s * L/2
    out = [q for q in rows if q is not gk[0]]
    if len(out) < 4:
        return None
    depth = sorted(L / 2 - s * q[1][along] for q in out)  # metres from own goal line
    xs = [q[1][along] for q in out]
    ys = [q[1][lat] for q in out]
    cx, cy = statistics.fmean(xs), statistics.fmean(ys)
    m = {"own_goal": s,
         "backline_m": statistics.fmean(depth[:4]),
         "length_m": depth[-1] - depth[0],
         "width_m": max(ys) - min(ys),
         "compact_m": statistics.fmean(math.hypot(x - cx, y - cy) for x, y in zip(xs, ys)),
         "centroid_m": L / 2 - s * cx}
    m["backline_norm"] = m["backline_m"] / L
    m["centroid_norm"] = m["centroid_m"] / L
    if carrier_pos is not None and carrier_team is not None and carrier_team != team:
        d = sorted(math.hypot(q[1][along] - carrier_pos[along], q[1][lat] - carrier_pos[lat]) for q in out)
        m["press_m"] = statistics.fmean(d[:3])
    return m


def summarise(samples, players, L, along, lat):
    """samples: [(t, positions, ball, carrier)] -> per-team summary"""
    teams = sorted(set(p["team"] for p in players))
    per = {t: [] for t in teams}
    poss = {t: 0 for t in teams}
    poss_none = 0
    territory = {t: [] for t in teams}
    frozen = 0
    prev = None
    used = 0
    hidx = {p["handle"]: i for i, p in enumerate(players)}
    for smp in samples:
        t, pos, ball, carrier = smp[:4]
        offense = smp[4] if len(smp) > 4 else None
        if prev is not None and pos == prev:
            frozen += 1
            continue
        prev = pos
        on = [(p, q) for p, q in zip(players, pos) if q is not None and abs(q[along]) <= L / 2 + 4 and abs(q[lat]) <= 50]
        cteam, cpos = None, None
        if carrier is not None and carrier in hidx and pos[hidx[carrier]] is not None:
            cteam = players[hidx[carrier]]["team"]
            cpos = pos[hidx[carrier]]
        used += 1
        if cteam is None:
            poss_none += 1
        else:
            poss[cteam] += 1
        for tm in teams:
            rows = [r for r in on if r[0]["team"] == tm]
            if len(rows) < 7:
                continue
            m = team_sample_metrics(rows, L, along, lat, cpos, cteam, tm)
            if m:
                # with / without the ball: the game's own "team on offense" flag (FGDB route), else the ball carrier's team
                with_ball = bool(offense.get(tm)) if offense is not None else (None if cteam is None else cteam == tm)
                if with_ball is True:
                    m["width_with_m"], m["length_with_m"] = m["width_m"], m["length_m"]
                elif with_ball is False:
                    m["width_without_m"], m["length_without_m"] = m["width_m"], m["length_m"]
                per[tm].append(m)
                if ball is not None:
                    territory[tm].append((L / 2 - m["own_goal"] * ball[along]) / L)
    summ = {"samples": len(samples), "used": used, "frozen": frozen, "teams": {}}
    for tm in teams:
        ms = per[tm]
        d = {"samples": len(ms)}
        for k in ("backline_norm", "backline_m", "length_m", "width_m", "width_with_m", "width_without_m", "length_with_m", "length_without_m", "compact_m", "centroid_norm", "press_m"):
            v = [m[k] for m in ms if k in m]
            if v:
                d[k] = {"mean": round(statistics.fmean(v), 3), "sd": round(statistics.pstdev(v), 3), "n": len(v)}
        d["possession_share"] = round(poss[tm] / used, 3) if used else None
        if territory[tm]:
            d["ball_depth_norm"] = {"mean": round(statistics.fmean(territory[tm]), 3), "n": len(territory[tm])}
        d["players"] = [{"handle": p["handle"], "pid": p["pid"], "fpos": p["fpos"]} for p in players if p["team"] == tm]
        summ["teams"][str(tm)] = d
    summ["possession_none_share"] = round(poss_none / used, 3) if used else None
    return summ


def report_text(summ, meta):
    lines = ["MATCH MEASURE  %s  label=%s  build=%s" % (meta["start"], meta["label"], meta["build"]),
             "pitch %.1f x %.1f m, along-pitch axis %s, %d samples (%d used, %d frozen/paused skipped), %.1f s"
             % (meta["length"], meta["width"], "xyz"[meta["along"]], summ["samples"], summ["used"], summ["frozen"], meta["duration"]),
             "%-24s %18s %18s" % ("metric (mean +- sd)", "team 0" + meta.get("tag0", ""), "team 1" + meta.get("tag1", ""))]
    names = [("backline_norm", "back-line height /L"), ("backline_m", "back-line height m"), ("length_m", "team length m"),
             ("width_m", "width m"), ("compact_m", "compactness m"), ("centroid_norm", "centroid depth /L"),
             ("press_m", "pressing dist m (def.)")]
    T = summ["teams"]
    for k, label in names:
        cells = []
        for tm in ("0", "1"):
            v = T.get(tm, {}).get(k)
            cells.append("%7.3f +- %5.3f (%d)" % (v["mean"], v["sd"], v["n"]) if v else "-")
        lines.append("%-24s %18s %18s" % (label, cells[0], cells[1]))
    lines.append("%-24s %18s %18s" % ("possession share", *(str(T.get(tm, {}).get("possession_share")) for tm in ("0", "1"))))
    lines.append("%-24s %18s %18s" % ("ball depth /L (own goal 0)", *(str(T.get(tm, {}).get("ball_depth_norm", {}).get("mean")) for tm in ("0", "1"))))
    lines.append("no carrier (ball loose / in flight) share: %s" % summ.get("possession_none_share"))
    return "\n".join(lines)


def axis_from_gks(players, pos):
    """along-pitch axis: the coordinate in which the two keepers are farthest apart (0 = x, 2 = z)"""
    gks = [q for p, q in zip(players, pos) if p["fpos"] == 0 and q is not None]
    if len(gks) >= 2:
        return 0 if abs(gks[0][0] - gks[1][0]) >= abs(gks[0][2] - gks[1][2]) else 2
    xs = [abs(q[0]) for q in pos if q]
    zs = [abs(q[2]) for q in pos if q]
    return 0 if max(xs) >= max(zs) else 2


# ---------------------------------------------------------------- commands
def team_pids(args):
    return [int(x) for x in (args.team_pids or "").split(",") if x.strip()]


def locate_any(key, lay, args, log=print):
    """route A (Lua root slot S0) when it is set, else route C (match object global G1), else route B (FifaPlayerData
    heap objects)"""
    global _LAST_B
    try:
        m = Match(lay).locate()
        m.route = "A (Lua gameplay root slot S0)"
        return m
    except (DevError, TypeError, struct.error) as e:
        log("route A unavailable: %s -> route C" % e)
    try:
        ensure_route_c(key, lay["base"], lay.get("img_size", 0x30000000), lay, getattr(args, "relocate", False), log)
        m = MatchC(lay, log).locate()
        log("  route C: G1 %s  M %s  T %s  players from the %s list: %d (team 0: %d, team 1: %d)" % (
            H(lay["c_G1"]), H(m.M), H(m.T), m.source, len(m.players), sum(1 for p in m.players if p["team"] == 0),
            sum(1 for p in m.players if p["team"] == 1)))
        return m
    except (DevError, TypeError, KeyError, struct.error) as e:
        log("route C unavailable: %s -> route B" % e)
    if _LAST_B is not None:  # same process (--check: locate then measure): re-check the objects instead of scanning again
        blobs = mbytes([(p["obj"], FPD["size"]) for p in _LAST_B.players])
        if all(bb and u64(bb, _LAST_B.key_off) == _LAST_B.vtable and bb[FPD["on_field"]] == 1 for bb in blobs):
            log("  route B: the %d objects found before still hold the FifaPlayerData vtable, reused" % len(blobs))
            return _LAST_B
    had = lay.get("fpd_vtable_rva")
    try:
        m = FgdbMatch(lay, team_pids(args), log).locate()
    except DevError:
        if not had:
            raise
        lay.pop("fpd_vtable_rva", None)  # stale cache: discover again
        m = FgdbMatch(lay, team_pids(args), log).locate()
    if lay.get("fpd_vtable_rva") != had:
        try:
            cache = json.load(open(CACHE))
            cache.setdefault(key, {})["fpd_vtable_rva"] = lay["fpd_vtable_rva"]
            cache[key]["fpd_key_off"] = lay.get("fpd_key_off", 0)
            json.dump(cache, open(CACHE, "w"), indent=1)
        except Exception:
            pass
    _LAST_B = m
    return m


_LAST_B = None


def cmd_locate(args):
    key, base, lay = load_layout(args.relocate)
    m = locate_any(key, lay, args)
    t, pos, ball, carrier = m.read_sample()
    print("route %s" % m.route)
    if isinstance(m, MatchC):
        print("build %s  FC27.exe %s  G1 %s (rva 0x%X)  M %s  T %s  list %s  ball slot %s" % (
            key, H(base), H(lay["c_G1"]), lay["c_G1_rva"], H(m.M), H(m.T), m.source, m.ball_index))
    else:
        print("build %s  FC27.exe %s  S0 %s (rva 0x%X)  P %s  W %s" % (key, H(base), H(lay["S0"]), lay["S0_rva"], H(m.P), H(m.W)))
    print("pitch %.2f x %.2f%s  players %d  ball ring %s  carrier %s" % (
        m.length, m.width, "" if getattr(m, "pitch_read", True) and not isinstance(m, FgdbMatch) else " (assumed)",
        len(m.players), H(m.ball or 0), carrier))
    for p, q in zip(m.players, pos):
        print("  h%-3d team %d fpos %-2d pid %-7s obj %s state %s pos %s" % (p["handle"], p["team"], p["fpos"], p["pid"], H(p["obj"]), H(p["state"]),
              "(%.2f, %.2f, %.2f)" % q if q else None))
    print("  ball", "(%.2f, %.2f, %.2f)" % ball if ball else None)
    if getattr(args, "check", False) or args.verbose:
        time.sleep(1.0)
        t2, pos2, _, _ = m.read_sample()
        moved = [math.dist(a, b) for a, b in zip(pos, pos2) if a and b]
        print("liveness: %d of %d players moved > 5 cm in %.1f s (max %.2f m)" % (
            sum(1 for d in moved if d > 0.05), len(moved), t2 - t, max(moved) if moved else 0))
    return m


def cmd_measure(args):
    key, base, lay = load_layout(args.relocate)
    m = locate_any(key, lay, args)
    start = time.strftime("%Y%m%d-%H%M%S")
    outdir = getattr(args, "run_dir", None) or os.path.join(args.out or OUT_ROOT, "%s_%s" % (start, args.label))
    os.makedirs(outdir, exist_ok=True)
    samples = []
    extras = []  # v2: per-sample window label, ball velocity, clock
    if getattr(args, "clock_addr", None):
        m.clock_addr = int(args.clock_addr, 16)
    t0 = _now()
    next_t = t0
    relocated = 0
    csv_f = open(os.path.join(outdir, "positions.csv"), "w", newline="")  # WP-C: one row block per sample, flushed
    csv_w = csv.writer(csv_f)
    csv_w.writerow(POS_HEADER)
    print("writing: %s" % outdir)
    sys.stdout.flush()
    paused = 0
    while _now() - t0 < args.seconds:
        if getattr(args, "stop", None) and os.path.exists(args.stop):
            print("stopped by %s" % args.stop)
            break
        if getattr(args, "pause", None) and os.path.exists(args.pause):
            # WP-C: a writer (x1_run.sh) needs the dev-service lock: no dev call while the pause file exists (the samples of the
            # gap are simply missing; the next sample's dt marks the gap for mm_stats --max-dt)
            if not paused:
                print("paused by %s" % args.pause)
                sys.stdout.flush()
            paused += 1
            time.sleep(0.05)
            next_t = _now()
            continue
        if paused:
            print("resumed after %d polls" % paused)
            paused = 0
        try:
            s = m.read_sample()
        except DevError as e:
            print("sample failed (%s): locating again" % e)
            m = locate_any(key, lay, args)
            relocated += 1
            continue
        bad = sum(1 for q in s[1] if q is None or not all(math.isfinite(c) for c in q))
        if bad > 4:
            m = locate_any(key, lay, args)
            relocated += 1
            continue
        samples.append(tuple(s) + (dict(m.last_offense),) if hasattr(m, "last_offense") else s)
        extras.append({"win": read_marker(args.marker, args.label), "vel": getattr(m, "last_ball_vel", None),
                       "clock": getattr(m, "last_clock", None)})
        write_sample_rows(csv_w, len(samples) - 1, samples[-1], extras[-1], m.players, t0)
        csv_f.flush()
        if args.verbose:
            print("%5.1fs  win %s  carrier %s  ball %s  vel %s  clock %s" % (s[0] - t0, extras[-1]["win"], s[3], s[2],
                                                                           extras[-1]["vel"], extras[-1]["clock"]))
        next_t += 1.0 / args.hz
        _sleep(next_t - _now())
    csv_f.close()
    along = axis_from_gks(m.players, samples[0][1]) if samples else 0
    lat = 2 if along == 0 else 0
    L, Wd = (m.length, m.width)
    summ = summarise(samples, m.players, L, along, lat)
    summ.update(v2_extras(samples, extras, m.players, L, along, lat))
    meta = {"start": start, "label": args.label, "build": key, "length": L, "width": Wd, "along": along,
            "duration": (samples[-1][0] - samples[0][0]) if len(samples) > 1 else 0.0, "relocated": relocated,
            "S0": H(lay["S0"]) if "S0" in lay else None, "hz": args.hz, "route": getattr(m, "route", "A"),
            "pitch_assumed": isinstance(m, FgdbMatch) or not getattr(m, "pitch_read", True)}
    if isinstance(m, MatchC):
        meta.update({"G1": H(lay["c_G1"]), "M": H(m.M), "T": H(m.T), "player_list": m.source, "ball_slot": m.ball_index})
    if args.team_pids:
        want = set(int(x) for x in args.team_pids.split(",") if x.strip())
        for tm, d in summ["teams"].items():
            if want & set(p["pid"] for p in d["players"] if p["pid"]):
                meta["tag" + tm] = "*"
    summ["meta"] = meta
    json.dump(summ, open(os.path.join(outdir, "summary.json"), "w"), indent=1)
    rep = report_text(summ, meta)
    open(os.path.join(outdir, "report.txt"), "w").write(rep + "\n")
    print(rep)
    print("route:", meta["route"], "(pitch size assumed 105 x 68)" if meta["pitch_assumed"] else "")
    print("written:", outdir)
    return summ, samples, m


def cmd_check(args):
    """one-shot live verdict: 22 on-field players (11 + 11), positions moving, pitch size, 20 s of metrics"""
    if args.seconds == 60.0:
        args.seconds = 20.0
    fails = []
    t0 = time.time()
    try:
        print("== locate ==")
        cmd_locate(args)
        print("== measure %.0f s ==" % args.seconds)
        summ, samples, m = cmd_measure(args)
    except DevError as e:
        print("FAIL: %s (%.0f s)" % (e, time.time() - t0))
        return harness.finish("FAIL", tool="match_measure --check")
    n = len(m.players)
    per = [sum(1 for p in m.players if p["team"] == t) for t in (0, 1)]
    if n != 22 or per != [11, 11]:
        fails.append("players %d (per team %s), expected 22 = 11 + 11" % (n, per))
    moving = 0
    for a, b in zip(samples, samples[1:]):
        if any(p and q and math.dist(p, q) > 0.05 for p, q in zip(a[1], b[1])):
            moving += 1
    if len(samples) < 5 or moving < (len(samples) - 1) * 0.5:
        fails.append("positions not moving: %d of %d sample pairs changed" % (moving, max(0, len(samples) - 1)))
    xs = [abs(q[0]) for s in samples for q in s[1] if q] or [0]
    zs = [abs(q[2]) for s in samples for q in s[1] if q] or [0]
    big = max(max(xs), max(zs))
    if big > 70 or big < 5:
        fails.append("positions not metre-scale on a pitch (max |x| %.1f, |z| %.1f)" % (max(xs), max(zs)))
    if not (90 <= m.length <= 120 and 45 <= m.width <= 90):
        fails.append("pitch %.1f x %.1f m out of range" % (m.length, m.width))
    for tm in ("0", "1"):
        d = summ["teams"].get(tm, {})
        if d.get("samples", 0) < max(3, len(samples) // 2):
            fails.append("team %s: only %s usable samples" % (tm, d.get("samples")))
    print("route %s, pitch %.1f x %.1f m%s, %d players, %d samples, %d moving pairs, %.0f s total" % (
        m.route, m.length, m.width, " (assumed)" if isinstance(m, FgdbMatch) or not getattr(m, "pitch_read", True)
        else " (read)", n, len(samples), moving, time.time() - t0))
    print("PASS" if not fails else "FAIL: " + "; ".join(fails))
    return harness.finish("PASS" if not fails else "FAIL", tool="match_measure --check")


def _hx(v):
    return "-" if v is None else "%X" % v


def cmd_probe(args):
    """one short read-only live run that decides between the candidate roots: for each root its value, the players it
    lists (per team) and two position reads 1 s apart. Read-only dev ops only (modules, find, bytes, multi)."""
    t0 = time.time()
    key, base, lay = load_layout(args.relocate)
    print("probe  build %s  FC27.exe %s  (layout %.1f s)" % (key, H(base), time.time() - t0))
    cands = []  # (label, players)
    rings = []  # (label, H)
    L = lay
    # ---- candidate C: the match object global G1
    try:
        ensure_route_c(key, base, lay.get("img_size", 0x30000000), lay, args.relocate)
    except DevError as e:
        print("[C] signatures: %s" % e)
    if lay.get("c_G1"):
        print("[C] layout: G1 %s (rva 0x%X)  T = M+0x%X  slots T+0x%s x 0x%s  vector T+0x%s..0x%s  teams T+0x%s (idx +0x%s "
              "cont +0x%s stride 0x%s vec +0x%s)  balls M+0x%s x %s  possession M+0x%s  field M+0x%s  hits %s" % (
                  H(L["c_G1"]), L["c_G1_rva"], L["c_t_off"], _hx(L.get("c_t_arr")), _hx(L.get("c_t_cnt")),
                  _hx(L.get("c_t_vbeg")), _hx(L.get("c_t_vend")), _hx(L.get("c_t_teams")), _hx(L.get("c_tm_idx")),
                  _hx(L.get("c_tm_cont")), _hx(L.get("c_tm_stride")), _hx(L.get("c_tm_vec")), _hx(L.get("c_m_balls")),
                  L.get("c_b_cnt"), _hx(L.get("c_m_poss")), _hx(L.get("c_m_field")), L.get("c_sig_hits")))
        try:
            mc = MatchC(lay)
            g1v = u64(rd(L["c_G1"], 8))
            print("[C] G1 value %s" % H(g1v))
            M, mb = mc.m_read()
            T = u64(mb, L["c_t_off"])
            F = u64(mb, L["c_m_field"]) if L.get("c_m_field") else 0
            dims = rd(F + L["flen"], 8) if ptr_ok(F) else None
            R = u64(mb, L["c_m_poss"]) if L.get("c_m_poss") else 0
            print("[C] M %s  T %s  field %s %s  possession record %s" % (
                H(M), H(T), H(F), "%.2f x %.2f m" % struct.unpack("<2f", dims) if dims else "-", H(R)))
            tb = rd(T, mc.t_span()) if ptr_ok(T) else None
            if tb:
                for name, ptrs in mc.sources(T, tb).items():
                    pl = mc.players_from(ptrs)
                    per = [sum(1 for p in pl if p["team"] == t) for t in (0, 1)]
                    print("[C/%s] %d pointers -> %d players: team0 %d, team1 %d, other %d, fpos 0 (GK) %d, handles %s" % (
                        name, len(ptrs), len(pl), per[0], per[1], len(pl) - sum(per),
                        sum(1 for p in pl if p["fpos"] == 0), sorted(p["handle"] for p in pl)))
                    pl = [p for p in pl if p["team"] in (0, 1)]
                    cands.append(("C/" + name, pl))
                    if pl and max(per) > 11:
                        ln, wd = struct.unpack("<2f", dims) if dims else (105.0, 68.0)
                        keep = trim_on_pitch(lay, pl, ln, wd)
                        print("[C/%s] on the pitch: %d + %d" % (name, sum(1 for p in keep if p["team"] == 0),
                                                                sum(1 for p in keep if p["team"] == 1)))
                        cands.append(("C/%s+on-pitch" % name, keep))
            for i in range(L.get("c_b_cnt") or 0):
                bp = u64(mb, L["c_m_balls"] + 8 * i)
                b1 = rd(bp + L.get("c_bh1", L["bh1"]), 8) if ptr_ok(bp) else None
                b2 = rd(u64(b1) + L.get("c_bh2", L["bh2"]), 8) if b1 and ptr_ok(u64(b1)) else None
                hr = u64(b2) if b2 else 0
                print("[C] ball slot %d: Ball %s  ring %s" % (i, H(bp), H(hr)))
                if ptr_ok(hr):
                    rings.append(("C/ball%d" % i, hr))
        except (DevError, TypeError, struct.error) as e:
            print("[C] %s" % e)
    # ---- candidate A: the gameplay-script slot S0 (expected empty outside scripted modes)
    try:
        P = u64(rd(L["S0"], 8))
        print("[A] S0 %s value %s%s" % (H(L["S0"]), H(P), "" if ptr_ok(P) else "  (empty: no gameplay script running)"))
        if ptr_ok(P):
            ma = Match(lay).locate()
            per = [sum(1 for p in ma.players if p["team"] == t) for t in (0, 1)]
            print("[A/map] W %s: %d players (team0 %d, team1 %d), ball ring %s" % (H(ma.W), len(ma.players), per[0], per[1],
                                                                                 H(ma.ball or 0)))
            cands.append(("A/map", [p for p in ma.players if p["team"] in (0, 1)]))
            if ma.ball:
                rings.append(("A/ball", ma.ball))
    except (DevError, TypeError, KeyError, struct.error) as e:
        print("[A] %s" % e)
    print("[B] heap scan, not part of the probe (cached FifaPlayerData vtable rva: %s)" % _hx(L.get("fpd_vtable_rva")))
    # ---- two reads 1 s apart
    reads, breads = [], []
    for k in range(2):
        if k:
            time.sleep(max(0.0, 1.0 - (time.time() - tk)))
        tk = time.time()
        reads.append({lab: read_positions(lay, pl) for lab, pl in cands})
        bb = mbytes([(h, L["bframe"] + 4) for _, h in rings]) if rings else []
        bout = {}
        for (lab, h), rb in zip(rings, bb):
            if rb:
                frame = struct.unpack_from("<i", rb, L["bframe"])[0]
                rec = ((frame % L["ring"]) << L["rshift"]) + L["rpos"]
                bout[lab] = (frame, struct.unpack_from("<3f", rb, rec) if 0 <= rec <= L["bframe"] - 12 else None)
        breads.append(bout)
    best = None
    for lab, pl in cands:
        p1, p2 = reads[0][lab], reads[1][lab]
        ok = [(a, b) for a, b in zip(p1, p2) if a and b and all(math.isfinite(c) for c in a + b)]
        moved = [math.dist(a, b) for a, b in ok]
        big = max([max(abs(a[0]), abs(a[2])) for a, _ in ok] or [0])
        if 75 < big <= 250:  # feet (route C's match engine units): judge in metres
            moved = [d * 0.3048 for d in moved]
            big *= 0.3048
        per = [sum(1 for p in pl if p["team"] == t) for t in (0, 1)]
        good = per == [11, 11] and len(ok) >= 20 and sum(1 for d in moved if d > 0.05) >= 6 and 5 <= big <= 75
        print("%-9s players %2d (%d + %d)  positions %2d/%2d  moved > 5 cm in 1 s: %2d  max move %.2f m  max |x|,|z| %.1f  -> %s" % (
            lab, len(pl), per[0], per[1], len(ok), len(pl), sum(1 for d in moved if d > 0.05),
            max(moved) if moved else 0.0, big, "OK" if good else "no"))
        if good and best is None:
            best = lab
        if args.verbose or (good and best == lab):
            for p, a, b in zip(pl, p1, p2):
                print("    h%-3d team %d fpos %-2d obj %s  %s -> %s" % (
                    p["handle"], p["team"], p["fpos"], H(p["obj"]), "(%.2f, %.2f, %.2f)" % a if a else None,
                    "(%.2f, %.2f, %.2f)" % b if b else None))
    for lab, _ in rings:
        a, b = breads[0].get(lab), breads[1].get(lab)
        print("%-9s frame %s -> %s  pos %s -> %s" % (lab, a and a[0], b and b[0],
                                                     a and a[1] and "(%.2f, %.2f, %.2f)" % a[1],
                                                     b and b[1] and "(%.2f, %.2f, %.2f)" % b[1]))
    print("PROBE VERDICT (%.1f s): %s" % (time.time() - t0, ("use %s" % best) if best else
                                           "no candidate root lists 11 + 11 moving players"))
    return 0 if best else 1


def cmd_compare(a, b):
    def ld(p):
        return json.load(open(p if p.endswith(".json") else os.path.join(p, "summary.json")))
    A, B = ld(a), ld(b)
    print("A = %s (%s)   B = %s (%s)" % (A["meta"]["label"], A["meta"]["start"], B["meta"]["label"], B["meta"]["start"]))
    for tm in ("0", "1"):
        print("team %s" % tm)
        for k in ("backline_norm", "backline_m", "length_m", "width_m", "width_with_m", "width_without_m", "length_with_m", "length_without_m", "compact_m", "centroid_norm", "press_m"):
            x, y = A["teams"].get(tm, {}).get(k), B["teams"].get(tm, {}).get(k)
            if not x or not y:
                continue
            sd = math.sqrt((x["sd"] ** 2 + y["sd"] ** 2) / 2) or 1e-9
            dlt = y["mean"] - x["mean"]
            print("  %-14s A %8.3f  B %8.3f  B-A %+8.3f  effect %+5.2f sd%s" % (k, x["mean"], y["mean"], dlt, dlt / sd, "  <- visible" if abs(dlt / sd) >= 0.8 else ""))
        print("  possession     A %s  B %s" % (A["teams"].get(tm, {}).get("possession_share"), B["teams"].get(tm, {}).get("possession_share")))


# ---------------------------------------------------------------- v2 helpers (T4)
def read_marker(path, default):
    """first token of the marker file, else the run label. The coordinator's write script writes e.g. 'B1' / 'A1' into it."""
    if not path:
        return default
    try:
        tok = open(path, encoding="utf-8", errors="replace").read().split()
        return tok[0] if tok else default
    except OSError:
        return default


POS_HEADER = ["t", "sample", "handle", "team", "fpos", "pid", "x", "y", "z", "ball_x", "ball_y", "ball_z", "carrier",
              "win", "ball_vx", "ball_vy", "ball_vz", "clock"]


def write_sample_rows(w, i, smp, ex, players, t0):
    """the rows of ONE sample (players as located when it was taken: a relocate cannot mislabel earlier samples)"""
    t, pos, ball, carrier = smp[:4]
    vel = ex.get("vel") or (math.nan,) * 3
    clk = ex.get("clock")
    for p, q in zip(players, pos):
        w.writerow(["%.3f" % (t - t0), i, p["handle"], p["team"], p["fpos"], p["pid"],
                    *(("%.3f" % c) for c in (q or (math.nan,) * 3)),
                    *(("%.3f" % c) for c in (ball or (math.nan,) * 3)), carrier,
                    ex.get("win", ""), *(("%.3f" % c) for c in vel), "" if clk is None else "%.3f" % clk])


def write_positions_v2(path, samples, extras, players, t0):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t", "sample", "handle", "team", "fpos", "pid", "x", "y", "z", "ball_x", "ball_y", "ball_z", "carrier",
                    "win", "ball_vx", "ball_vy", "ball_vz", "clock"])
        for i, smp in enumerate(samples):
            t, pos, ball, carrier = smp[:4]
            ex = extras[i] if i < len(extras) else {}
            vel = ex.get("vel") or (math.nan,) * 3
            clk = ex.get("clock")
            for p, q in zip(players, pos):
                w.writerow(["%.3f" % (t - t0), i, p["handle"], p["team"], p["fpos"], p["pid"],
                            *(("%.3f" % c) for c in (q or (math.nan,) * 3)),
                            *(("%.3f" % c) for c in (ball or (math.nan,) * 3)), carrier,
                            ex.get("win", ""), *(("%.3f" % c) for c in vel), "" if clk is None else "%.3f" % clk])


def v2_extras(samples, extras, players, L, along, lat):
    """achieved cadence, per-window sample counts, dead-ball share (ball speed < 0.3 m/s from the velocity field when present,
    else from consecutive ball positions), width by possession x ball third per team"""
    ts = [smp[0] for smp in samples]
    dts = [b - a for a, b in zip(ts, ts[1:])]
    out = {"cadence": {"dt_mean": round(statistics.fmean(dts), 3) if dts else None,
                       "dt_sd": round(statistics.pstdev(dts), 3) if dts else None,
                       "dt_min": round(min(dts), 3) if dts else None, "dt_max": round(max(dts), 3) if dts else None}}
    wins = {}
    for ex in extras:
        wins[ex.get("win")] = wins.get(ex.get("win"), 0) + 1
    out["windows"] = wins
    hidx = {p["handle"]: i for i, p in enumerate(players)}
    still = 0
    strata = {0: {}, 1: {}}
    prev_ball = None
    for i, smp in enumerate(samples):
        t, pos, ball, carrier = smp[:4]
        ex = extras[i] if i < len(extras) else {}
        vel = ex.get("vel")
        speed = None
        if vel and all(math.isfinite(c) for c in vel):
            speed = math.hypot(vel[along], vel[lat])
        elif ball and prev_ball and prev_ball[1] and i > 0:
            dt = t - prev_ball[0]
            if dt > 0.05:
                speed = math.hypot(ball[along] - prev_ball[1][along], ball[lat] - prev_ball[1][lat]) / dt
        prev_ball = (t, ball)
        if speed is not None and speed < 0.3:
            still += 1
        cteam = players[hidx[carrier]]["team"] if carrier is not None and carrier in hidx else None
        for tm in (0, 1):
            rows = [(p, q) for p, q in zip(players, pos) if q is not None and p["team"] == tm]
            if len(rows) < 7:
                continue
            m = team_sample_metrics(rows, L, along, lat, None, None, tm)
            if not m or ball is None:
                continue
            depth = (L / 2 - m["own_goal"] * ball[along]) / L
            third = "def" if depth < 1 / 3 else ("mid" if depth < 2 / 3 else "att")
            poss = "dead" if cteam is None else ("own" if cteam == tm else "opp")
            key = "%s/%s" % (poss, third)
            strata[tm].setdefault(key, []).append(m["width_m"])
    out["ball_still_share"] = round(still / len(samples), 3) if samples else None
    out["width_by_stratum"] = {str(tm): {k: {"mean": round(statistics.fmean(v), 2), "n": len(v)} for k, v in d.items()}
                               for tm, d in strata.items()}
    return out


def cmd_replay(args):
    """offline: positions.csv -> samples -> summarise + v2 extras; prints the report; writes summary_v2.json next to it"""
    path = os.path.join(args.replay, "positions.csv")
    rows = list(csv.DictReader(open(path, newline="")))
    by = {}
    for r in rows:
        by.setdefault(int(r["sample"]), []).append(r)
    players, samples, extras = [], [], []
    for i in sorted(by):
        grp = by[i]
        if not players:
            players = [{"handle": int(r["handle"]), "team": int(r["team"]), "fpos": int(r["fpos"]), "pid": None} for r in grp]
        pos = []
        for r in grp:
            try:
                q = tuple(float(r[k]) for k in ("x", "y", "z"))
                pos.append(q if all(math.isfinite(c) for c in q) else None)
            except ValueError:
                pos.append(None)
        ball = tuple(float(grp[0][k]) for k in ("ball_x", "ball_y", "ball_z"))
        car = grp[0].get("carrier")
        carrier = int(car) if car not in (None, "", "None") else None
        samples.append((float(grp[0]["t"]), pos, ball, carrier))
        vel = None
        if "ball_vx" in grp[0] and grp[0]["ball_vx"] not in ("", "nan"):
            vel = tuple(float(grp[0][k]) for k in ("ball_vx", "ball_vy", "ball_vz"))
        clk = grp[0].get("clock")
        extras.append({"win": grp[0].get("win") or args.label, "vel": vel, "clock": float(clk) if clk else None})
    along = axis_from_gks(players, samples[0][1]) if samples else 0
    lat = 2 if along == 0 else 0
    L, Wd = 105.0, 68.0
    summ = summarise(samples, players, L, along, lat)
    summ.update(v2_extras(samples, extras, players, L, along, lat))
    meta = {"start": "replay", "label": args.label, "build": "replay", "length": L, "width": Wd, "along": along,
            "duration": (samples[-1][0] - samples[0][0]) if len(samples) > 1 else 0.0}
    summ["meta"] = meta
    print(report_text(summ, meta))
    print("cadence", summ["cadence"], "windows", summ["windows"], "ball_still_share", summ["ball_still_share"])
    for tm in ("0", "1"):
        print("team %s width by stratum: %s" % (tm, summ["width_by_stratum"][tm]))
    here = os.path.dirname(os.path.abspath(__file__))
    odir = args.out or os.path.join(here, "replay_" + os.path.basename(os.path.normpath(args.replay)))
    os.makedirs(odir, exist_ok=True)
    out = os.path.join(odir, "summary_v2.json")
    json.dump(summ, open(out, "w"), indent=1)
    print("written:", out)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=60.0)
    ap.add_argument("--hz", type=float, default=1.0)
    ap.add_argument("--label", default="run")
    ap.add_argument("--out", default=None)
    ap.add_argument("--team-pids", default=os.environ.get("MM_TEAM_PIDS", ""),
                    help="comma list of your starters' player ids: route B needs them (find the objects) and the report marks that "
                         "team with *; default env MM_TEAM_PIDS (kept outside git: no player ids in the code)")
    ap.add_argument("--locate", action="store_true", help="locate + one sample, print, exit")
    ap.add_argument("--relocate", action="store_true", help="ignore the per-build cache, scan the signatures again")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"))
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--check", action="store_true", help="live check: locate (+ liveness) then a --seconds measure "
                    "(default 20 s) and print PASS / FAIL")
    ap.add_argument("--probe", action="store_true", help="read-only: print every candidate root (G1, S0) with its "
                    "player counts and two position reads 1 s apart; exit 0 when one lists 11 + 11 moving players")
    ap.add_argument("--marker", default=None, help="v2: text file whose first token is the current window label (per sample)")
    ap.add_argument("--clock-addr", default=None, help="v2: hex address of an f32 to read per sample (match clock), read-only")
    ap.add_argument("--replay", default=None, metavar="DIR", help="v2: offline re-summary of DIR/positions.csv (no game)")
    ap.add_argument("--stop", default=None, help="end the run (normally, files written) as soon as this file exists")
    ap.add_argument("--pause", default=None, help="no dev call while this file exists (a writer needs the dev-service lock)")
    ap.add_argument("--run-dir", default=None, help="write the run into exactly this folder (default OUT_ROOT/<stamp>_<label>)")
    ap.add_argument("--budget", type=float, default=None, help="time box in s (default: seconds + 120; --check 120; --probe 120)")
    ap.add_argument("--mock", action="store_true", help="offline: a synthetic engine instead of the dev service (no game)")
    ap.add_argument("--mock-mem", default=os.environ.get("TURBO_MOCK_MEM"), help="--mock: harness.MockMem file holding the knob")
    ap.add_argument("--mock-engine", default=os.environ.get("X1_MOCK_ENGINE", "live"),
                    choices=["live", "once", "none", "b1only"], help="--mock: how the synthetic engine reads the knob")
    ap.add_argument("--time-scale", type=float, default=float(os.environ.get("X1_TIME_SCALE", "1.0")),
                    help="--mock: real seconds per simulated second (e.g. 0.02)")
    ap.add_argument("--self-test", action="store_true", help="offline: replay + mock check + mock measure")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.replay:
        return cmd_replay(args)
    if args.compare:
        return cmd_compare(*args.compare)
    harness.require_live(args.mock, tool="match_measure")
    if args.mock:
        install_mock(args)
    budget = args.budget or ((120.0 if (args.check or args.probe or args.locate) else args.seconds * SCALE + 120.0))
    harness.timebox(budget, "match_measure")
    if args.probe:
        rc = cmd_probe(args)
        return harness.finish("PASS" if rc == 0 else "FAIL", tool="match_measure --probe")
    if args.check:
        return cmd_check(args)
    if args.locate:
        cmd_locate(args)
        return 0
    summ, samples, m = cmd_measure(args)
    return 0 if samples else 1


# ---------------------------------------------------------------- --mock: a synthetic engine (offline proof of the X1 wrapper)
class MockMatch:
    """22 synthetic players at 1 Hz (mm_stats.Synth, the sweep T4 model). The knob (harness.KNOBS width_in) is read from the
    MockMem file per sample; engine modes: live = the current value scales the in-possession width; once = the value seen at the
    FIRST sample of the match is used for the whole match (stored in the mock file, shared by every process); none = never read;
    b1only = synthetic, effect only while the marker says B1 (exercises the outcome-2 path; not a physical model)."""
    route = "mock (synthetic engine)"

    def __init__(self, args):
        import mm_stats
        self.args = args
        self.mem = harness.MockMem(args.mock_mem)
        self.inst = int(self.mem.kv("x1_inst"), 16)
        # deterministic per run label (a test must not flake); AR 0.6 = the low end of the 10-13 data (sweep T4 r1 0.6-0.75)
        self.synth = mm_stats.Synth(int(self.mem.kv("seed", 1)) * 1000 + sum(map(ord, args.label)), ar=0.6)
        self.players = [{"handle": tm * 25 + k, "team": tm, "fpos": 0 if k == 0 else k + 3, "pid": None, "obj": 0, "state": 0}
                        for tm in (0, 1) for k in range(11)]
        self.length, self.width, self.ball, self.pitch_read = 105.0, 68.0, None, True

    def locate(self):
        return self

    def factor(self):
        k = harness.KNOBS["width_in"]
        r = self.mem.call({"op": "bytes", "addr": "0x%X" % (self.inst + k["off"]), "len": 4}, timeout=30)  # the mock lock
        if not r.get("ok"):
            raise DevError("mock read: %s" % r.get("error"))
        val = struct.unpack(k["fmt"], bytes.fromhex(r["hex"]))[0]
        mode = self.args.mock_engine
        if mode == "live":
            return val
        if mode == "once":
            first = self.mem.kv("latched")
            if first is None:
                self.mem.set_kv("latched", val)
                first = val
            return first
        if mode == "b1only":
            return val if read_marker(self.args.marker, "") == "B1" else 1.0
        return 1.0

    def read_sample(self):
        t, pl, ball, car = self.synth.sample(self.factor())
        self.last_ball_vel, self.last_clock = None, None
        pos = [(x, 0.0, z) for h, tm, fp, x, z in pl]
        return _now(), pos, ball, car


def install_mock(args):
    """route every live entry point to the synthetic engine (no dev service, no game)"""
    global SCALE, load_layout, locate_any, cmd_locate
    if not args.mock_mem:
        raise SystemExit("--mock needs --mock-mem (or TURBO_MOCK_MEM)")
    SCALE = args.time_scale
    load_layout = lambda relocate=False, log=print: ("MOCK", harness.IMAGE_BASE, {})  # noqa: E731
    locate_any = lambda key, lay, a, log=print: MockMatch(args)  # noqa: E731

    def _loc(a):
        m = MockMatch(a)
        t, pos, ball, car = m.read_sample()
        print("route %s  players %d  pitch %.0f x %.0f  carrier %s" % (m.route, len(m.players), m.length, m.width, car))
        return m
    cmd_locate = _loc


def self_test():
    import contextlib, io, shutil, subprocess, tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    with tempfile.TemporaryDirectory() as d:
        mem_p = os.path.join(d, "mem.json")
        mem, inst = harness.build_x1_world(mem_p, None)
        mem.set_kv("x1_inst", "0x%X" % inst)
        marker = os.path.join(d, "win.txt")
        harness.write_marker(marker, "B1")
        env = dict(os.environ, TURBO_MOCK_MEM=mem_p, X1_TIME_SCALE="0.01")
        me = os.path.abspath(__file__)
        r = subprocess.run([sys.executable, me, "--mock", "--check", "--label", "chk", "--marker", marker, "--run-dir",
                            os.path.join(d, "chk")], capture_output=True, text=True, env=env, timeout=120)
        check(r.returncode == 0 and "VERDICT PASS" in r.stdout, "mock --check: 22 players moving, PASS (exit %d)" % r.returncode)
        run = os.path.join(d, "run")
        r = subprocess.run([sys.executable, me, "--mock", "--seconds", "30", "--label", "x1", "--marker", marker, "--run-dir", run],
                           capture_output=True, text=True, env=env, timeout=120)
        rows = list(csv.DictReader(open(os.path.join(run, "positions.csv"), newline="")))
        check(r.returncode == 0 and len(rows) >= 22 * 25 and all(x["win"] == "B1" for x in rows),
              "mock measure 30 s: %d rows written per sample, win column = marker B1" % len(rows))
        check(os.path.exists(os.path.join(run, "summary.json")) and "width" in open(os.path.join(run, "report.txt")).read(),
              "summary.json + report.txt written")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = cmd_replay(argparse.Namespace(replay=run, label="x1", out=os.path.join(d, "replay")))
        check(rc == 0 and "windows {'B1'" in buf.getvalue(), "--replay re-summarises the run (windows B1)")
        st = os.path.join(d, "GAME_STATE.txt")
        with open(st, "w") as f:
            f.write("state: CLOSED (21:00) - offline\n")
        r = subprocess.run([sys.executable, me, "--check"], capture_output=True, text=True, timeout=60,
                           env=dict(os.environ, TURBO_GAME_STATE=st))
        check(r.returncode == 3 and "refused" in r.stdout, "live --check while GAME_STATE is CLOSED: refused, exit 3")
        shutil.rmtree(run, ignore_errors=True)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
