"""battery_lib.py - pure part of the Turbo 2.5 tuning-knob battery (tools/, offline; no game access, no dev service).

The battery decides whether a gameplay-table knob earns a slider (AGENT_RULES_25: (1) a deterministic engine-read check passes in game
AND (2) a visible effect at an extreme value is seen in game). This module holds everything that needs no game:
  - PROBES: per knob the engine-read observable (or why there is none), the route-C metric, the extreme values with the expected sign,
    the practical minimum shift (dmin) and the engine-safe test range;
  - parse_knob_list(): the knob list file (one knob per line, optional explicit extreme values);
  - plan_schedule(): the window schedule (baseline A | extreme E1.. | A | ...) fitted to a time budget;
  - position metrics per team from the 22 players (10 outfield players: lateral spread, depth spread, back line, centroid, centre-back
    split, players ahead of the ball, pressing distance, nearest-opponent distance), with the ball / without the ball;
  - effect statistics: covariate-adjusted window means, batch-means standard errors, trend-interpolated baseline, MDE, pre-registered
    PASS / FAIL / INCONCLUSIVE per knob extreme.
Run  python battery_lib.py --self-test  for the offline proof.

Pre-registered criterion (also printed by the driver). For one extreme window E of knob k and the metric m (context with / without the ball):
  effect = mean_adj(E) - baseline(t_E)           baseline(t) = linear interpolation of the neighbouring baseline windows' adjusted means
  SE     = sqrt(SE_E^2 + SE_base^2)              SE of a window mean = max(batch means SE, window-to-window sd of the baselines): the
                                                 game state drifts from window to window (X1P.md 1: A/A floor 3.6 - 12.6 m), which the within-
                                                 window batch SE cannot see, so the neighbouring baseline windows set a floor (inflated for few df)
  MDE    = 2.8 * SE                              minimum effect detected with 95 % confidence and 80 % power (1.96 + 0.84)
  PASS         |effect| >= dmin AND |effect| / SE >= Z_PASS AND sign as expected (expected 0 = either sign)
  FAIL         (|effect| < dmin AND MDE <= dmin: the extreme moved the metric by less than dmin although the test could see dmin)
               or (significant effect in the WRONG direction: |effect| >= dmin, z >= Z_PASS)
  INCONCLUSIVE otherwise (too few samples, MDE > dmin = under-powered, effect between the two bars, unstable baseline)
The A/A control is printed (adjacent baseline windows' difference and the implied window sd); a noisy game state shows up as a large SE and
hence a large MDE, i.e. INCONCLUSIVE - never as a PASS.
"""
import csv, math, os, re, statistics, sys

BLOCK = 6          # samples per batch-means block (autocorrelation time of the route-C metrics: 2.4 - 3.5 samples, X1P.md 1)
Z_PASS = 3.0
Z_MDE = 2.8
PAIRED_WAIT = 18.0   # s allowed to wait for a frozen-input moment before a paired-observable write (battery_write.py)
N_MIN = 12         # valid samples a window needs to be judged
CONTEXTS = ("with", "without")
VERDICTS = ("PASS", "FAIL", "INCONCLUSIVE")

