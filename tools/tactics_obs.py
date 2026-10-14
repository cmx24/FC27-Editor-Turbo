"""tactics_obs.py - Turbo 2.7 track "tactics-obs": low-noise, read-only observables for the AI's own state (tools/; bytes reads only, never writes).

Why: realized positions are dominated by ball / possession noise (MDE 4-7 m in 40 s windows). The AI's own variables are deterministic functions of
the knob; reading them needs one pointer chain, not a statistic. Tags: [V OLD] = read in the disassembly of the old image 6AB9813C (docs/re/tactics_obs/),
[A] = assessed, [U] = unknown. NOTHING here is proven live: every live offset is [A] until `press-raw` / `state-watch` is looked at once by eye.

What is observable
  press (pressing.partial_*), engine state machine 0x14164F338 [V OLD]: per player s = [Player* + 0x28]; s+0x294 flag, s+0x298 reason, s+0x288 start tick,
      s+0x284 rest-end tick, s+0x28C = the tick of the last machine update, s+0x23C work-rate level; s+0x18 -> the player's PlayerInfo (first dword =
      handle index), s+0x28 -> the registry T ([T + 0x58 + handle*8] = Player*), [s] -> a brain whose [+0xEC8] + 0x18 is the clock (float ticks).
      IDENTITY (positive, not "zeros are plausible"): [s+0x18] == the Player's info pointer from route C AND s+0x28C tracks the clock (the machine writes
      `now` into s+0x28C on almost every path, so 0 <= now - last <= a few ticks for a player the machine is ticked for). The old gate accepted zeroed
      memory, so TEST_28's 0 bursts could not tell "wrong chain" from "press never fired".
      EXACT CYCLE: at the end of a burst the engine stores s+0x284 = start + D + rest with start still in s+0x288: ANY sample in the rest phase (flag 1,
      start > 0, end > 0) gives cycle = end - start to the tick = D + rest, D = 60 x workTime(level), rest = 60 x lerp(maxPenalty, minPenalty, frac in 0..1).
      The written table moves the cycle by tens of seconds, so ONE completed cycle decides (judge_press_cycle_exact).
      Trigger [V OLD 0x146DF7EAC, vtable 0x14AEA61A0 slot; A semantics]: at most 2 pressers per team (controller +0x318 / +0x328 pairs), aborted by 0x146DEB404
      unless controller+0x204 == 0x15 and [[tm+0x80]] + 0x53C <= + 0x530 (tactic block words, printed by press-raw).
  formation state (shape.*): owner = [tm + 0x2250], state = owner + 0xE0 (live-verified chain, battery_engine.resolve): +0x2C length, +0x30 cap, +0x34 back
      width, +0x40 possession blend, +0x48 / +0x4C formation origin along the pitch (the defensive-line anchor: 0x141CCAF58 smooths it towards the target
      0x140F581DC, which uses tactic style table, ball, [owner+0x58]+0x5B3C+idx*0x38) [A: roles of +0x48 vs +0x4C], +0x90 ball vector.
      judge_cov_adjusted compares the origin against the ball covariate (fitted on the baseline condition only).
Commands (live; GAME_STATE must allow it):  python tools/tactics_obs.py press-raw [secs=60] | press-judge expect=hi,lo,pmin,pmax stale=hi,lo,pmin,pmax [secs=120]
  | state-watch [secs=60]  | state-judge A.csv B.csv [dmin=3]      offline: --self-test
"""
import argparse, csv, json, math, os, statistics, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import battery_engine as be  # noqa: E402
import harness  # noqa: E402

HZ = 60.0
B0 = 0x23C                    # start of the block read per player: s+0x23C .. s+0x2A0
BLEN = 0x64
OFF = {"wr": 0x23C, "end": 0x284, "start": 0x288, "last": 0x28C, "acc": 0x290, "flag": 0x294, "why": 0x298}
S_INFO, S_T, S_BRAIN, CLOCK_CHAIN = 0x18, 0x28, 0x00, (0xEC8, 0x18)
TICK_MAX = 30                 # now - last accepted for a ticked machine (ticks)
# press table fields [V OLD 0x14164F5A8 / 0x14164F338]: regular class (hi 0x84, lo 0x9C, penalties 0xA0 / 0x88), Relentless regular (0x70 / 0x6C; 0x64 / 0x5C),
# Relentless icon (0x8C / 0x58; 0x60 / 0x68)
TAB_OFF = {"hi": 0x84, "lo": 0x9C, "pmin": 0xA0, "pmax": 0x88}
TAB_OTHER = [{"hi": 0x70, "lo": 0x6C, "pmin": 0x64, "pmax": 0x5C}, {"hi": 0x8C, "lo": 0x58, "pmin": 0x60, "pmax": 0x68}]
TB_WORDS = (0x488, 0x48C, 0x4D4, 0x530, 0x53C, 0x554, 0x7AC)      # tactic block words printed next to the press rows ([A] meaning, [V OLD] reads)


def _f(b, o):
    return struct.unpack_from("<f", b, o)[0]


def _i(b, o):
    return struct.unpack_from("<i", b, o)[0]


def _q(b, o=0):
    return struct.unpack_from("<Q", b, o)[0] if b is not None and len(b) >= o + 8 else None


