"""consumers_obs.py - Turbo 2.6 track "consumers": read-only observables for the engine tables that move play (tools/; never writes).

Companion data: docs/re/consumers_6AC07E31.json (per knob: my ranking, the consumer read site found in the OLD image 6AB9813C with a byte
signature, how fresh the read is, which database copy it sees, the observable and the on-pitch metric, proposed extremes). Everything about
the live build 6AC07E31-2145C000 is [A] until the coordinator's check passes; the old-image facts are [V OLD] where the data file says V.

Observables are plain functions returning (verdict, evidence) like battery_engine.py. They get a `dev` with .multi(pairs) (battery_engine.DevMulti
over dev2, or over the offline mock world) and only ever issue `bytes` reads:
  obs_instance(dev, knob_id, cat)            generic chain identity for ANY catalogue knob: [G] -> manager -> current database (gameplay or
                                             prematch handle) -> [db + family_slot] & ~4 -> [group + group_slot] & ~4 -> instance; vtable, TypeInfo,
                                             Name (case-insensitive against the locator's variants) and the value; says WHICH COPY (base /
                                             simulation / other) the chain the consumers read resolves to. PASS | INCONCLUSIVE | REFUSED.
  obs_readback(dev, evidence, knob, expect)  the field holds the value just written (type-exact). PASS | FAIL.
  obs_latched_copy(dev, ctx, cat, want_min, want_max)   the match-construction copy of the defensive-line slider range (customization group)
                                             at [K object + 0x5BA0 / 0x5BA4] [A] (the K object = [owner + 0x58], battery_engine's chain).
                                             PASS (copy = value written before kick-off) | FAIL (copy kept another value: written too late or not
                                             read) | INCONCLUSIVE (object not identified).
  extra_metrics / extra_records / judge_pair  phase-conditioned route-C metrics for the knobs without a persistent engine observable (pressing
                                             density, last-defender depth, keeper depth, attackers beyond the line) and the two-arm judge (reuses
                                             battery_lib.judge, so the bar is the battery's: |shift| >= dmin and z >= 3 with the expected sign).
  plan_windows(sd, dmin, p_phase, ...)       how many windows per arm a metric needs (the bar to beat: width moved ~3 m against an MDE of ~7 m).
  python tools/consumers_obs.py --table [--top N]    the ranked table;  --check-data  validates the data file against the catalogue;
  python tools/consumers_obs.py probe <knob id> [--mock]    one read-only identity probe (live: GAME_STATE must allow it);  --self-test
  python tools/consumers_obs.py press-watch secs=90 expect=25 stale=6 [wr=2]   |   press-watch secs=120 rest=0,4.5 old=7,15     (live, read-only)
"""
import argparse, json, math, os, statistics, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import battery_engine as be  # noqa: E402
import battery_lib as bl  # noqa: E402
import harness  # noqa: E402

DATA = os.path.join(ROOT, "docs", "re", "consumers_6AC07E31.json")
CAT = os.path.join(ROOT, "turbo", "package", "turbo", "tuning", "knobs_6AC07E31.json")
INV = os.path.join(ROOT, "docs", "re", "attrib_inventory_6AC07E31.json")
KOBJ_DEFLINE_MIN, KOBJ_DEFLINE_MAX = 0x5BA0, 0x5BA4     # [A] constructor copy 0x146A521EC (OLD): [r11+0x5BA0] <- +0x178, [r11+0x5BA4] <- +0x110
SIM_PATH = "/databases/simulation/"
FMT = {"f32": "<f", "i32": "<i", "u32": "<I", "bool": "<B", "i8": "<b", "u8": "<B"}
TAU = 3.0                    # autocorrelation time in samples (battery_lib.window_means)
Z_MDE = getattr(bl, "Z_MDE", 2.8)


def load_data(path=DATA):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load_cat(path=CAT):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _knob(cat, kid):
    return next((k for k in cat["knobs"] if k["id"] == kid), None)


def _loc(cat, knob):
    return next((l for l in cat["locators"] if l["name"] == knob["locator"]), None)


def _hexint(v):
    return int(v, 16) if isinstance(v, str) else int(v)


def _cstr(b):
    if not b:
        return None
    return b.split(b"\0", 1)[0].decode("latin-1", "replace")


# ------------------------------------------------------------------------------------------------------------- identity
def check_build(dev):
    """-> (ok, text): the PE header at the preferred base names build 6AC07E31-2145C000 (same rule as battery_engine.resolve)"""
    hdr = dev.multi([(be.BASE, 0x400)])[0]
    if not hdr or hdr[:2] != b"MZ":
        return False, "no MZ header at %s" % be.H(be.BASE)
    e = be.i32(hdr, 0x3C)
    if not (0 < e < 0x300) or hdr[e:e + 4] != b"PE\0\0":
        return False, "bad PE header"
    build = "%08X-%08X" % (struct.unpack_from("<I", hdr, e + 8)[0], struct.unpack_from("<I", hdr, e + 0x50)[0])
    return build == be.BUILD, build