# ----------------------------------------------------------------------------------------------------------------- probes
# ext = [(value, expected sign of the metric shift at that value)]; the FIRST entry is the primary extreme (always scheduled), the others
# only with --both. obs: engine-read observable kind (battery_engine.py) or None; why_no_obs says why none exists.
# dmin: smallest shift worth a slider, in the metric's unit. tmin / tmax: the engine-safe range the battery may write (wider than
# Turbo's slider range on purpose: an extreme must beat game-state noise; narrower than anything that could crash the game).
# Consumers: [V OLD] = read in the disassembly of build 6AB9813C (turbo_output\assessment_20261013\T1 / X1P\re, and 0x14164F338 and
# 0x141B314B4 disassembled from the same image for this track); live = 6AC07E31, same offsets unverified until the battery runs [A].
PROBES = {
    "shape.width_in": dict(
        obs="band_att", metric="lat_sd", ctx="with", unit="m", dmin=2.5, tmin=0.2, tmax=2.5, ext=[(0.3, -1), (1.7, +1)],
        consumer="0x140F56030 cap + 0x140F52B4C width [V OLD]: formation width state+0x30 / +0x34 every call",
        note="engine band is exact for low values; a high value saturates at 1.2 W (ineligible), so the engine check uses the low extreme"),
    "shape.width_out": dict(
        obs="band_def", metric="lat_sd", ctx="without", unit="m", dmin=2.5, tmin=0.2, tmax=2.5, ext=[(0.3, -1), (1.7, +1)],
        consumer="0x140F56030 term D = h * [def + 0x28] [V OLD]: formation width state+0x30 at t = 0 (without the ball)"),
    "shape.width_in_cb": dict(
        obs=None, metric="cb_split", ctx="with", unit="m", dmin=3.0, tmin=0.2, tmax=2.5, ext=[(0.3, -1), (1.7, +1)],
        consumer="0x1417F5F38 slot mapper (CB class, 0x1417F60C5) [V OLD]",
        why_no_obs="the mapper returns the player's target through an out-vector of its caller; no persistent field holding the targets "
                   "is located [U]"),
    "shape.length_in": dict(
        obs="paired_len", obs_style=1, metric="len_sd", ctx="with", unit="m", dmin=2.0, tmin=0.3, tmax=1.6, ext=[(0.3, -1), (1.6, +1)],
        consumer="0x140F57098 [V OLD]: [att + 0x30] when the team's build-up style [[[tm+0x80]]+0x554] == 1 -> state+0x2C (formation length)"),
    "shape.length_in_possession": dict(
        obs="paired_len", obs_style=0, metric="len_sd", ctx="with", unit="m", dmin=2.0, tmin=0.3, tmax=1.6, ext=[(0.3, -1), (1.6, +1)],
        consumer="0x140F57098 [V OLD]: [att + 0x50] when the team's build-up style == 0 -> state+0x2C"),
    "shape.length_out": dict(
        obs=None, metric="len_sd", ctx="without", unit="m", dmin=2.0, tmin=0.3, tmax=1.6, ext=[(0.4, -1), (1.6, +1)],
        consumer="none found: 0x140F57098 reads only the attacking group; the defensive instance's +0x30 has no reader in T1 / X1P\\re [U]",
        why_no_obs="no consumer of the defensive length field is in the supplied disassemblies [U]"),
    "shape.gap_dmf": dict(
        obs=None, metric="gap_back_mid", ctx="without", unit="m", dmin=1.5, tmin=2.0, tmax=30.0, ext=[(3.0, -1), (20.0, +1)],
        consumer="zonal_defense group (+0x148): one direct reader of +0x634 only (0x140D199D4, T5 slice) [V OLD]; this field's reader is "
                 "behind a cached group pointer [U]",
        why_no_obs="reader not located [U]"),
    "shape.gap_dmf_deep": dict(
        obs=None, metric="gap_back_mid", ctx="without", unit="m", dmin=1.5, tmin=2.0, tmax=30.0, ext=[(3.0, -1), (22.0, +1)],
        consumer="zonal_defense: reader not located [U]", why_no_obs="reader not located [U]"),
    "shape.gap_mf_deep": dict(
        obs=None, metric="len_range", ctx="without", unit="m", dmin=2.0, tmin=2.0, tmax=32.0, ext=[(6.0, -1), (26.0, +1)],
        consumer="zonal_defense: reader not located [U]", why_no_obs="reader not located [U]"),
    "shape.holding_line_depth": dict(
        obs=None, metric="backline_m", ctx="without", unit="m", dmin=2.0, tmin=0.0, tmax=1.0, ext=[(0.0, 0), (1.0, 0)],
        consumer="defense_general (+0xA8): no direct reader from G (T5 slice) [U]", why_no_obs="reader not located [U]"),
    "pressing.trigger_depth": dict(
        obs=None, metric="centroid_m", ctx="without", unit="m", dmin=2.5, tmin=0.0, tmax=1.0, ext=[(0.05, +1), (0.9, -1)],
        consumer="zonal_defense: reader not located [U]", why_no_obs="reader not located [U]"),
    "pressing.zone_marking_depth": dict(
        obs=None, metric="nn_opp", ctx="without", unit="m", dmin=1.0, tmin=0.0, tmax=1.5, ext=[(0.05, 0)],
        consumer="zonal_defense: reader not located [U]", why_no_obs="reader not located [U]"),
    "pressing.no_double_marking_own_half": dict(
        obs=None, metric=None, ctx="without", unit="", dmin=0.0, tmin=0, tmax=1, ext=[(0, 0)],
        consumer="defense_general: reader not located [U]", why_no_obs="a boolean without a route-C metric",
        skip_effect="a boolean switch with no position metric: the visible-effect check does not apply"),
    "marking.tightness": dict(
        obs=None, metric="nn_opp", ctx="without", unit="m", dmin=1.0, tmin=0.0, tmax=3.0, ext=[(2.5, -1), (0.0, +1)],
        consumer="markingtightness (+0x68): no direct reader from G (T5 slice) [U]", why_no_obs="reader not located [U]"),
    "runs.frequency": dict(
        obs=None, metric="ahead_share", ctx="with", unit="", dmin=0.05, tmin=0.0, tmax=5.0, ext=[(3.0, +1), (0.1, -1)],
        consumer="attackingaggressiveness (+0x30): no direct reader from G (T5 slice) [U]", why_no_obs="reader not located [U]"),
    "pressing.partial_highworkratetime": dict(
        obs=None, metric="press_m", ctx="without", unit="m", dmin=1.0, tmin=0.5, tmax=30.0, ext=[(25.0, -1), (0.5, +1)],
        consumer="0x14164F5A8 (reads +0x84 at 0x14164F62F) called from 0x14164F338 [V OLD, disassembled for this track]; the per-team press "
                 "controller object it works on is not located [U]", why_no_obs="the press controller object is not located [U]"),
    "pressing.partial_cooldownmultiplier": dict(
        obs=None, metric="press_m", ctx="without", unit="m", dmin=1.0, tmin=0.1, tmax=5.0, ext=[(0.1, -1), (5.0, +1)],
        consumer="0x14164F338 [V OLD]: [group + 0x74] divides the elapsed time of the press counter at +0x290 of the controller object "
                 "(0x14164F444); the object is not located [U]", why_no_obs="the press controller object is not located [U]"),
}
for _k in ("deep_max", "balanced_min", "balanced_max", "high_min", "high_max", "aggressive_min"):
    PROBES["lineband." + _k] = dict(
        obs=None, metric="backline_m", ctx="without", unit="m", dmin=2.0, tmin=0, tmax=100, ext=[], setup_only=True,
        consumer="0x141B314B4 [V OLD]: depth -> defensive style with HARD-CODED 30 / 60 / 90 (24 call sites); no reader of these table "
                 "fields is in the supplied disassemblies [U]",
        why_no_obs="read at the next match set-up, not in a running match; the one classifier found ignores the table",
        skip_effect="applies at the next match set-up: a one-launch in-match battery cannot A/B it (needs a hub pre-write and a second "
                    "launch)")


def probe_of(knob_id):
    return PROBES.get(knob_id)


