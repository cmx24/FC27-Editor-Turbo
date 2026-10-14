"""battery_engine.py - read-only engine side of the Turbo 2.5 knob battery (tools/). Generalises turbo_output\\assessment_20261013\\X1P\\
x1p_check.py (width, deterministic) to the other consumers that the supplied disassemblies tie to a located knob.

READ-ONLY: only {"op":"multi","ops":[{"op":"bytes",...}]} requests are ever built (asserted). It gets a `dev` object with .multi(pairs)
([(addr, n)] -> [bytes | None]); battery_write.py supplies one over dev2 or over the offline mock world. No tool in this file writes.

The observables (all offsets of build 6AC07E31 are the OLD-image offsets, route C unchanged [A]; every pointer step is identity-checked,
a mismatch is a REFUSAL, never a verdict):
  team object tm = [[T + 0x38] + 8k]  (T = [M + 0xA10], M = [G1]) ; owner = [tm + 0x2250] = AiFormationPositioning (one per team);
  state = owner + 0xE0 ; tactic block tb = [[tm + 0x80]] (+0x554 build-up style as the length function reads it, +0x4D4 / +0x4DC / +0x7AC).
  band_att  (shape.width_in)   a = cap / (W K) = g * L with g in [0.85, 1.20] while the team has the ball (state+0x40 == 1.0)      [V OLD 0x140F56030]
  band_def  (shape.width_out)  a = cap / (W K) = h * Ldef with h in [0.50, 0.80] while the OTHER team has the ball (state+0x40 == 0) [V OLD 0x140F56030]
            -> for a value v the band is disjoint from the vanilla band whenever v is not within about 40 % of 1.0 (0.3: [0.14, 0.25] vs
            [0.49, 0.81]); no game state can move a sample from one band to the other, so ONE eligible sample judges, a window judges by share.
  paired_len (shape.length_in / length_in_possession)  the formation length state+0x2C is a function of the table value ([att + 0x30]
            when the team's style == 1, [att + 0x50] when style == 0) AND of ball / team inputs [V OLD 0x140F57098]; no closed band exists
            (the table value enters through a clamped blend whose weight depends on the ball), so the check is a PAIRED response: with
            every declared input frozen for two consecutive reads, a write must change the output at the next read while the inputs stay
            frozen; control events (frozen pair, no write) estimate the false-response rate. It can only PASS or stay INCONCLUSIVE: an
            unchanged output may be a state where the blend weight is zero.
The other located knobs have no observable here (battery_lib.PROBES[..]['why_no_obs']).
"""
import math, os, re, statistics, struct, sys

BUILD = "6AC07E31-2145C000"
BASE = 0x140000000
IMAGE_SIZE = 0x2145C000
C = {
    "G_ATTRIB": 0x14C26B128, "GETTER": 0x141077070, "G1_RVA": 0xC27F138,
    "TI_LLW": 0x14BE84240, "TI_GRP_PF": 0x14BE865A0, "TI_DB_GP": 0x14BE7F5E0,
    "DB_PF": 0x128, "GRP_ATT": 0x28, "GRP_DEF": 0x30, "LWM": 0x28, "B5": 0x38, "CB": 0x3C, "B3": 0x4C, "LEN_BAL": 0x30, "LEN_POS": 0x50,
    "M_T": 0xA10, "M_F": 0xA50, "T_TEAMS": 0x38, "TM_M": 0x10, "TM_IDX": 0x74, "TM_OWNER": 0x2250, "TM_TAC": 0x80,
    "OW_T": 0x08, "OW_TM": 0x10, "OW_F": 0x18, "OW_M": 0x30, "OW_KOBJ": 0x58, "OW_TEAM": 0x6C, "OW_STATE": 0xE0, "OW_BLOCK": 0x200,
    "ST_FLAGS": 0x08, "ST_MODE": 0x10, "ST_PHASE": 0x18, "ST_POSS": 0x20, "ST_LEN": 0x2C, "ST_CAP": 0x30, "ST_WIDTH": 0x34, "ST_T": 0x40,
    "ST_ZC": 0x50, "ST_BALL": 0x90, "ST_BACK": 0x1BE0,
    "K_BASE": 0x5B38, "K_STRIDE": 0x38, "K_ALT": 0x1C, "T_KFLAG": 0x35A1, "F_LEN": 0x10, "F_WID": 0x14,
    "TB_IN": 0x4D4, "TB_IN_LEN": 0x90, "TB_IN2": 0x7A8, "TB_STYLE": 0x554,
}
NAME_ATT = "Fifa/Attribulator/Gameplay/groups/gp_positioningformation/gp_positioningformation_attackinglinelengthwidth_runtime"
NAME_DEF = "Fifa/Attribulator/Gameplay/groups/gp_positioningformation/gp_positioningformation_defensivelinelengthwidth_runtime"
GETTER_PAT = "48 8B 0D ?? ?? ?? ?? 33 C0 48 85 C9 74 0C 48 8B 11 48 85 D2 74 04 48 8B 42 08 C3"
UPDATE_IDX = (0, 5, 7, 11)
G_RANGE = (0.85, 1.20)
H_RANGE = (0.50, 0.80)
EPS = 0.01
GUARD_S = 1.0
MIN_N = 6
CONFORM_SHARE, STALE_SHARE = 0.85, 0.80
MULTI_MAX = 200


class DevError(RuntimeError):
    pass