def obs_instance(dev, knob_id, cat=None):
    """Generic chain identity of the instance a consumer reads for `knob_id`. -> (verdict, evidence).
    PASS: every pointer valid, vtable inside FC27.exe, Name matches a variant of the locator (case-insensitive) -> evidence["copy"] says which.
    INCONCLUSIVE: chain fine but the Name is unreadable / matches no variant.  REFUSED: a pointer or the build is wrong (never a verdict)."""
    cat = cat or load_cat()
    knob = _knob(cat, knob_id)
    ev = {"knob": knob_id}
    if knob is None:
        return "REFUSED", dict(ev, why="unknown knob")
    loc = _loc(cat, knob)
    ok, build = check_build(dev)
    if not ok:
        return "REFUSED", dict(ev, why="build %s is not %s" % (build, be.BUILD))
    prematch = "prematch" in loc.get("db_type", "")
    fam, slot, off = _hexint(loc["family_slot"]), _hexint(loc["group_slot"]), _hexint(knob["offset"])
    mgr = be.q(dev.multi([(be.C["G_ATTRIB"], 8)])[0])
    if not be.ptr_ok(mgr):
        return "REFUSED", dict(ev, why="attribute manager [G] = %s" % mgr)
    handle = be.q(dev.multi([(mgr + (8 if prematch else 0), 8)])[0])
    if not be.ptr_ok(handle):
        return "REFUSED", dict(ev, why="manager handle = %s" % handle)
    db = be.q(dev.multi([(handle + 8, 8)])[0])
    if not be.ptr_ok(db):
        return "REFUSED", dict(ev, why="current %s database = %s" % ("prematch" if prematch else "gameplay", db))
    gr = be.q(dev.multi([(db + fam, 8)])[0])
    grp = be.untag(gr)
    if not be.ptr_ok(grp):
        return "REFUSED", dict(ev, why="[db + %s] = %s" % (be.H(fam), gr))
    ir = be.q(dev.multi([(grp + slot, 8)])[0])
    inst = be.untag(ir)
    if not be.ptr_ok(inst):
        return "REFUSED", dict(ev, why="[group + %s] = %s" % (be.H(slot), ir))
    vt_ti, nameptr_b = dev.multi([(inst, 0x20), (inst + 0x18, 8)])
    vt, ti, nptr = be.q(vt_ti, 0), be.q(vt_ti, 8), be.q(nameptr_b)
    ev.update(db=db, group=grp, inst=inst, db_kind="prematch" if prematch else "gameplay", group_tag=int(bool((gr or 0) & 4)),
              inst_tag=int(bool((ir or 0) & 4)), vtable=vt, typeinfo=ti)
    if not (be.BASE <= (vt or 0) < be.BASE + be.IMAGE_SIZE) or not be.ptr_ok(ti):
        return "REFUSED", dict(ev, why="vtable %s / TypeInfo %s do not look like an engine object" % (vt, ti))
    name = _cstr(dev.multi([(nptr, 160)])[0]) if be.ptr_ok(nptr) else None
    typ = knob.get("type", "f32")
    fmt = FMT.get(typ)
    vb = dev.multi([(inst + off, struct.calcsize(fmt) if fmt else 4)])[0]
    val = struct.unpack(fmt, vb)[0] if (fmt and vb and len(vb) == struct.calcsize(fmt)) else None
    ev.update(name=name, field_addr=inst + off, value=val)
    variants = {v["name"]: v["instance"].lower() for v in loc["variants"]}
    copy = None
    if name:
        low = name.lower()
        for vn, path in variants.items():
            if low == path:
                copy = vn
        if copy is None and SIM_PATH in low:
            copy = "simulation"
    ev["copy"] = copy
    ev["variants"] = sorted(variants)
    ev["sim_instance_exists"] = "simulation" in variants
    dflt = knob.get("default")
    cd = (knob.get("copy_defaults") or {})
    ref = cd.get(copy, dflt) if copy else dflt
    if val is not None and ref is not None:
        ev["equals_fc27_value"] = abs(val - float(ref)) <= 1e-4 * max(1.0, abs(float(ref)))
    if not name or copy is None:
        return "INCONCLUSIVE", dict(ev, why="Name %r matches no variant of the locator (%s); the instance may be a database copy the catalogue does not list" % (
            name, ", ".join(sorted(variants.values()))))
    if SIM_PATH in (name or "").lower():
        ev["note"] = "this chain resolves to the SIMULATION copy: the one a played match reads (TEST_27)"
    elif loc.get("variants") and len(variants) == 1:
        ev["note"] = "the locator lists one instance: shared by every database copy [A]"
    else:
        ev["note"] = "BASE copy: if the match runs on the simulation database a write here is invisible to it"
    return "PASS", dict(ev, why="chain resolves to the %s instance %s" % (copy, name))


def obs_readback(dev, ev, knob, expect):
    """the field at ev['field_addr'] holds `expect` now (type-exact for ints / bools, 1e-6 relative for f32). -> (verdict, evidence)"""
    typ = knob.get("type", "f32")
    fmt = FMT[typ]
    b = dev.multi([(ev["field_addr"], struct.calcsize(fmt))])[0]
    if b is None or len(b) != struct.calcsize(fmt):
        return "INCONCLUSIVE", {"why": "field unreadable"}
    got = struct.unpack(fmt, b)[0]
    exp = struct.unpack(fmt, struct.pack(fmt, float(expect) if typ == "f32" else int(expect)))[0]
    same = abs(got - exp) <= 1e-6 * max(1.0, abs(exp)) if typ == "f32" else got == exp
    return ("PASS" if same else "FAIL"), {"got": got, "expected": exp, "field_addr": ev["field_addr"]}


def obs_latched_copy(dev, ctx, cat, want_min, want_max, k=0, default=(-7.5, 7.5)):
    """The customization group is copied into the match-settings object when the match is constructed (OLD 0x146A5EBB4 -> 0x146A521EC, [A] that this
    object is the K object [owner + 0x58]). With the values written BEFORE kick-off the copy must hold them: PASS; if it still holds FC 27's
    values the write came too late / is not read: FAIL; anything else (not finite, outside +/-40) = the object is not identified: INCONCLUSIVE."""
    kobj = ctx["kobj"][k]
    b = dev.multi([(kobj + KOBJ_DEFLINE_MIN, 8)])[0]
    if b is None or len(b) != 8:
        return "INCONCLUSIVE", {"why": "K object +0x5BA0 unreadable"}
    cmin, cmax = struct.unpack("<ff", b)
    ev = {"kobj": kobj, "copy_min": cmin, "copy_max": cmax, "want_min": want_min, "want_max": want_max}
    if not (all(math.isfinite(x) and abs(x) <= 40.0 for x in (cmin, cmax)) and cmin <= 0.0 <= cmax):
        return "INCONCLUSIVE", dict(ev, why="[K + 0x5BA0 / 0x5BA4] = %.4g / %.4g is not a plausible slider range: the object is not identified" % (cmin, cmax))
    near = lambda a, bb: abs(a - bb) <= 1e-4 * max(1.0, abs(bb))
    if near(cmin, want_min) and near(cmax, want_max):
        return "PASS", dict(ev, why="the construction copy holds the written range [A: identity of the object rests on the plausibility check and the constructor trace]")
    if near(cmin, default[0]) and near(cmax, default[1]) and not (near(want_min, default[0]) and near(want_max, default[1])):
        return "FAIL", dict(ev, why="the copy still holds FC 27's range: the write came after the match was constructed (latched) or the table is not the one copied")
    return "INCONCLUSIVE", dict(ev, why="the copy holds neither the written nor FC 27's range")


# ------------------------------------------------------------------------------------------------------------- phase metrics
def _fin(p):
    return p is not None and all(math.isfinite(c) for c in p)