# ----------------------------------------------------------------------------------------------------------------- knob list
def parse_knob_list(text, catalogue=None):
    """-> (entries, problems). Line: '<knob id> [value ...] [both]' ; '#' starts a comment. Values are the explicit extremes (replace
    the probe's defaults, expected sign = the probe's sign for the nearest default extreme, else 0). 'both' = schedule every default
    extreme. entries: [{id, ext:[(value, sign)], both}] ; problems: strings (unknown knob, value outside the engine-safe range)."""
    entries, probs = [], []
    seen = set()
    for ln, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        toks = line.replace(",", " ").split()
        kid, rest = toks[0], toks[1:]
        pr = probe_of(kid)
        if pr is None:
            probs.append("line %d: unknown knob %r (known: %d probes)" % (ln, kid, len(PROBES)))
            continue
        if catalogue is not None and kid not in catalogue:
            probs.append("line %d: %s is not in the knob catalogue" % (ln, kid))
            continue
        if kid in seen:
            probs.append("line %d: %s listed twice" % (ln, kid))
            continue
        seen.add(kid)
        both = "both" in rest
        vals = []
        for t in rest:
            if t == "both":
                continue
            try:
                vals.append(float(t))
            except ValueError:
                probs.append("line %d: %r is neither a number nor 'both'" % (ln, t))
        ext = []
        if vals:
            for v in vals:
                if not (pr["tmin"] <= v <= pr["tmax"]):
                    probs.append("line %d: %s = %g is outside the engine-safe test range %g .. %g" % (ln, kid, v, pr["tmin"], pr["tmax"]))
                    continue
                near = min(pr["ext"], key=lambda e: abs(e[0] - v)) if pr["ext"] else (v, 0)
                ext.append((v, near[1] if abs(near[0] - v) <= 0.35 * (abs(near[0]) + 1e-9) else 0))
            if not ext:
                continue
        else:
            ext = list(pr["ext"]) if both else list(pr["ext"][:1])
        entries.append({"id": kid, "ext": ext, "both": both})
    return entries, probs


# ----------------------------------------------------------------------------------------------------------------- schedule
def plan_schedule(entries, win=35.0, guard=5.0, base_every=3, budget=440.0):
    """-> (windows, deferred). windows: [{name, kind 'base'|'ext', knob, value, sign, dur, guard}] in time order, always starting and
    ending with a baseline. A baseline every base_every extreme windows (trend control). Extreme windows that do not fit the budget
    (sum of dur + guard) are deferred, last ones first. win is capped at 40 s (the battery's rule)."""
    win = min(float(win), 40.0)
    exts = []
    deferred = []
    for e in entries:
        pr = probe_of(e["id"])
        if pr.get("skip_effect") or pr["metric"] is None:
            deferred.append((e["id"], pr.get("skip_effect") or "no position metric"))
            continue
        for v, sg in e["ext"]:
            exts.append((e["id"], v, sg))

    def extra_of(kid):
        return PAIRED_WAIT if (probe_of(kid) or {}).get("obs") == "paired_len" else 0.0

    def build(n):
        w, nb = [], 0
        w.append({"name": "A1", "kind": "base", "knob": None, "value": None, "sign": 0, "dur": win, "guard": 0.0})
        nb = 1
        for j in range(n):
            kid, v, sg = exts[j]
            w.append({"name": "E%d" % (j + 1), "kind": "ext", "knob": kid, "value": v, "sign": sg, "dur": win,
                      "guard": guard + extra_of(kid)})
            last = j == n - 1
            if (j + 1) % base_every == 0 or last:
                nb += 1
                # the revert of a paired-observable knob also waits for a frozen moment, inside this baseline's guard
                w.append({"name": "A%d" % nb, "kind": "base", "knob": None, "value": None, "sign": 0, "dur": win,
                          "guard": guard + extra_of(kid)})
        return w

    n = len(exts)
    while n > 0 and sum(x["dur"] + x["guard"] for x in build(n)) > budget:
        n -= 1
    if n == 0 and exts:
        deferred += [(kid, "no room in the %.0f s budget" % budget) for kid, _, _ in exts]
        return [], deferred
    for kid, v, sg in exts[n:]:
        deferred.append((kid, "extreme %g does not fit the %.0f s budget" % (v, budget)))
    return (build(n) if n else []), deferred


def schedule_seconds(windows):
    return sum(w["dur"] + w["guard"] for w in windows)


# ----------------------------------------------------------------------------------------------------------------- geometry
def axes_from_rows(players):
    """players: [{team, fpos, p:(x,y,z)}] of ONE sample -> along index (0 or 2). Same rule as match_measure.axis_from_gks."""
    gk = {}
    for q in players:
        if q["fpos"] == 0 and q["p"] is not None:
            gk[q["team"]] = q["p"]
    if 0 in gk and 1 in gk:
        return 0 if abs(gk[0][0] - gk[1][0]) >= abs(gk[0][2] - gk[1][2]) else 2
    pts = [q["p"] for q in players if q["p"] is not None]
    if not pts:
        return 0
    return 0 if statistics.median(abs(p[0]) for p in pts) >= statistics.median(abs(p[2]) for p in pts) else 2