# ------------------------------------------------------------------------------------------------------------- press: bind / read
def press_bind(dev, players):
    """players: [{handle, team, obj, info}] (route C). Resolves s = [obj+0x28], its info / registry / brain / clock once. -> rig dict.
    row["ident"] = [s+0x18] equals the player's info pointer (positive identity); rig["bound"] = rows with ident and one shared clock address."""
    ss = [_q(b) for b in dev.multi([(p["obj"] + 0x28, 8) for p in players])]
    blk = dev.multi([(s, 0x30) if be.ptr_ok(s) else (be.BASE, 8) for s in ss])
    rows = []
    for p, s, b in zip(players, ss, blk):
        ok = be.ptr_ok(s) and b is not None and len(b) >= 0x30
        brain, info_s, t_s = (_q(b, 0), _q(b, S_INFO), _q(b, S_T)) if ok else (None, None, None)
        rows.append({"handle": p["handle"], "team": p["team"], "obj": p["obj"], "s": s if ok else None, "brain": brain, "T": t_s,
                     "ident": bool(ok and p.get("info") is not None and info_s == p["info"] and be.ptr_ok(brain))})
    ys = dev.multi([(r["brain"] + CLOCK_CHAIN[0], 8) if r["ident"] else (be.BASE, 8) for r in rows])
    for r, yb in zip(rows, ys):
        r["y"] = _q(yb) if r["ident"] else None
        if not be.ptr_ok(r["y"]):
            r["ident"] = False
    cnt = {}
    for r in rows:
        if r["ident"]:
            cnt[r["y"]] = cnt.get(r["y"], 0) + 1
    clock = max(cnt, key=cnt.get) if cnt else None
    for r in rows:
        r["bound"] = bool(r["ident"] and r["y"] == clock)
    cb = dev.multi([(clock + CLOCK_CHAIN[1], 4)]) [0] if clock else None
    now0 = _f(cb, 0) if cb is not None and len(cb) == 4 else None
    if now0 is None or not math.isfinite(now0):
        for r in rows:
            r["bound"] = False
    return {"rows": rows, "clock": (clock + CLOCK_CHAIN[1]) if clock else None, "bound": sum(1 for r in rows if r["bound"]), "n": len(rows), "now0": now0}


def press_read(dev, rig, t):
    """one request: every bound player's block + the clock + the [obj+0x28] identity re-check. -> list of record dicts (all rows; ok False when not
    bound / not ticked / implausible)"""
    rows = rig["rows"]
    pairs = [(r["s"] + B0, BLEN) if r["bound"] else (be.BASE, 8) for r in rows] + [(rig["clock"] or be.BASE, 4)] + [(r["obj"] + 0x28, 8) for r in rows]
    res = dev.multi(pairs)
    n = len(rows)
    nowb = res[n]
    now = int(_f(nowb, 0)) if nowb is not None and len(nowb) == 4 and math.isfinite(_f(nowb, 0)) and abs(_f(nowb, 0)) < 2e9 else None
    out = []
    for i, r in enumerate(rows):
        b = res[i]
        rec = {"t": t, "handle": r["handle"], "team": r["team"], "bound": r["bound"], "ok": False, "ticked": False, "now": now}
        same = _q(res[n + 1 + i]) == r["s"]
        rec["same_s"] = bool(same)
        if r["bound"] and same and b is not None and len(b) >= BLEN and now is not None:
            g = lambda k: _i(b, OFF[k] - B0)
            rec.update(wr=g("wr"), end=g("end"), start=g("start"), last=g("last"), flag=b[OFF["flag"] - B0], why=g("why"), acc=_f(b, OFF["acc"] - B0))
            rec["ticked"] = bool(rec["last"] > 0 and -2 <= now - rec["last"] <= TICK_MAX)
            rec["ok"] = bool(rec["ticked"] and rec["flag"] in (0, 1) and -1 <= rec["why"] < 64 and 0 <= rec["wr"] < 8 and rec["end"] >= 0 and rec["start"] >= 0
                             and math.isfinite(rec["acc"]) and 0.0 <= rec["acc"] <= 1.5)
        out.append(rec)
    return out


def tactic_words(dev, ctx):
    """per team the tactic block words that gate the trigger (0x146DEB404: +0x53C <= +0x530) and the style / line words. ctx = battery_engine.resolve()"""
    out = []
    for k in range(2):
        tb = ctx["tb"][k] if ctx.get("tb_ok", [False, False])[k] else None
        b = dev.multi([(tb + 0x480, 0x340)])[0] if tb else None
        out.append({w: (_f(b, w - 0x480), _i(b, w - 0x480)) for w in TB_WORDS} if b is not None and len(b) >= 0x340 else None)
    return out


def press_cycles(samples):
    """exact cycles: one per (handle, start) seen in the rest phase (flag 1, start > 0, end > 0): cycle = end - start ticks. The work-rate level must be the same
    in every sample that saw this burst, else the cycle is skipped. -> [{handle, team, wr, cycle, start}]"""
    seen, wrs, out = {}, {}, []
    for smp in samples:
        for r in smp:
            if not r.get("ok") or r["flag"] != 1 or r["start"] <= 0:
                continue
            key = (r["handle"], r["start"])
            wrs.setdefault(key, set()).add(r["wr"])
            if r["end"] > 0 and key not in seen:
                seen[key] = {"handle": r["handle"], "team": r["team"], "cycle": r["end"] - r["start"], "start": r["start"]}
    for key, c in seen.items():
        if len(wrs[key]) == 1:
            out.append(dict(c, wr=next(iter(wrs[key]))))
    return out