def H(a):
    return "0x%X" % a


def q(b, o=0):
    return struct.unpack_from("<Q", b, o)[0] if b is not None and len(b) >= o + 8 else None


def i32(b, o=0):
    return struct.unpack_from("<i", b, o)[0] if b is not None and len(b) >= o + 4 else None


def f32(b, o=0):
    return struct.unpack_from("<f", b, o)[0] if b is not None and len(b) >= o + 4 else None


def ptr_ok(v):
    return v is not None and 0x10000 <= v < 0x7FFFFFFFFFFF


def untag(v):
    return None if v is None else v & ~4


def pat_match(pattern, data):
    toks = pattern.split()
    return data is not None and len(data) >= len(toks) and all(t == "??" or int(t, 16) == data[i] for i, t in enumerate(toks))


def band_att(L):
    return (G_RANGE[0] * L - EPS, G_RANGE[1] * L + EPS)


def band_def(Ld):
    return (H_RANGE[0] * Ld - EPS, H_RANGE[1] * Ld + EPS)


def in_band(x, b):
    return x is not None and b[0] <= x <= b[1]


def bands_disjoint(b1, b2):
    return b1[1] < b2[0] or b2[1] < b1[0]


class DevMulti:
    """adapter: .multi(pairs) over a call(req) function (the dev-service client's call or a mock's); read-only by construction"""

    def __init__(self, call, timeout=30):
        self.call, self.timeout, self.calls = call, timeout, 0

    def multi(self, pairs):
        out = []
        for i in range(0, len(pairs), MULTI_MAX):
            ops = [{"op": "bytes", "addr": H(a), "len": int(n)} for a, n in pairs[i:i + MULTI_MAX]]
            assert all(o["op"] == "bytes" for o in ops)
            r = self.call({"op": "multi", "ops": ops}, timeout=self.timeout)
            if "results" not in r:
                raise DevError(str(r.get("error") or r))
            for x in r["results"]:
                out.append(bytes.fromhex(x["hex"]) if x.get("ok") and x.get("hex") is not None else None)
            self.calls += 1
        return out