def team_metrics(rows, opp_rows, L, along, lat, ball, carrier_pos):
    """rows: [{fpos, p}] of one team (11 players), opp_rows the other team. -> dict of metrics, or None when fewer than 8 outfielders.
    depth = metres from the team's own goal line along the pitch. All distances in the unit of the positions (metres)."""
    pts = [q for q in rows if q["p"] is not None and all(math.isfinite(c) for c in q["p"])]
    gk = [q for q in pts if q["fpos"] == 0]
    if gk:
        s = 1.0 if gk[0]["p"][along] > 0 else -1.0
        out = [q for q in pts if q is not gk[0]]
    else:
        s = 1.0 if statistics.fmean(q["p"][along] for q in pts) > 0 else -1.0
        gkq = max(pts, key=lambda q: s * q["p"][along]) if pts else None
        out = [q for q in pts if q is not gkq]
    if len(out) < 8:
        return None
    dep = [L / 2 - s * q["p"][along] for q in out]
    lt = [q["p"][lat] for q in out]
    order = sorted(range(len(out)), key=lambda i: dep[i])
    back = order[:4]
    mid = order[4:7]
    m = {"lat_sd": statistics.pstdev(lt), "len_sd": statistics.pstdev(dep), "len_range": max(dep) - min(dep),
         "backline_m": statistics.fmean(dep[i] for i in back), "centroid_m": statistics.fmean(dep),
         "own_goal": s}
    m["gap_back_mid"] = (statistics.fmean(dep[i] for i in mid) - m["backline_m"]) if len(mid) == 3 else float("nan")
    cl = statistics.fmean(lt)
    cb = sorted(back, key=lambda i: abs(lt[i] - cl))[:2]
    m["cb_split"] = abs(lt[cb[0]] - lt[cb[1]])
    if ball is not None:
        att = -s  # direction of the opponent's goal along the pitch
        m["ahead_share"] = sum(1 for q in out if att * (q["p"][along] - ball[along]) > 0) / len(out)
    opp = [q for q in opp_rows if q["p"] is not None and all(math.isfinite(c) for c in q["p"]) and q["fpos"] != 0]
    if opp:
        nn = [min(math.hypot(q["p"][along] - o["p"][along], q["p"][lat] - o["p"][lat]) for o in opp) for q in out]
        m["nn_opp"] = statistics.fmean(nn)
    if carrier_pos is not None:
        d = sorted(math.hypot(q["p"][along] - carrier_pos[along], q["p"][lat] - carrier_pos[lat]) for q in out)
        m["press_m"] = statistics.fmean(d[:3])
    return m


def sample_records(samples, L, W):
    """samples: [{i, t, win, players:[{handle, team, fpos, p}], ball:(x,y,z)|None, carrier: handle|None}] ->
    [{i, t, win, v:{'<metric>:with'|'<metric>:without': value}, cov:{with:(depth, lat), without:(depth, lat)}}]
    One value per context per sample: 'with' = the carrier's team, 'without' = the other team. Samples without a carrier give nothing."""
    out = []
    for smp in samples:
        pl = smp["players"]
        if smp["carrier"] is None:
            continue
        cteam = next((q["team"] for q in pl if q["handle"] == smp["carrier"]), None)
        if cteam is None:
            continue
        cpos = next((q["p"] for q in pl if q["handle"] == smp["carrier"]), None)
        along = axes_from_rows(pl)
        lat = 2 if along == 0 else 0
        rows = {t: [q for q in pl if q["team"] == t] for t in (0, 1)}
        v, cov = {}, {}
        for ctx, tm in (("with", cteam), ("without", 1 - cteam)):
            m = team_metrics(rows[tm], rows[1 - tm], L, along, lat, smp["ball"], cpos if ctx == "without" else None)
            if m is None:
                continue
            for k, val in m.items():
                if k != "own_goal" and isinstance(val, float) and math.isfinite(val):
                    v["%s:%s" % (k, ctx)] = val
            if smp["ball"] is not None:
                bd = (L / 2 - m["own_goal"] * smp["ball"][along]) / L
                cov[ctx] = (bd, abs(smp["ball"][lat]) / (W / 2))
        if v:
            out.append({"i": smp["i"], "t": smp["t"], "win": smp["win"], "v": v, "cov": cov})
    return out


def load_positions_csv(path):
    """positions.csv of match_measure (one row per player per sample) -> samples list of sample_records()'s input"""
    by = {}
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            i = int(r["sample"])
            s = by.setdefault(i, {"i": i, "t": float(r["t"]), "win": r["win"], "players": [], "ball": None, "carrier": None})
            try:
                p = (float(r["x"]), float(r["y"]), float(r["z"]))
                if not all(math.isfinite(c) for c in p):
                    p = None
            except ValueError:
                p = None
            s["players"].append({"handle": int(r["handle"]), "team": int(r["team"]), "fpos": int(r["fpos"]), "p": p})
            try:
                b = (float(r["ball_x"]), float(r["ball_y"]), float(r["ball_z"]))
                s["ball"] = b if all(math.isfinite(c) for c in b) else None
            except ValueError:
                pass
            c = r["carrier"]
            s["carrier"] = int(c) if c not in ("", "None", "-1") else None
    return [by[i] for i in sorted(by)]


# ----------------------------------------------------------------------------------------------------------------- statistics
def ols(X, y):
    """least squares by the normal equations (Gauss-Jordan); X: list of rows. -> coefficient list or None when singular"""
    n, p = len(X), len(X[0])
    A = [[sum(X[k][i] * X[k][j] for k in range(n)) for j in range(p)] + [sum(X[k][i] * y[k] for k in range(n))] for i in range(p)]
    for c in range(p):
        piv = max(range(c, p), key=lambda r: abs(A[r][c]))
        if abs(A[piv][c]) < 1e-9:
            return None
        A[c], A[piv] = A[piv], A[c]
        A[c] = [x / A[c][c] for x in A[c]]
        for r in range(p):
            if r != c:
                f = A[r][c]
                A[r] = [x - f * z for x, z in zip(A[r], A[c])]
    return [A[i][p] for i in range(p)]


def window_means(values, block=BLOCK):
    """-> (mean, se, n). se: sd of the block means / sqrt(blocks) (>= 3 blocks), else sd / sqrt(n / 3) (autocorrelation time 3)"""
    n = len(values)
    if n == 0:
        return float("nan"), float("nan"), 0
    mean = statistics.fmean(values)
    if n < 2:
        return mean, float("inf"), n
    nb = n // block
    if nb >= 3:
        bm = [statistics.fmean(values[k * block:(k + 1) * block]) for k in range(nb)]
        return mean, statistics.stdev(bm) / math.sqrt(nb), n
    return mean, statistics.stdev(values) / math.sqrt(max(1.0, n / 3.0)), n


