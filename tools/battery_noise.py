"""battery_noise.py - offline: the long-run noise of every battery metric, measured on saved battery evidence (tools/, no game access).

  python battery_noise.py EVIDENCE_DIR [EVIDENCE_DIR ...] [--bin 20] [--json OUT]    sigma table (m * sqrt(s)) per run and the prior to paste into
                                                                                     battery_lib.NOISE_PRIOR; the settling profile after a write
  python battery_noise.py --self-test

Method: per run the positions are turned into the battery's metrics (battery_lib.sample_records, wide players from the baseline windows), each metric
is adjusted for ball depth, |ball lateral| and the team (battery_lib.adjusted_series) and the residual around the mean of each WINDOW CONDITION
(baseline / each extreme value) is cut into bins of --bin seconds; sigma = sqrt(var(bin means) * bin) is the long-run sd of a metric mean per sqrt(second)
(the sd of a T-second window mean is sigma / sqrt(T)). It is estimated on the residuals of the condition means, so a real effect of the knob is not
counted as noise, but slow drift between windows of one condition is. The prior = 1.3 x the larger of the runs, rounded up to 0.5.
"""
import collections, json, math, os, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import battery_lib as bl  # noqa: E402

KEYS = ["lat_sd", "wide_abs", "back_sd", "cb_split", "len_sd", "compact", "backline_m", "gap_back_mid", "nn_opp", "press_m", "press1", "ndef",
        "lat_range", "centroid_m", "ahead_share", "len_range"]


def load_run(d):
    plan = json.load(open(os.path.join(d, "plan.json")))
    samples = bl.load_positions_csv(os.path.join(d, "positions.csv"))
    windows = [w for w in plan["windows"] if "t1" in w]
    return samples, windows, plan


def condition(windows):
    cond = {}
    for w in windows:
        cond[w["name"]] = "A" if w["kind"] == "base" else "%s=%g" % (w.get("knob"), w.get("value"))
    return cond


def sigma_table(samples, windows, plan, binw=20.0):
    """-> {key: (sigma, lag-1 autocorrelation of the bin means, n bins, sample sd)}"""
    base = {w["name"] for w in windows if w["kind"] == "base"}
    roles = bl.derive_roles(samples, base)
    recs = bl.sample_records(samples, plan["L"], plan["W"], roles)
    cond = condition(windows)
    out = {}
    for ctx in bl.CONTEXTS:
        for k in KEYS:
            key = "%s:%s" % (k, ctx)
            ser = bl.adjusted_series(recs, key, ctx, base)
            pts = sorted((t, w, a) for w, v in ser.items() if w in cond for t, a in v)
            if len(pts) < 40:
                continue
            cm = collections.defaultdict(list)
            for t, w, a in pts:
                cm[cond[w]].append(a)
            mu = {c: statistics.fmean(v) for c, v in cm.items()}
            res = [(t, a - mu[cond[w]]) for t, w, a in pts]
            bins = collections.defaultdict(list)
            for t, r in res:
                bins[int(t // binw)].append(r)
            bm = [statistics.fmean(v) for v in bins.values() if len(v) >= max(3, binw / 2 / 2.0 * 0.5)]
            if len(bm) < 5:
                continue
            mbar = statistics.fmean(bm)
            den = sum((x - mbar) ** 2 for x in bm)
            ac = sum((x - mbar) * (y - mbar) for x, y in zip(bm, bm[1:])) / den if den > 0 else 0.0
            out[key] = (math.sqrt(statistics.pvariance(bm) * binw), ac, len(bm), statistics.pstdev([r for t, r in res]))
    return out


def prior_from(tables):
    keys = set().union(*[set(t) for t in tables])
    pri = {}
    for k in sorted(keys):
        s = max(t[k][0] for t in tables if k in t)
        pri[k] = math.ceil(s * 1.3 * 2.0) / 2.0
    return pri


def self_test():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    plan = [("A1", 60), ("E1", 60), ("A2", 60), ("E2", 60), ("A3", 60)]
    smp = bl._synth_samples(plan, 5, {"E1": {"lat_with": 0.8}, "E2": {"lat_with": 0.8}}, hz=1.0, slow_rel=0.14, tau=10.0)
    wins = [{"name": n, "kind": "base" if n[0] == "A" else "ext", "knob": None if n[0] == "A" else "shape.width_in", "value": None if n[0] == "A" else 0.3,
             "t1": 1, "dur": 60, "guard": 0} for n, _ in plan]
    tb = sigma_table(smp, wins, {"L": 105.0, "W": 68.0}, 20.0)
    s = tb.get("lat_sd:with")
    check(s is not None and 4.0 < s[0] < 14.0, "synthetic stream with a 8 m*sqrt(s) slow component: sigma(lat_sd:with) = %s (expected about 8)" % (round(s[0], 1) if s else None))
    pr = prior_from([tb])
    check(pr["lat_sd:with"] >= tb["lat_sd:with"][0], "the prior is at least the measured sigma (x1.3, rounded up to 0.5): %s" % pr["lat_sd:with"])
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


def main(argv):
    if "--self-test" in argv:
        return self_test()
    dirs = [a for a in argv if not a.startswith("--") and os.path.isdir(a)]
    if not dirs:
        print(__doc__)
        return 2
    binw = float(argv[argv.index("--bin") + 1]) if "--bin" in argv else 20.0
    tables = []
    for d in dirs:
        s, w, p = load_run(d)
        t = sigma_table(s, w, p, binw)
        tables.append(t)
        print("== %s (%d samples, bins of %.0f s)" % (d, len(s), binw))
        for k in sorted(t):
            print("  %-22s sigma %6.2f m*sqrt(s)   sample sd %5.2f   lag-1 ac %5.2f   bins %d" % (k, t[k][0], t[k][3], t[k][1], t[k][2]))
    pri = prior_from(tables)
    print("\nNOISE_PRIOR = {")
    for k, v in sorted(pri.items()):
        print('    "%s": %.1f,' % (k, v))
    print("}")
    if "--json" in argv:
        json.dump({"bin": binw, "runs": dirs, "prior": pri}, open(argv[argv.index("--json") + 1], "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