# ------------------------------------------------------------------------------------------------------------- resolve
def resolve(dev, g1_rva=None, need_match=True):
    """-> (ctx, reasons). Same identity chain as x1p_check.resolve, plus the tactic block of each team."""
    bad = []
    ctx = {"build": None}
    hdr, gb = dev.multi([(BASE, 0x400), (C["GETTER"], 27)])
    if not hdr or hdr[:2] != b"MZ":
        return None, ["no MZ header at %s (image not at the preferred base?)" % H(BASE)]
    e = i32(hdr, 0x3C)
    if not (0 < e < 0x300) or hdr[e:e + 4] != b"PE\0\0":
        return None, ["bad PE header"]
    build = "%08X-%08X" % (struct.unpack_from("<I", hdr, e + 8)[0], struct.unpack_from("<I", hdr, e + 0x50)[0])
    ctx["build"] = build
    if build != BUILD:
        return None, ["build %s, this battery's constants are for %s" % (build, BUILD)]
    if not pat_match(GETTER_PAT, gb):
        return None, ["get_current_gameplay_attribdb bytes at %s do not match the signature" % H(C["GETTER"])]
    g_tgt = C["GETTER"] + 7 + struct.unpack_from("<i", gb, 3)[0]
    if g_tgt != C["G_ATTRIB"]:
        return None, ["getter reads %s, expected G %s" % (H(g_tgt), H(C["G_ATTRIB"]))]
    G1 = BASE + (g1_rva if g1_rva else C["G1_RVA"])
    ctx.update(G=C["G_ATTRIB"], G1=G1)
    mgr_b, m_b = dev.multi([(C["G_ATTRIB"], 8), (G1, 8)])
    mgr, M = q(mgr_b), q(m_b)
    if not ptr_ok(mgr):
        return None, ["attribute manager [G] = %s" % (H(mgr) if mgr is not None else "unreadable")]
    ctx.update(mgr=mgr, M=M)
    pairs = [(mgr, 8)]
    if need_match:
        if not ptr_ok(M):
            return None, ["match object [G1 %s] = %s: not in a running match" % (H(G1), H(M or 0))]
        pairs += [(M + C["M_T"], 8), (M + C["M_F"], 8)]
    r = dev.multi(pairs)
    A = q(r[0])
    if not ptr_ok(A):
        return None, ["[mgr] = %s" % A]
    ctx["A"] = A
    pairs = [(A + 8, 8)]
    if need_match:
        T, F = q(r[1]), q(r[2])
        if not (ptr_ok(T) and ptr_ok(F)):
            return None, ["M+0xA10 / M+0xA50 not pointers (%s / %s)" % (T, F)]
        ctx.update(T=T, F=F)
        pairs += [(T + C["T_TEAMS"], 8), (F + C["F_LEN"], 8)]
    r = dev.multi(pairs)
    DB = q(r[0])
    if not ptr_ok(DB):
        return None, ["[A+8] (current gameplay DB) = %s" % DB]
    ctx["DB"] = DB
    pairs = [(DB, 0x10), (DB + C["DB_PF"], 8)]
    if need_match:
        arr = q(r[1])
        flen, fwid = f32(r[2], 0), f32(r[2], 4)
        in_m = bool(flen and 90 <= flen <= 125 and fwid and 45 <= fwid <= 90)
        in_ft = bool(flen and 295 <= flen <= 410 and fwid and 148 <= fwid <= 296)
        if not ptr_ok(arr):
            return None, ["T+0x38 team array = %s" % arr]
        if not (in_m or in_ft):
            return None, ["pitch F+0x10/+0x14 = %s / %s (neither metres nor feet: implausible)" % (flen, fwid)]
        ctx.update(arr=arr, flen=flen, W=fwid, pitch_unit="m" if in_m else "ft")
        pairs += [(arr, 16)]
    r = dev.multi(pairs)
    if q(r[0], 8) != C["TI_DB_GP"]:
        bad.append("current DB TypeInfo %s != gameplayattribdb %s" % (H(q(r[0], 8) or 0), H(C["TI_DB_GP"])))
    grp = untag(q(r[1]))
    if not ptr_ok(grp):
        return None, bad + ["[DB+0x128] (gp_positioningformation group) = %s" % grp]
    ctx["grp"] = grp
    pairs = [(grp, 0x40)]
    if need_match:
        tms = [q(r[2], 0), q(r[2], 8)]
        if not all(ptr_ok(t) for t in tms):
            return None, bad + ["team objects %s" % [H(t or 0) for t in tms]]
        ctx["tm"] = tms
        for tm in tms:
            pairs += [(tm + C["TM_M"], 8), (tm + C["TM_IDX"], 4), (tm + C["TM_OWNER"], 8), (tm + C["TM_TAC"], 8)]
    r = dev.multi(pairs)
    gb = r[0]
    if q(gb, 8) != C["TI_GRP_PF"]:
        bad.append("group TypeInfo %s != gp_positioningformation %s" % (H(q(gb, 8) or 0), H(C["TI_GRP_PF"])))
    inst, instd = untag(q(gb, C["GRP_ATT"])), untag(q(gb, C["GRP_DEF"]))
    if not (ptr_ok(inst) and ptr_ok(instd)):
        return None, bad + ["group slots +0x28 / +0x30 = %s / %s" % (inst, instd)]
    ctx.update(inst=inst, instd=instd)
    pairs = [(inst, 0x58), (instd, 0x58)]
    cells = []
    if need_match:
        owners, idxs = [], []
        for k, tm in enumerate(ctx["tm"]):
            tm_m, idx, ow, cell = q(r[1 + 4 * k]), i32(r[2 + 4 * k]), q(r[3 + 4 * k]), q(r[4 + 4 * k])
            if tm_m != ctx["M"]:
                bad.append("team %d: [tm+0x10] %s != M %s" % (k, H(tm_m or 0), H(ctx["M"])))
            if idx not in (0, 1):
                bad.append("team %d: [tm+0x74] = %s" % (k, idx))
            if not ptr_ok(ow):
                return None, bad + ["team %d: [tm+0x2250] (AiFormationPositioning) = %s" % (k, ow)]
            owners.append(ow)
            idxs.append(idx)
            cells.append(cell)
        if len(set(idxs)) != 2:
            bad.append("team indices %s are not {0, 1}" % idxs)
        ctx.update(owner=owners, idx=idxs, cell=cells)
        for ow in owners:
            pairs += [(ow, C["OW_BLOCK"])]
        pairs += [(c, 8) if ptr_ok(c) else (BASE, 8) for c in cells]
    r = dev.multi(pairs)
    ib, idb = r[0], r[1]
    for name, b in (("attacking llw", ib), ("defensive llw", idb)):
        if q(b, 8) != C["TI_LLW"]:
            bad.append("%s TypeInfo %s != %s" % (name, H(q(b, 8) or 0), H(C["TI_LLW"])))
    ctx["L0"], ctx["Ld0"] = f32(ib, C["LWM"]), f32(idb, C["LWM"])
    pairs = [(q(ib, 0x18) or BASE, len(NAME_ATT) + 1), (q(idb, 0x18) or BASE, len(NAME_DEF) + 1)]
    if need_match:
        tbs = []
        for k, ow in enumerate(ctx["owner"]):
            ob = r[2 + k]
            checks = (("+0x08 T", q(ob, C["OW_T"]), ctx["T"]), ("+0x10 tm", q(ob, C["OW_TM"]), ctx["tm"][k]),
                      ("+0x18 F", q(ob, C["OW_F"]), ctx["F"]), ("+0x30 M", q(ob, C["OW_M"]), ctx["M"]),
                      ("+0x6C team", i32(ob, C["OW_TEAM"]), ctx["idx"][k]))
            for nm, got, exp in checks:
                if got != exp:
                    bad.append("owner %d %s: %s != %s" % (k, nm, H(got) if isinstance(got, int) and got > 9 else got,
                                                           H(exp) if isinstance(exp, int) and exp > 9 else exp))
            if not (BASE <= (q(ob, 0) or 0) < BASE + IMAGE_SIZE):
                bad.append("owner %d vtable %s outside FC27.exe" % (k, H(q(ob, 0) or 0)))
            kobj = q(ob, C["OW_KOBJ"])
            if not ptr_ok(kobj):
                bad.append("owner %d [+0x58] = %s" % (k, kobj))
            ctx.setdefault("kobj", []).append(kobj)
            tbs.append(q(r[4 + k]))
        # the tactic block chain is [U] live (X1P never read it): an unreadable block only disables the paired-length observable
        ctx["tb_ok"] = [ptr_ok(tb) for tb in tbs]
        ctx["tb"] = [tb if ptr_ok(tb) else ctx["owner"][k] for k, tb in enumerate(tbs)]
    names = dev.multi([(p[0], p[1]) for p in pairs])
    if (names[0] or b"").rstrip(b"\0").decode("latin-1", "replace") != NAME_ATT:
        bad.append("attacking llw Name != %s" % NAME_ATT)
    if (names[1] or b"").rstrip(b"\0").decode("latin-1", "replace") != NAME_DEF:
        bad.append("defensive llw Name mismatch (read %r)" % (names[1] or b"")[:60])
    return (None if bad else ctx), bad