def cycle_range(tab, wr, tol=3):
    """[lo, hi] ticks a cycle can take for a regular-class player of work-rate level wr under table tab (seconds): D + rest, rest between the penalties"""
    hi, lo = tab["hi"], tab["lo"]
    d = 60.0 * (hi if wr == 2 else lo if wr == 1 else hi - 0.5 * (hi - lo))
    a, b = sorted((tab["pmin"], tab["pmax"]))
    return d + 60.0 * a - tol, d + 60.0 * b + tol


def judge_press_cycle_exact(cycles, new, old, others=(), min_n=2):
    """new / old: {hi, lo, pmin, pmax} in seconds (the table written / the table FC 27 had); others: the Relentless classes' tables (not written). A cycle that fits a
    Relentless range AND the new range is ambiguous and dropped. PASS: >= min_n usable cycles, >= 80 % in the new range, none only in the old range;
    FAIL: >= min_n only in the old range and none in the new; else INCONCLUSIVE (never a verdict from fewer cycles). -> (verdict, evidence)"""
    ev = {"cycles": len(cycles)}
    nin = oin = amb = use = 0
    for c in cycles:
        lo_n, hi_n = cycle_range(new, c["wr"])
        lo_o, hi_o = cycle_range(old, c["wr"])
        fn, fo = lo_n <= c["cycle"] <= hi_n, lo_o <= c["cycle"] <= hi_o
        fx = any(lo_x <= c["cycle"] <= hi_x for lo_x, hi_x in (cycle_range(o, c["wr"]) for o in others))
        if fx and (fn or fo):
            amb += 1
            continue
        if fn and fo:
            amb += 1
            continue
        use += 1
        nin += int(fn)
        oin += int(fo)
    ev.update(usable=use, in_new=nin, in_old=oin, ambiguous=amb)
    if amb and not use:
        return "INCONCLUSIVE", dict(ev, why="every cycle fits both the written and the old (or a Relentless) range: write values further apart")
    if use < min_n:
        return "INCONCLUSIVE", dict(ev, why="%d usable completed cycles (need %d): the press did not run a full cycle in the watch" % (use, min_n))
    if nin >= max(min_n, math.ceil(0.8 * use)) and oin == 0:
        return "PASS", dict(ev, why="%d of %d cycles lie in the range of the written table" % (nin, use))
    if oin >= min_n and nin == 0:
        return "FAIL", dict(ev, why="%d cycles still lie in the range of the old table: the written values are ignored or latched" % oin)
    return "INCONCLUSIVE", dict(ev, why="mixed: %d new, %d old of %d usable" % (nin, oin, use))


def press_diag(rig, samples):
    """why 0 bursts? -> (code, text). Codes: CHAIN_IDENTITY_FAILED (fewer than 8 players pass [s+0x18] == info), NOT_TICKED (identity ok but the machine's
    last-tick word does not follow the clock: wrong object or it is not run), NEVER_TRIGGERED (chain verified, flag never 1), TRIGGERED_NO_CYCLE, CYCLES."""
    if rig["bound"] < 8:
        return "CHAIN_IDENTITY_FAILED", "only %d of %d players have [s+0x18] == their PlayerInfo and a shared clock: Player+0x28 is not the press object on this build, or route C picked the wrong player list (try source=teams)" % (rig["bound"], rig["n"])
    tk = [sum(1 for r in smp if r["ticked"]) for smp in samples]
    if not tk or statistics.median(tk) < 8:
        return "NOT_TICKED", "identity holds but s+0x28C does not follow the clock for %s players (median): the state machine is not run for them (partial press disabled in this match type?) or the offsets differ" % (statistics.median(tk) if tk else 0)
    flags = {(r["team"], r["handle"]) for smp in samples for r in smp if r["ok"] and r["flag"] == 1}
    if not flags:
        return "NEVER_TRIGGERED", "chain verified (identity + ticked in %d samples) and no player ever had flag 1: the trigger (0x146DF7EAC) did not select a presser: its gate needs controller+0x204 == 0x15 and tactic +0x53C <= +0x530 (see the tactic words); try Defensive Approach High (channel E) or a longer watch" % len(samples)
    if not press_cycles(samples):
        return "TRIGGERED_NO_CYCLE", "%d player(s) pressed (flag 1) but no burst finished and rested inside the watch: watch longer or write a shorter burst" % len(flags)
    return "CYCLES", "%d player(s) pressed, %d complete cycle(s)" % (len(flags), len(press_cycles(samples)))


def press_raw_lines(rig, rec, words=None):
    """the viewer: one line per player for one sample (first live run: validate offsets by eye)"""
    L = ["t=%.1f now=%s bound=%d/%d" % (rec[0]["t"] if rec else 0, rec[0]["now"] if rec else None, rig["bound"], rig["n"])]
    for r in rec:
        if not r["bound"]:
            L.append("  h%-3s team%s  UNBOUND" % (r["handle"], r["team"]))
            continue
        L.append("  h%-3s team%s wr=%s flag=%s why=%s start=%s end=%s last=%s (now-last=%s) acc=%.2f ticked=%d ok=%d same_s=%d" % (
            r["handle"], r["team"], r.get("wr"), r.get("flag"), r.get("why"), r.get("start"), r.get("end"), r.get("last"),
            None if r.get("last") is None or r["now"] is None else r["now"] - r["last"], r.get("acc", 0.0) or 0.0, r["ticked"], r["ok"], r["same_s"]))
    if words:
        for k, w in enumerate(words):
            if w:
                L.append("  tactic team%d: " % k + " ".join("+%X=%.3g/%d" % (o, v[0], v[1]) for o, v in sorted(w.items())) + "  (trigger gate: +53C <= +530)")
    return L