def extra_metrics(rows, opp_rows, L, along, lat, ball, carrier_pos, ctx):
    """Phase-conditioned metrics for one team of ONE sample (rows/opp_rows: [{fpos, p}], like battery_lib.team_metrics). ctx 'without' = the
    team that does not have the ball, 'with' = the carrier's team. Returns {} when the sample is outside the phase of a metric.
    depth = metres from the team's own goal line. cd = the carrier's depth as a share of the pitch length from the DEFENDING team's goal.
      press_n8   (without) defenders within 8 m of the carrier, carrier at cd 0.30..0.85           (pressing density)
      press_min  (without) nearest outfield defender to the carrier, same phase                      (who closes down)
      last_def_depth (without) mean depth of the two deepest outfield defenders, ball at cd >= 0.45   (line height / offside trap)
      gk_out     (without) keeper's distance from the own goal line, carrier within 30 m of it         (sweeper keeper)
      beyond_share (with) share of the carrier team's outfielders nearer the opposing goal than every opposing outfielder, ball beyond halfway  (runs)"""
    pts = [q for q in rows if _fin(q.get("p"))]
    if len(pts) < 9:
        return {}
    gk = [q for q in pts if q["fpos"] == 0]
    if gk:
        s = 1.0 if gk[0]["p"][along] > 0 else -1.0
        out = [q for q in pts if q is not gk[0]]
    else:
        s = 1.0 if statistics.fmean(q["p"][along] for q in pts) > 0 else -1.0
        gq = max(pts, key=lambda q: s * q["p"][along])
        out = [q for q in pts if q is not gq]
        gk = [gq]
    if len(out) < 8:
        return {}
    dep = [L / 2 - s * q["p"][along] for q in out]
    m = {}
    if ctx == "without" and carrier_pos is not None and _fin(carrier_pos):
        cdep = L / 2 - s * carrier_pos[along]
        cd = cdep / L
        d = sorted(math.hypot(q["p"][along] - carrier_pos[along], q["p"][lat] - carrier_pos[lat]) for q in out)
        if 0.30 <= cd <= 0.85:
            m["press_n8"] = float(sum(1 for x in d if x <= 8.0))
            m["press_min"] = d[0]
        if cd >= 0.45:
            m["last_def_depth"] = statistics.fmean(sorted(dep)[:2])
        if cdep <= 30.0:
            m["gk_out"] = L / 2 - s * gk[0]["p"][along]
    if ctx == "with" and ball is not None and _fin(ball):
        opp = [q for q in opp_rows if _fin(q.get("p")) and q["fpos"] != 0]
        if len(opp) >= 8:
            att = -s                                    # direction of the opposing goal along the pitch
            bd_opp = (L / 2 + s * ball[along]) / L        # ball depth from the opposing goal, share of L (0 = at their goal)
            if bd_opp <= 0.5:
                last = min(L / 2 + s * q["p"][along] for q in opp)   # depth from their goal of their deepest outfielder (coordinates: their own)
                n = sum(1 for q in out if (L / 2 + s * q["p"][along]) < last)
                m["beyond_share"] = n / len(out)
            del att
    return m


def extra_records(samples, L, W):
    """battery_lib.sample_records for the extra metrics: same sample shape ({i,t,win,players:[{handle,team,fpos,p}],ball,carrier}) and the same
    record shape ({i,t,win,v:{'<metric>:with|without': value},cov}) so battery_lib.effect_table can take them (merge_records)."""
    out = []
    for smp in samples:
        pl = smp["players"]
        if smp["carrier"] is None:
            continue
        cteam = next((q["team"] for q in pl if q["handle"] == smp["carrier"]), None)
        cpos = next((q["p"] for q in pl if q["handle"] == smp["carrier"]), None)
        if cteam is None:
            continue
        along = bl.axes_from_rows(pl)
        lat = 2 if along == 0 else 0
        rows = {t: [q for q in pl if q["team"] == t] for t in (0, 1)}
        v, cov = {}, {}
        for ctx, tm in (("with", cteam), ("without", 1 - cteam)):
            m = extra_metrics(rows[tm], rows[1 - tm], L, along, lat, smp["ball"], cpos, ctx)
            for k_, val in m.items():
                if math.isfinite(val):
                    v["%s:%s" % (k_, ctx)] = val
            if smp["ball"] is not None and ctx not in cov:
                gkq = [q for q in rows[tm] if q["fpos"] == 0 and _fin(q.get("p"))]
                if gkq:
                    s = 1.0 if gkq[0]["p"][along] > 0 else -1.0
                    cov[ctx] = ((L / 2 - s * smp["ball"][along]) / L, abs(smp["ball"][lat]) / (W / 2))
        if v:
            out.append({"i": smp["i"], "t": smp["t"], "win": smp["win"], "v": v, "cov": cov})
    return out


def merge_records(base, extra):
    """the extra metrics' values added into the base records of the same sample index (records only in `extra` are appended)"""
    by = {r["i"]: r for r in base}
    for r in extra:
        if r["i"] in by:
            by[r["i"]]["v"].update(r["v"])
            for c, x in r.get("cov", {}).items():
                by[r["i"]].setdefault("cov", {}).setdefault(c, x)
        else:
            base.append(r)
    return base


def judge_pair(a_vals, b_vals, dmin, sign=0):
    """two arms of a metric (A = baseline windows pooled, B = extreme windows pooled), values in time order -> (verdict, evidence).
    Same bar as the battery: PASS needs |shift| >= dmin and z >= 3 with the expected sign; FAIL = no effect although the MDE <= dmin (or a
    significant wrong sign); else INCONCLUSIVE. Reuses battery_lib.judge / window_means."""
    ma, sea, na = bl.window_means(list(a_vals))
    mb, seb, nb = bl.window_means(list(b_vals))
    if na == 0 or nb == 0:
        return "INCONCLUSIVE", {"why": "an arm is empty (the phase never occurred)", "n": (na, nb)}
    effect = mb - ma
    se = math.hypot(sea, seb) if math.isfinite(sea) and math.isfinite(seb) else float("inf")
    v, why, mde = bl.judge(effect, se, dmin, sign, nb, na)
    return v, {"effect": effect, "se": se, "mde": mde, "n_a": na, "n_b": nb, "why": why}


def plan_windows(sd, dmin, p_phase, hz=1.0, win_s=40.0, tau=TAU, max_win=12):
    """windows per arm so that MDE (2.8 SE, autocorrelation time tau samples) <= dmin for a metric of per-sample sd `sd` observed only in a
    phase that holds a share p_phase of the samples. -> dict(n_per_window, windows_per_arm, mde_at_that, feasible)"""
    n_win = max(1.0, hz * win_s * p_phase)
    for w in range(1, max_win + 1):
        n_eff = n_win * w * 2 / tau          # both arms pooled would double; per arm n = n_win * w
        se = sd / math.sqrt(max(1.0, n_win * w / tau)) * math.sqrt(2.0)
        mde = Z_MDE * se
        if mde <= dmin:
            return {"n_per_window": n_win, "windows_per_arm": w, "mde": mde, "feasible": True, "seconds": w * 2 * win_s}
    se = sd / math.sqrt(max(1.0, n_win * max_win / tau)) * math.sqrt(2.0)
    return {"n_per_window": n_win, "windows_per_arm": max_win, "mde": Z_MDE * se, "feasible": False, "seconds": max_win * 2 * win_s}