# ------------------------------------------------------------------------------------------------------------- sample
def sample_pairs(ctx):
    pairs = [(ctx["G"], 8), (ctx["mgr"], 8), (ctx["A"] + 8, 8), (ctx["DB"] + C["DB_PF"], 8), (ctx["grp"] + C["GRP_ATT"], 8),
             (ctx["grp"] + C["GRP_DEF"], 8), (ctx["inst"] + C["LWM"], 0x30), (ctx["instd"] + C["LWM"], 4),
             (ctx["F"] + C["F_LEN"], 8)]
    for k, ow in enumerate(ctx["owner"]):
        idx = ctx["idx"][k]
        pairs += [(ow, C["OW_BLOCK"]), (ow + C["OW_STATE"] + C["ST_BACK"], 4), (ctx["T"] + idx + C["T_KFLAG"], 1),
                  (ctx["kobj"][k] + C["K_BASE"] + idx * C["K_STRIDE"], 0x20), (ctx["tm"][k] + C["TM_TAC"], 8),
                  (ctx["tb"][k] + C["TB_IN"], C["TB_IN_LEN"]), (ctx["tb"][k] + C["TB_IN2"], 8)]
    return pairs


NPER = 7


def parse_sample(ctx, r, t):
    """-> (chain_ok, tab, [per-team record]) ; tab = the table values read from the engine-resolved instances"""
    chain = [q(r[0]), q(r[1]), q(r[2]), untag(q(r[3])), untag(q(r[4])), untag(q(r[5]))]
    want = [ctx["mgr"], ctx["A"], ctx["DB"], ctx["grp"], ctx["inst"], ctx["instd"]]
    chain_ok = chain == want
    lb = r[6]
    tab = {"L": f32(lb, 0), "B5": f32(lb, C["B5"] - C["LWM"]), "CB": f32(lb, C["CB"] - C["LWM"]), "B3": f32(lb, C["B3"] - C["LWM"]),
           "len_bal": f32(lb, C["LEN_BAL"] - C["LWM"]), "len_pos": f32(lb, C["LEN_POS"] - C["LWM"]), "Ld": f32(r[7], 0),
           "W": f32(r[8], 4)}
    recs = []
    s = C["OW_STATE"]
    for k in range(2):
        ob, bb, fb, kb, cellb, tbb, tb2 = r[9 + NPER * k: 9 + NPER * (k + 1)]
        idx = ctx["idx"][k]
        ident = (ob is not None and q(ob, C["OW_M"]) == ctx["M"] and q(ob, C["OW_TM"]) == ctx["tm"][k]
                 and q(ob, C["OW_KOBJ"]) == ctx["kobj"][k] and q(cellb) == ctx["cell"][k])
        flags = q(ob, s + C["ST_FLAGS"])
        mode = i32(ob, s + C["ST_MODE"])
        disp = None if flags is None else (0 if flags & 0x420 else mode)
        kflag = fb[0] if fb else None
        K = f32(kb, C["K_ALT"] if kflag else 0)
        style = i32(tbb, C["TB_STYLE"] - C["TB_IN"]) if (tbb and ctx["tb_ok"][k]) else None
        if style not in (0, 1, 2, 3):
            style = None             # the length function selects among four fields (0x50, 0x30, 0x2C, 0x54): anything else is not that field
        cap, wid, tval = f32(ob, s + C["ST_CAP"]), f32(ob, s + C["ST_WIDTH"]), f32(ob, s + C["ST_T"])
        # declared inputs of the length function (0x140F57098 [V OLD]): the state header and ball vector, the owner's own floats
        # and back-pointers, the tactic block words it reads, the K pair, the pitch length - everything except the outputs
        inp = None
        if ob is not None and tbb is not None and tb2 is not None and style is not None:
            hdr = ob[:C["OW_STATE"]]
            st = ob[s:s + 0xB0]
            st_in = st[:C["ST_LEN"]] + st[C["ST_LEN"] + 12:]  # without +0x2C (length), +0x30 (cap), +0x34 (width)
            inp = (hdr, st_in, tbb, tb2, kb or b"", fb or b"", r[8] or b"")
        rec = {"t": round(t, 6), "team": idx, "ident": int(bool(ident)), "chain": int(chain_ok), "flags": flags, "mode": mode, "disp": disp,
               "phase": i32(ob, s + C["ST_PHASE"]), "poss": i32(ob, s + C["ST_POSS"]), "tval": tval, "cap": cap, "width": wid,
               "length": f32(ob, s + C["ST_LEN"]), "back": i32(bb, 0), "kflag": kflag, "K": K, "style": style, "inp": inp,
               "W": tab["W"], "L": tab["L"], "Ld": tab["Ld"], "B5": tab["B5"], "B3": tab["B3"], "len_bal": tab["len_bal"],
               "len_pos": tab["len_pos"]}
        recs.append(rec)
    return chain_ok, tab, recs