def adjusted_series(recs, key, ctx, base_names):
    """covariate adjustment fitted on the baseline windows only: value - b . (cov - mean cov of the baseline). -> {win: [(t, adj)]}.
    Without enough baseline samples (< 8) or a singular fit the raw values are used."""
    rows = [(r["win"], r["t"], r["v"][key], r["cov"].get(ctx)) for r in recs if key in r["v"]]
    base = [x for x in rows if x[0] in base_names and x[3] is not None]
    coef, c0 = None, (0.0, 0.0)
    if len(base) >= 8:
        c0 = (statistics.fmean(x[3][0] for x in base), statistics.fmean(x[3][1] for x in base))
        X = [[1.0, x[3][0] - c0[0], x[3][1] - c0[1]] for x in base]
        coef = ols(X, [x[2] for x in base])
    out = {}
    for w, t, val, cv in rows:
        adj = val
        if coef is not None and cv is not None:
            adj = val - coef[1] * (cv[0] - c0[0]) - coef[2] * (cv[1] - c0[1])
        out.setdefault(w, []).append((t, adj))
    return out


def judge(effect, se, dmin, sign, n_e, n_b):
    """-> (verdict, why, mde)"""
    mde = Z_MDE * se if math.isfinite(se) else float("inf")
    if n_e < N_MIN or n_b < N_MIN or not math.isfinite(se) or se <= 0:
        return "INCONCLUSIVE", "too few valid samples (extreme %d, baseline %d; need %d each)" % (n_e, n_b, N_MIN), mde
    z = abs(effect) / se
    right = sign == 0 or (effect > 0) == (sign > 0)
    if abs(effect) >= dmin and z >= Z_PASS:
        if right:
            return "PASS", "shift %+.2f (>= dmin %.2f), z %.1f" % (effect, dmin, z), mde
        return "FAIL", "UNEXPECTED SIGN: shift %+.2f, expected %s, z %.1f" % (effect, "+" if sign > 0 else "-", z), mde
    if abs(effect) < dmin and mde <= dmin:
        return "FAIL", "no visible effect: shift %+.2f < dmin %.2f and MDE %.2f <= dmin (the test could see dmin)" % (effect, dmin, mde), mde
    if abs(effect) < dmin:
        return "INCONCLUSIVE", "under-powered: shift %+.2f, MDE %.2f > dmin %.2f" % (effect, mde, dmin), mde
    return "INCONCLUSIVE", "shift %+.2f >= dmin but z %.1f < %.1f" % (effect, z, Z_PASS), mde


def sigma_floor(bmeans):
    """bmeans: [(t, mean)] of the baseline windows in time order -> inflated window-to-window sd (0 with fewer than two windows).
    sigma^2 = mean over adjacent pairs of diff^2 / 2 (includes any trend: conservative); inflation 2.0 / 1.5 / 1.25 for 1 / 2 / >= 3 df."""
    ms = [m for t, m in sorted(bmeans)]
    if len(ms) < 2:
        return 0.0
    d2 = [(b - a) ** 2 / 2.0 for a, b in zip(ms, ms[1:])]
    infl = {1: 2.0, 2: 1.5}.get(len(d2), 1.25)
    return math.sqrt(statistics.fmean(d2)) * infl


def effect_table(recs, windows):
    """recs: sample_records output ; windows: plan_schedule output. -> {'aa': {...}, 'ext': [row,...]} with one row per extreme window:
    {name, knob, value, sign, metric, ctx, effect, se, mde, dmin, n, n_base, verdict, why, base, mean, sigma}"""
    base_names = [w["name"] for w in windows if w["kind"] == "base"]
    res = {"aa": {}, "ext": []}
    cache = {}
    sigmas = {}
    for w in windows:
        if w["kind"] != "ext":
            continue
        pr = probe_of(w["knob"])
        key = "%s:%s" % (pr["metric"], pr["ctx"])
        if key not in cache:
            cache[key] = adjusted_series(recs, key, pr["ctx"], set(base_names))
        ser = cache[key]
        bstat = {}
        for b in base_names:
            vals = [a for t, a in ser.get(b, [])]
            ts = [t for t, a in ser.get(b, [])]
            if len(vals) >= 3:
                m, se, n = window_means(vals)
                bstat[b] = [statistics.fmean(ts), m, se, n]
        if key not in sigmas:
            sigmas[key] = sigma_floor([(v[0], v[1]) for v in bstat.values()])
        sig = sigmas[key]
        for v in bstat.values():
            v[2] = max(v[2], sig)
        ev = ser.get(w["name"], [])
        if not ev or not bstat:
            res["ext"].append(dict(name=w["name"], knob=w["knob"], value=w["value"], sign=w["sign"], metric=pr["metric"], ctx=pr["ctx"],
                                   effect=float("nan"), se=float("inf"), mde=float("inf"), dmin=pr["dmin"], n=len(ev), n_base=0,
                                   verdict="INCONCLUSIVE", why="no samples in the window or no baseline", base=float("nan"),
                                   mean=float("nan"), sigma=sig))
            continue
        em, ese, en = window_means([a for t, a in ev])
        ese = max(ese, sig)
        te = statistics.fmean([t for t, a in ev])
        names = sorted(bstat, key=lambda b: bstat[b][0])
        before = [b for b in names if bstat[b][0] <= te]
        after = [b for b in names if bstat[b][0] > te]
        if before and after:
            b1, b2 = bstat[before[-1]], bstat[after[0]]
            f = (te - b1[0]) / (b2[0] - b1[0]) if b2[0] != b1[0] else 0.5
            bm = (1 - f) * b1[1] + f * b2[1]
            bse = math.hypot((1 - f) * b1[2], f * b2[2])
            bn = b1[3] + b2[3]
        else:
            b1 = bstat[(before or after)[-1 if before else 0]]
            bm, bse, bn = b1[1], b1[2], b1[3]
        eff = em - bm
        se = math.hypot(ese, bse)
        v, why, mde = judge(eff, se, pr["dmin"], w["sign"], en, bn)
        res["ext"].append(dict(name=w["name"], knob=w["knob"], value=w["value"], sign=w["sign"], metric=pr["metric"], ctx=pr["ctx"],
                               effect=eff, se=se, mde=mde, dmin=pr["dmin"], n=en, n_base=bn, verdict=v, why=why, base=bm, mean=em,
                               sigma=sig))
    # A/A display: the adjacent baseline windows of the metric with the largest implied window sd
    worst = None
    for key, ser in cache.items():
        pts = []
        for b in base_names:
            vals = [a for t, a in ser.get(b, [])]
            if len(vals) >= 3:
                pts.append((statistics.fmean([t for t, a in ser[b]]), window_means(vals)[0]))
        pts.sort()
        if len(pts) >= 2:
            diffs = [b[1] - a[1] for a, b in zip(pts, pts[1:])]
            dmax = max(diffs, key=abs)
            sg = sigmas.get(key, 0.0)
            if worst is None or sg > worst["sigma"]:
                worst = {"metric": key, "diff": dmax, "sigma": sg, "se": sg * math.sqrt(2.0), "z": abs(dmax) / (sg * math.sqrt(2.0)) if sg > 0 else 0.0}
    res["aa"] = worst or {}
    return res