# ------------------------------------------------------------------------------------------------------------- partial-press cycle
# Per-player partial team press state, OLD 0x14164F338 (state machine, called once per player and tick from the loop at 0x14164F2D8) and 0x14164F5A8
# (burst length). s = [Player* + 0x28] [A: the loop walks the team's player vector, route C's 0x1B8-stride vector at +0x900]; all offsets [V OLD]:
#   s+0x294 u8 press active (the trigger at 0x146DF8012 sets it to 1), s+0x298 i32 reason (-1 = none), s+0x288 i32 burst start tick (flag 1) /
#   cooldown progress (flag 0), s+0x284 i32 rest-end tick (0 during the burst), s+0x28C i32 last tick, s+0x290 f32 sub-tick accumulator,
#   s+0x23C i32 work-rate level (2 high, 1 low, else medium).  now = (int)[[[s] + 0xEC8] + 0x18].
# Burst length D (ticks, 60 per second) = 60 * [grp + 0x84] (level 2), 60 * [grp + 0x9C] (level 1), 60 * ([0x84] + [0x9C]) / 2 (otherwise) for a regular
# player; the Relentless variants read 0x70/0x6C (regular) and 0x8C/0x58 (icon). The burst ends when now >= s+0x288 + D; the rest that follows is
# s+0x284 = s+0x288 + D + lerp(60*[grp+0x88]|0x5C|0x68, 60*[grp+0xA0]|0x64|0x60, frac) with frac a stamina-like player term in [0, 1].
PRESS = {"flag": 0x294, "why": 0x298, "start": 0x288, "end": 0x284, "last": 0x28C, "acc": 0x290, "wr": 0x23C}
PRESS_NOW = (0xEC8, 0x18)
PRESS_HZ = 60.0


def press_sample(dev, objs, t):
    """objs: [(handle, team, Player* address)] (route C). One sample of every player's press state, bytes reads only. -> list of dicts (ok False when
    the chain is not plausible: the caller treats an all-False sample as a REFUSAL)."""
    sp = dev.multi([(o + 0x28, 8) for (_, _, o) in objs])
    ss = [be.q(b) for b in sp]
    n = len(objs)
    blocks = dev.multi([(s + 0x23C, 0x64) if be.ptr_ok(s) else (be.BASE, 8) for s in ss] + [(s, 8) if be.ptr_ok(s) else (be.BASE, 8) for s in ss])
    brain = [be.q(blocks[n + i]) for i in range(n)]
    ys = dev.multi([(b + PRESS_NOW[0], 8) if be.ptr_ok(b) else (be.BASE, 8) for b in brain])
    ps = [be.q(b) for b in ys]
    nowb = dev.multi([(p + PRESS_NOW[1], 4) if be.ptr_ok(p) else (be.BASE, 4) for p in ps])
    out = []
    for i, (h, team, o) in enumerate(objs):
        b = blocks[i]
        rec = {"t": t, "handle": h, "team": team, "ok": False}
        if be.ptr_ok(ss[i]) and b is not None and len(b) >= 0x64 and nowb[i] is not None and len(nowb[i]) == 4:
            def rd(off, f="<i"):
                return struct.unpack_from(f, b, off - 0x23C)[0]
            nowf = struct.unpack("<f", nowb[i])[0]
            rec.update(flag=b[PRESS["flag"] - 0x23C], why=rd(PRESS["why"]), start=rd(PRESS["start"]), end=rd(PRESS["end"]), last=rd(PRESS["last"]),
                       acc=rd(PRESS["acc"], "<f"), wr=rd(PRESS["wr"]), now=int(nowf) if math.isfinite(nowf) and abs(nowf) < 2e9 else None, s=ss[i])
            rec["ok"] = bool(rec["flag"] in (0, 1) and -1 <= rec["why"] < 64 and 0 <= rec["wr"] < 8 and rec["now"] is not None and 0 <= rec["end"] < 2e9
                             and math.isfinite(rec["acc"]) and 0.0 <= rec["acc"] <= 1.5)
        out.append(rec)
    return out


def press_bursts(samples):
    """samples: [press_sample result, ...] in time order -> completed bursts [{handle, team, wr, lo, hi, start}] in ticks. A burst is a stretch with
    flag 1, start > 0 and end == 0; D is bracketed by the last elapsed value inside it (lo) and the first elapsed value after it (hi)."""
    last, bursts = {}, []
    for smp in samples:
        for r in smp:
            if not r.get("ok"):
                continue
            key = r["handle"]
            inb = r["flag"] == 1 and r["start"] > 0 and r["end"] == 0
            st = last.get(key)
            if inb:
                el = r["now"] - r["start"]
                if st is None or st["start"] != r["start"]:
                    last[key] = {"start": r["start"], "el": el, "wr": r["wr"], "team": r["team"]}
                else:
                    st["el"] = el
            elif st is not None:
                if (r["flag"] == 1 and r["end"] > 0) or r["flag"] == 0:
                    hi = r["now"] - st["start"]
                    if hi >= st["el"] >= 0:
                        bursts.append({"handle": key, "team": st["team"], "wr": st["wr"], "lo": st["el"], "hi": hi, "start": st["start"],
                                       "cycle": (r["end"] - st["start"]) if (r["flag"] == 1 and r["end"] > 0) else None})
                last.pop(key, None)
    return bursts


def judge_press_rest(bursts, min_s, max_s, stale_min_s, stale_max_s, tol_s=1.0, hz=PRESS_HZ, min_n=3):
    """The rest that follows a burst is s+0x284 - (s+0x288 + D); the cycle (s+0x284 - s+0x288 = D + rest) is read exactly, D is bracketed by the
    sampling, so rest lies in [cycle - hi, cycle - lo]. It must fall inside the written [min_s, max_s] (the table's min / max penalty time of
    the player's class); if it falls inside the old range instead, the table is ignored / latched. -> (verdict, evidence)"""
    cyc = [b for b in bursts if b.get("cycle")]
    lo_s, hi_s = min(min_s, max_s) - tol_s, max(min_s, max_s) + tol_s
    s_lo, s_hi = min(stale_min_s, stale_max_s) - tol_s, max(stale_min_s, stale_max_s) + tol_s

    def inside(b, a, c):
        r_lo, r_hi = (b["cycle"] - b["hi"]) / hz, (b["cycle"] - b["lo"]) / hz
        return r_hi >= a and r_lo <= c
    n_new = sum(1 for b in cyc if inside(b, lo_s, hi_s))
    n_old = sum(1 for b in cyc if inside(b, s_lo, s_hi))
    ev = {"cycles": len(cyc), "fit_written_range": n_new, "fit_old_range": n_old, "written": (min_s, max_s), "old": (stale_min_s, stale_max_s)}
    if not (hi_s < s_lo or s_hi < lo_s):
        return "INCONCLUSIVE", dict(ev, why="the written and the old rest ranges overlap: write ranges that are disjoint")
    if len(cyc) < min_n:
        return "INCONCLUSIVE", dict(ev, why="%d completed press cycles (need %d)" % (len(cyc), min_n))
    if n_new >= max(min_n, int(0.8 * len(cyc))) and n_old == 0:
        return "PASS", dict(ev, why="%d of %d rests fall in the written %.1f..%.1f s range" % (n_new, len(cyc), min_s, max_s))
    if n_old >= max(min_n, int(0.8 * len(cyc))) and n_new == 0:
        return "FAIL", dict(ev, why="the rests still fall in the old range: the written penalty times are ignored or latched")
    return "INCONCLUSIVE", dict(ev, why="mixed (%d new, %d old of %d cycles)" % (n_new, n_old, len(cyc)))