def derive(rec):
    """adds a = cap/(W K), classes (t1: with the ball, t0: other team has it), eligibility. Pure function of the record."""
    W, K, cap, wid, tv = rec.get("W"), rec.get("K"), rec.get("cap"), rec.get("width"), rec.get("tval")
    ok = all(isinstance(x, (int, float)) and math.isfinite(x) for x in (W, K, cap, wid, tv)) and W and W > 0
    rec["k_ok"] = int(bool(ok and 0.3 <= K <= 3.0))
    rec["a"] = (cap / (W * K)) if ok and rec["k_ok"] else None
    rec["capW"] = (cap / W) if ok else None
    rec["t1"] = int(bool(ok and abs(tv - 1.0) < 1e-6))
    rec["t0"] = int(bool(ok and abs(tv) < 1e-6))
    rec["reset"] = int(bool(ok and abs(cap - W) < 1e-4))
    rec["clamped"] = int(bool(ok and cap >= 1.2 * W - 1e-3))
    plaus = ok and 0.0 < cap <= 1.2 * W + 1e-3 and 0.0 <= tv <= 1.0 and rec.get("poss") in (-1, 0, 1) \
        and rec.get("mode") is not None and 0 <= rec["mode"] < 64
    rec["plaus"] = int(bool(plaus))
    rec["upd"] = int(rec.get("disp") in UPDATE_IDX)
    rec["elig"] = int(bool(plaus and rec["ident"] and rec["chain"] and rec["upd"] and not rec["reset"] and not rec["clamped"]
                           and rec["k_ok"]))
    return rec


# ------------------------------------------------------------------------------------------------------------- analysis: bands
def band_window(recs, kind, value, prev_value, alt_value=None):
    """recs: derived records of ONE window (guard samples already dropped). kind 'band_att' (t1, a vs g*L) or 'band_def' (t0, a vs h*Ld).
    -> dict(status CONFORM|STALE|MIXED|THIN, n, in, in_prev, a_med, disc). prev_value = the vanilla value the window followed."""
    cls = "t1" if kind == "band_att" else "t0"
    bf = band_att if kind == "band_att" else band_def
    el = [r for r in recs if r["elig"] and r[cls]]
    lk = "L" if kind == "band_att" else "Ld"
    # the band is built from the table value READ from the engine-resolved instance in the same dev call (not from the schedule)
    n_in = sum(1 for r in el if in_band(r["a"], bf(r[lk])))
    n_prev = sum(1 for r in el if (prev_value is not None and in_band(r["a"], bf(prev_value)))
                 or (alt_value is not None and in_band(r["a"], bf(alt_value))))   # alt = the pre-kick-off value a set-up latch would keep
    vals = [r[lk] for r in el if r[lk] is not None]
    cur = statistics.median(vals) if vals else value
    disc = prev_value is not None and bands_disjoint(bf(cur), bf(prev_value))
    out = {"n": len(el), "in": n_in, "in_prev": n_prev, "a_med": statistics.median([r["a"] for r in el]) if el else None,
           "disc": disc, "read_value": cur}
    if len(el) < MIN_N:
        out["status"] = "THIN"
    elif n_in >= CONFORM_SHARE * len(el):
        out["status"] = "CONFORM"
    elif (prev_value is not None or alt_value is not None) and n_prev >= STALE_SHARE * len(el):
        out["status"] = "STALE"
    else:
        out["status"] = "MIXED"
    return out


def first_conform_latency(recs, kind, t_write):
    """seconds from the write to the first eligible sample inside the new band (None if none); lag = eligible samples before it"""
    cls = "t1" if kind == "band_att" else "t0"
    bf = band_att if kind == "band_att" else band_def
    lk = "L" if kind == "band_att" else "Ld"
    lag = 0
    for r in sorted(recs, key=lambda r: r["t"]):
        if r["t"] < t_write or not (r["elig"] and r[cls]):
            continue
        if in_band(r["a"], bf(r[lk])):
            return r["t"] - t_write, lag
        lag += 1
    return None, lag


def control_ok(recs, kind):
    """the built-in control: samples of the OPPOSITE ball context must stay in their own band whatever the knob (att: t0 vs h*Ld;
    def: t1 vs g*L) - proves the model and the offsets on this build. -> (ok, n_in, n)"""
    if kind == "band_att":
        el = [r for r in recs if r["elig"] and r["t0"]]
        n_in = sum(1 for r in el if in_band(r["a"], band_def(r["Ld"] or 1.0)))
    else:
        el = [r for r in recs if r["elig"] and r["t1"]]
        n_in = sum(1 for r in el if in_band(r["a"], band_att(r["L"] or 1.0)))
    return (len(el) >= MIN_N and n_in >= 0.8 * len(el)), n_in, len(el)