def aa_ok(aa):
    """the A/A control no longer gates a run: it inflates the standard errors through sigma_floor (a noisy run is INCONCLUSIVE by MDE)"""
    return True


def knob_verdicts(table, entries, deferred):
    """-> {knob: {'effect': verdict, 'why': str, 'rows': [...]}} . A knob PASSes when one extreme PASSes and none shows an unexpected
    sign; FAILs when every judged extreme FAILs; else INCONCLUSIVE. A drifting A/A control turns everything INCONCLUSIVE."""
    out = {}
    drift = False
    for e in entries:
        rows = [r for r in table["ext"] if r["knob"] == e["id"]]
        if not rows:
            why = dict(deferred).get(e["id"], "not scheduled")
            out[e["id"]] = {"effect": "SKIP", "why": why, "rows": []}
            continue
        vs = [r["verdict"] for r in rows]
        if drift:
            out[e["id"]] = {"effect": "INCONCLUSIVE", "why": "A/A control drifts (z %.1f > %.1f): the baseline is not stable enough" % (
                table["aa"]["z"], Z_PASS), "rows": rows}
        elif any(r["verdict"] == "FAIL" and r["why"].startswith("UNEXPECTED SIGN") for r in rows):
            out[e["id"]] = {"effect": "FAIL", "why": "unexpected sign at an extreme", "rows": rows}
        elif "PASS" in vs:
            out[e["id"]] = {"effect": "PASS", "why": "; ".join(r["why"] for r in rows if r["verdict"] == "PASS"), "rows": rows}
        elif all(v == "FAIL" for v in vs):
            out[e["id"]] = {"effect": "FAIL", "why": "; ".join(r["why"] for r in rows), "rows": rows}
        else:
            out[e["id"]] = {"effect": "INCONCLUSIVE", "why": "; ".join(r["why"] for r in rows), "rows": rows}
    return out


def slider_decision(engine, effect):
    """engine: 'PASS' | 'FAIL' | 'NA' (no deterministic observable) | 'INCONCLUSIVE' | 'NOT_EXERCISED' ; effect: knob_verdicts value.
    -> (decision, explanation). Rule (AGENT_RULES_25): a slider needs (1) AND (2); where no observable exists (2) alone decides,
    flagged so the coordinator can overrule."""
    if effect == "SKIP":
        return "DEFER", "the visible-effect check does not apply to this knob in a one-launch battery"
    if engine == "FAIL":
        return "NO", "the engine-read check proves the value is ignored"
    if effect == "FAIL":
        return "NO", "no visible effect at the extreme value (or unexpected sign)"
    if effect == "INCONCLUSIVE" or engine in ("INCONCLUSIVE", "NOT_EXERCISED"):
        return "DEFER", "a check was inconclusive: repeat or strengthen"
    if effect == "PASS" and engine == "PASS":
        return "YES", "engine-read PASS and visible effect PASS"
    if effect == "PASS" and engine == "NA":
        return "YES (effect only)", "no deterministic observable exists; the visible effect alone passed - the coordinator decides"
    return "DEFER", "unclassified"


# ----------------------------------------------------------------------------------------------------------------- report helpers
def fmt_row(r):
    return "%-3s %-34s %7.3g  %-12s %+8.2f  se %5.2f  MDE %6.2f  dmin %5.2f  n %3d/%3d  %-12s %s" % (
        r["name"], r["knob"], r["value"], "%s:%s" % (r["metric"], r["ctx"]), r["effect"], r["se"], r["mde"], r["dmin"], r["n"],
        r["n_base"], r["verdict"], r["why"])