def press_watch2(dev, rig, secs, hz, clock, sleep, words_fn=None, log=print, raw=True):
    """sample every 1/hz s. With dev.seq (one request, several snapshots) the cadence is not limited by the ~1 s request latency; else one multi per sample."""
    t0 = clock()
    samples = []
    while clock() - t0 < secs:
        smp = press_read(dev, rig, clock() - t0)
        samples.append(smp)
        if raw and (len(samples) <= 3 or len(samples) % 5 == 0):
            for line in press_raw_lines(rig, smp, words_fn() if (words_fn and len(samples) % 10 == 1) else None):
                log(line)
        sleep(max(0.0, 1.0 / hz - 0.0))
    return samples


def read_press_table(dev, cat=None):
    """the live press table of the instance the consumers read: fields hi / lo / pmin / pmax of the regular class and the Relentless classes (seconds).
    -> (tabs dict | None, why). Uses consumers_obs.obs_instance for the identity-checked instance."""
    import consumers_obs as co
    v, ev = co.obs_instance(dev, "pressing.partial_highworkratetime", cat or co.load_cat())
    if v != "PASS":
        return None, "instance not identified (%s): %s" % (v, ev.get("why"))
    b = dev.multi([(ev["inst"] + 0x58, 0x50)])[0]
    if b is None or len(b) < 0x50:
        return None, "instance fields unreadable"
    g = lambda o: _f(b, o - 0x58)
    tab = {k: g(o) for k, o in TAB_OFF.items()}
    oth = [{k: g(o) for k, o in t.items()} for t in TAB_OTHER]
    allv = list(tab.values()) + [x for t in oth for x in t.values()]
    if not all(math.isfinite(x) and 0.0 <= x <= 120.0 for x in allv):
        return None, "press table values implausible: %s" % allv
    return {"regular": tab, "others": oth, "copy": ev.get("copy")}, "ok"


# ------------------------------------------------------------------------------------------------------------- formation state: line anchor
def state_pairs(ctx):
    """per team: the owner state header (owner + 0xE0 .. + 0x1A0) and the K pair (+0x5B38 + idx*0x38, 0x40 wide)"""
    pairs = []
    for k, ow in enumerate(ctx["owner"]):
        pairs += [(ow + be.C["OW_STATE"], 0xC0), (ctx["kobj"][k] + be.C["K_BASE"] + ctx["idx"][k] * be.C["K_STRIDE"], 0x40)]
    return pairs


def state_parse(ctx, res, t, scale=1.0):
    """-> [per-team record]; plausible False unless every float is finite and the origin / ball lie within 1.3 pitch lengths (a wrong object is a refusal, not a value)"""
    out = []
    flen = ctx.get("flen") or 105.0
    for k in range(2):
        sb, kb = res[2 * k], res[2 * k + 1]
        rec = {"t": round(t, 3), "team": ctx["idx"][k], "plausible": False}
        if sb is not None and len(sb) >= 0xA0:
            f = lambda o: _f(sb, o)
            rec.update(length=f(0x2C), cap=f(0x30), width=f(0x34), tval=f(0x40), x0=f(0x48), xline=f(0x4C), zc=f(0x50), ball_x=f(0x90), ball_z=f(0x98),
                       poss=_i(sb, 0x20), mode=_i(sb, 0x10), K2=(_f(kb, 4) if kb is not None and len(kb) >= 8 else float("nan")))
            fl = [rec[x] for x in ("length", "cap", "width", "tval", "x0", "xline", "zc", "ball_x", "ball_z", "K2")]
            rec["plausible"] = bool(all(math.isfinite(x) for x in fl) and abs(rec["x0"]) <= 1.3 * flen and abs(rec["xline"]) <= 1.3 * flen
                                    and abs(rec["ball_x"]) <= 1.3 * flen and 0.0 <= rec["tval"] <= 1.0 and rec["poss"] in (-1, 0, 1))
        out.append(rec)
    return out


def state_watch(dev, ctx, secs, hz, clock, sleep, win="A", log=print):
    t0 = clock()
    recs = []
    while clock() - t0 < secs:
        t = clock() - t0
        for r in state_parse(ctx, dev.multi(state_pairs(ctx)), t):
            r["win"] = win
            recs.append(r)
        if len(recs) <= 4 or len(recs) % 20 == 0:
            r = recs[-1]
            log("  t=%.0f team%s x0=%s xline=%s ball_x=%s tval=%s K2=%s plausible=%d" % (t, r["team"], r.get("x0"), r.get("xline"), r.get("ball_x"), r.get("tval"), r.get("K2"), r["plausible"]))
        sleep(1.0 / hz)
    return recs