def press_watch(dev, objs, secs, hz, clock, sleep, log=print):
    """sample the press state of `objs` at `hz` for `secs` seconds -> (samples, refusal text or None). Read-only. The first sample must contain at
    least 8 plausible players (flag 0/1, reason -1..63, work-rate level 0..7, now readable), else the chain is refused."""
    t0 = clock()
    samples = []
    next_t = t0
    while clock() - t0 < secs:
        smp = press_sample(dev, objs, clock() - t0)
        if not samples and sum(1 for r in smp if r["ok"]) < 8:
            return samples, "only %d of %d players have a plausible press state: [Player* + 0x28] chain not identified on this build" % (
                sum(1 for r in smp if r["ok"]), len(smp))
        samples.append(smp)
        next_t += 1.0 / hz
        sleep(max(0.0, next_t - clock()))
    log("press-watch: %d samples of %d players over %.0f s" % (len(samples), len(objs), clock() - t0))
    return samples, None


def judge_press_cycle(bursts, expect_s, stale_s, wr=2, tol_s=0.8, hz=PRESS_HZ, min_n=2):
    """The press-burst length of players with work-rate level `wr` must bracket expect_s seconds (the value written, e.g. 0x84 for level 2) within
    tol_s; if instead they bracket stale_s (the old value) the table is ignored / latched. -> (verdict, evidence)"""
    sel = [b for b in bursts if b["wr"] == wr]

    def fit(b, sec):
        return b["lo"] - tol_s * hz <= sec * hz <= b["hi"] + tol_s * hz
    n_new = sum(1 for b in sel if fit(b, expect_s))
    n_old = sum(1 for b in sel if fit(b, stale_s))
    ev = {"bursts": len(sel), "fit_expected": n_new, "fit_stale": n_old, "expect_s": expect_s, "stale_s": stale_s, "tol_s": tol_s}
    if abs(expect_s - stale_s) <= 2 * tol_s:
        return "INCONCLUSIVE", dict(ev, why="expected and stale lengths are closer than twice the tolerance: write a value further from the old one")
    if len(sel) < min_n:
        return "INCONCLUSIVE", dict(ev, why="%d completed bursts of work-rate level %d (need %d): the press did not trigger often enough" % (len(sel), wr, min_n))
    if n_new >= max(min_n, int(0.8 * len(sel))):
        return "PASS", dict(ev, why="%d of %d completed bursts last %.1f s (+/- %.1f): the engine uses the written length" % (n_new, len(sel), expect_s, tol_s))
    if n_old >= max(min_n, int(0.8 * len(sel))) and n_new == 0:
        return "FAIL", dict(ev, why="the bursts still last %.1f s: the written length is ignored or latched" % stale_s)
    return "INCONCLUSIVE", dict(ev, why="mixed bursts (%d new, %d stale of %d)" % (n_new, n_old, len(sel)))


# ------------------------------------------------------------------------------------------------------------- data checks / table
def check_data(data=None, cat=None, inv_path=INV):
    """-> list of problems (empty = consistent): ids vs the catalogue, unique ranks, V rows carry a unique signature inside the image, extremes
    inside the knob's range and away from its default, sim-distinct flags vs the inventory"""
    data = data or load_data()
    cat = cat or load_cat()
    inv = json.load(open(inv_path, encoding="utf-8"))["groups"] if os.path.exists(inv_path) else None
    bad = []
    ids = {k["id"] for k in cat["knobs"]}
    ks = data["knobs"]
    if set(ks) != ids:
        bad.append("ids differ from the catalogue: missing %s extra %s" % (sorted(ids - set(ks))[:3], sorted(set(ks) - ids)[:3]))
    ranks = sorted(v["c_rank"] for v in ks.values())
    if ranks != list(range(1, len(ks) + 1)):
        bad.append("c_rank is not 1..%d unique" % len(ks))
    for kid, v in ks.items():
        k = _knob(cat, kid)
        if k is None:
            continue
        if v["status"] not in ("V", "A", "U") or v["tier"] not in ("S", "A", "B", "C"):
            bad.append("%s: bad status/tier" % kid)
        c = v.get("consumer")
        if v["status"] == "V":
            if not c or c.get("sig_hits") != 1 or not c.get("sig"):
                bad.append("%s: status V without a unique signature" % kid)
            else:
                for key in ("func", "read"):
                    a = int(c[key], 16)
                    if not (be.BASE <= a < be.BASE + be.IMAGE_SIZE):
                        bad.append("%s: %s outside the image" % (kid, key))
        for e in v.get("ext") or []:
            lo, hi, d = k.get("min"), k.get("max"), k.get("default")
            if lo is not None and hi is not None and not (lo - 1e-9 <= e <= hi + 1e-9):
                bad.append("%s: extreme %s outside [%s, %s]" % (kid, e, lo, hi))
        if v.get("ext") and k.get("default") is not None and k.get("type") == "f32":
            d = float(k["default"])
            if len(v["ext"]) >= 2 and not any(abs(e - d) >= 0.25 * max(abs(d), 0.1) for e in v["ext"]):
                bad.append("%s: no extreme is 25 %% away from the default %s" % (kid, d))
        if inv is not None:
            g = inv.get(k["collection"])
            sd = bool(g and any(x.startswith("simulation") for x in g["variants"]))
            if sd != (v["copy"] == "sim-distinct"):
                bad.append("%s: copy flag %s vs inventory" % (kid, v["copy"]))
    return bad


def table(data=None, top=24):
    data = data or load_data()
    rows = sorted(data["knobs"].values(), key=lambda v: v["c_rank"])[:top]
    lines = ["| # | knob | tier | consumer (OLD) | status | fresh | copy | observable | metric | extremes |", "|---|---|---|---|---|---|---|---|---|---|"]
    for v in rows:
        c = v.get("consumer") or {}
        lines.append("| %d | %s | %s | %s | %s | %s | %s | %s | %s | %s |" % (
            v["c_rank"], v["id"], v["tier"], c.get("read") or "-", v["status"], v["freshness"], v["copy"], v.get("observable") or "-",
            v.get("metric") or "-", "/".join(str(e) for e in v.get("ext") or []) or "-"))
    return "\n".join(lines)


