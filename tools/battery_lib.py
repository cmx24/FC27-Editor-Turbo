"""battery_lib.py - pure part of the Turbo knob check battery (tools/, offline; no game access, no dev service). 2.6 revision ("battery2").

The battery decides whether a gameplay-table knob earns a slider (AGENT_RULES_25: (1) a deterministic engine-read check passes in game
AND (2) a visible effect at an extreme value is seen in game). This module holds everything that needs no game:
  - PROBES: per knob the engine-read observable (or why there is none), the PRIMARY effect metric aimed at the knob's consumer, the extreme value
    (LOW / strong only: the engine clips high values, formation width caps at 1.2 W) with the expected sign, the practical minimum shift dmin and
    the engine-safe test range;
  - NOISE_PRIOR: the pre-registered long-run noise (m * sqrt(s)) of every metric, measured on the TEST_27 evidence (tools/battery_noise.py);
  - parse_knob_list(), plan_schedule(): A E A E ... A windows with >= MIN_REPS replicates per extreme, deferring (with the seconds it would
    need) every extreme whose a-priori MDE is above its dmin;
  - position metrics per team from the 22 players (lateral spread, wide players' spread, back-line width, centre-back split, depth spread, compactness,
    pressing distance, nearest-defender distance, nearest-opponent distance) with / without the ball;
  - effect statistics: covariate-adjusted window means, the replicate-window estimator against the interpolated neighbouring baselines,
    SE = max(a-priori, replicate), pre-registered PASS / FAIL / INCONCLUSIVE per knob extreme.
Run  python battery_lib.py --self-test  for the offline proof.

Pre-registered criterion (also printed by the driver). For one extreme (knob, value) with m >= MIN_REPS replicate windows E_j of T seconds and the
metric m (context with / without the ball), alternating with baseline windows A (A E A E ... A):
  effect = mean_j [ mean_adj(E_j) - base(t_j) ]   base(t) = linear interpolation of the neighbouring A windows' adjusted means (drift cancels to 1st order)
  SE     = max( sigma * sqrt(sum_i w_i^2 / T_i) ,  sd_j(E_j - base_j) / sqrt(m) )     sigma = NOISE_PRIOR (long-run sd, m * sqrt(s)); w_i the
                                                 design weights of the estimator (design_weights); the first term is the a-priori SE
  MDE    = 2.8 * SE                              minimum effect detected with 95 % confidence and 80 % power (1.96 + 0.84)
  PASS         |effect| >= dmin AND |effect| / SE >= Z_PASS AND sign as expected AND >= 75 % of the replicate windows moved the expected way
  FAIL         (|effect| < dmin AND MDE <= dmin: the extreme moved the metric by less than dmin although the test could see dmin)
               or (significant effect in the WRONG direction: |effect| >= dmin, z >= Z_PASS)
  INCONCLUSIVE otherwise (< MIN_REPS replicates, too few samples, MDE > dmin = under-powered, effect between the two bars)
The long-run noise of the game state (possession, ball position, tempo: correlation time about 10 s) - not the sample count - limits the power:
SE shrinks with the sampled SECONDS, so more samples per second help little. plan prints the a-priori MDE per knob and defers what cannot reach dmin.
"""
import csv, math, os, re, statistics, sys, time

Z_PASS = 3.0
Z_MDE = 2.8
PAIRED_WAIT = 18.0   # s allowed to wait for a frozen-input moment before a paired-observable write (battery_write.py)
N_MIN = 12           # valid samples a group (and its baselines) need to be judged
MIN_REPS = 3         # replicate extreme windows per extreme value (the spread between replicates needs >= 3)
AGREE_SHARE = 0.75   # share of the replicate windows that must move the expected way for a PASS
GUARD_S = 8.0        # s after every write that are not judged: the formation needs 5 - 10 s to settle (TEST_27 trajectories, battery_noise.py settle)
N_WIDE = 4           # wide players per team (derive_roles)
SLACK = 1.10         # an extreme is scheduled when its a-priori MDE is within 10 % of its dmin (flagged borderline when above dmin)
CONTEXTS = ("with", "without")
VERDICTS = ("PASS", "FAIL", "INCONCLUSIVE")
# Long-run noise sigma (m * sqrt(s): the sd of a window mean is sigma / sqrt(T seconds)) of the covariate-adjusted metrics, measured on the TEST_27
# evidence (turbo_dev/evidence/2026-10-14/battery_121437 and battery_122701) by tools/battery_noise.py: the larger of the two launches,
# x 1.3, rounded up to 0.5. [A] - two launches of one match type; plan prints "no measurement: default" for a metric that is not in the table.
NOISE_PRIOR = {
    "ahead_share:with": 1.5, "ahead_share:without": 1.5, "back_sd:with": 11.5,
    "back_sd:without": 12.0, "backline_m:with": 40.0, "backline_m:without": 28.5,
    "cb_split:with": 12.5, "cb_split:without": 18.0, "centroid_m:with": 30.0,
    "centroid_m:without": 30.5, "compact:with": 13.0, "compact:without": 7.5,
    "gap_back_mid:with": 26.5, "gap_back_mid:without": 12.0, "lat_sd:with": 7.5,
    "lat_sd:without": 4.0, "len_range:with": 39.5, "len_range:without": 29.0,
    "len_sd:with": 14.0, "len_sd:without": 10.0, "ndef:without": 39.0,
    "nn_opp:with": 11.5, "nn_opp:without": 12.0, "press1:without": 23.5,
    "press_m:without": 25.0, "wide_abs:with": 10.5, "wide_abs:without": 8.0,
}
NOISE_DEFAULT = 16.0