# ----------------------------------------------------------------------------------------------------------------- self-test
def _synth_samples(plan, seed, effect_by_win, L=105.0, W=68.0, ar=0.6, slow_sd=0.0):
    """a synthetic stream: plan = [(win, seconds)] at ~1 Hz; effect_by_win[win] = (metric key -> shift added to the metric's TRUE
    value through the geometry: lateral spread scales for the with-ball team). Returns samples for sample_records()."""
    import random
    rng = random.Random(seed)
    samples, t, i = [], 0.0, 0
    lat_noise = {(tm, k): 0.0 for tm in (0, 1) for k in range(11)}
    ballx, poss = 0.0, 0
    slow = {}
    base_lat = [0, -28, -10, 10, 28, -22, -6, 6, 22, -12, 12]
    base_dep = [3, 14, 16, 16, 14, 34, 36, 36, 34, 55, 55]
    for win, secs in plan:
        for _ in range(int(secs)):
            t += 1.0
            if rng.random() < 1 / 7:
                poss = 1 - poss
            ballx = max(-45, min(45, ballx + rng.gauss(0, 6)))
            if win not in slow:
                slow[win] = 1.0 + rng.gauss(0, slow_sd) if slow_sd else 1.0   # slow game-state drift: one level per window
            f_with = effect_by_win.get(win, {}).get("lat_with", 1.0) * slow[win]
            pl = []
            for tm in (0, 1):
                s = 1.0 if tm == 0 else -1.0
                for k in range(11):
                    lat_noise[(tm, k)] = ar * lat_noise[(tm, k)] + rng.gauss(0, 3.0 * math.sqrt(1 - ar * ar))
                    spread = (1.1 * f_with) if tm == poss else 0.9
                    lat = base_lat[k] * spread + lat_noise[(tm, k)]
                    dep = base_dep[k] + 0.2 * (s * ballx + 52.5) - 10 + rng.gauss(0, 1.5)
                    pl.append({"handle": tm * 25 + k, "team": tm, "fpos": 0 if k == 0 else k + 3, "p": (s * (52.5 - dep), 0.0, lat)})
            samples.append({"i": i, "t": t, "win": win, "players": pl, "ball": (ballx, 0.1, rng.gauss(0, 8)), "carrier": poss * 25 + 7})
            i += 1
    return samples


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        sys.stdout.flush()
        ok = ok and cond

    # --- knob list
    txt = "# comment\nshape.width_in\nshape.width_out 0.4 both\nshape.length_in 9\nnope.knob\nshape.width_in\nmarking.tightness 2.0,0.0\n"
    ent, probs = parse_knob_list(txt)
    ids = [e["id"] for e in ent]
    check(ids == ["shape.width_in", "shape.width_out", "marking.tightness"], "knob list: three valid entries kept in order %s" % ids)
    check(any("outside the engine-safe" in p for p in probs) and any("unknown knob" in p for p in probs) and any("twice" in p for p in probs),
          "knob list: out-of-range value, unknown knob and duplicate are reported")
    check(ent[0]["ext"] == [(0.3, -1)] and ent[1]["ext"] == [(0.4, -1)] and ent[2]["ext"] == [(2.0, -1), (0.0, +1)],
          "knob list: default primary extreme with its sign; explicit values keep the nearest default's sign: %s" % [e["ext"] for e in ent])
    ent2, _ = parse_knob_list("shape.width_in both\n")
    check(ent2[0]["ext"] == [(0.3, -1), (1.7, +1)], "knob list: 'both' schedules every default extreme")
    check(all(k in PROBES for k in ("shape.width_in", "lineband.deep_max", "runs.frequency")) and len(PROBES) == 23,
          "the probe table covers the 23 catalogue knobs (%d)" % len(PROBES))
    # --- schedule
    ent3, _ = parse_knob_list("\n".join(["shape.width_in", "shape.width_out", "shape.width_in_cb", "shape.length_in", "marking.tightness",
                                         "runs.frequency", "pressing.trigger_depth", "lineband.deep_max"]))
    wins, dfr = plan_schedule(ent3, win=35, guard=5, base_every=3, budget=440)
    names = [w["name"] for w in wins]
    check(names[0] == "A1" and wins[-1]["kind"] == "base", "schedule: starts and ends with a baseline %s" % names)
    check(schedule_seconds(wins) <= 440 and all(w["dur"] <= 40 for w in wins), "schedule: fits the budget (%.0f s) and no window > 40 s" % schedule_seconds(wins))
    check(any(k == "lineband.deep_max" for k, _ in dfr), "schedule: the set-up-only knob is deferred with a reason")
    w2, d2 = plan_schedule(ent3, win=50, budget=300)
    check(all(w["dur"] == 40 for w in w2) and schedule_seconds(w2) <= 300 and len(d2) > 1, "schedule: win capped at 40 s, a small budget defers extremes (%d deferred)" % len(d2))
    kinds = [w["kind"] for w in wins]
    check(kinds.count("ext") <= 7 and sum(1 for w in wins if w["kind"] == "base") >= 3, "schedule: a baseline at least every 3 extremes")
    # --- metrics
    smp = _synth_samples([("A1", 5)], 1, {})
    rec = sample_records(smp, 105.0, 68.0)
    check(len(rec) >= 3 and "lat_sd:with" in rec[0]["v"] and "cb_split:with" in rec[0]["v"] and "nn_opp:without" in rec[0]["v"],
          "metrics: with / without-ball metrics computed per sample (%d records)" % len(rec))
    # --- statistics: a real effect, no effect, a tiny effect
    plan = [("A1", 40), ("E1", 40), ("A2", 40)]
    s_eff = _synth_samples(plan, 11, {"E1": {"lat_with": 0.3 / 1.0 * 1.0}})  # lateral spread of the with-ball team x0.3
    rec = sample_records(s_eff, 105.0, 68.0)
    wl = [{"name": "A1", "kind": "base", "knob": None, "value": None, "sign": 0, "dur": 40, "guard": 0},
          {"name": "E1", "kind": "ext", "knob": "shape.width_in", "value": 0.3, "sign": -1, "dur": 40, "guard": 5},
          {"name": "A2", "kind": "base", "knob": None, "value": None, "sign": 0, "dur": 40, "guard": 5}]
    tb = effect_table(rec, wl)
    r = tb["ext"][0]
    check(r["verdict"] == "PASS" and r["effect"] < -2.5, "stats: a x0.3 lateral spread is PASS (effect %.2f, se %.2f, MDE %.2f)" % (r["effect"], r["se"], r["mde"]))
    check(tb["aa"] and tb["aa"]["sigma"] >= 0, "stats: the A/A line is reported (window sd %.2f m, adjacent diff %+.2f)" % (tb["aa"]["sigma"], tb["aa"]["diff"]))
    s_null = _synth_samples(plan, 12, {})
    tb0 = effect_table(sample_records(s_null, 105.0, 68.0), wl)
    r0 = tb0["ext"][0]
    check(r0["verdict"] in ("FAIL", "INCONCLUSIVE") and r0["verdict"] != "PASS", "stats: no real effect is never PASS (%s: %s)" % (r0["verdict"], r0["why"]))
    s_tiny = _synth_samples(plan, 13, {"E1": {"lat_with": 0.97}})
    rt = effect_table(sample_records(s_tiny, 105.0, 68.0), wl)["ext"][0]
    check(rt["verdict"] in ("FAIL", "INCONCLUSIVE"), "stats: a 3 percent change stays below dmin (%s)" % rt["verdict"])
    s_wrong = _synth_samples(plan, 14, {"E1": {"lat_with": 1.8}})
    rw = effect_table(sample_records(s_wrong, 105.0, 68.0), wl)["ext"][0]
    check(rw["verdict"] == "FAIL" and "UNEXPECTED SIGN" in rw["why"], "stats: a significant shift in the wrong direction is FAIL (%s)" % rw["why"])
    # false-positive rate under the null: 40 seeds, PASS must be (almost) absent
    npass = 0
    for sd in range(40):
        t0 = effect_table(sample_records(_synth_samples(plan, 100 + sd, {}), 105.0, 68.0), wl)["ext"][0]
        npass += t0["verdict"] == "PASS"
    check(npass <= 1, "stats: false PASS under the null in %d of 40 seeded runs (<= 1 allowed)" % npass)
    # power: a x0.5 effect (about -8 m spread) is detected in most runs
    hits = sum(effect_table(sample_records(_synth_samples(plan, 200 + sd, {"E1": {"lat_with": 0.5}}), 105.0, 68.0), wl)["ext"][0]["verdict"] == "PASS"
               for sd in range(20))
    check(hits >= 17, "stats: a x0.5 lateral spread is PASS in %d of 20 seeded runs (>= 17 expected)" % hits)
    # --- window-to-window game-state variation (slow, invisible to the within-window SE): the baseline floor keeps the false PASS rate low
    npass2 = sum(effect_table(sample_records(_synth_samples(plan, 300 + sd, {}, slow_sd=0.2), 105.0, 68.0), wl)["ext"][0]["verdict"] == "PASS"
                 for sd in range(40))
    check(npass2 <= 2, "stats: with 20 percent slow window-to-window variation the false PASS rate stays low (%d of 40, <= 2 allowed)" % npass2)
    plan4 = [("A1", 40), ("E1", 40), ("A2", 40), ("E2", 40), ("A3", 40)]
    wl4 = wl[:2] + [wl[2], dict(wl[1], name="E2"), dict(wl[2], name="A3")]
    wl4[2] = dict(wl[2], name="A2")
    npass3 = sum(effect_table(sample_records(_synth_samples(plan4, 400 + sd, {"E1": {"lat_with": 0.3}}, slow_sd=0.1), 105.0, 68.0), wl4)["ext"][0]["verdict"] == "PASS"
                 for sd in range(20))
    check(npass3 >= 15, "stats: a x0.3 lateral spread still passes with 10 percent slow variation and three baselines (%d of 20)" % npass3)
    # --- trend control: a baseline that drifts linearly does not create an effect at the midpoint window
    drift = _synth_samples(plan, 15, {})
    for smp_ in drift:
        for q in smp_["players"]:
            q["p"] = (q["p"][0], q["p"][1], q["p"][2] * (1.0 + 0.0015 * smp_["t"]))  # lateral spread grows 18 % over the run
    rd = effect_table(sample_records(drift, 105.0, 68.0), wl)["ext"][0]
    check(abs(rd["effect"]) < 1.6, "stats: linear drift is removed by the interpolated baseline (effect %.2f m)" % rd["effect"])
    # --- slider decision table
    dd = slider_decision
    check(dd("PASS", "PASS")[0] == "YES" and dd("NA", "PASS")[0].startswith("YES") and dd("FAIL", "PASS")[0] == "NO"
          and dd("PASS", "FAIL")[0] == "NO" and dd("NA", "INCONCLUSIVE")[0] == "DEFER" and dd("PASS", "SKIP")[0] == "DEFER",
          "slider decision table: YES needs the effect; engine FAIL / effect FAIL = NO; inconclusive = DEFER")
    # --- judge edges
    check(judge(5.0, 0.5, 2.5, -1, 20, 20)[0] == "FAIL" and judge(-5.0, 0.5, 2.5, -1, 20, 20)[0] == "PASS"
          and judge(0.2, 0.5, 2.5, -1, 20, 20)[0] == "FAIL" and judge(0.2, 2.0, 2.5, -1, 20, 20)[0] == "INCONCLUSIVE"
          and judge(3.0, 2.0, 2.5, 0, 20, 20)[0] == "INCONCLUSIVE" and judge(-5.0, 0.5, 2.5, -1, 5, 20)[0] == "INCONCLUSIVE"
          and judge(5.0, 0.5, 2.5, 0, 20, 20)[0] == "PASS", "judge: sign, dmin, MDE and sample-size edges")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    print(__doc__)
    sys.exit(2)