# ------------------------------------------------------------------------------------------------------------- self-test
def _synth_samples(L=105.0, W=68.0, n=240, seed=3, defend_shift=0.0, press_shift=0.0, line_shift=0.0, gk_shift=0.0, run_shift=0.0):
    """synthetic samples: team 0 attacks +along. The carrier (team 0) walks; team 1 defends its goal at +L/2. shifts change the defenders' behaviour."""
    import random
    r = random.Random(seed)
    out = []
    for i in range(n):
        ball_x = -30.0 + 55.0 * (0.5 + 0.5 * math.sin(i / 17.0))
        pl = []
        # team 0 (attacking, goal keeper at -L/2): carrier plus 9 outfielders
        pl.append({"handle": 1, "team": 0, "fpos": 0, "p": (-L / 2 + 4.0, 0.0, 0.0)})
        for k in range(10):
            ahead = run_shift if k >= 7 else 0.0
            pl.append({"handle": 2 + k, "team": 0, "fpos": k + 3, "p": (ball_x - 20.0 + 5.0 * k + 15.0 * ahead + r.gauss(0, 1.0), 0.0, -25.0 + 5.0 * k)})
        carrier = 2 + 5
        # team 1 (defending, keeper at +L/2)
        pl.append({"handle": 30, "team": 1, "fpos": 0, "p": (L / 2 - 3.0 - gk_shift, 0.0, 0.0)})
        line = 22.0 + line_shift
        for k in range(10):
            lat = -24.0 + 5.3 * k
            if k < 4:
                x = L / 2 - line + r.gauss(0, 0.6)
            else:
                x = L / 2 - line - 8.0 * (k - 3) + r.gauss(0, 0.6)
            if k >= 6:
                x = ball_x + 14.0 - press_shift * (k - 5) * 3.0 + r.gauss(0, 0.8)
                lat = 0.0 + 2.0 * (k - 7) + r.gauss(0, 0.8)
            pl.append({"handle": 40 + k, "team": 1, "fpos": k + 3, "p": (x, 0.0, lat)})
        for q in pl:
            if q["handle"] == carrier:
                q["p"] = (ball_x, 0.0, 0.0)
        out.append({"i": i, "t": i * 0.5, "win": "A1", "players": pl, "ball": (ball_x, 0.0, 0.0), "carrier": carrier})
    return out


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        sys.stdout.flush()
        ok = ok and cond

    import battery_mock as bm
    cat = load_cat()
    data = load_data()
    bad = check_data(data, cat)
    check(not bad, "data file consistent with the catalogue and the inventory (%d problems %s)" % (len(bad), bad[:2]))
    check(len(data["knobs"]) == len(cat["knobs"]) == 124, "all 124 catalogue knobs have a consumers entry")
    check(sum(1 for v in data["knobs"].values() if v["status"] == "V") >= 30, "at least 30 knobs carry a verified-in-the-old-image read site")
    top = sorted(data["knobs"].values(), key=lambda v: v["c_rank"])
    check(top[0]["tier"] == "S" and all(v["tier"] in "SAB" for v in top[:24]), "the first rank is tier S; the top 24 are S/A/B")
    check("| 1 | marking.tightness | S |" in table(data, 3), "table() renders the ranked rows")

    # --- generic identity on the mock world (every locator of the catalogue has a group chain there)
    w = bm.MockWorld(scale=0.01, match_delay=0.0)
    dev = be.DevMulti(w.mem.call)
    for kid in ("marking.tightness", "pressing.stepup_distance_bonus", "goalkeeper.rush_radius", "shape.def_line_range_max", "lineband.high_min", "defending.jockey_speed"):
        v, ev = obs_instance(dev, kid, cat)
        check(v == "PASS" and ev["copy"] == "base" and ev["db_kind"] == ("prematch" if kid.startswith("lineband") else "gameplay") and ev["equals_fc27_value"],
              "obs_instance(%s): PASS, %s copy, value = FC 27's (%s)" % (kid, ev.get("copy"), ev.get("value")))
    v, ev = obs_instance(dev, "marking.tightness", cat)
    kn = _knob(cat, "marking.tightness")
    w.put_knob(kn, 0.9)
    v2, rb = obs_readback(dev, ev, kn, 0.9)
    v3, rb3 = obs_readback(dev, ev, kn, 0.5)
    check(v2 == "PASS" and v3 == "FAIL", "obs_readback: the written 0.9 reads back (PASS); 0.5 does not (FAIL)")
    ci = next(l for l in cat["locators"] if l["name"] == kn["locator"])
    grp = ev["group"]
    check(be.ptr_ok(grp), "the mock chain used by the test is a real pointer chain")
    w.put(grp + _hexint(ci["group_slot"]), "<Q", 0)
    v4, ev4 = obs_instance(dev, "marking.tightness", cat)
    check(v4 == "REFUSED", "a broken group slot is a REFUSAL, never a verdict (%s)" % ev4.get("why", "")[:50])
    w.put(grp + _hexint(ci["group_slot"]), "<Q", ev["inst"] | 4)
    v5, ev5 = obs_instance(dev, "marking.tightness", cat)
    check(v5 == "PASS" and ev5["inst_tag"] == 1, "a tagged instance pointer (bit 2) is masked like the engine does")
    # name that matches no variant -> INCONCLUSIVE
    nm = be.q(dev.multi([(ev["inst"] + 0x18, 8)])[0])
    w.mem.write(nm, b"Fifa/Attribulator/Gameplay/groups/other/unknown_runtime\0")
    v6, ev6 = obs_instance(dev, "marking.tightness", cat)
    check(v6 == "INCONCLUSIVE" and ev6["copy"] is None, "an instance Name that matches no variant is INCONCLUSIVE")
    # simulation path is recognised even when the locator lists only the base instance
    w.mem.write(nm, b"Fifa/Attribulator/Gameplay/databases/simulation/groups/gp_positioning/gp_positioning_markingtightness_runtime\0")
    v7, ev7 = obs_instance(dev, "marking.tightness", cat)
    check(v7 == "PASS" and ev7["copy"] == "simulation" and "SIMULATION" in ev7["note"], "a simulation-path Name is reported as the simulation copy")

    # --- latched copy
    c, bad_ = be.resolve(dev)
    check(c is not None, "mock match context resolves (%s)" % (bad_[:1],))
    w.put(c["kobj"][0] + KOBJ_DEFLINE_MIN, "<2f", -7.5, 7.5)
    check(obs_latched_copy(dev, c, cat, -7.5, 7.5)[0] == "PASS", "latched copy: vanilla range, vanilla written -> PASS")
    check(obs_latched_copy(dev, c, cat, -20.0, 20.0)[0] == "FAIL", "latched copy: FC 27's range kept although -20/20 was written -> FAIL (written too late)")
    w.put(c["kobj"][0] + KOBJ_DEFLINE_MIN, "<2f", -20.0, 20.0)
    check(obs_latched_copy(dev, c, cat, -20.0, 20.0)[0] == "PASS", "latched copy: the written range was copied -> PASS")
    w.put(c["kobj"][0] + KOBJ_DEFLINE_MIN, "<2f", 3.0, -3.0)
    check(obs_latched_copy(dev, c, cat, -20.0, 20.0)[0] == "INCONCLUSIVE", "latched copy: implausible values (min > max) -> object not identified")

    # --- phase metrics
    base = _synth_samples()
    rec = extra_records(base, 105.0, 68.0)
    keys = {k for r in rec for k in r["v"]}
    check({"press_n8:without", "press_min:without", "last_def_depth:without", "gk_out:without", "beyond_share:with"} <= keys,
          "extra_records produce all five phase metrics (%s)" % sorted(keys))
    mean = lambda recs, k: statistics.fmean([r["v"][k] for r in recs if k in r["v"]])
    pressed = extra_records(_synth_samples(seed=4, press_shift=2.5), 105.0, 68.0)
    check(mean(pressed, "press_n8:without") > mean(rec, "press_n8:without") + 0.5, "press_n8 rises when the defenders close in")
    hi = extra_records(_synth_samples(seed=5, line_shift=10.0), 105.0, 68.0)
    check(mean(hi, "last_def_depth:without") > mean(rec, "last_def_depth:without") + 8.0, "last_def_depth follows the defensive line (+10 m)")
    gk = extra_records(_synth_samples(seed=6, gk_shift=12.0), 105.0, 68.0)
    check(mean(gk, "gk_out:without") > mean(rec, "gk_out:without") + 8.0, "gk_out follows the keeper's position (+12 m)")
    rn = extra_records(_synth_samples(seed=7, run_shift=1.0), 105.0, 68.0)
    check(mean(rn, "beyond_share:with") > mean(rec, "beyond_share:with"), "beyond_share rises when attackers run beyond the line")
    # phase gating: carrier in the defenders' deep zone -> no press metrics
    s0 = _synth_samples(n=1)[0]
    for q in s0["players"]:
        if q["handle"] == s0["carrier"]:
            q["p"] = (-45.0, 0.0, 0.0)
    s0["ball"] = (-45.0, 0.0, 0.0)
    r0 = extra_records([s0], 105.0, 68.0)
    check(not r0 or "press_n8:without" not in r0[0]["v"], "no pressing metric outside its phase (carrier near the attackers' own goal)")
    merged = merge_records([{"i": r["i"], "t": r["t"], "win": r["win"], "v": {"nn_opp:without": 5.0}, "cov": {}} for r in rec[:3]], rec[:3])
    check("nn_opp:without" in merged[0]["v"] and "press_n8:without" in merged[0]["v"], "merge_records adds the extra metrics next to battery_lib's")
    # judge_pair
    import random
    rr = random.Random(2)
    a = [10.0 + rr.gauss(0, 1.0) for _ in range(200)]
    b = [8.0 + rr.gauss(0, 1.0) for _ in range(200)]
    check(judge_pair(a, b, 1.0, -1)[0] == "PASS", "judge_pair: a -2 shift with the expected sign passes")
    check(judge_pair(a, b, 1.0, +1)[0] == "FAIL", "judge_pair: the same shift with the wrong expected sign fails")
    flat = [10.0 + rr.gauss(0, 1.0) for _ in range(200)]
    check(judge_pair(a, flat, 1.0, -1)[0] == "FAIL", "judge_pair: no shift with a small MDE is a clean FAIL")
    check(judge_pair(a[:6], flat[:6], 1.0, -1)[0] == "INCONCLUSIVE", "judge_pair: six samples per arm are INCONCLUSIVE")
    check(judge_pair(a, [], 1.0, -1)[0] == "INCONCLUSIVE", "judge_pair: an empty arm is INCONCLUSIVE")
    p1 = plan_windows(sd=6.0, dmin=2.5, p_phase=0.25)
    p2 = plan_windows(sd=6.0, dmin=2.5, p_phase=1.0)
    check(p1["windows_per_arm"] >= p2["windows_per_arm"], "plan_windows: a rarer phase needs at least as many windows (%d vs %d)" % (p1["windows_per_arm"], p2["windows_per_arm"]))
    check(not plan_windows(sd=20.0, dmin=1.0, p_phase=0.05, max_win=4)["feasible"], "plan_windows: an unreachable design says so")
    # --- partial-press cycle on a synthetic player-state world (bytes reads only; the machine follows 0x14164F338 / 0x14164F5A8 as documented)
    class FakePress:
        BASE_ADDR = harness.MOCK_HEAP + 0x400000

        def __init__(self, n=6, burst_s=(6.0, 4.0), rest_s=9.0, seed=5):
            import random
            self.r = random.Random(seed)
            self.n, self.burst_s, self.rest_s = n, list(burst_s), rest_s
            self.tick = 6000
            self.mem = {}
            self.objs = []
            self.state = []
            self.brain = self.BASE_ADDR + 0x100000
            self.y = self.brain + 0x1000
            self.mem[self.brain + PRESS_NOW[0]] = struct.pack("<Q", self.y)
            for i in range(n):
                o = self.BASE_ADDR + i * 0x4000
                s = o + 0x2000
                self.objs.append((i, i % 2, o))
                self.mem[o + 0x28] = struct.pack("<Q", s)
                self.mem[s] = struct.pack("<Q", self.brain)
                self.state.append({"s": s, "flag": 0, "why": -1, "start": 0, "end": 0, "last": 0, "acc": 0.0, "wr": 2 if i % 3 == 0 else (1 if i % 3 == 1 else 0),
                                   "next_trigger": 120 + 70 * i})
            self.sync()

        def sync(self):
            self.mem[self.y + PRESS_NOW[1]] = struct.pack("<f", float(self.tick))
            for st in self.state:
                blk = bytearray(0x64)
                struct.pack_into("<i", blk, 0, st["wr"])
                struct.pack_into("<i", blk, PRESS["end"] - 0x23C, st["end"])
                struct.pack_into("<i", blk, PRESS["start"] - 0x23C, st["start"])
                struct.pack_into("<i", blk, PRESS["last"] - 0x23C, st["last"])
                struct.pack_into("<f", blk, PRESS["acc"] - 0x23C, st["acc"])
                blk[PRESS["flag"] - 0x23C] = st["flag"]
                struct.pack_into("<i", blk, PRESS["why"] - 0x23C, st["why"])
                self.mem[st["s"] + 0x23C] = bytes(blk)

        def burst_ticks(self, st):
            hi, lo = self.burst_s
            sec = hi if st["wr"] == 2 else (lo if st["wr"] == 1 else (hi + lo) / 2.0)
            return int(sec * 60)

        def advance(self, ticks):
            for _ in range(ticks):
                self.tick += 1
                for st in self.state:
                    if st["flag"] == 0 and self.tick >= st["next_trigger"]:
                        st.update(flag=1, why=3, start=self.tick, end=0)
                    elif st["flag"] == 1 and st["end"] == 0 and self.tick >= st["start"] + self.burst_ticks(st):
                        st["end"] = self.tick + int(self.rest_s * 60)
                    elif st["flag"] == 1 and st["end"] > 0 and self.tick >= st["end"]:
                        st.update(flag=0, why=-1, start=0, end=0, next_trigger=self.tick + 200)
            self.sync()

        def multi(self, pairs):
            out = []
            for a, n in pairs:
                hit = None
                for base, b in self.mem.items():
                    if base <= a and a + n <= base + len(b):
                        hit = b[a - base:a - base + n]
                        break
                out.append(hit)
            return out

    fp = FakePress()
    smps = []
    for _ in range(260):
        fp.advance(12)                  # 0.2 s per sample
        smps.append(press_sample(fp, fp.objs, fp.tick / 60.0))
    check(all(r["ok"] for r in smps[0]), "press_sample: the synthetic player-state chain reads as plausible")
    bursts = press_bursts(smps)
    hi_b = [b for b in bursts if b["wr"] == 2]
    check(len(hi_b) >= 4 and all(abs((b["lo"] + b["hi"]) / 2 - 360) <= 12 for b in hi_b), "press_bursts: high work-rate bursts bracket 6 s = 360 ticks (%d bursts)" % len(hi_b))
    check(judge_press_cycle(bursts, 6.0, 25.0)[0] == "PASS", "judge_press_cycle: the engine keeps the 6 s length it is told -> PASS")
    check(judge_press_cycle(bursts, 25.0, 6.0)[0] == "FAIL", "judge_press_cycle: 25 s written but bursts still last 6 s -> FAIL (ignored / latched)")
    check(judge_press_cycle(bursts, 6.5, 6.0)[0] == "INCONCLUSIVE", "judge_press_cycle: expected and stale too close -> INCONCLUSIVE")
    fp2 = FakePress(burst_s=(25.0, 4.0))
    smps2 = []
    for _ in range(900):
        fp2.advance(12)
        smps2.append(press_sample(fp2, fp2.objs, fp2.tick / 60.0))
    check(judge_press_cycle(press_bursts(smps2), 25.0, 6.0)[0] == "PASS", "judge_press_cycle: bursts of 25 s after the write -> PASS")
    # rest 9 s in the synthetic world: written range 8..10 passes, old 3..5 fails
    check(judge_press_rest(bursts, 8.0, 10.0, 3.0, 5.0)[0] == "PASS", "judge_press_rest: rests of 9 s fall in the written 8..10 s range -> PASS")
    check(judge_press_rest(bursts, 3.0, 5.0, 8.0, 10.0)[0] == "FAIL", "judge_press_rest: rests of 9 s while 3..5 s is written -> FAIL (ignored / latched)")
    check(judge_press_rest(bursts, 8.0, 10.0, 9.0, 11.0)[0] == "INCONCLUSIVE", "judge_press_rest: overlapping ranges -> INCONCLUSIVE")
    check(judge_press_cycle(press_bursts(smps[:3]), 6.0, 25.0)[0] == "INCONCLUSIVE", "judge_press_cycle: no completed burst -> INCONCLUSIVE")
    clk = {"t": 0.0}
    fpw = FakePress(n=10)

    def _sl(d):
        clk["t"] += max(d, 0.0)
        fpw.advance(max(1, int(round(max(d, 0.0) * 60))))
    sm_w, ref = press_watch(fpw, fpw.objs, 60.0, 5.0, lambda: clk["t"], _sl, log=lambda *_: None)
    check(ref is None and len(sm_w) >= 50 and judge_press_cycle(press_bursts(sm_w), 6.0, 25.0)[0] in ("PASS", "INCONCLUSIVE"),
          "press_watch: %d samples, no refusal, the bursts judge without error" % len(sm_w))
    fpb = FakePress(n=10)
    for _, _, o in fpb.objs:
        fpb.mem[o + 0x28] = struct.pack("<Q", 0)
    sm_b, ref_b = press_watch(fpb, fpb.objs, 5.0, 5.0, lambda: clk["t"], lambda d: clk.update(t=clk["t"] + d))
    check(ref_b is not None and not sm_b, "press_watch: an unidentified chain is refused on the first sample")
    fp3 = FakePress()
    fp3.mem[fp3.objs[0][2] + 0x28] = struct.pack("<Q", 0)
    check(not press_sample(fp3, fp3.objs, 0.0)[0]["ok"], "press_sample: a null state pointer is not plausible (ok False), never a value")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--table", action="store_true")
    ap.add_argument("--top", type=int, default=24)
    ap.add_argument("--check-data", action="store_true")
    ap.add_argument("--mock", action="store_true")
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if a.table:
        print(table(top=a.top))
        return 0
    if a.check_data:
        bad = check_data()
        for b in bad:
            print("PROBLEM", b)
        print("check-data: %d problem(s)" % len(bad))
        return 1 if bad else 0
    if a.cmd[:1] == ["press-watch"]:
        harness.require_live(False, tool="consumers_obs.py press-watch")
        import time, dev2
        import match_measure as mm
        opts = dict(x.split("=", 1) for x in a.cmd[1:] if "=" in x)
        secs, wr = float(opts.get("secs", 90)), int(opts.get("wr", 2))
        key, base, lay = mm.load_layout(False)
        m = mm.locate_any(key, lay, argparse.Namespace(team_pids="", relocate=False), print)
        objs = [(p["handle"], p["team"], p["obj"]) for p in m.players]
        samples, refusal = press_watch(be.DevMulti(dev2.call), objs, secs, float(opts.get("hz", 5)), time.time, time.sleep)
        if refusal:
            print("REFUSED:", refusal)
            print("VERDICT INCONCLUSIVE")
            return harness.EXIT["INCONCLUSIVE"]
        bursts = press_bursts(samples)
        print("completed bursts: %d (work-rate levels %s)" % (len(bursts), sorted({b["wr"] for b in bursts})))
        for b in bursts[:12]:
            print("  player %s team %s level %s: burst %.1f..%.1f s%s" % (b["handle"], b["team"], b["wr"], b["lo"] / PRESS_HZ, b["hi"] / PRESS_HZ,
                                                                         "" if b.get("cycle") is None else ", cycle %.1f s" % (b["cycle"] / PRESS_HZ)))
        if "expect" in opts and "stale" in opts:
            v, ev = judge_press_cycle(bursts, float(opts["expect"]), float(opts["stale"]), wr=wr)
        elif "rest" in opts and "old" in opts:
            lo_, hi_ = (float(x) for x in opts["rest"].split(","))
            olo_, ohi_ = (float(x) for x in opts["old"].split(","))
            v, ev = judge_press_rest(bursts, lo_, hi_, olo_, ohi_)
        else:
            v, ev = "INCONCLUSIVE", {"why": "no judgement requested (give expect=<s> stale=<s> [wr=2] or rest=<min>,<max> old=<min>,<max>)"}
        print(json.dumps(ev))
        print("VERDICT %s" % v)
        return harness.EXIT.get(v, harness.INCONCLUSIVE)
    if a.cmd[:1] == ["probe"] and len(a.cmd) == 2:
        harness.require_live(a.mock, tool="consumers_obs.py")
        cat = load_cat()
        if a.mock:
            import battery_mock as bm
            w = bm.MockWorld(scale=0.01, match_delay=0.0)
            call = w.mem.call
        else:
            import dev2
            call = dev2.call
        v, ev = obs_instance(be.DevMulti(call), a.cmd[1], cat)
        print(json.dumps(ev, default=lambda x: be.H(x) if isinstance(x, int) else str(x), indent=1))
        print("VERDICT %s" % ("PASS" if v == "PASS" else "INCONCLUSIVE"))
        return harness.EXIT["PASS" if v == "PASS" else "INCONCLUSIVE"]
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