# ----------------------------------------------------------------------------------------------------------------- probes
# ext = [(value, expected sign of the metric shift at that value)]; the FIRST entry is the primary extreme (always scheduled), the others
# only with --both. obs: engine-read observable kind (battery_engine.py) or None; why_no_obs says why none exists.
# dmin: smallest shift worth a slider, in the metric's unit. tmin / tmax: the engine-safe range the battery may write (wider than
# Turbo's slider range on purpose: an extreme must beat game-state noise; narrower than anything that could crash the game).
# Consumers: [V OLD] = read in the disassembly of build 6AB9813C (turbo_output\assessment_20261013\T1 / X1P\re, and 0x14164F338 and
# 0x141B314B4 disassembled from the same image for this track); live = 6AC07E31, same offsets unverified until the battery runs [A].
PROBES = {
    "shape.width_in": dict(
        obs="band_att", metric="lat_sd", ctx="with", unit="m", dmin=2.5, tmin=0.2, tmax=2.5, ext=[(0.3, -1)],
        consumer="0x140F56030 cap + 0x140F52B4C width [V OLD]: formation width state+0x30 / +0x34 every call",
        note="LOW extreme only: the engine caps the formation width at 1.2 W, so 1.8 is mostly clipped; engine band exact for low values. "
             "TEST_27 saw -2.6 / -3.2 / -2.9 m at 0.3 in three windows (not a pre-registered result). Primary metric lat_sd:with (13.6 m baseline, "
             "dmin 2.5 m = 18 %); the same data give wide_abs:with -4 to -5 m but lat_sd was pre-registered in TEST_27 and is kept"),
    "shape.width_out": dict(
        obs="band_def", metric="wide_abs", ctx="without", unit="m", dmin=2.5, tmin=0.2, tmax=2.5, ext=[(0.3, -1)],
        consumer="0x140F56030 term D = h * [def + 0x28] [V OLD]: formation width state+0x30 at t = 0 (without the ball)",
        note="the wide players' spread of the team WITHOUT the ball (15.2 m baseline, dmin 2.5 m = 16 %); whole-team lat_sd:without was blind to it in "
             "TEST_27 (+0.15 m, the wide players are 4 of 10)"),
    "shape.width_in_cb": dict(
        obs=None, metric="cb_split", ctx="with", unit="m", dmin=2.0, tmin=0.2, tmax=2.5, ext=[(0.3, -1)],
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


# ----------------------------------------------------------------------------------------------------------------- design / power
def design_weights(windows, group):
    """the estimator of one (knob, value) group is  effect = mean over the group's extreme windows j of  [ m(E_j) - base(t_j) ]  with
    base(t) = linear interpolation (by window mid-time) between the neighbouring baseline windows. This is a linear combination of window
    means, sum_i w_i m_i. -> ({window name: w}, m = number of extreme windows in the group). Windows need 'name', 'kind', 'dur' and the
    schedule order; times are the cumulative start + guard + dur / 2."""
    t, mids = 0.0, {}
    for w in windows:
        t += w.get("guard", 0.0)
        mids[w["name"]] = t + w["dur"] / 2.0
        t += w["dur"]
    base = [w for w in windows if w["kind"] == "base"]
    ext = [w for w in windows if w["kind"] == "ext" and w["knob"] == group[0] and abs(w["value"] - group[1]) < 1e-9]
    wts = {}
    for e in ext:
        te = mids[e["name"]]
        before = [b for b in base if mids[b["name"]] <= te]
        after = [b for b in base if mids[b["name"]] > te]
        wts[e["name"]] = wts.get(e["name"], 0.0) + 1.0 / len(ext)
        if before and after:
            b1, b2 = before[-1], after[0]
            f = (te - mids[b1["name"]]) / (mids[b2["name"]] - mids[b1["name"]])
            wts[b1["name"]] = wts.get(b1["name"], 0.0) - (1 - f) / len(ext)
            wts[b2["name"]] = wts.get(b2["name"], 0.0) - f / len(ext)
        else:
            b1 = (before or after)[-1 if before else 0]
            wts[b1["name"]] = wts.get(b1["name"], 0.0) - 1.0 / len(ext)
    return wts, len(ext)


def prior_se(windows, group, sigma):
    """a-priori standard error of the group's effect: Var = sum_i w_i^2 sigma^2 / T_i  (sigma = long-run sd of the metric in m * sqrt(s),
    T_i the window's sampled seconds); the pre-registered floor of the SE used by the verdict"""
    wts, m = design_weights(windows, group)
    dur = {w["name"]: w["dur"] for w in windows}
    return sigma * math.sqrt(sum(w * w / dur[n] for n, w in wts.items())) if m else float("inf")


def sigma_for(metric_key):
    """-> (sigma, measured?) the pre-registered long-run noise of a metric key 'metric:ctx' (NOISE_PRIOR, from TEST_27)"""
    s = NOISE_PRIOR.get(metric_key)
    return (s, True) if s is not None else (NOISE_DEFAULT, False)


def group_key(entry_or_window):
    return (entry_or_window["knob"] if "knob" in entry_or_window else entry_or_window["id"], None)


END_WIN = 20.0       # s: the two end baselines each serve one extreme with half weight, so they are shorter (their share of the variance is negligible)


def build_windows(groups, m, win, guard, extra=None):
    """A E A E ... A : for repetition r the groups in order (reversed on odd repetitions: no knob is always first), an A window between
    every two extremes and at both ends (the end ones END_WIN s). groups: [(knob, value, sign)]. extra: {knob: additional guard seconds}."""
    extra = extra or {}
    w = [{"name": "A1", "kind": "base", "knob": None, "value": None, "sign": 0, "dur": min(win, END_WIN), "guard": 0.0}]
    nb, ne = 1, 0
    for r in range(m):
        order = groups if r % 2 == 0 else list(reversed(groups))
        for kid, v, sg in order:
            ne += 1
            w.append({"name": "E%d" % ne, "kind": "ext", "knob": kid, "value": v, "sign": sg, "dur": win,
                      "guard": guard + extra.get(kid, 0.0)})
            nb += 1
            # the revert of a paired-observable knob also waits for a frozen moment, inside this baseline's guard
            w.append({"name": "A%d" % nb, "kind": "base", "knob": None, "value": None, "sign": 0, "dur": win,
                      "guard": guard + extra.get(kid, 0.0)})
    w[-1]["dur"] = min(win, END_WIN)
    return w


def plan_schedule(entries, win=30.0, guard=GUARD_S, budget=420.0, min_reps=MIN_REPS, max_reps=10, allow_underpowered=False):
    """win 'auto' / None: the window length (40, 30, 24, 20 s) that schedules the most decidable extremes, then the lowest MDE / dmin"""
    if win in (None, "auto"):
        best = None
        for wl in (40.0, 30.0, 24.0, 20.0):
            ws, dfr, nts = _plan_one(entries, wl, guard, budget, min_reps, max_reps, allow_underpowered)
            groups = {(w["knob"], w["value"]) for w in ws if w["kind"] == "ext"}
            score = (len(groups), -max([nts[g]["mde"] / nts[g]["dmin"] for g in groups] or [9e9]))
            if best is None or score > best[0]:
                best = (score, ws, dfr, nts)
        return best[1], best[2], best[3]
    return _plan_one(entries, win, guard, budget, min_reps, max_reps, allow_underpowered)


def _plan_one(entries, win, guard, budget, min_reps, max_reps, allow_underpowered):
    """-> (windows, deferred, notes). The windows alternate baseline A and extreme E (A E A E ... A; see build_windows). Every extreme group
    (knob, value) gets the same number m of replicate windows (>= min_reps: with fewer the replicate spread cannot be judged), the largest m that
    fits the budget (up to max_reps). Steps: (1) a group whose a-priori MDE is above its dmin even with the WHOLE budget for itself is deferred as
    under-powered, with the seconds it would need - the pre-registered answer to "impossible" - unless allow_underpowered; (2) of the rest, in
    knob-list order, the largest prefix that fits min_reps replicates is scheduled; the groups that do not fit together are deferred to another
    launch; (3) a group that is decidable alone but not at the shared m is dropped from the end until all are decidable. win is capped at 40 s.
    notes: {(knob, value): dict(m, mde, dmin, sigma, feasible, need_reps, need_s, metric)}"""
    win = min(float(win), 40.0)
    exts, deferred = [], []
    for e in entries:
        pr = probe_of(e["id"])
        if pr.get("skip_effect") or pr["metric"] is None:
            deferred.append((e["id"], pr.get("skip_effect") or "no position metric"))
            continue
        for v, sg in e["ext"]:
            exts.append((e["id"], v, sg))

    def extra_for(gs):
        ex = {}
        for kid, _, _ in gs:
            if (probe_of(kid) or {}).get("obs") == "paired_len":
                ex[kid] = PAIRED_WAIT
        return ex

    def seconds(gs, m):
        return schedule_seconds(build_windows(gs, m, win, guard, extra_for(gs)))

    def max_m(gs):
        if seconds(gs, min_reps) > budget:
            return 0
        m = min_reps
        while m < max_reps and seconds(gs, m + 1) <= budget:
            m += 1
        return m

    def mde_at(g, gs, m):
        pr = probe_of(g[0])
        sg, _ = sigma_for("%s:%s" % (pr["metric"], pr["ctx"]))
        return Z_MDE * prior_se(build_windows(gs, m, win, guard, extra_for(gs)), (g[0], g[1]), sg)

    notes = {}

    def note(g, gs, m):
        pr = probe_of(g[0])
        key = "%s:%s" % (pr["metric"], pr["ctx"])
        sgm, meas = sigma_for(key)
        mde = mde_at(g, gs, m)
        need_m = next((mm_ for mm_ in range(min_reps, 400) if mde_at(g, [g], mm_) <= pr["dmin"]), None)
        notes[(g[0], g[1])] = {"m": m, "mde": mde, "dmin": pr["dmin"], "sigma": sgm, "sigma_measured": meas, "feasible": mde <= pr["dmin"] * SLACK, "borderline": pr["dmin"] < mde <= pr["dmin"] * SLACK,
                               "need_reps": need_m, "need_s": seconds([g], need_m) if need_m else None, "metric": key}
        return notes[(g[0], g[1])]

    # (1) decidable alone?
    alone = []
    for g in exts:
        m1 = max_m([g])
        if m1 == 0:
            deferred.append((g[0], "no room in the %.0f s budget (%d replicate windows of one group need %.0f s)" % (budget, min_reps, seconds([g], min_reps))))
            continue
        n1 = note(g, [g], m1)
        if n1["feasible"] or allow_underpowered:
            alone.append(g)
        else:
            deferred.append((g[0], "under-powered: a-priori MDE %.2f > dmin %.2f even with the whole budget (%d replicates of %.0f s, sigma %.1f m*sqrt(s)); %s" % (
                n1["mde"], n1["dmin"], m1, win, n1["sigma"],
                ("it needs %d replicates = %.0f s of windows" % (n1["need_reps"], n1["need_s"])) if n1["need_reps"] else "not reachable at any length")))
    notes = {k: v for k, v in notes.items() if (k[0], k[1]) in {(g[0], g[1]) for g in alone}}
    # (2) the largest prefix that fits together, (3) dropped from the end until every group is decidable at the shared m
    chosen = list(alone)
    while chosen and max_m(chosen) == 0:
        g = chosen.pop()
        deferred.append((g[0], "extreme %g does not fit the %.0f s budget together with the knobs listed before it (%d replicates each): list it first for another launch" % (
            g[1], budget, min_reps)))
    m = max_m(chosen) if chosen else 0
    while chosen and not allow_underpowered and any(mde_at(g, chosen, m) > probe_of(g[0])["dmin"] * SLACK for g in chosen) and len(chosen) > 1:
        g = chosen.pop()
        deferred.append((g[0], "extreme %g is decidable alone but not together with the others (%d replicates each would leave its MDE above dmin): "
                         "another launch" % (g[1], m)))
        m = max_m(chosen)
    if not chosen:
        return [], deferred, {}
    for g in chosen:
        note(g, chosen, m)
    notes = {k: v for k, v in notes.items() if (k[0], k[1]) in {(g[0], g[1]) for g in chosen}}
    return build_windows(chosen, m, win, guard, extra_for(chosen)), deferred, notes


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


def _finite(q):
    return q["p"] is not None and all(math.isfinite(c) for c in q["p"])


def team_metrics(rows, opp_rows, L, along, lat, ball, carrier_pos, wide=None):
    """rows: [{handle, fpos, p}] of one team (11 players), opp_rows the other team. -> dict of metrics, or None when fewer than 8 outfielders.
    depth = metres from the team's own goal line along the pitch. All distances in the unit of the positions (metres).
    wide: set of handles of the team's wide players (derive_roles) -> wide_abs. carrier_pos given (the team WITHOUT the ball): press_m (mean
    distance of the 3 outfielders nearest the carrier), press1 (the nearest), ndef (nearest of the four deepest = the back line)."""
    pts = [q for q in rows if _finite(q)]
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
    cl = statistics.fmean(lt)
    cx = statistics.fmean(q["p"][along] for q in out)
    m = {"lat_sd": statistics.pstdev(lt), "len_sd": statistics.pstdev(dep), "len_range": max(dep) - min(dep),
         "backline_m": statistics.fmean(dep[i] for i in back), "centroid_m": statistics.fmean(dep),
         "back_sd": statistics.pstdev([lt[i] for i in back]),
         "compact": statistics.fmean(math.hypot(q["p"][along] - cx, q["p"][lat] - cl) for q in out),
         "own_goal": s}
    m["gap_back_mid"] = (statistics.fmean(dep[i] for i in mid) - m["backline_m"]) if len(mid) == 3 else float("nan")
    cb = sorted(back, key=lambda i: abs(lt[i] - cl))[:2]
    m["cb_split"] = abs(lt[cb[0]] - lt[cb[1]])
    if wide:
        w = [lt[i] for i, q in enumerate(out) if q.get("handle") in wide]
        if len(w) >= 2:
            m["wide_abs"] = statistics.fmean(abs(x - cl) for x in w)
    if ball is not None:
        att = -s  # direction of the opponent's goal along the pitch
        m["ahead_share"] = sum(1 for q in out if att * (q["p"][along] - ball[along]) > 0) / len(out)
    opp = [q for q in opp_rows if _finite(q) and q["fpos"] != 0]
    if opp:
        nn = [min(math.hypot(q["p"][along] - o["p"][along], q["p"][lat] - o["p"][lat]) for o in opp) for q in out]
        m["nn_opp"] = statistics.fmean(nn)
    if carrier_pos is not None:
        d = sorted(math.hypot(q["p"][along] - carrier_pos[along], q["p"][lat] - carrier_pos[lat]) for q in out)
        m["press_m"] = statistics.fmean(d[:3])
        m["press1"] = d[0]
        m["ndef"] = min(math.hypot(out[i]["p"][along] - carrier_pos[along], out[i]["p"][lat] - carrier_pos[lat]) for i in back)
    return m


def derive_roles(samples, base_names, n_wide=N_WIDE):
    """the wide players of each team by a fixed rule on the BASELINE windows only (the knob never acts there): per team the n_wide outfield
    handles with the largest mean |lateral offset from the team's lateral centroid|. -> {team: set(handles)} (a team with fewer than 8 baseline
    samples of a handle gets none: wide_abs is then not computed)"""
    acc = {}
    for smp in samples:
        if smp["win"] not in base_names:
            continue
        pl = smp["players"]
        along = axes_from_rows(pl)
        lat = 2 if along == 0 else 0
        for tm in (0, 1):
            rows = [q for q in pl if q["team"] == tm and _finite(q) and q["fpos"] != 0]
            if len(rows) < 8:
                continue
            cl = statistics.fmean(q["p"][lat] for q in rows)
            for q in rows:
                acc.setdefault((tm, q["handle"]), []).append(abs(q["p"][lat] - cl))
    roles = {}
    for tm in (0, 1):
        sc = [(statistics.fmean(v), h) for (t, h), v in acc.items() if t == tm and len(v) >= 8]
        sc.sort(reverse=True)
        if len(sc) >= n_wide:
            roles[tm] = {h for _, h in sc[:n_wide]}
    return roles


def sample_records(samples, L, W, roles=None):
    """samples: [{i, t, win, players:[{handle, team, fpos, p}], ball:(x,y,z)|None, carrier: handle|None}] ->
    [{i, t, win, v:{'<metric>:with'|'<metric>:without': value}, cov:{with:(ball depth, |ball lateral|, team), without:(...)}}]
    One value per context per sample: 'with' = the carrier's team, 'without' = the other team. Samples without a carrier give nothing."""
    out = []
    roles = roles or {}
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
            m = team_metrics(rows[tm], rows[1 - tm], L, along, lat, smp["ball"], cpos if ctx == "without" else None, roles.get(tm))
            if m is None:
                continue
            for k, val in m.items():
                if k != "own_goal" and isinstance(val, float) and math.isfinite(val):
                    v["%s:%s" % (k, ctx)] = val
            if smp["ball"] is not None:
                bd = (L / 2 - m["own_goal"] * smp["ball"][along]) / L
                cov[ctx] = (bd, abs(smp["ball"][lat]) / (W / 2), float(tm))
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


def adjusted_series(recs, key, ctx, base_names):
    """covariate adjustment fitted on the BASELINE windows only (the knob never acts there): value - b . (cov - mean cov of the baseline) with
    the covariates ball depth, |ball lateral position| and the team (the two sides play different formations and the possession share
    varies from window to window). Falls back to fewer covariates / the raw values when the fit is singular or the baseline has < 12 samples.
    -> {win: [(t, adj)]}"""
    rows = [(r["win"], r["t"], r["v"][key], r["cov"].get(ctx)) for r in recs if key in r["v"]]
    base = [x for x in rows if x[0] in base_names and x[3] is not None]
    coef, c0, use = None, None, 0
    if len(base) >= 12:
        for use in (3, 2):
            c0 = [statistics.fmean(x[3][k] for x in base) for k in range(use)]
            X = [[1.0] + [x[3][k] - c0[k] for k in range(use)] for x in base]
            coef = ols(X, [x[2] for x in base])
            if coef is not None:
                break
    out = {}
    for w, t, val, cv in rows:
        adj = val
        if coef is not None and cv is not None:
            adj = val - sum(coef[1 + k] * (cv[k] - c0[k]) for k in range(use))
        out.setdefault(w, []).append((t, adj))
    return out


def judge(effect, se, dmin, sign, n_e, n_b, m=None, agree=None):
    """-> (verdict, why, mde). m = replicate extreme windows, agree = how many of them moved the metric the expected way (None: not checked)."""
    mde = Z_MDE * se if math.isfinite(se) else float("inf")
    if n_e < N_MIN or n_b < N_MIN or not math.isfinite(se) or se <= 0:
        return "INCONCLUSIVE", "too few valid samples (extreme %d, baseline %d; need %d each)" % (n_e, n_b, N_MIN), mde
    if m is not None and m < MIN_REPS:
        return "INCONCLUSIVE", "only %d replicate extreme window(s); %d are needed to judge the spread between replicates" % (m, MIN_REPS), mde
    z = abs(effect) / se
    right = sign == 0 or (effect > 0) == (sign > 0)
    if abs(effect) >= dmin and z >= Z_PASS:
        if right:
            if agree is not None and m and agree < math.ceil(AGREE_SHARE * m):
                return "INCONCLUSIVE", "shift %+.2f (>= dmin, z %.1f) but only %d of %d replicate windows moved the expected way" % (effect, z, agree, m), mde
            return "PASS", "shift %+.2f (>= dmin %.2f), z %.1f%s" % (effect, dmin, z, "" if agree is None else ", %d of %d windows agree" % (agree, m)), mde
        return "FAIL", "UNEXPECTED SIGN: shift %+.2f, expected %s, z %.1f" % (effect, "+" if sign > 0 else "-", z), mde
    if abs(effect) < dmin and mde <= dmin:
        return "FAIL", "no visible effect: shift %+.2f < dmin %.2f and MDE %.2f <= dmin (the test could see dmin)" % (effect, dmin, mde), mde
    if abs(effect) < dmin:
        return "INCONCLUSIVE", "under-powered: shift %+.2f, MDE %.2f > dmin %.2f" % (effect, mde, dmin), mde
    return "INCONCLUSIVE", "shift %+.2f >= dmin but z %.1f < %.1f" % (effect, z, Z_PASS), mde


def _win_stats(ser, names):
    """-> {name: (mean t, mean adj, n, span s)} for the windows in names that have samples"""
    out = {}
    for b in names:
        pts = ser.get(b, [])
        if len(pts) >= 3:
            ts = [t for t, a in pts]
            out[b] = (statistics.fmean(ts), statistics.fmean(a for t, a in pts), len(pts), max(ts) - min(ts))
    return out


def effect_table(recs, windows):
    """recs: sample_records output ; windows: plan_schedule output (with 'dur'; 'guard'). -> {'aa': {...}, 'ext': [row per (knob, value)], 'win': [row
    per extreme window]}. Row = {name, knob, value, sign, metric, ctx, effect, se, se_prior, se_emp, mde, dmin, n, n_base, m, agree, verdict, why, base,
    mean, sigma, sigma_measured}.
    Effect of a group = mean over its windows of (window mean - baseline interpolated between the neighbouring A windows), the window means
    being covariate-adjusted. SE = max(a-priori SE from the pre-registered long-run noise, empirical SE of the replicate differences)."""
    base_names = [w["name"] for w in windows if w["kind"] == "base"]
    res = {"aa": {}, "ext": [], "win": []}
    cache = {}
    groups = []
    for w in windows:
        if w["kind"] == "ext" and (w["knob"], w["value"]) not in [(g["knob"], g["value"]) for g in groups]:
            groups.append(w)
    aa_done = {}
    for g in groups:
        pr = probe_of(g["knob"])
        key = "%s:%s" % (pr["metric"], pr["ctx"])
        if key not in cache:
            cache[key] = adjusted_series(recs, key, pr["ctx"], set(base_names))
        ser = cache[key]
        stats = _win_stats(ser, [w["name"] for w in windows])
        sig, meas = sigma_for(key)
        names = [w["name"] for w in windows if w["kind"] == "ext" and w["knob"] == g["knob"] and abs(w["value"] - g["value"]) < 1e-9]
        bnames = [b for b in base_names if b in stats]
        wrow = dict(name=None, knob=g["knob"], value=g["value"], sign=g["sign"], metric=pr["metric"], ctx=pr["ctx"], dmin=pr["dmin"],
                    sigma=sig, sigma_measured=meas, effect=float("nan"), se=float("inf"), se_prior=float("inf"), se_emp=float("inf"),
                    mde=float("inf"), n=0, n_base=sum(stats[b][2] for b in bnames), m=0, agree=0, base=float("nan"), mean=float("nan"))
        effs, used, n_e, ms = [], [], 0, []
        for nme in names:
            if nme not in stats or not bnames:
                continue
            te, me, ne_, _ = stats[nme]
            order = sorted(bnames, key=lambda b: stats[b][0])
            before = [b for b in order if stats[b][0] <= te]
            after = [b for b in order if stats[b][0] > te]
            if before and after:
                b1, b2 = stats[before[-1]], stats[after[0]]
                f = (te - b1[0]) / (b2[0] - b1[0]) if b2[0] != b1[0] else 0.5
                bm = (1 - f) * b1[1] + f * b2[1]
            else:
                bm = stats[(before or after)[-1 if before else 0]][1]
            effs.append(me - bm)
            ms.append(me)
            used.append(nme)
            n_e += ne_
            res["win"].append(dict(wrow, name=nme, effect=me - bm, n=ne_, mean=me, base=bm, verdict="-", why="one replicate window"))
        if not effs:
            res["ext"].append(dict(wrow, name=names[0] if names else "?", verdict="INCONCLUSIVE", why="no samples in the window(s) or no baseline"))
            continue
        eff = statistics.fmean(effs)
        m = len(effs)
        used_w = [w for w in windows if w["name"] in used or w["kind"] == "base"]
        se_p = prior_se(windows, (g["knob"], g["value"]), sig)
        se_e = (statistics.stdev(effs) / math.sqrt(m)) if m >= 2 else float("inf")
        se = max(se_p, se_e if math.isfinite(se_e) else 0.0)
        agree = sum(1 for e in effs if g["sign"] == 0 or (e > 0) == (g["sign"] > 0))
        v, why, mde = judge(eff, se, pr["dmin"], g["sign"], n_e, wrow["n_base"], m, agree)
        res["ext"].append(dict(wrow, name="+".join(used) if len(used) <= 2 else "%s..%s" % (used[0], used[-1]), effect=eff, se=se, se_prior=se_p,
                               se_emp=se_e, mde=mde, n=n_e, m=m, agree=agree, verdict=v, why=why,
                               base=statistics.fmean(ms) - eff, mean=statistics.fmean(ms)))
        # A/A control: the neighbouring baseline windows differ by about sigma * sqrt(2 / T)
        if key not in aa_done and len(bnames) >= 2:
            order = sorted(bnames, key=lambda b: stats[b][0])
            dur = {w["name"]: w["dur"] for w in windows}
            diffs = [(a, b, stats[b][1] - stats[a][1]) for a, b in zip(order, order[1:])]
            worst = max(diffs, key=lambda x: abs(x[2]))
            exp = sig * math.sqrt(1.0 / dur[worst[0]] + 1.0 / dur[worst[1]])
            aa_done[key] = {"metric": key, "diff": worst[2], "sigma": sig, "se": exp, "z": abs(worst[2]) / exp if exp > 0 else 0.0}
    if aa_done:
        res["aa"] = max(aa_done.values(), key=lambda a: a["z"])
    return res


def aa_ok(aa):
    """the A/A control does not gate a run: a noisy baseline shows up in the empirical SE (INCONCLUSIVE), never as a PASS"""
    return True


def knob_verdicts(table, entries, deferred):
    """-> {knob: {'effect': verdict, 'why': str, 'rows': [...]}} . A knob PASSes when one extreme PASSes and none shows an unexpected
    sign; FAILs when every judged extreme FAILs; else INCONCLUSIVE."""
    out = {}
    for e in entries:
        rows = [r for r in table["ext"] if r["knob"] == e["id"]]
        if not rows:
            why = dict(deferred).get(e["id"], "not scheduled")
            out[e["id"]] = {"effect": "SKIP", "why": why, "rows": []}
            continue
        vs = [r["verdict"] for r in rows]
        if any(r["verdict"] == "FAIL" and r["why"].startswith("UNEXPECTED SIGN") for r in rows):
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
    flagged so the coordinator can overrule. engine 'NA' is never upgraded to PASS."""
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
    return "%-9s %-30s %6.3g  %-16s %+7.2f  se %5.2f (prior %4.2f, repl %4.2f)  MDE %5.2f  dmin %4.2f  m %d  n %4d/%4d  %-12s %s" % (
        r["name"], r["knob"], r["value"], "%s:%s" % (r["metric"], r["ctx"]), r["effect"], r["se"], r["se_prior"],
        r["se_emp"] if math.isfinite(r["se_emp"]) else -1.0, r["mde"], r["dmin"], r["m"], r["n"], r["n_base"], r["verdict"], r["why"])


def power_lines(windows, notes):
    """the a-priori power table printed by `plan` (and stored in plan.json)"""
    out = []
    for (kid, v), n in sorted(notes.items()):
        out.append("  %-30s %6.3g  %-16s m %d replicates  sigma %4.1f m*sqrt(s)%s  a-priori SE %.2f  MDE %.2f vs dmin %.2f  -> %s%s" % (
            kid, v, n["metric"], n["m"], n["sigma"], "" if n["sigma_measured"] else " (no measurement: default)", n["mde"] / Z_MDE, n["mde"], n["dmin"],
            ("decidable (BORDERLINE: MDE just above dmin, a null result stays INCONCLUSIVE)" if n.get("borderline") else "decidable") if n["feasible"]
            else "UNDER-POWERED (a small effect ends INCONCLUSIVE)",
            "" if n["feasible"] or not n["need_s"] else "; alone it needs %d replicates = %.0f s" % (n["need_reps"], n["need_s"])))
    return out


# ----------------------------------------------------------------------------------------------------------------- self-test
def _synth_samples(plan, seed, effect_by_win, hz=1.0, slow_rel=0.14, tau=10.0, drift=0.0, L=105.0, W=68.0, ar=0.6):
    """a synthetic stream: plan = [(win, seconds)] at hz; effect_by_win[win] = {'lat_with': factor on the with-ball team's lateral spread}.
    Game-state noise as measured in TEST_27: a SLOW common spread factor (AR(1), correlation time tau s, relative sd slow_rel: lat_sd long-run
    sigma = 13 m * slow_rel * sqrt(2 tau) = 8 m*sqrt(s)) on top of per-player AR noise; drift = relative linear drift per second."""
    import random
    rng = random.Random(seed)
    dt = 1.0 / hz
    a_slow = math.exp(-dt / tau)
    a_pl = ar ** dt
    samples, t, i = [], 0.0, 0
    lat_noise = {(tm, k): 0.0 for tm in (0, 1) for k in range(11)}
    slow = 0.0
    ballx, poss = 0.0, 0
    base_lat = [0, -28, -10, 10, 28, -22, -6, 6, 22, -12, 12]
    base_dep = [3, 14, 16, 16, 14, 34, 36, 36, 34, 55, 55]
    for win, secs in plan:
        for _ in range(int(round(secs * hz))):
            t += dt
            if rng.random() < dt / 7.0:
                poss = 1 - poss
            ballx = max(-45, min(45, ballx + rng.gauss(0, 6 * math.sqrt(dt))))
            slow = a_slow * slow + rng.gauss(0, math.sqrt(1 - a_slow ** 2))
            f_with = effect_by_win.get(win, {}).get("lat_with", 1.0) * (1.0 + slow_rel * slow) * (1.0 + drift * t)
            pl = []
            for tm in (0, 1):
                s = 1.0 if tm == 0 else -1.0
                for k in range(11):
                    lat_noise[(tm, k)] = a_pl * lat_noise[(tm, k)] + rng.gauss(0, 3.0 * math.sqrt(1 - a_pl ** 2))
                    spread = (1.1 * f_with) if tm == poss else 0.9
                    lat = base_lat[k] * spread + lat_noise[(tm, k)]
                    dep = base_dep[k] + 0.2 * (s * ballx + 52.5) - 10 + rng.gauss(0, 1.5)
                    pl.append({"handle": tm * 25 + k, "team": tm, "fpos": 0 if k == 0 else k + 3, "p": (s * (52.5 - dep), 0.0, lat)})
            samples.append({"i": i, "t": t, "win": win, "players": pl, "ball": (ballx, 0.1, rng.gauss(0, 8)), "carrier": poss * 25 + 7})
            i += 1
    return samples


def _synth_for_windows(windows, seed, effect_for, **kw):
    """windows: plan_schedule output -> samples; the guard seconds are labelled 'x' (not judged), the window seconds with the window name.
    effect_for(window) -> {'lat_with': factor} for an extreme window"""
    plan, eff = [], {}
    for w in windows:
        if w["guard"]:
            plan.append(("x", w["guard"]))
        plan.append((w["name"], w["dur"]))
        if w["kind"] == "ext":
            eff[w["name"]] = effect_for(w)
    return _synth_samples(plan, seed, eff, **kw)


def _table_of(smp, windows):
    bases = {w["name"] for w in windows if w["kind"] == "base"}
    return effect_table(sample_records(smp, 105.0, 68.0, derive_roles(smp, bases)), windows)


def _judge_synth(windows, seed, factor, **kw):
    """-> effect_table of one synthetic run with the with-ball lateral spread x factor in every extreme window"""
    return _table_of(_synth_for_windows(windows, seed, lambda w: {"lat_with": factor}, **kw), windows)


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
    check(ent[0]["ext"] == [(0.3, -1)] and ent[1]["ext"] == [(0.4, -1)] and ent[2]["ext"][0][1] == -1,
          "knob list: default primary extreme (LOW / strong) with its sign; explicit values keep the nearest default's sign: %s" % [e["ext"] for e in ent])
    check(all(v < 1.0 for e in parse_knob_list("shape.width_in both\nshape.width_out both\n")[0] for v, _ in e["ext"]),
          "knob list: the width knobs have no high extreme (the engine clips width at 1.2 W)")
    check(all(k in PROBES for k in ("shape.width_in", "lineband.deep_max", "runs.frequency")) and len(PROBES) == 23,
          "the probe table covers the 23 catalogue knobs (%d)" % len(PROBES))
    check(all(("%s:%s" % (p["metric"], p["ctx"])) in NOISE_PRIOR for p in PROBES.values() if p.get("metric") and p["ext"]),
          "every scheduled metric has a measured noise prior")
    # --- design weights / a-priori SE
    ws = build_windows([("shape.width_in", 0.3, -1)], 3, 30.0, 8.0)
    wts, m = design_weights(ws, ("shape.width_in", 0.3))
    check(m == 3 and abs(sum(wts.values())) < 1e-12 and len(ws) == 7 and ws[0]["kind"] == "base" and ws[-1]["kind"] == "base",
          "design: 3 replicates -> A E A E A E A, the estimator weights sum to zero (drift-free to first order)")
    se = prior_se(ws, ("shape.width_in", 0.3), 8.0)
    var = sum(w * w / 30.0 for w in wts.values()) * 64.0
    check(abs(se - math.sqrt(var)) < 1e-9 and 0.8 < se < 1.6, "design: a-priori SE of 3 x 30 s windows at sigma 8 is %.2f m" % se)
    # --- schedule
    ent3, _ = parse_knob_list("\n".join(["shape.width_in", "shape.width_out", "shape.width_in_cb", "marking.tightness", "lineband.deep_max"]))
    wins, dfr, notes = plan_schedule(ent3, win=30, guard=8, budget=420)
    names = [w["name"] for w in wins]
    check(wins and names[0] == "A1" and wins[-1]["kind"] == "base", "schedule: starts and ends with a baseline %s" % names)
    check(schedule_seconds(wins) <= 420 and all(w["dur"] <= 40 for w in wins), "schedule: fits the budget (%.0f s) and no window > 40 s" % schedule_seconds(wins))
    reps = {}
    for w in wins:
        if w["kind"] == "ext":
            reps[(w["knob"], w["value"])] = reps.get((w["knob"], w["value"]), 0) + 1
    check(reps and min(reps.values()) >= MIN_REPS and len(set(reps.values())) == 1,
          "schedule: every extreme has the same number of replicates >= %d (%s)" % (MIN_REPS, reps))
    kinds = [w["kind"] for w in wins]
    check(all(kinds[i] != kinds[i + 1] for i in range(len(kinds) - 1)), "schedule: baseline and extreme windows strictly alternate")
    check(any(k == "lineband.deep_max" for k, _ in dfr), "schedule: the set-up-only knob is deferred with a reason")
    check(all(((k, v) in notes) for k, v in reps), "schedule: the a-priori power is reported per scheduled extreme")
    check(any("under-powered" in why for k, why in dfr) or len(reps) == len(notes), "schedule: under-powered extremes are deferred with the seconds they would need: %s" % (
        [w for k, w in dfr if "under" in w][:1]))
    w2, d2, n2 = plan_schedule(ent3, win=50, budget=100)
    check(w2 == [] and len(d2) >= 4, "schedule: a budget that fits no 3-replicate group schedules nothing and says why (%d deferred)" % len(d2))
    w3, d3, n3 = plan_schedule(ent3, win=50, budget=900, allow_underpowered=True)
    check(w3 and all(w["dur"] == 40 for w in w3), "schedule: win capped at 40 s")
    # --- metrics
    smp = _synth_samples([("A1", 25)], 1, {})
    roles = derive_roles(smp, {"A1"})
    rec = sample_records(smp, 105.0, 68.0, roles)
    check(len(rec) >= 15 and all(k in rec[0]["v"] for k in ("lat_sd:with", "cb_split:with", "nn_opp:without", "wide_abs:with", "press1:without",
                                                           "ndef:without", "back_sd:with", "compact:with")),
          "metrics: with / without-ball metrics (spread, wide players, back line, CB split, pressing, nearest defender) per sample (%d records)" % len(rec))
    check(len(roles.get(0, ())) == N_WIDE and 1 in roles[0] and 4 in roles[0],
          "roles: the wide players come from the baseline windows by a fixed rule (team 0: %s)" % sorted(roles.get(0, ())))
    # --- statistics. Full-size run: width_in, 30 s windows + 8 s guard.
    ent1, _ = parse_knob_list("shape.width_in\n")
    W1, D1, N1 = plan_schedule(ent1, win=30, guard=8, budget=420)
    n1 = N1[("shape.width_in", 0.3)]
    check(len(W1) >= 9 and not D1, "stats: width_in alone gets %d windows (m %d, a-priori MDE %.2f vs dmin %.2f)" % (len(W1), n1["m"], n1["mde"], n1["dmin"]))
    t0 = time.time()
    tb = _judge_synth(W1, 11, 0.5)
    r = tb["ext"][0]
    check(r["verdict"] == "PASS" and r["effect"] < -2.5, "stats: a x0.5 with-ball spread (about -6 m) is PASS (effect %.2f, se %.2f, prior %.2f, MDE %.2f)" % (
        r["effect"], r["se"], r["se_prior"], r["mde"]))
    check(tb["aa"] and tb["aa"]["sigma"] > 0, "stats: the A/A line is reported (sigma %.1f, adjacent diff %+.2f, z %.1f)" % (tb["aa"]["sigma"], tb["aa"]["diff"], tb["aa"]["z"]))
    r0 = _judge_synth(W1, 12, 1.0)["ext"][0]
    check(r0["verdict"] != "PASS", "stats: no real effect is never PASS (%s: %s)" % (r0["verdict"], r0["why"]))
    rw = _judge_synth(W1, 14, 1.8)["ext"][0]
    check(rw["verdict"] == "FAIL" and "UNEXPECTED SIGN" in rw["why"], "stats: a significant shift in the wrong direction is FAIL (%s)" % rw["why"])
    npass = sum(_judge_synth(W1, 100 + sd, 1.0)["ext"][0]["verdict"] == "PASS" for sd in range(40))
    check(npass <= 2, "stats: false PASS under the null with the measured slow game-state noise in %d of 40 seeded runs (<= 2 allowed)" % npass)
    hits = sum(_judge_synth(W1, 200 + sd, 0.5)["ext"][0]["verdict"] == "PASS" for sd in range(20))
    check(hits >= 16, "stats: a x0.5 spread is PASS in %d of 20 seeded runs (>= 16 expected)" % hits)
    h22 = sum(_judge_synth(W1, 250 + sd, 0.78)["ext"][0]["verdict"] == "PASS" for sd in range(30))
    print("  info  a x0.78 spread (about -3 m, the size seen in TEST_27) is PASS in %d of 30 runs (a-priori MDE %.2f, dmin %.2f)" % (h22, n1["mde"], n1["dmin"]))
    # the sampling rate does not change the verdict statistics (seconds, not samples, limit the power)
    h15 = sum(_judge_synth(W1, 300 + sd, 0.5, hz=1.5)["ext"][0]["verdict"] == "PASS" for sd in range(8))
    check(h15 >= 6, "stats: the same design sampled at 1.5 Hz judges the same way (PASS %d of 8)" % h15)
    # drift: a linear drift of the lateral spread (30 % over the run) must not create an effect
    td = _judge_synth(W1, 15, 1.0, drift=0.3 / 385.0)["ext"][0]
    check(abs(td["effect"]) < 2.0, "stats: a linear drift is removed by the interpolated neighbouring baselines (effect %+.2f m)" % td["effect"])
    # two replicates are never decisive
    W2 = W1[:5]
    t2 = _judge_synth(W2, 16, 0.5)["ext"][0]
    check(t2["verdict"] == "INCONCLUSIVE" and "replicate" in t2["why"], "stats: fewer than %d replicate windows are INCONCLUSIVE (%s)" % (MIN_REPS, t2["why"][:60]))
    # one wild replicate does not make a PASS: the spread changes in one window of five
    smp = _synth_for_windows(W1, 17, lambda w: {"lat_with": 0.1 if w["name"] == "E1" else 1.0})
    tw = _table_of(smp, W1)["ext"][0]
    check(tw["verdict"] != "PASS", "stats: one wild replicate window (the others unchanged) is not a PASS (%s: %s)" % (tw["verdict"], tw["why"][:70]))
    print("  (statistics block: %.0f s)" % (time.time() - t0))
    # --- slider decision table
    dd = slider_decision
    check(dd("PASS", "PASS")[0] == "YES" and dd("NA", "PASS")[0].startswith("YES") and dd("FAIL", "PASS")[0] == "NO"
          and dd("PASS", "FAIL")[0] == "NO" and dd("NA", "INCONCLUSIVE")[0] == "DEFER" and dd("PASS", "SKIP")[0] == "DEFER",
          "slider decision table: YES needs the effect; engine FAIL / effect FAIL = NO; inconclusive = DEFER; NA is never upgraded")
    # --- judge edges
    check(judge(5.0, 0.5, 2.5, -1, 20, 20)[0] == "FAIL" and judge(-5.0, 0.5, 2.5, -1, 20, 20)[0] == "PASS"
          and judge(0.2, 0.5, 2.5, -1, 20, 20)[0] == "FAIL" and judge(0.2, 2.0, 2.5, -1, 20, 20)[0] == "INCONCLUSIVE"
          and judge(3.0, 2.0, 2.5, 0, 20, 20)[0] == "INCONCLUSIVE" and judge(-5.0, 0.5, 2.5, -1, 5, 20)[0] == "INCONCLUSIVE"
          and judge(5.0, 0.5, 2.5, 0, 20, 20)[0] == "PASS" and judge(-5.0, 0.5, 2.5, -1, 20, 20, 5, 3)[0] == "INCONCLUSIVE"
          and judge(-5.0, 0.5, 2.5, -1, 20, 20, 5, 4)[0] == "PASS", "judge: sign, dmin, MDE, sample-size and replicate-agreement edges")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    print(__doc__)
    sys.exit(2)