def judge_band_knob(kind, recs_by_win, windows, writes):
    """recs_by_win: {window name: derived records (guard dropped)} ; windows: plan list (kind base/ext/pre, knob, value, t0, t1);
    writes: [{t, knob, value}] -> dict(verdict PASS|FAIL|INCONCLUSIVE|NOT_EXERCISED, why, rows, live, latency)"""
    ext = [w for w in windows if w["kind"] in ("ext", "pre")]
    allr = [r for k, rs in recs_by_win.items() if k != "x" for r in rs]
    lat_all = sorted(allr + list(recs_by_win.get("x", [])), key=lambda r: r["t"])  # guard-time records count for the latency only
    vanilla = [w for w in windows if w["kind"] == "base"]
    rows, lats = [], []
    pre_w = next((w for w in ext if w["kind"] == "pre"), None)
    for w in ext:
        prev = 1.0 if kind in ("band_att", "band_def") else None
        alt = pre_w["value"] if (pre_w is not None and w["kind"] == "ext" and abs(pre_w["value"] - w["value"]) > 1e-6) else None
        res = band_window(recs_by_win.get(w["name"], []), kind, w["value"], prev, alt)
        res["name"], res["value"], res["kind"] = w["name"], w["value"], w["kind"]
        tw = next((x["t"] for x in writes if x["knob"] == w["knob"] and abs(x["value"] - w["value"]) < 1e-6 and x["t"] <= (w.get("t0") or 1e18)),
                  w.get("t0") or 0.0)
        t_end = w.get("t1") or 1e18
        res["latency"], res["lag"] = first_conform_latency([r for r in lat_all if tw <= r["t"] < t_end], kind, tw)
        if res["latency"] is not None:
            lats.append(res["latency"])
        rows.append(res)
    van_rows = []
    for w in vanilla:
        rs = recs_by_win.get(w["name"], [])
        van_rows.append(band_window(rs, kind, 1.0, None))
    ctrl, c_in, c_n = control_ok(allr, kind)
    judged = [r for r in rows if r["status"] != "THIN" and r["disc"]]
    van_conform = [v for v in van_rows if v["status"] == "CONFORM"]
    out = {"rows": rows, "vanilla": van_rows, "control": [c_in, c_n], "latency": max(lats) if lats else None}
    if not rows:
        return dict(out, verdict="INCONCLUSIVE", why="no extreme window", live=None)
    if not judged:
        thin = all(r["status"] == "THIN" for r in rows)
        return dict(out, verdict="NOT_EXERCISED" if thin else "INCONCLUSIVE", live=None,
                    why=("no eligible %s samples in any extreme window (the team never had / lacked the ball, or the value clamps)" % (
                        "with-ball" if kind == "band_att" else "without-ball")) if thin else
                    "the extreme's band is not disjoint from vanilla's (choose a lower value)")
    pre = [r for r in judged if r["kind"] == "pre"]
    mid = [r for r in judged if r["kind"] == "ext"]
    if all(r["status"] == "CONFORM" for r in judged):
        lat = out["latency"]
        what = "live (per tick)" if mid else "the value present at kick-off is used; mid-match behaviour undecided"
        return dict(out, verdict="PASS", live=bool(mid) or None, why="every judged window conforms to the band of the value read from the "
                    "engine-resolved instance (%d window(s)); %s%s" % (len(judged), what, "; latency <= %.2f s" % lat if lat is not None else ""))
    if pre and pre[0]["status"] == "CONFORM" and mid and all(r["status"] in ("STALE", "MIXED") for r in mid) and \
            any(r["status"] == "STALE" for r in mid):
        return dict(out, verdict="PASS", live=False, why="set-up latch: the pre-kick-off value is used, mid-match writes are not followed")
    if all(r["status"] == "STALE" for r in judged) and van_conform and ctrl:
        return dict(out, verdict="FAIL", live=False, why="the formation stays in the vanilla band with the extreme value written and read back "
                    "from the resolved instance, while the vanilla windows and the control conform: the value is ignored or latched at set-up "
                    "(add --prewrite to separate the two)")
    return dict(out, verdict="INCONCLUSIVE", live=None, why="mixed windows: %s" % ", ".join("%s:%s" % (r["name"], r["status"]) for r in rows))


# ------------------------------------------------------------------------------------------------------------- analysis: paired
def _same(a, b):
    return a is not None and b is not None and a == b


def paired_events(reads, writes, style, key="length", settle=0.15):
    """reads: derived records (all teams, time order) of teams whose style == style ; writes: [{t, knob, value}] of THIS knob.
    Event = a write with two frozen reads before it (inputs and output equal) and at least one read after it with the inputs still
    equal to the pre-write inputs. Response = the output differs from the pre-write output at that read.
    Control = every frozen pair followed by a read with equal inputs, no write in between. -> dict"""
    by_team = {}
    for r in reads:
        if r.get("style") == style and r.get("inp") is not None and r.get("elig", 1) is not None:
            by_team.setdefault(r["team"], []).append(r)
    events, ctrl = [], []
    wt = sorted(w["t"] for w in writes)
    for team, rs in by_team.items():
        rs.sort(key=lambda r: r["t"])
        for w in writes:
            pre = [r for r in rs if r["t"] <= w["t"]][-2:]
            post = [r for r in rs if r["t"] >= w.get("t_end", w["t"] + settle)][:2]
            if len(pre) < 2 or not post:
                continue
            frozen = _same(pre[0]["inp"], pre[1]["inp"]) and pre[0][key] == pre[1][key]
            if not frozen:
                events.append({"t": w["t"], "team": team, "value": w["value"], "valid": False, "why": "not frozen before the write"})
                continue
            p = post[0]
            if not _same(p["inp"], pre[1]["inp"]):
                events.append({"t": w["t"], "team": team, "value": w["value"], "valid": False, "why": "inputs moved across the write"})
                continue
            resp = p[key] is not None and pre[1][key] is not None and abs(p[key] - pre[1][key]) > max(1e-3, 1e-4 * abs(pre[1][key]))
            events.append({"t": w["t"], "team": team, "value": w["value"], "valid": True, "response": bool(resp), "dt": p["t"] - w["t"],
                           "before": pre[1][key], "after": p[key]})
        for a, b, c in zip(rs, rs[1:], rs[2:]):
            if any(a["t"] < x < c["t"] for x in wt):
                continue
            if _same(a["inp"], b["inp"]) and a[key] == b[key] and _same(b["inp"], c["inp"]):
                ctrl.append(bool(c[key] is not None and b[key] is not None and abs(c[key] - b[key]) > max(1e-3, 1e-4 * abs(b[key]))))
    valid = [e for e in events if e["valid"]]
    return {"events": events, "valid": len(valid), "responses": sum(1 for e in valid if e["response"]), "control_n": len(ctrl),
            "control_resp": sum(ctrl)}