def _ols(xs, ys):
    n = len(xs)
    mx, my = statistics.fmean(xs), statistics.fmean(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx > 1e-9 else 0.0
    return my - b * mx, b


def judge_cov_adjusted(base, test, key="xline", cov="ball_x", dmin=3.0, sign=0, tau=3.0, z_pass=3.0, z_mde=2.8, min_n=30):
    """Covariate-adjusted comparison of an AI state variable between a baseline condition and a test condition (within one match: windows, or between matches).
    base / test: record dicts with `key`, `cov`, `team`, `tval` and `plausible`. Per ball-possession class (tval == 1 / == 0 exactly) and team the baseline gets a
    straight line key ~ cov; the test residuals against it are the effect. SE from the baseline and the test residual sd with n_eff = n / tau (samples are autocorrelated). PASS |effect| >= dmin, |z| >= z_pass and the expected sign; FAIL |effect| < dmin although MDE = z_mde x SE <= dmin;
    INCONCLUSIVE otherwise. The classes pool by sign-aligned weights. -> (verdict, evidence)"""
    effs, wts, rows = [], [], []
    for team in (0, 1):
        for cls in (1.0, 0.0):
            b = [r for r in base if r["plausible"] and r["team"] == team and r["tval"] == cls]
            t = [r for r in test if r["plausible"] and r["team"] == team and r["tval"] == cls]
            if len(b) < min_n or len(t) < min_n:
                continue
            a0, a1 = _ols([r[cov] for r in b], [r[key] for r in b])
            sd = statistics.pstdev([r[key] - (a0 + a1 * r[cov]) for r in b]) or 1e-6
            res = [r[key] - (a0 + a1 * r[cov]) for r in t]
            eff = statistics.fmean(res)
            sd_t = statistics.pstdev(res) or 1e-6
            se = math.sqrt(sd ** 2 / (len(b) / tau) + sd_t ** 2 / (len(t) / tau))
            cov_lo, cov_hi = min(r[cov] for r in b), max(r[cov] for r in b)
            extrap = sum(1 for r in t if not cov_lo <= r[cov] <= cov_hi) / len(t)
            rows.append({"team": team, "class": cls, "n_base": len(b), "n_test": len(t), "effect": eff, "se": se, "resid_sd": sd, "slope": a1, "extrapolated_share": extrap})
            if extrap <= 0.2:
                effs.append(eff)
                wts.append(1.0 / se ** 2)
    ev = {"rows": rows, "key": key, "dmin": dmin}
    if not effs:
        return "INCONCLUSIVE", dict(ev, why="no team/possession class with >= %d plausible samples in both conditions and the test inside the baseline's ball range" % min_n)
    eff = sum(e * w for e, w in zip(effs, wts)) / sum(wts)
    se = 1.0 / math.sqrt(sum(wts))
    mde = z_mde * se
    ev.update(effect=eff, se=se, mde=mde, z=eff / se)
    if abs(eff) >= dmin and abs(eff / se) >= z_pass and (sign == 0 or eff * sign > 0):
        return "PASS", dict(ev, why="origin shifted %.2f (se %.2f) against the ball-adjusted baseline" % (eff, se))
    if abs(eff) < dmin and mde <= dmin:
        return "FAIL", dict(ev, why="|effect| %.2f < dmin %.2f with MDE %.2f <= dmin: no shift" % (abs(eff), dmin, mde))
    if abs(eff / se) >= z_pass and abs(eff) >= dmin:
        return "FAIL", dict(ev, why="significant shift with the wrong sign")
    return "INCONCLUSIVE", dict(ev, why="effect %.2f se %.2f MDE %.2f vs dmin %.2f" % (eff, se, mde, dmin))


# ------------------------------------------------------------------------------------------------------------- CLI helpers
def bind_best(dev, m, log=print):
    """bind the press chain on route C's player list; if fewer than 8 players are identified, retry on the other registry sources (the trigger loop walks
    [tm+0x18] + idx*0x1B8 + 0x900 = the 'teams' source). -> (rig, source name)"""
    import match_measure as mm
    best = (press_bind(dev, m.players), m.source)
    log("press bind: source %s -> %d of %d players identified" % (m.source, best[0]["bound"], best[0]["n"]))
    if best[0]["bound"] < 8:
        try:
            tb = mm.rd(m.T, m.t_span())
            for name, ptrs in m.sources(m.T, tb).items():
                pl = [p for p in m.players_from(ptrs) if p["team"] in (0, 1)]
                if not pl:
                    continue
                rg = press_bind(dev, pl)
                log("press bind: source %s -> %d of %d" % (name, rg["bound"], rg["n"]))
                if rg["bound"] > best[0]["bound"]:
                    best = (rg, name)
        except Exception as e:  # noqa: BLE001 - the retry is a convenience
            log("press bind: retry failed: %s" % e)
    return best


def _tab(s):
    hi, lo, a, b = (float(x) for x in s.split(","))
    return {"hi": hi, "lo": lo, "pmin": a, "pmax": b}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("cmd", nargs="*")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    opts = dict(x.split("=", 1) for x in a.cmd[1:] if "=" in x)
    if a.cmd[:1] in (["press-raw"], ["press-judge"]):
        harness.require_live(False, tool="tactics_obs.py " + a.cmd[0])
        import time, dev2
        import match_measure as mm
        key, base, lay = mm.load_layout(False)
        m = mm.locate_any(key, lay, argparse.Namespace(team_pids="", relocate=False), print)
        if not str(getattr(m, "route", "")).startswith("C"):
            print("REFUSED: positions come from route %r; the press chain starts at route C's Player*" % (getattr(m, "route", None),))
            print("VERDICT INCONCLUSIVE")
            return harness.EXIT["INCONCLUSIVE"]
        dev = be.DevMulti(dev2.call)
        rig, src = bind_best(dev, m)
        ctx, _bad = be.resolve(dev)
        words_fn = (lambda: tactic_words(dev, ctx)) if ctx else None
        secs = float(opts.get("secs", 60 if a.cmd[0] == "press-raw" else 120))
        samples = press_watch2(dev, rig, secs, float(opts.get("hz", 1)), time.time, time.sleep, words_fn)
        code, text = press_diag(rig, samples)
        print("DIAGNOSIS %s: %s" % (code, text))
        cyc = press_cycles(samples)
        for c in cyc[:12]:
            print("  cycle h%s team%s level %s: %d ticks = %.1f s" % (c["handle"], c["team"], c["wr"], c["cycle"], c["cycle"] / HZ))
        if a.cmd[0] == "press-judge":
            tabs, why = read_press_table(dev)
            print("press table: %s" % (json.dumps(tabs) if tabs else why))
            if "expect" not in opts or "stale" not in opts:
                print("VERDICT INCONCLUSIVE (give expect=hi,lo,pmin,pmax stale=hi,lo,pmin,pmax)")
                return harness.EXIT["INCONCLUSIVE"]
            v, ev = judge_press_cycle_exact(cyc, _tab(opts["expect"]), _tab(opts["stale"]), tabs["others"] if tabs else ())
            if code in ("CHAIN_IDENTITY_FAILED", "NOT_TICKED"):
                v, ev = "INCONCLUSIVE", dict(ev, why="chain not verified: " + code)
            print(json.dumps(ev))
            print("VERDICT %s" % v)
            return harness.EXIT.get(v, harness.EXIT["INCONCLUSIVE"])
        print("VERDICT INCONCLUSIVE (viewer only: compare the printed words with the model by eye)")
        return harness.EXIT["INCONCLUSIVE"]
    if a.cmd[:1] == ["state-watch"]:
        harness.require_live(False, tool="tactics_obs.py state-watch")
        import time, dev2
        dev = be.DevMulti(dev2.call)
        ctx, bad = be.resolve(dev)
        if ctx is None:
            print("REFUSED: %s" % "; ".join(bad[:3]))
            return harness.EXIT["INCONCLUSIVE"]
        recs = state_watch(dev, ctx, float(opts.get("secs", 60)), float(opts.get("hz", 1)), time.time, time.sleep, opts.get("win", "A"))
        out = opts.get("out")
        if out:
            with open(out, "w", newline="", encoding="utf-8") as fh:
                w = csv.DictWriter(fh, fieldnames=sorted({k for r in recs for k in r}))
                w.writeheader()
                w.writerows(recs)
        print("state-watch: %d records, %d plausible" % (len(recs), sum(1 for r in recs if r["plausible"])))
        return 0
    if a.cmd[:1] == ["state-judge"] and len(a.cmd) >= 3:
        def load(p):
            with open(p, encoding="utf-8") as fh:
                rows = list(csv.DictReader(fh))
            for r in rows:
                for k in ("x0", "xline", "ball_x", "tval", "team"):
                    r[k] = float(r[k]) if k != "team" else int(float(r[k]))
                r["plausible"] = r["plausible"] in ("True", "1")
            return rows
        v, ev = judge_cov_adjusted(load(a.cmd[1]), load(a.cmd[2]), dmin=float(opts.get("dmin", 3)), sign=int(opts.get("sign", 0)))
        print(json.dumps(ev, default=str))
        print("VERDICT %s" % v)
        return harness.EXIT.get(v, harness.EXIT["INCONCLUSIVE"])
    print(__doc__)
    return 2


# ------------------------------------------------------------------------------------------------------------- self-test (mock worlds)
class _Mem:
    def __init__(self):
        self.mem = {}

    def put(self, a, b):
        self.mem[a] = bytes(b)

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


class _PressWorld(_Mem):
    """22 players; s = [obj+0x28]; the machine of 0x14164F338 documented in the header; table hi / lo / penalties in seconds; ticked=False = state not ticked"""
    BASE_ADDR = harness.MOCK_HEAP + 0x800000

    def __init__(self, hi=6.0, lo=4.0, pmin=7.0, pmax=15.0, trigger=True, tick_machine=True, zero=False, wrong_info=False, seed=3):
        import random
        super().__init__()
        self.r = random.Random(seed)
        self.tab = (hi, lo, pmin, pmax)
        self.trigger, self.tick_machine, self.zero = trigger, tick_machine, zero
        self.tick = 9000
        self.brain = self.BASE_ADDR + 0x300000
        self.y = self.brain + 0x2000
        self.put(self.brain + CLOCK_CHAIN[0], struct.pack("<Q", self.y))
        self.players, self.st = [], []
        for i in range(22):
            o = self.BASE_ADDR + i * 0x4000
            s = o + 0x2000
            info = self.BASE_ADDR + 0x200000 + i * 0x100
            self.players.append({"handle": i, "team": i % 2, "obj": o, "info": info})
            self.put(o + 0x28, struct.pack("<Q", s))
            hdr = bytearray(0x30)
            struct.pack_into("<Q", hdr, 0, self.brain)
            struct.pack_into("<Q", hdr, S_INFO, info + (8 if wrong_info else 0))
            struct.pack_into("<Q", hdr, S_T, self.BASE_ADDR + 0x280000)
            self.put(s, hdr)
            self.st.append({"s": s, "flag": 0, "why": -1, "start": 0, "end": 0, "last": 0, "acc": 0.0, "wr": 2 if i % 3 == 0 else (1 if i % 3 == 1 else 0),
                            "next": 300 + 40 * i if trigger else 10 ** 9})
        self.sync()

    def D(self, wr):
        hi, lo = self.tab[0], self.tab[1]
        return int(60 * (hi if wr == 2 else lo if wr == 1 else hi - 0.5 * (hi - lo)))

    def sync(self):
        self.put(self.y + CLOCK_CHAIN[1], struct.pack("<f", float(self.tick)))
        for st in self.st:
            blk = bytearray(BLEN)
            if not self.zero:
                struct.pack_into("<i", blk, 0, st["wr"])
                for k in ("end", "start", "last"):
                    struct.pack_into("<i", blk, OFF[k] - B0, st[k])
                struct.pack_into("<f", blk, OFF["acc"] - B0, st["acc"])
                blk[OFF["flag"] - B0] = st["flag"]
                struct.pack_into("<i", blk, OFF["why"] - B0, st["why"])
            self.put(st["s"] + B0, blk)

    def advance(self, n):
        for _ in range(n):
            self.tick += 1
            for st in self.st:
                if self.tick_machine:
                    st["last"] = self.tick
                if st["flag"] == 0 and self.tick >= st["next"]:
                    st.update(flag=1, why=3, start=self.tick, end=0)
                elif st["flag"] == 1 and st["end"] == 0 and self.tick >= st["start"] + self.D(st["wr"]):
                    frac = 0.5
                    st["end"] = int(st["start"] + self.D(st["wr"]) + 60 * (self.tab[3] + frac * (self.tab[2] - self.tab[3])))
                elif st["flag"] == 1 and st["end"] > 0 and self.tick >= st["end"]:
                    st.update(flag=0, why=-1, start=0, end=0, next=self.tick + 400)
        self.sync()


def _run_press(w, secs=140, step=30):
    rig = press_bind(w, w.players)
    smps = []
    for _ in range(int(secs * 60 / step)):
        w.advance(step)
        smps.append(press_read(w, rig, w.tick / HZ))
    return rig, smps


def self_test():
    fails = []

    def check(c, msg):
        print(("  ok   " if c else "  FAIL ") + msg)
        if not c:
            fails.append(msg)
    NEW, OLD = {"hi": 25.0, "lo": 4.0, "pmin": 7.0, "pmax": 15.0}, {"hi": 6.0, "lo": 4.0, "pmin": 7.0, "pmax": 15.0}
    # 1. a good world: identity, ticked, cycles, exact judge
    w = _PressWorld(hi=25.0, lo=4.0)
    rig, smps = _run_press(w, 200)
    check(rig["bound"] == 22, "press_bind: all 22 players identified by [s+0x18] == info (bound %d)" % rig["bound"])
    check(all(r["ok"] for r in smps[-1]), "press_read: every player ok (ticked, plausible)")
    cyc = press_cycles(smps)
    check(len(cyc) >= 4, "press_cycles: %d exact cycles from rest-phase samples" % len(cyc))
    lvl2 = [c for c in cyc if c["wr"] == 2]
    check(lvl2 and all(32 * 60 - 2 <= c["cycle"] <= 40 * 60 + 2 for c in lvl2), "level-2 cycles lie in [32, 40] s for D = 25 s (%s)" % [c["cycle"] / 60 for c in lvl2][:4])
    check(judge_press_cycle_exact(cyc, NEW, OLD)[0] == "PASS", "judge_press_cycle_exact: written 25 s -> PASS")
    check(judge_press_cycle_exact(cyc, OLD, NEW)[0] == "FAIL", "judge_press_cycle_exact: cycles in the other table's range -> FAIL")
    check(judge_press_cycle_exact(cyc, NEW, {"hi": 25.0, "lo": 4.0, "pmin": 9.0, "pmax": 13.0})[0] == "INCONCLUSIVE", "overlapping tables -> INCONCLUSIVE")
    check(judge_press_cycle_exact(cyc[:1], NEW, OLD)[0] == "INCONCLUSIVE", "one cycle is not enough (min 2) -> INCONCLUSIVE")
    check(press_diag(rig, smps)[0] == "CYCLES", "press_diag: CYCLES")
    # level-0 and level-1 players follow their own formula
    check(all(cycle_range(NEW, 1)[0] < c["cycle"] < cycle_range(NEW, 1)[1] for c in cyc if c["wr"] == 1), "level-1 cycles fit D = 60 x lo + rest")
    # 2. zeroed memory: the old gate would have passed it; the new one must refuse (identity + ticked)
    wz = _PressWorld(zero=True, tick_machine=False)
    rz, sz = _run_press(wz, 20)
    check(rz["bound"] == 22 and not any(r["ok"] for s_ in sz for r in s_), "zeroed press blocks: identity holds but NOT ticked -> never ok (the old gate accepted this)")
    check(press_diag(rz, sz)[0] == "NOT_TICKED", "press_diag: NOT_TICKED for a machine that does not follow the clock")
    # 3. wrong chain (info mismatch)
    wi = _PressWorld(wrong_info=True)
    ri, si = _run_press(wi, 10)
    check(ri["bound"] == 0 and press_diag(ri, si)[0] == "CHAIN_IDENTITY_FAILED", "press_bind: [s+0x18] != info -> nothing bound, CHAIN_IDENTITY_FAILED")
    check(not any(r["ok"] for r in si[-1]), "unbound players never produce a value")
    # 4. press never triggered
    wn = _PressWorld(trigger=False)
    rn, sn = _run_press(wn, 60)
    code, text = press_diag(rn, sn)
    check(code == "NEVER_TRIGGERED" and "0x15" in text, "press_diag: chain verified, flag never 1 -> NEVER_TRIGGERED with the gate text")
    check(judge_press_cycle_exact(press_cycles(sn), NEW, OLD)[0] == "INCONCLUSIVE", "no cycles -> INCONCLUSIVE")
    # 5. triggered but the watch is too short for a full cycle
    wt = _PressWorld(hi=25.0, lo=25.0)
    rt, st_ = _run_press(wt, 20)
    check(press_diag(rt, st_)[0] in ("TRIGGERED_NO_CYCLE", "NEVER_TRIGGERED"), "short watch: no cycle diagnosed (%s)" % press_diag(rt, st_)[0])
    # 6. work-rate level that changes during the burst is skipped
    smp = [dict(r) for r in smps[-1]]
    check(isinstance(press_raw_lines(rig, smp, None), list) and len(press_raw_lines(rig, smp, None)) == 23, "press_raw_lines: header + 22 rows")
    # 7. state series: covariate-adjusted judge on a synthetic deterministic origin
    import random
    rnd = random.Random(7)

    def series(shift, n=400, noise=0.4):
        out = []
        for k in range(n):
            for team in (0, 1):
                bx = rnd.uniform(-45, 45)
                cls = 1.0 if (k // 40) % 2 == 0 else 0.0
                out.append({"team": team, "tval": cls, "ball_x": bx, "xline": 10.0 + 0.30 * bx + shift + rnd.gauss(0, noise), "plausible": True})
        return out
    base = series(0.0)
    check(judge_cov_adjusted(base, series(6.0))[0] == "PASS", "judge_cov_adjusted: +6 m origin shift -> PASS (ball-adjusted)")
    check(judge_cov_adjusted(base, series(0.0))[0] == "FAIL", "judge_cov_adjusted: no shift with a small MDE -> FAIL")
    check(judge_cov_adjusted(base, series(6.0), sign=-1)[0] == "FAIL", "judge_cov_adjusted: significant shift with the wrong sign -> FAIL")
    check(judge_cov_adjusted(base, series(1.0, noise=25.0), dmin=3.0)[0] == "INCONCLUSIVE", "judge_cov_adjusted: noisy small shift -> INCONCLUSIVE")
    shifted_ball = [dict(r, ball_x=r["ball_x"] + 200) for r in series(0.0)]
    check(judge_cov_adjusted(base, shifted_ball)[0] == "INCONCLUSIVE", "judge_cov_adjusted: test ball range outside the baseline's -> INCONCLUSIVE (no extrapolation)")
    check(judge_cov_adjusted(base[:10], series(6.0)[:10])[0] == "INCONCLUSIVE", "judge_cov_adjusted: too few samples -> INCONCLUSIVE")
    # ball position confounds raw means but not the adjusted effect
    hi_ball = [dict(r, ball_x=r["ball_x"] * 0.5 + 15, xline=10.0 + 0.30 * (r["ball_x"] * 0.5 + 15)) for r in series(0.0)]
    check(abs(statistics.fmean(r["xline"] for r in hi_ball) - statistics.fmean(r["xline"] for r in base)) > 1.5 and judge_cov_adjusted(base, hi_ball)[0] == "FAIL",
          "a different ball distribution moves the raw mean by > 1.5 m but not the adjusted effect -> FAIL (no knob effect)")
    # 8. state_parse refusal
    class Ctx(dict):
        pass
    ctx = {"owner": [0x1000, 0x2000], "kobj": [0x3000, 0x4000], "idx": [0, 1], "flen": 105.0}
    good = bytearray(0xC0)
    struct.pack_into("<f", good, 0x2C, 50.0)
    struct.pack_into("<f", good, 0x30, 60.0)
    struct.pack_into("<f", good, 0x34, 40.0)
    struct.pack_into("<f", good, 0x40, 1.0)
    struct.pack_into("<f", good, 0x48, 20.0)
    struct.pack_into("<f", good, 0x4C, 21.0)
    struct.pack_into("<i", good, 0x20, 0)
    kb = bytearray(0x40)
    struct.pack_into("<f", kb, 4, 0.5)
    ok = state_parse(ctx, [bytes(good), bytes(kb), bytes(good), bytes(kb)], 1.0)
    check(all(r["plausible"] for r in ok), "state_parse: a plausible state is accepted")
    bad = bytearray(good)
    struct.pack_into("<f", bad, 0x48, 9e9)
    check(not state_parse(ctx, [bytes(bad), bytes(kb), None, None], 1.0)[0]["plausible"] and not state_parse(ctx, [bytes(bad), bytes(kb), None, None], 1.0)[1]["plausible"],
          "state_parse: implausible / unreadable state is not plausible (never a value)")
    nan = bytearray(good)
    struct.pack_into("<f", nan, 0x4C, float("nan"))
    check(not state_parse(ctx, [bytes(nan), bytes(kb), bytes(good), bytes(kb)], 1.0)[0]["plausible"], "state_parse: NaN origin is not plausible")
    print("SELF-TEST %s" % ("FAILED" if fails else "PASSED"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