def judge_paired(reads, writes, style, min_events=1, max_ctrl_rate=0.05, min_ctrl=20):
    """-> dict(verdict PASS|INCONCLUSIVE|NOT_EXERCISED, why, detail). Never FAIL (an unchanged output can be a zero blend weight)."""
    teams = {r["team"] for r in reads if r.get("style") == style}
    if not teams:
        styles = sorted({r.get("style") for r in reads if r.get("style") is not None})
        return {"verdict": "NOT_EXERCISED", "why": "no team has build-up style %s in this match (styles seen: %s)" % (style, styles), "detail": {}}
    d = paired_events(reads, writes, style)
    if d["valid"] < min_events:
        return {"verdict": "INCONCLUSIVE", "why": "only %d valid frozen-input write events (need %d); events: %s" % (
            d["valid"], min_events, [e.get("why", "ok") for e in d["events"]]), "detail": d}
    if d["responses"] < d["valid"]:
        return {"verdict": "INCONCLUSIVE", "why": "%d of %d valid events changed the output (a state with zero blend weight cannot be told "
                "from an ignored value)" % (d["responses"], d["valid"]), "detail": d}
    if d["control_n"] < min_ctrl:
        return {"verdict": "INCONCLUSIVE", "why": "all %d valid events responded but only %d control pairs exist (need %d)" % (
            d["valid"], d["control_n"], min_ctrl), "detail": d}
    rate = d["control_resp"] / d["control_n"]
    if rate > max_ctrl_rate:
        return {"verdict": "INCONCLUSIVE", "why": "the control (frozen pair, no write) responded in %.0f %% of %d pairs: hidden inputs" % (
            100 * rate, d["control_n"]), "detail": d}
    return {"verdict": "PASS", "why": "all %d frozen-input write events changed the output (response latency <= %.2f s), control %d/%d" % (
        d["valid"], max(e["dt"] for e in d["events"] if e["valid"]), d["control_resp"], d["control_n"]), "detail": d}


def wait_frozen(dev, ctx, pairs, team_style, style, timeout, clock, sleep, gap=0.35):
    """poll until two consecutive reads of a team with `style` have equal inputs and output; True when frozen within timeout"""
    t_end = clock() + timeout
    prev = {}
    while clock() < t_end:
        r = dev.multi(pairs)
        chain_ok, tab, trs = parse_sample(ctx, r, clock())
        for rec in trs:
            derive(rec)
            if rec["style"] == style and rec["inp"] is not None:
                p = prev.get(rec["team"])
                if p is not None and _same(p["inp"], rec["inp"]) and p["length"] == rec["length"]:
                    return True
                prev[rec["team"]] = rec
        sleep(gap)
    return False


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        sys.stdout.flush()
        ok = ok and cond

    # pure analysis checks on hand-made records (the mock-world tests are in battery_write.py --self-test)
    def rec(t, team, a_cap, tv, L=1.0, Ld=1.0, style=1, inp=b"x", length=100.0, W=100.0, K=1.0):
        return derive({"t": t, "team": team, "ident": 1, "chain": 1, "flags": 0, "mode": 0, "disp": 0, "phase": 0, "poss": 0, "tval": tv,
                       "cap": a_cap * W * K, "width": 50.0, "length": length, "back": 4, "kflag": 0, "K": K, "style": style, "inp": inp,
                       "W": W, "L": L, "Ld": Ld, "B5": 0.8, "B3": 0.7, "len_bal": 0.84, "len_pos": 0.8})
    ok_att = [rec(i * 0.5, i % 2, 0.3 + 0.01 * (i % 3), 1.0, L=0.3) for i in range(20)]
    r = band_window(ok_att, "band_att", 0.3, 1.0)
    check(r["status"] == "CONFORM" and r["disc"] and r["n"] == 20, "band_att: a = 0.30 with L read 0.3 -> CONFORM, disjoint from vanilla's band")
    stale = [rec(i * 0.5, i % 2, 0.97, 1.0, L=0.3) for i in range(20)]
    check(band_window(stale, "band_att", 0.3, 1.0)["status"] == "STALE", "band_att: a stays 0.97 although L reads 0.3 -> STALE")
    check(band_window(ok_att[:4], "band_att", 0.3, 1.0)["status"] == "THIN", "band_att: 4 samples are THIN")
    clamp = [rec(i * 0.5, 0, 1.5, 1.0, L=1.7) for i in range(20)]
    check(band_window(clamp, "band_att", 1.7, 1.0)["n"] == 0, "band_att: clamped samples (cap >= 1.2 W) are not eligible")
    dfn = [rec(i * 0.5, i % 2, 0.18 + 0.01 * (i % 3), 0.0, Ld=0.3) for i in range(20)]
    rd = band_window(dfn, "band_def", 0.3, 1.0)
    check(rd["status"] == "CONFORM" and rd["disc"], "band_def: a = 0.18..0.20 with Ldef read 0.3 and t == 0 -> CONFORM")
    wins = [{"name": "A1", "kind": "base", "knob": None, "value": None}, {"name": "E1", "kind": "ext", "knob": "k", "value": 0.3, "t0": 10.0},
            {"name": "A2", "kind": "base", "knob": None, "value": None}]
    base = [rec(i * 0.5, i % 2, 0.95, 1.0, L=1.0) for i in range(20)] + [rec(i * 0.5, i % 2, 0.65, 0.0, Ld=1.0) for i in range(20)]
    j = judge_band_knob("band_att", {"A1": base, "E1": ok_att, "A2": base}, wins, [{"t": 10.0, "knob": "k", "value": 0.3}])
    check(j["verdict"] == "PASS" and j["live"] is True, "judge_band_knob: all windows conform -> PASS live (%s)" % j["why"][:60])
    j = judge_band_knob("band_att", {"A1": base, "E1": stale, "A2": base}, wins, [{"t": 10.0, "knob": "k", "value": 0.3}])
    check(j["verdict"] == "FAIL" and j["live"] is False, "judge_band_knob: extreme stale, vanilla + control conform -> FAIL (ignored / latched)")
    j = judge_band_knob("band_att", {"A1": base, "E1": ok_att[:3], "A2": base}, wins, [])
    check(j["verdict"] == "NOT_EXERCISED", "judge_band_knob: no eligible sample in the extreme window -> NOT_EXERCISED")
    pre_recs = [rec(i * 0.5, i % 2, 0.30, 1.0, L=0.3) for i in range(20)]
    latched = [rec(i * 0.5, i % 2, 0.31, 1.0, L=0.6) for i in range(20)]       # the engine keeps the pre-kick-off 0.3 although 0.6 is written
    wl_ = [{"name": "A1", "kind": "base", "knob": None, "value": None}, {"name": "P1", "kind": "pre", "knob": "k", "value": 0.3, "t0": 0.0},
           {"name": "E1", "kind": "ext", "knob": "k", "value": 0.6, "t0": 10.0}]
    jl = judge_band_knob("band_att", {"A1": base, "P1": pre_recs, "E1": latched}, wl_, [{"t": 10.0, "knob": "k", "value": 0.6}])
    check(jl["verdict"] == "PASS" and jl["live"] is False and "latch" in jl["why"], "judge_band_knob: pre window conforms, the extreme keeps the pre value -> set-up latch (%s)" % jl["why"][:50])
    live_ = [rec(i * 0.5, i % 2, 0.62, 1.0, L=0.6) for i in range(20)]
    jl2 = judge_band_knob("band_att", {"A1": base, "P1": pre_recs, "E1": live_}, wl_, [{"t": 10.0, "knob": "k", "value": 0.6}])
    check(jl2["verdict"] == "PASS" and jl2["live"] is True, "judge_band_knob: pre window and the extreme both conform -> live")
    # paired events
    def mk(frozen_response=True, control_noise=False):
        reads, t = [], 0.0
        out = 100.0
        for i in range(30):
            t += 0.4
            if i == 12 and frozen_response:
                out = 80.0
            reads.append(rec(t, 0, 0.5, 1.0, inp=b"frozen", length=out))
        if control_noise:
            for i, r_ in enumerate(reads):
                r_["length"] += 0.5 * (i % 2)
        return reads
    wr = [{"t": 12 * 0.4 + 0.4 - 0.1, "knob": "k", "value": 0.5}]
    reads = mk()
    jp = judge_paired([r for r in reads], wr + [{"t": 20 * 0.4 + 0.3, "knob": "k", "value": 1.0}], 1)
    check(jp["verdict"] in ("PASS", "INCONCLUSIVE") and jp["detail"]["valid"] >= 1, "paired: frozen events are found (%s)" % jp["why"][:70])
    reads2 = [dict(r_) for r_ in reads]
    reads3 = [dict(r_, style=2) for r_ in reads]
    check(judge_paired(reads3, wr, 1)["verdict"] == "NOT_EXERCISED", "paired: no team with the knob's style -> NOT_EXERCISED")
    moving = [dict(r_, inp=("m%d" % i).encode()) for i, r_ in enumerate(reads2)]
    check(judge_paired(moving, wr, 1)["verdict"] == "INCONCLUSIVE", "paired: inputs always moving -> no valid event -> INCONCLUSIVE, never FAIL")
    none_resp = mk(frozen_response=False)
    jn = judge_paired(none_resp, wr + [{"t": 8.0, "knob": "k", "value": 1.0}], 1)
    check(jn["verdict"] == "INCONCLUSIVE", "paired: frozen inputs but the output never changes -> INCONCLUSIVE, never FAIL")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    print(__doc__)
    sys.exit(2)
