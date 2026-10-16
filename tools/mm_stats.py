"""mm_stats.py - statistics for match_measure windows (offline, read-only on the data). From sweep T4 (2026-10-13), plus the gate X1
decision (--x1, docs/TACTICS_OVERHAUL_DECISION.md 4.1) and a synthetic --self-test. Offline: it never talks to the game, so it runs
whatever GAME_STATE says (it is run after the kill).

Reads one or more match_measure output dirs (positions.csv, metres, 22 players + ball + carrier per sample), labels them
as windows, applies a stoppage filter and strata, computes per-team per-stratum metrics, and runs a BLOCK PERMUTATION
TEST between the windows of condition A and condition B. Prints PASS / FAIL / INCONCLUSIVE against a criterion.

usage
  python mm_stats.py --x1 --csv RUN_DIR/positions.csv [--x1-baseline DIR_AT_1.0]   gate X1: OUTCOME 1/2/3 + VERDICT
  python mm_stats.py --self-test                                                 offline proof on synthetic ABAB data
  python mm_stats.py --win A1=DIR --win B1=DIR [--win A2=DIR:15-30 ...] --A A1,A2 --B B1,B2
                     [--metric width_with] [--criterion "width_with_ball both_teams +3m 2x_noise p<0.05"]
                     [--block N | --block auto] [--perm 5000] [--stoppage 0.5] [--ball-still 0.3] [--drop-dead-ball]
                     [--stratum poss=own,third=any] [--noise A1,A2] [--json OUT] [--diag]
  python mm_stats.py --win X=DIR --diag                 noise floor only: per-sample sd, lag-1 r, tau, ESS per metric
  A window spec is LABEL=DIR or LABEL=DIR:START-END (seconds within that run, to split one run into sub-windows), or
  LABEL=FILE.csv for a v2 positions.csv with a 'win' column (then LABEL selects the rows whose win == LABEL).

criterion grammar (space separated tokens, any order):
  <metric>            width_with_ball | width_without_ball | width | backline | length | compact | press | lat_sd_with
  both_teams | pooled | either
  +<x>m | -<x>m       required shift of B - A in metres (sign = predicted direction)
  <k>x_noise          |B - A| must be >= k * |A_i - A_j| (the noise floor from --noise, default: the A windows against
                      each other; if only one A window exists it is split in two halves)
  p<<alpha>           block permutation p-value (two-sided) threshold
  without_half        (optional) without-ball shift must be < half of the with-ball shift (engine-wide in-possession knob)
verdict
  PASS          every team (or the pool) meets direction, magnitude, noise multiple and p
  FAIL          a team's shift is significant (p < alpha) in the WRONG direction, or the required shift is excluded in every
                team (shift in the predicted direction + MDE95 < required magnitude)
  INCONCLUSIVE  anything else (not enough effective samples, noise floor too high, teams disagree, MDE > magnitude)
"""
import argparse, csv, json, math, os, random, statistics, sys

L_DEFAULT, W_DEFAULT = 105.0, 68.0
METRICS = ["width", "width_with", "width_without", "lat_sd", "lat_sd_with", "lat_sd_without", "backline", "length",
           "compact", "centroid", "press"]
ALIASES = {"width_with_ball": "width_with", "width_without_ball": "width_without", "lat_sd_with_ball": "lat_sd_with"}


# ------------------------------------------------------------------ loading
def load_positions(path):
    """positions.csv -> list of samples {t, players:[{handle, team, fpos, p:(x,y,z)}], ball, carrier, win?}"""
    rows = list(csv.DictReader(open(path, newline="")))
    out, cur = [], None
    for r in rows:
        s = int(r["sample"])
        if cur is None or cur["i"] != s:
            car = r.get("carrier")
            cur = {"i": s, "t": float(r["t"]), "players": [],
                   "ball": tuple(float(r[k]) for k in ("ball_x", "ball_y", "ball_z")),
                   "carrier": int(car) if car not in (None, "", "None") else None,
                   "win": r.get("win") or None, "clock": float(r["clock"]) if r.get("clock") not in (None, "", "nan") else None}
            out.append(cur)
        try:
            p = tuple(float(r[k]) for k in ("x", "y", "z"))
        except ValueError:
            p = None
        if p is not None and not all(math.isfinite(c) for c in p):
            p = None
        cur["players"].append({"handle": int(r["handle"]), "team": int(r["team"]), "fpos": int(r["fpos"]), "p": p})
    return out


def parse_win(spec):
    label, _, rest = spec.partition("=")
    rng = None
    src = rest
    if ":" in rest and not rest[1:3] == ":\\" and not rest[1:3] == ":/":
        src, _, r = rest.rpartition(":")
        if "-" in r:
            a, b = r.split("-")
            rng = (float(a), float(b))
        else:
            src = rest
    elif rest.count(":") >= 2:  # C:\dir:10-20
        src, _, r = rest.rpartition(":")
        a, b = r.split("-")
        rng = (float(a), float(b))
    return label, src, rng


def load_window(label, src, rng):
    path = src if src.lower().endswith(".csv") else os.path.join(src, "positions.csv")
    S = load_positions(path)
    if S and S[0].get("win"):
        S = [s for s in S if s["win"] == label]
    if rng:
        t0 = S[0]["t"] if S else 0.0
        S = [s for s in S if rng[0] <= s["t"] - t0 < rng[1]]
    for s in S:
        s["win"] = label
    return S


# ------------------------------------------------------------------ per-sample metrics
def axis_from_gks(S):
    for s in S:
        gk = {p["team"]: p["p"] for p in s["players"] if p["fpos"] == 0 and p["p"]}
        if 0 in gk and 1 in gk:
            return 0 if abs(gk[0][0] - gk[1][0]) >= abs(gk[0][2] - gk[1][2]) else 2
    return 0


def team_metrics(rows, L, along, lat, cpos, cteam, team):
    gk = [q for q in rows if q[0]["fpos"] == 0]
    if not gk:
        side = 1.0 if statistics.fmean(q[1][along] for q in rows) > 0 else -1.0
        gk = [max(rows, key=lambda q: side * q[1][along])]
    s = 1.0 if gk[0][1][along] > 0 else -1.0
    out = [q for q in rows if q is not gk[0]]
    if len(out) < 4:
        return None
    depth = sorted(L / 2 - s * q[1][along] for q in out)
    xs = [q[1][along] for q in out]
    ys = [q[1][lat] for q in out]
    cx, cy = statistics.fmean(xs), statistics.fmean(ys)
    m = {"own_goal": s, "backline": statistics.fmean(depth[:4]), "length": depth[-1] - depth[0],
         "width": max(ys) - min(ys), "lat_sd": statistics.pstdev(ys),
         "compact": statistics.fmean(math.hypot(x - cx, y - cy) for x, y in zip(xs, ys)), "centroid": L / 2 - s * cx}
    if cpos is not None and cteam is not None and cteam != team:
        d = sorted(math.hypot(q[1][along] - cpos[along], q[1][lat] - cpos[lat]) for q in out)
        m["press"] = statistics.fmean(d[:3])
    return m


def per_sample(S, L=L_DEFAULT, W=W_DEFAULT):
    """-> list of records {win, t, team, poss (own/opp/dead), third (def/mid/att), maxspeed, ballspeed, dt, metrics...}"""
    along = axis_from_gks(S)
    lat = 2 - along
    recs = []
    prev = None
    for s in S:
        pos = {p["handle"]: p["p"] for p in s["players"]}
        dt = (s["t"] - prev["t"]) if prev is not None and prev["win"] == s["win"] else None
        maxspeed = ballspeed = None
        if dt and dt > 0.05:
            moves = []
            for h, q in pos.items():
                q0 = prev["pos"].get(h)
                if q and q0:
                    moves.append(math.hypot(q[along] - q0[along], q[lat] - q0[lat]) / dt)
            maxspeed = max(moves) if moves else None
            b0, b1 = prev["ball"], s["ball"]
            if all(math.isfinite(c) for c in b0 + b1):
                ballspeed = math.hypot(b1[along] - b0[along], b1[lat] - b0[lat]) / dt
        frozen = prev is not None and prev["win"] == s["win"] and all(pos.get(h) == prev["pos"].get(h) for h in pos)
        prev = {"t": s["t"], "pos": pos, "ball": s["ball"], "win": s["win"]}
        cteam = cpos = None
        if s["carrier"] is not None:
            for p in s["players"]:
                if p["handle"] == s["carrier"] and p["p"]:
                    cteam, cpos = p["team"], p["p"]
        on = [(p, p["p"]) for p in s["players"] if p["p"] and abs(p["p"][along]) <= L / 2 + 4 and abs(p["p"][lat]) <= W / 2 + 6]
        for tm in (0, 1):
            rows = [r for r in on if r[0]["team"] == tm]
            if len(rows) < 7:
                continue
            m = team_metrics(rows, L, along, lat, cpos, cteam, tm)
            if not m:
                continue
            poss = "dead" if cteam is None else ("own" if cteam == tm else "opp")
            bx = s["ball"][along] if math.isfinite(s["ball"][along]) else None
            depth = None if bx is None else (L / 2 - m["own_goal"] * bx) / L  # 0 = own goal line
            third = None if depth is None else ("def" if depth < 1 / 3 else ("mid" if depth < 2 / 3 else "att"))
            r = {"win": s["win"], "t": s["t"], "i": s["i"], "team": tm, "poss": poss, "third": third, "ball_depth": depth,
                 "maxspeed": maxspeed, "ballspeed": ballspeed, "dt": dt, "frozen": frozen, "clock": s.get("clock")}
            r.update({k: m[k] for k in ("backline", "length", "width", "lat_sd", "compact", "centroid")})
            if "press" in m:
                r["press"] = m["press"]
            if poss == "own":
                r["width_with"], r["lat_sd_with"], r["length_with"] = m["width"], m["lat_sd"], m["length"]
            elif poss == "opp":
                r["width_without"], r["lat_sd_without"], r["length_without"] = m["width"], m["lat_sd"], m["length"]
            recs.append(r)
    return recs


# ------------------------------------------------------------------ filters / strata
def apply_filters(recs, stoppage=None, ball_still=None, drop_dead=False, max_dt=None, strata=None, drop_frozen=True):
    out = []
    for r in recs:
        if drop_frozen and r["frozen"]:
            continue
        if stoppage is not None and r["maxspeed"] is not None and r["maxspeed"] < stoppage:
            continue
        if ball_still is not None and r["ballspeed"] is not None and r["ballspeed"] < ball_still:
            continue
        if drop_dead and r["poss"] == "dead":
            continue
        if max_dt is not None and r["dt"] is not None and r["dt"] > max_dt:
            continue
        if strata:
            ok = True
            for k, v in strata.items():
                if v != "any" and r.get(k) != v:
                    ok = False
            if not ok:
                continue
        out.append(r)
    return out


# ------------------------------------------------------------------ noise floor
def acf(x, k):
    n = len(x)
    if n <= k + 1:
        return None
    m = statistics.fmean(x)
    v = sum((a - m) ** 2 for a in x)
    if v == 0:
        return None
    return sum((x[i] - m) * (x[i + k] - m) for i in range(n - k)) / v


def tau_int(x, maxlag=None):
    """integrated autocorrelation time in samples (Sokal-style: sum of positive lags until the first non-positive)"""
    n = len(x)
    if n < 4:
        return None
    maxlag = maxlag or max(1, n // 3)
    t = 1.0
    for k in range(1, maxlag + 1):
        r = acf(x, k)
        if r is None or r <= 0:
            break
        t += 2 * r
    return t


def series(recs, team, metric):
    """ordered per-window series of one metric for one team (None where the stratum does not apply)"""
    out = {}
    for r in recs:
        if r["team"] != team:
            continue
        out.setdefault(r["win"], []).append(r.get(metric))
    return out


def diag(recs, metrics=METRICS):
    """per window per team per metric: n, mean, sd, lag-1 r, tau, ESS"""
    res = {}
    wins = sorted(set(r["win"] for r in recs), key=lambda w: min(r["t"] for r in recs if r["win"] == w))
    for w in wins:
        for tm in (0, 1):
            for mt in metrics:
                xs = [r[mt] for r in recs if r["win"] == w and r["team"] == tm and r.get(mt) is not None]
                if len(xs) < 3:
                    continue
                # for the with / without series use the contiguous sequence (gaps are closed): tau is then conservative
                r1 = acf(xs, 1)
                tau = tau_int(xs)
                res[(w, tm, mt)] = {"n": len(xs), "mean": statistics.fmean(xs), "sd": statistics.pstdev(xs),
                                    "r1": r1, "tau": tau, "ess": (len(xs) / tau) if tau else None}
    return res


# ------------------------------------------------------------------ block permutation test
def blocks_of(recs, wins, team, metric, block):
    """contiguous blocks (by sample index within a window) of the metric values of one team"""
    out = []
    for w in wins:
        xs = [r for r in recs if r["win"] == w and r["team"] == team]
        xs.sort(key=lambda r: r["i"])
        for k in range(0, len(xs), block):
            vals = [r[metric] for r in xs[k:k + block] if r.get(metric) is not None]
            if vals:
                out.append(vals)
    return out


def perm_test(blocks_a, blocks_b, nperm=5000, seed=1):
    """statistic = mean(B samples) - mean(A samples); blocks are reassigned to A/B keeping the block counts.
    returns (observed, p two-sided, null 95th percentile of |stat|, n_a, n_b)"""
    def stat(ba, bb):
        va = [v for b in ba for v in b]
        vb = [v for b in bb for v in b]
        if not va or not vb:
            return None
        return statistics.fmean(vb) - statistics.fmean(va)
    obs = stat(blocks_a, blocks_b)
    if obs is None:
        return None
    pool = blocks_a + blocks_b
    na = len(blocks_a)
    rng = random.Random(seed)
    cnt, absn = 0, []
    for _ in range(nperm):
        rng.shuffle(pool)
        s = stat(pool[:na], pool[na:])
        if s is None:
            continue
        absn.append(abs(s))
        if abs(s) >= abs(obs) - 1e-12:
            cnt += 1
    absn.sort()
    p = (cnt + 1) / (len(absn) + 1)
    mde = absn[int(0.95 * len(absn))] if absn else None  # the shift that would be significant at 0.05 with this data
    va = [v for b in blocks_a for v in b]
    vb = [v for b in blocks_b for v in b]
    return {"obs": obs, "p": p, "mde95": mde, "n_a": len(va), "n_b": len(vb), "mean_a": statistics.fmean(va),
            "mean_b": statistics.fmean(vb), "blocks_a": len(blocks_a), "blocks_b": len(blocks_b)}


# ------------------------------------------------------------------ criterion
def parse_criterion(text):
    c = {"metric": "width_with", "mode": "both_teams", "shift": 3.0, "noise_mult": 2.0, "alpha": 0.05, "without_half": False}
    for tok in text.split():
        t = tok.lower()
        if t in ("both_teams", "pooled", "either"):
            c["mode"] = t
        elif t.endswith("m") and (t[0] in "+-") and t[1:-1].replace(".", "", 1).isdigit():
            c["shift"] = float(t[:-1])
        elif t.endswith("x_noise"):
            c["noise_mult"] = float(t[:-len("x_noise")])
        elif t.startswith("p<"):
            c["alpha"] = float(t[2:])
        elif t == "without_half":
            c["without_half"] = True
        elif t.startswith("x") and t[1:].replace(".", "", 1).isdigit():
            c["ratio"] = float(t[1:])  # B/A ratio: <= r when r < 1, >= r when r > 1
        else:
            c["metric"] = ALIASES.get(t, t)
    return c


def verdict(results, crit, noise, without=None, underblocked=False):
    """results: {team or 'pooled': perm result}; noise: {team: |A_i - A_j|}; underblocked: the blocks are shorter than
    2 tau (too few samples), so p is optimistic -> neither PASS nor FAIL may rest on it"""
    sign = 1.0 if crit["shift"] >= 0 else -1.0
    need = abs(crit["shift"])
    keys = ["pooled"] if crit["mode"] == "pooled" else [0, 1]
    checks, reasons = {}, []
    for k in keys:
        r = results.get(k)
        if not r:
            checks[k] = "no data"
            continue
        nz = noise.get(k)
        d = r["obs"] * sign
        ok_dir = d > 0
        ok_mag = d >= need
        ok_noise = (nz is None) or (abs(r["obs"]) >= crit["noise_mult"] * nz)
        ok_p = r["p"] < crit["alpha"]
        wrong_sig = (d < 0) and ok_p and not underblocked
        powered = r["mde95"] is not None and r["mde95"] <= need and not underblocked
        enough_blocks = r["blocks_a"] >= 3 and r["blocks_b"] >= 3 and not underblocked
        ok_ratio = True
        if crit.get("ratio") is not None and r.get("mean_a"):
            ratio = r["mean_b"] / r["mean_a"]
            ok_ratio = ratio <= crit["ratio"] if crit["ratio"] < 1 else ratio >= crit["ratio"]
        excluded = (r["mde95"] is not None) and (d + r["mde95"] < need) and not underblocked
        checks[k] = {"shift": r["obs"], "p": r["p"], "mde95": r["mde95"], "noise": nz, "dir": ok_dir, "mag": ok_mag,
                     "excluded": excluded,
                     "noise_ok": ok_noise, "p_ok": ok_p, "wrong_sig": wrong_sig, "powered": powered and enough_blocks,
                     "blocks_ok": enough_blocks, "ratio_ok": ok_ratio,
                     "ratio": (r["mean_b"] / r["mean_a"]) if r.get("mean_a") else None,
                     "pass": ok_dir and ok_mag and ok_noise and ok_p and enough_blocks and ok_ratio}
    vals = [v for v in checks.values() if isinstance(v, dict)]
    if not vals or len(vals) < len(keys):
        return "INCONCLUSIVE", checks, ["missing data for a team"]
    passed = [v["pass"] for v in vals]
    if crit["without_half"]:  # review minor 10: a missing without-ball control is NOT a pass
        for k in keys:
            w = (without or {}).get(k)
            r = results.get(k)
            if r is None:
                continue
            if not w:
                reasons.append("without-ball control missing for %s: the 'without < half' condition cannot pass" % (
                    "the pool" if k == "pooled" else "team %s" % k))
                passed = [False]
            elif abs(w["obs"]) >= 0.5 * abs(r["obs"]):
                reasons.append("without-ball shift %.1f m is not < half the with-ball shift %.1f m (team %s)" % (w["obs"], r["obs"], k))
                passed = [False]
    if all(passed) and not reasons:
        return "PASS", checks, reasons
    if crit["mode"] == "either" and any(passed) and not reasons:
        return "PASS", checks, reasons
    if any(v["wrong_sig"] for v in vals):
        return "FAIL", checks, reasons + ["significant shift in the wrong direction"]
    if all(v["excluded"] for v in vals):
        return "FAIL", checks, reasons + ["the required shift is excluded in every team: shift + MDE95 < %.1f m" % need]
    for k, v in checks.items():
        if isinstance(v, dict) and not v["pass"]:
            why = []
            if not v["dir"]:
                why.append("wrong direction")
            if not v["mag"]:
                why.append("|shift| %.1f < %.1f m" % (abs(v["shift"]), need))
            if not v["noise_ok"]:
                why.append("|shift| < %.1fx noise floor %.1f m" % (crit["noise_mult"], v["noise"]))
            if not v["p_ok"]:
                why.append("p %.3f >= %.2f" % (v["p"], crit["alpha"]))
            if not v["powered"]:
                why.append("MDE %.1f m > %.1f m (underpowered)" % (v["mde95"] or float("nan"), need))
            if not v["blocks_ok"]:
                why.append("fewer than 3 blocks of 2 tau per condition (windows too short for the autocorrelation time)")
            if not v["ratio_ok"]:
                why.append("B/A ratio %.2f misses x%.2f" % (v["ratio"], crit["ratio"]))
            reasons.append("team %s: %s" % (k, "; ".join(why)))
    return "INCONCLUSIVE", checks, reasons


# ------------------------------------------------------------------ the A/B test (shared by the CLI and the X1 decision)
def auto_block(D, recs, mt, A, B, block_arg="auto", say=print):
    """block length in samples: ceil(2 x median tau) of the metric (tau = max(integrated, AR(1))), shortened (and flagged
    'underblocked') only if a condition would keep fewer than 3 blocks"""
    if block_arg != "auto":
        return int(block_arg), False
    taus = []
    for (w, tm, m), v in D.items():
        if m == mt and v["tau"]:
            ar1 = (1 + v["r1"]) / (1 - v["r1"]) if v["r1"] is not None and 0 < v["r1"] < 0.97 else 1.0
            taus.append(max(v["tau"], ar1))
    block = max(2, int(math.ceil(2 * statistics.median(taus)))) if taus else 5
    nmin = min((sum(1 for r in recs if r["win"] in ws and r["team"] == tm and r.get(mt) is not None)
                for ws in (A, B) for tm in (0, 1)), default=0)
    if nmin and block > max(2, nmin // 3):
        block = max(2, nmin // 3)
        say("  note: block shortened to %d so that every condition keeps >= 3 blocks (tau says %.1f): the blocks are "
            "then shorter than the autocorrelation time -> p is optimistic; the verdict cannot be PASS on fewer than "
            "3 blocks per condition" % (block, max(taus) if taus else 0))
        return block, True
    return block, False


def noise_floor(recs, mt, nz, say=print):
    """|mean(A_j) - mean(A_i)| per team and pooled; a single label is split in two halves (its records are relabelled)"""
    noise = {}
    if len(nz) == 1:
        xs = sorted(set(r["i"] for r in recs if r["win"] == nz[0]))
        if len(xs) >= 6:
            mid = xs[len(xs) // 2]
            for r in recs:
                if r["win"] == nz[0]:
                    r["win"] = nz[0] + ("#1" if r["i"] < mid else "#2")
            nz = [nz[0] + "#1", nz[0] + "#2"]
            say("  noise floor from the two halves of %s" % nz[0][:-2])
    if len(nz) >= 2:
        for tm in (0, 1):
            a = [r[mt] for r in recs if r["win"] == nz[0] and r["team"] == tm and r.get(mt) is not None]
            b = [r[mt] for r in recs if r["win"] == nz[1] and r["team"] == tm and r.get(mt) is not None]
            if a and b:
                noise[tm] = abs(statistics.fmean(b) - statistics.fmean(a))
        a = [r[mt] for r in recs if r["win"] == nz[0] and r.get(mt) is not None]
        b = [r[mt] for r in recs if r["win"] == nz[1] and r.get(mt) is not None]
        if a and b:
            noise["pooled"] = abs(statistics.fmean(b) - statistics.fmean(a))
        say("  noise floor |%s - %s|: %s" % (nz[1], nz[0], ", ".join("%s %.2f m" % (k, v) for k, v in noise.items())))
    return noise


def ab_test(recs, D, A, B, crit, noise_labels=None, block_arg="auto", perm=5000, seed=1, say=print):
    """block permutation test of B - A per team, the without-ball control and the pool; verdict against crit"""
    mt = crit["metric"]
    block, underblocked = auto_block(D, recs, mt, A, B, block_arg, say)
    say("\nTEST  metric %s  A=%s  B=%s  block %d samples  %d permutations" % (mt, A, B, block, perm))
    results, without = {}, {}
    for tm in (0, 1):
        r = perm_test(blocks_of(recs, A, tm, mt, block), blocks_of(recs, B, tm, mt, block), perm, seed)
        if r:
            results[tm] = r
            say("  team %d: B - A = %+6.2f m  p = %.3f  MDE95 = %.2f m  n_A %d n_B %d (blocks %d / %d)" % (
                tm, r["obs"], r["p"], r["mde95"], r["n_a"], r["n_b"], r["blocks_a"], r["blocks_b"]))
        if mt == "width_with":
            w = perm_test(blocks_of(recs, A, tm, "width_without", block), blocks_of(recs, B, tm, "width_without", block), perm, seed)
            if w:
                without[tm] = w
                say("          without-ball width B - A = %+6.2f m  p = %.3f" % (w["obs"], w["p"]))
    pa = blocks_of(recs, A, 0, mt, block) + blocks_of(recs, A, 1, mt, block)
    pb = blocks_of(recs, B, 0, mt, block) + blocks_of(recs, B, 1, mt, block)
    rp = perm_test(pa, pb, perm, seed)
    if rp:
        results["pooled"] = rp
        say("  pooled: B - A = %+6.2f m  p = %.3f  MDE95 = %.2f m  n_A %d n_B %d" % (rp["obs"], rp["p"], rp["mde95"], rp["n_a"], rp["n_b"]))
    if mt == "width_with":  # the pooled without-ball control (the pooled criterion's 'without < half')
        wp = perm_test(blocks_of(recs, A, 0, "width_without", block) + blocks_of(recs, A, 1, "width_without", block),
                       blocks_of(recs, B, 0, "width_without", block) + blocks_of(recs, B, 1, "width_without", block), perm, seed)
        if wp:
            without["pooled"] = wp
            say("          pooled without-ball width B - A = %+6.2f m  p = %.3f" % (wp["obs"], wp["p"]))
    noise = noise_floor(recs, mt, [w for w in (noise_labels or A) if w], say)
    v, checks, reasons = verdict(results, crit, noise, without, underblocked)
    return {"verdict": v, "checks": checks, "reasons": reasons, "results": results, "without": without, "noise": noise,
            "block": block, "underblocked": underblocked}


# ------------------------------------------------------------------ gate X1 (docs/TACTICS_OVERHAUL_DECISION.md 4.1)
# Pre-registered (decision 4.1): with-ball width of BOTH teams in B1 and in B2 is lower than in A1 and A2 by >= 3 m and >= 2 x |A1 - A2|,
# with without-ball width moving less than half of that; p < 0.05 on the block permutation test. "B1 only" = the same for B1 alone.
# Implemented per B window k: block permutation test B_k vs A1+A2 per team with this criterion AND B_k - A_i <= -3 m for EACH A window
# and each team. Outcome (table 4.1): 1 = B1 and B2 pass, 2 = B1 passes and B2 does not, 3 = B1 does not pass.
X1_CRITERION = "width_with_ball both_teams -3m 2x_noise p<0.05 without_half"
X1_SECONDARY = "width_with_ball pooled -8m 2x_noise p<0.05 without_half"  # sweep T4 5.3: printed, never decides
X1_WINDOWS = ("B1", "A1", "B2", "A2")
X1_BRANCH = {1: "read per tick / live: `live: true` for this group; sliders usable in match; the three apply moments stay",
             2: "read at match setup or earlier: automatic moments only (boot / career_load / hub); mid-match UI greyed",
             3: "neither: second launch per table 4.1 row 3 (runs.frequency if located / longer width window / boot apply); "
                "if still nothing: R-G1-1, escalate"}


def window_mean(recs, label, team, metric):
    xs = [r[metric] for r in recs if r["win"] == label and r["team"] == team and r.get(metric) is not None]
    return statistics.fmean(xs) if xs else None


def x1_decide(recs, perm=5000, seed=1, block_arg="auto", baseline=None, say=print):
    """-> dict(outcome 1/2/3, verdict PASS/FAIL/INCONCLUSIVE, per-window results, notes). baseline: records of a 1.0 reference run"""
    crit = parse_criterion(X1_CRITERION)
    need = abs(crit["shift"])
    present = {w: sum(1 for r in recs if r["win"] == w) for w in X1_WINDOWS}
    say("X1 windows (team-samples after filters): %s" % ", ".join("%s %d" % kv for kv in present.items()))
    missing = [w for w, n in present.items() if n == 0]
    if missing:
        return {"outcome": 3, "verdict": "INCONCLUSIVE", "per": {}, "branch": X1_BRANCH[3],
                "notes": ["window(s) %s have no samples" % ", ".join(missing)]}
    D = diag(recs)
    per = {}
    for bw in ("B1", "B2"):
        say("\n== %s vs A1+A2  criterion: %s  (+ %s lower than EACH A window by >= %.0f m, both teams)" % (bw, X1_CRITERION, bw, need))
        res = ab_test(recs, D, ["A1", "A2"], [bw], crit, ["A1", "A2"], block_arg, perm, seed, say)
        each, ok_each = [], True
        for tm in (0, 1):
            mb = window_mean(recs, bw, tm, "width_with")
            for aw in ("A1", "A2"):
                ma = window_mean(recs, aw, tm, "width_with")
                d = None if mb is None or ma is None else mb - ma
                each.append({"team": tm, "a": aw, "diff": d})
                if d is None or d > -need:
                    ok_each = False
        say("  per A window: " + ", ".join("team %d %s-%s %s" % (e["team"], bw, e["a"], "%+.2f m" % e["diff"] if e["diff"] is not None
                                                                   else "n/a") for e in each))
        reasons = list(res["reasons"])
        passed = res["verdict"] == "PASS" and ok_each
        if res["verdict"] == "PASS" and not ok_each:
            reasons.append("%s is not lower than EACH of A1 and A2 by >= %.0f m in both teams" % (bw, need))
        v = "PASS" if passed else ("INCONCLUSIVE" if res["verdict"] == "PASS" else res["verdict"])
        say("  %s: %s%s" % (bw, v, "" if passed else "  (" + "; ".join(reasons) + ")"))
        per[bw] = {"pass": passed, "verdict": v, "reasons": reasons, "each": each,
                   "shift": {str(k): r["obs"] for k, r in res["results"].items()},
                   "p": {str(k): r["p"] for k, r in res["results"].items()},
                   "without": {str(k): r["obs"] for k, r in res["without"].items()},
                   "noise": {str(k): x for k, x in res["noise"].items()}}
    b1, b2 = per["B1"]["pass"], per["B2"]["pass"]
    notes = []
    if b1 and b2:
        outcome = 1
    elif b1:
        outcome = 2
        if per["B2"]["verdict"] != "FAIL":
            notes.append("B2 is %s, not a proven 'no live effect': branch 2 is the conservative reading" % per["B2"]["verdict"])
    else:
        outcome = 3
        if b2:
            notes.append("B2 passes but B1 does not: contradictory (sweep T4 5.4) - repeat with longer windows before concluding")
    if outcome in (1, 2):
        verdict_ = "PASS"
    elif per["B1"]["verdict"] == "FAIL" and per["B2"]["verdict"] == "FAIL":
        verdict_ = "FAIL"
    else:
        verdict_ = "INCONCLUSIVE"
    sec = ab_test(recs, D, ["A1", "A2"], ["B1", "B2"], parse_criterion(X1_SECONDARY), ["A1", "A2"], block_arg, perm, seed,
                  say=lambda *a: None)
    rp = sec["results"].get("pooled")
    if rp:
        say("\nsecondary (sweep T4 5.3, informative): pooled B1+B2 - A1+A2 = %+.2f m p %.3f MDE95 %.2f m -> %s" % (
            rp["obs"], rp["p"], rp["mde95"], sec["verdict"]))
    out = {"outcome": outcome, "verdict": verdict_, "per": per, "notes": notes, "branch": X1_BRANCH[outcome],
           "secondary": {"verdict": sec["verdict"], "pooled_shift": rp["obs"] if rp else None, "p": rp["p"] if rp else None}}
    # Advisory (never changes the outcome): a value read ONCE at match setup gives NO B - A difference inside one match (the A
    # windows keep the setup value too), i.e. outcome 3; only a match-level comparison against a 1.0 run can show it.
    if baseline:
        adv = []
        for tm in (0, 1):
            xb = [r["width_with"] for r in baseline if r["team"] == tm and r.get("width_with") is not None]
            xs = [r["width_with"] for r in recs if r["win"] in X1_WINDOWS and r["team"] == tm and r.get("width_with") is not None]
            if xb and xs:
                adv.append((tm, statistics.fmean(xs) - statistics.fmean(xb), len(xb)))
        say("\nbaseline (1.0 reference run, another match): " + ", ".join("team %d all-window mean - baseline %+.2f m (n %d)" % a
                                                                            for a in adv))
        out["baseline"] = [{"team": a[0], "diff": a[1], "n": a[2]} for a in adv]
        if outcome == 3 and len(adv) == 2 and all(a[1] <= -max(need, 5.0) for a in adv):
            notes.append("SETUP-LATCH SUSPECT: all four windows' with-ball width is >= %.0f m below the 1.0 baseline in both teams: "
                         "consistent with the value being read ONCE at match setup (the in-match ABAB cannot show that); "
                         "confirm with a 1.0-at-kick-off control match" % max(need, 5.0))
    return out


# ------------------------------------------------------------------ gate G-T1 (tactics, extreme values; PLAN_tactics_settings.md section 2)
# Pre-registered reading (ONE reading, written here so TEST_31 can copy it): per team, the mean WITH-BALL team length (length_with) and
# width (width_with) of window B (extreme values written) and window A (defaults restored), after the stoppage / max-dt filters, each
# compared with the VANILLA with-ball means given explicitly (--gt1-vanilla; no default, a missing baseline is INCONCLUSIVE).
#   B passes   : in BOTH teams length_B <= 0.60 x vanilla length AND width_B <= 0.70 x vanilla width
#   A returns  : in BOTH teams length_A >= 0.85 x vanilla length AND width_A >= 0.85 x vanilla width
#   PASS-live  : B passes and A returns            -> VERDICT PASS
#   PASS-setup : B passes and A does not return    -> VERDICT PASS (the value is latched at setup: no mid-match moment)
#   FAIL       : B does NOT compress in ANY team x metric (every B ratio >= 0.85)  -> VERDICT FAIL
#   anything else (partial compression, one team only, fewer than --gt1-min-n with-ball samples in a window-team) -> INCONCLUSIVE,
#   counted as FAIL for route (a) by the plan.
GT1_LEN_MAX, GT1_WID_MAX, GT1_A_MIN, GT1_FAIL_MIN = 0.60, 0.70, 0.85, 0.85
GT1_BRANCH = {"PASS-live": "(a) ships sliders for the two knobs (live: true); (b) still built for arrays/curves, gated by G-T1b",
              "PASS-setup": "(a) sliders with hub / career-load / boot moments only, mid-match greyed; (b) as above",
              "FAIL": "(a) sliders stay read-only for good; everything rides on (b) (G-T1b)",
              "INCONCLUSIVE": "counted as FAIL for (a) (plan section 2)"}


def parse_gt1_vanilla(text):
    """'length=60,width=52' (both teams) or 'team0=60/52,team1=61/50' (length/width) -> {0: (L, W), 1: (L, W)}; ValueError otherwise"""
    out = {}
    parts = [x.strip() for x in text.split(",") if x.strip()]
    kv = dict(x.split("=", 1) for x in parts if "=" in x)
    if "length" in kv and "width" in kv:
        out = {0: (float(kv["length"]), float(kv["width"])), 1: (float(kv["length"]), float(kv["width"]))}
    else:
        for tm in (0, 1):
            if "team%d" % tm in kv:
                a, b = kv["team%d" % tm].split("/")
                out[tm] = (float(a), float(b))
    if set(out) != {0, 1} or any(v <= 0 for t in out.values() for v in t):
        raise ValueError("--gt1-vanilla needs length=M,width=M or team0=L/W,team1=L/W (positive metres)")
    return out


def gt1_decide(recs, vanilla, min_n=20, say=print):
    """-> {outcome, verdict, branch, rows, notes}; recs = per_sample records after filters (win 'B' / 'A')"""
    notes, rows = [], {}
    if not vanilla:
        return {"outcome": "INCONCLUSIVE", "verdict": "INCONCLUSIVE", "branch": GT1_BRANCH["INCONCLUSIVE"], "rows": rows,
                "notes": ["no vanilla baseline given (--gt1-vanilla): the ratios cannot be computed"]}
    for w in ("B", "A"):
        for tm in (0, 1):
            L = [r["length_with"] for r in recs if r["win"] == w and r["team"] == tm and r.get("length_with") is not None]
            W = [r["width_with"] for r in recs if r["win"] == w and r["team"] == tm and r.get("width_with") is not None]
            rows[(w, tm)] = {"n": len(L), "length": statistics.fmean(L) if L else None, "width": statistics.fmean(W) if W else None}
            vl, vw = vanilla[tm]
            if L:
                rows[(w, tm)].update(len_ratio=rows[(w, tm)]["length"] / vl, wid_ratio=rows[(w, tm)]["width"] / vw)
            say("G-T1 window %s team %d: n %d  length %s (%s of vanilla)  width %s (%s of vanilla)" % (
                w, tm, len(L), "%.1f" % rows[(w, tm)]["length"] if L else "-", "%.0f%%" % (100 * rows[(w, tm)]["len_ratio"]) if L else "-",
                "%.1f" % rows[(w, tm)]["width"] if W else "-", "%.0f%%" % (100 * rows[(w, tm)]["wid_ratio"]) if L else "-"))
    short = ["%s/team %d (n %d)" % (w, tm, v["n"]) for (w, tm), v in rows.items() if v["n"] < min_n]
    if short:
        notes.append("fewer than %d with-ball samples in %s" % (min_n, ", ".join(short)))
        return {"outcome": "INCONCLUSIVE", "verdict": "INCONCLUSIVE", "branch": GT1_BRANCH["INCONCLUSIVE"], "rows": rows, "notes": notes}
    b_pass = all(rows[("B", t)]["len_ratio"] <= GT1_LEN_MAX and rows[("B", t)]["wid_ratio"] <= GT1_WID_MAX for t in (0, 1))
    a_back = all(rows[("A", t)]["len_ratio"] >= GT1_A_MIN and rows[("A", t)]["wid_ratio"] >= GT1_A_MIN for t in (0, 1))
    b_none = all(rows[("B", t)][k] >= GT1_FAIL_MIN for t in (0, 1) for k in ("len_ratio", "wid_ratio"))
    if b_pass and a_back:
        oc = "PASS-live"
    elif b_pass:
        oc = "PASS-setup"
        notes.append("window A stayed compressed: the value is latched at setup (or the revert did not reach the table)")
    elif b_none:
        oc = "FAIL"
    else:
        oc = "INCONCLUSIVE"
        notes.append("partial compression: B passes %s (length <= %.0f%% and width <= %.0f%% of vanilla in both teams needed)" % (
            b_pass, 100 * GT1_LEN_MAX, 100 * GT1_WID_MAX))
    verdict_ = "PASS" if oc.startswith("PASS") else ("FAIL" if oc == "FAIL" else "INCONCLUSIVE")
    return {"outcome": oc, "verdict": verdict_, "branch": GT1_BRANCH[oc], "rows": rows, "notes": notes}


def x1_deviations(args):
    """review minor 9: the analysis settings of --x1 that differ from the pre-registered ones (decision 4.1 + sweep T4 defaults)"""
    dev = []
    for name, want in (("stratum", ""), ("stoppage", None), ("ball_still", None), ("drop_dead_ball", False), ("max_dt", 1.5),
                       ("block", "auto"), ("perm", 5000), ("seed", 1), ("length", L_DEFAULT), ("width", W_DEFAULT)):
        if getattr(args, name) != want:
            dev.append("--%s %s" % (name.replace("_", "-"), getattr(args, name)))
    return dev


def load_recs(specs, csv_path=None, length=L_DEFAULT, width=W_DEFAULT):
    """--win specs (LABEL=DIR | LABEL=DIR:S-E | LABEL=FILE.csv) and/or one continuous positions.csv whose 'win' column labels
    the rows (every label kept; per_sample computes dt within a label only)"""
    recs = []
    for spec in specs:
        label, src, rng = parse_win(spec)
        S = load_window(label, src, rng)
        if not S:
            print("window %s: no samples" % label)
            continue
        recs.extend(per_sample(S, length, width))
    if csv_path:
        path = csv_path if csv_path.lower().endswith(".csv") else os.path.join(csv_path, "positions.csv")
        S = load_positions(path)
        for s in S:
            s["win"] = s.get("win") or "run"
        recs.extend(per_sample(S, length, width))
    return recs


# ------------------------------------------------------------------ main
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--win", action="append", default=[], help="LABEL=DIR | LABEL=DIR:START-END | LABEL=FILE.csv")
    ap.add_argument("--csv", default=None, help="one continuous positions.csv (or its run dir) with a 'win' column (match_measure --marker)")
    ap.add_argument("--x1", action="store_true", help="gate X1 decision (decision 4.1): windows B1 A1 B2 A2 -> OUTCOME 1/2/3")
    ap.add_argument("--x1-baseline", action="append", default=[], help="a 1.0 reference run (DIR or CSV) for the setup-latch advisory")
    ap.add_argument("--gt1", action="store_true", help="gate G-T1 decision (windows B, A in --csv): see the GT1 block; needs --gt1-vanilla")
    ap.add_argument("--gt1-vanilla", default="", help="vanilla with-ball means in metres: length=M,width=M or team0=L/W,team1=L/W (from TEST_24 control windows)")
    ap.add_argument("--gt1-vanilla-csv", default="", help="a vanilla run (positions.csv or its dir): the vanilla means computed through the SAME per_sample / filters (all rows of that run)")
    ap.add_argument("--gt1-min-n", type=int, default=20, help="minimum with-ball samples per window and team (default 20)")
    ap.add_argument("--A", default="", help="comma list of A window labels")
    ap.add_argument("--B", default="", help="comma list of B window labels")
    ap.add_argument("--noise", default="", help="two A-like labels for the noise floor (default: the A windows)")
    ap.add_argument("--criterion", default="width_with_ball both_teams +3m 2x_noise p<0.05")
    ap.add_argument("--block", default="auto", help="block length in samples, or auto (= ceil(2 tau) of the metric, min 3)")
    ap.add_argument("--perm", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--stoppage", type=float, default=None, help="drop a sample when every player moved slower than X m/s")
    ap.add_argument("--ball-still", type=float, default=None, help="drop a sample when the ball moved slower than X m/s")
    ap.add_argument("--drop-dead-ball", action="store_true", help="drop samples with no carrier")
    ap.add_argument("--max-dt", type=float, default=None, help="drop samples after a gap longer than X s (X1 default 1.5)")
    ap.add_argument("--stratum", default="", help="e.g. poss=own,third=mid (any = no restriction)")
    ap.add_argument("--length", type=float, default=L_DEFAULT)
    ap.add_argument("--width", type=float, default=W_DEFAULT)
    ap.add_argument("--diag", action="store_true", help="print the noise-floor table")
    ap.add_argument("--strata-table", action="store_true", help="print width by possession x third per window")
    ap.add_argument("--json", default=None)
    ap.add_argument("--power", default=None, metavar="METRIC", help="print the samples-needed table for this metric")
    ap.add_argument("--self-test", action="store_true", help="offline proof on synthetic data (no game)")
    args = ap.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.win and not args.csv:
        ap.error("at least one --win or --csv")
    if (args.x1 or args.gt1) and args.max_dt is None:
        args.max_dt = 1.5
    if args.gt1 and args.stoppage is None:
        args.stoppage = 0.5  # G-T1: stoppages dropped (plan section 2)
    recs = load_recs(args.win, args.csv, args.length, args.width)
    strata = {}
    if args.stratum:
        for kv in args.stratum.split(","):
            k, v = kv.split("=")
            strata[k.strip()] = v.strip()
    raw_n = len(recs)
    recs = apply_filters(recs, args.stoppage, args.ball_still, args.drop_dead_ball, args.max_dt, strata)
    print("records: %d team-samples after filters (%d before)" % (len(recs), raw_n))
    if not recs:
        return finish("INCONCLUSIVE", ["no records"])
    wins = sorted(set(r["win"] for r in recs), key=lambda w: min(r["t"] for r in recs if r["win"] == w))
    out = {"windows": wins, "filters": {"stoppage": args.stoppage, "ball_still": args.ball_still, "drop_dead": args.drop_dead_ball,
                                        "max_dt": args.max_dt, "strata": strata}}
    D = diag(recs)
    if args.diag or not ((args.A and args.B) or args.x1):
        print("\nNOISE FLOOR  (per window, per team; sd = per-sample sd; r1 = lag-1 autocorrelation; tau = integrated "
              "autocorrelation time in samples; ESS = n / tau)")
        print("%-14s %-4s %-16s %4s %8s %7s %6s %6s %6s" % ("window", "team", "metric", "n", "mean", "sd", "r1", "tau", "ESS"))
        for (w, tm, mt), v in D.items():
            if mt in ("centroid",):
                continue
            print("%-14s %-4d %-16s %4d %8.2f %7.2f %6s %6s %6s" % (w, tm, mt, v["n"], v["mean"], v["sd"],
                  "%.2f" % v["r1"] if v["r1"] is not None else "-", "%.1f" % v["tau"] if v["tau"] else "-",
                  "%.1f" % v["ess"] if v["ess"] else "-"))
    if args.strata_table:
        print("\nSTRATA  width (m) by possession x ball third, per window per team: mean (n)")
        for w in wins:
            for tm in (0, 1):
                cells = []
                for ps in ("own", "opp", "dead"):
                    for th in ("def", "mid", "att"):
                        xs = [r["width"] for r in recs if r["win"] == w and r["team"] == tm and r["poss"] == ps and r["third"] == th]
                        cells.append("%s/%s %s" % (ps, th, ("%.1f(%d)" % (statistics.fmean(xs), len(xs))) if xs else "-"))
                print("%-14s team %d  %s" % (w, tm, "  ".join(cells)))
    out["diag"] = {"%s|%d|%s" % k: v for k, v in D.items()}
    if args.power:
        print("")
        print("POWER  samples per condition per team needed to detect a shift d (two-sided alpha 0.05, power 0.8),")
        print("       n_eff = 2 (1.96 + 0.84)^2 (sd / d)^2; samples = n_eff x tau / share (share = fraction of samples in the")
        print("       stratum, e.g. with the ball); real seconds = samples x mean dt. Per window per team, metric %s:" % args.power)
        print("%-14s %-4s %6s %5s %5s | %s" % ("window", "team", "sd", "tau", "share", "  ".join("d=%4.1fm" % d for d in (2, 3, 5, 8, 12))))
        for (w, tm, mt), v in D.items():
            if mt != args.power:
                continue
            ntot = sum(1 for r in recs if r["win"] == w and r["team"] == tm)
            share = v["n"] / ntot if ntot else 1.0
            cells = []
            for d in (2, 3, 5, 8, 12):
                neff = 2 * (1.96 + 0.84) ** 2 * (v["sd"] / d) ** 2
                cells.append("%7.0f" % (neff * (v["tau"] or 1) / share))
            print("%-14s %-4d %6.2f %5.1f %5.2f | %s" % (w, tm, v["sd"], v["tau"] or 1, share, "  ".join(cells)))
    if args.gt1:
        van = None
        try:
            if args.gt1_vanilla_csv:
                vr = apply_filters(load_recs([], args.gt1_vanilla_csv, args.length, args.width), args.stoppage, args.ball_still, args.drop_dead_ball,
                                   args.max_dt, strata)
                van = {}
                for tm in (0, 1):
                    L_ = [r["length_with"] for r in vr if r["team"] == tm and r.get("length_with") is not None]
                    W_ = [r["width_with"] for r in vr if r["team"] == tm and r.get("width_with") is not None]
                    if len(L_) >= args.gt1_min_n:
                        van[tm] = (statistics.fmean(L_), statistics.fmean(W_))
                if set(van) != {0, 1}:
                    print("vanilla run: fewer than %d with-ball samples for a team" % args.gt1_min_n)
                    van = None
                else:
                    print("vanilla from %s (same filters): " % args.gt1_vanilla_csv + ", ".join("team %d length %.1f width %.1f" % (t, *van[t]) for t in (0, 1)))
            elif args.gt1_vanilla:
                van = parse_gt1_vanilla(args.gt1_vanilla)
        except ValueError as e:
            print(e)
            van = None
        print("FILTERS    with-ball samples only (carrier on the team); stoppage %s m/s, max-dt %s s, ball-still %s, drop-dead-ball %s, frozen samples dropped; "
              "TEST_31's vanilla numbers must be computed with these same filters (or use --gt1-vanilla-csv)" % (
                  args.stoppage, args.max_dt, args.ball_still, args.drop_dead_ball))
        res = gt1_decide(recs, van, args.gt1_min_n)
        print("\nCRITERION  B: length <= %.0f%% and width <= %.0f%% of vanilla (both teams); A returns >= %.0f%%; FAIL = no B ratio < %.0f%%" % (
            100 * GT1_LEN_MAX, 100 * GT1_WID_MAX, 100 * GT1_A_MIN, 100 * GT1_FAIL_MIN))
        print("OUTCOME %s  - %s" % (res["outcome"], res["branch"]))
        out["gt1"] = {k: (v if k != "rows" else {"%s|%d" % kk: vv for kk, vv in v.items()}) for k, v in res.items()}
        if args.json:
            json.dump(out, open(args.json, "w"), indent=1, default=str)
        return finish(res["verdict"], res["notes"])
    if args.x1:
        base = []
        for b in args.x1_baseline:
            if os.path.exists(b if b.lower().endswith(".csv") else os.path.join(b, "positions.csv")):
                base.extend(apply_filters(load_recs([], b, args.length, args.width), max_dt=args.max_dt))  # every row
            else:
                print("baseline %s: no positions.csv, skipped" % b)
        devs = x1_deviations(args)
        if devs:
            print("NOT PRE-REGISTERED: the X1 analysis runs with %s (pre-registered: no stratum / stoppage / ball-still / dead-ball "
                  "filter, max-dt 1.5, block auto, 5000 permutations, seed 1)" % ", ".join(devs))
        res = x1_decide(recs, args.perm, args.seed, args.block, base or None)
        res["preregistered"] = not devs
        print("\nCRITERION  %s  (%s)" % (X1_CRITERION, "decision 4.1, pre-registered" if not devs else
                                          "NOT PRE-REGISTERED: " + ", ".join(devs)))
        print("OUTCOME %d  - %s" % (res["outcome"], res["branch"]))
        out["x1"] = res
        if args.json:
            json.dump(out, open(args.json, "w"), indent=1, default=str)
        return finish(res["verdict"], res["notes"])
    if not (args.A and args.B):
        if args.json:
            json.dump(out, open(args.json, "w"), indent=1, default=str)
        return 0
    A = [w for w in args.A.split(",") if w]
    B = [w for w in args.B.split(",") if w]
    crit = parse_criterion(args.criterion)
    if crit["metric"] not in METRICS and crit["metric"] not in ("length_with", "length_without"):
        print("unknown metric", crit["metric"])
        return 2
    res = ab_test(recs, D, A, B, crit, [w for w in args.noise.split(",") if w] or A, args.block, args.perm, args.seed)
    print("\nCRITERION  %s" % args.criterion)
    out.update({"criterion": crit, "block": res["block"], "results": {str(k): v for k, v in res["results"].items()},
                "without": {str(k): v for k, v in res["without"].items()}, "noise": {str(k): v for k, v in res["noise"].items()},
                "verdict": res["verdict"], "checks": {str(k): v for k, v in res["checks"].items()}, "reasons": res["reasons"]})
    if args.json:
        json.dump(out, open(args.json, "w"), indent=1, default=str)
    return finish(res["verdict"], res["reasons"])


def finish(v, reasons):
    for rsn in reasons:
        print("  - " + rsn)
    print("VERDICT    %s" % v)
    sys.stdout.flush()
    return 0 if v == "PASS" else (1 if v == "FAIL" else 3)


# ------------------------------------------------------------------ self-test (synthetic data; the model of sweep T4 synth_test.py)
BASE_LAT = [0, -12, -4, 4, 12, -15, -5, 5, 15, -8, 8]  # GK + 10 outfielders lateral slots (m)
BASE_DEP = [2, 20, 18, 18, 20, 35, 33, 33, 35, 48, 46]  # depth from own goal (m)
SYNTH_HEADER = ["t", "sample", "handle", "team", "fpos", "pid", "x", "y", "z", "ball_x", "ball_y", "ball_z", "carrier", "win",
                "ball_vx", "ball_vy", "ball_vz", "clock"]


class Synth:
    """synthetic 1 Hz match: AR(1) lateral noise (r 0.8, per-player sd 4 m), possession switching about every 6 s; the
    in-possession lateral spread is scaled by the knob factor (factor_without scales the out-of-possession spread)"""

    def __init__(self, seed, ar=0.8):
        self.rng = random.Random(seed)
        self.ar = ar  # lag-1 autocorrelation of the per-player lateral noise (T4 synth 0.8; the 10-13 width series r1 0.6-0.75)
        self.noise = {(tm, k): 0.0 for tm in (0, 1) for k in range(11)}
        self.dnoise = {(tm, k): 0.0 for tm in (0, 1) for k in range(11)}
        self.poss, self.ballx, self.t, self.i = 0, 0.0, 0.0, 0

    def sample(self, factor=1.0, factor_without=1.0, dt=1.0):
        """-> (t, [(handle, team, fpos, x, z)], ball (x, y, z), carrier handle)"""
        rng = self.rng
        self.t += dt + rng.gauss(0, 0.05)
        if rng.random() < 1 / 6:
            self.poss = 1 - self.poss
        self.ballx = max(-50, min(50, self.ballx + rng.gauss(0, 6)))
        pl = []
        for tm in (0, 1):
            s = 1.0 if tm == 0 else -1.0
            spread = (1.15 * factor) if tm == self.poss else (0.9 * factor_without)
            for k in range(11):
                self.noise[(tm, k)] = self.ar * self.noise[(tm, k)] + rng.gauss(0, 4 * math.sqrt(1 - self.ar ** 2))
                self.dnoise[(tm, k)] = 0.85 * self.dnoise[(tm, k)] + rng.gauss(0, 3 * math.sqrt(1 - 0.72))
                lat = BASE_LAT[k] * spread + self.noise[(tm, k)]
                dep = BASE_DEP[k] + self.dnoise[(tm, k)] + 0.25 * (s * self.ballx + 52.5)
                pl.append((tm * 25 + k, tm, 0 if k == 0 else k + 3, s * (52.5 - dep), lat))
        self.i += 1
        return self.t, pl, (self.ballx, 0.1, rng.gauss(0, 10)), self.poss * 25 + 9

    def rows(self, label, factor=1.0, factor_without=1.0, dt=1.0):
        t, pl, ball, car = self.sample(factor, factor_without, dt)
        return [["%.3f" % t, self.i - 1, h, tm, fp, "", "%.3f" % x, "0.000", "%.3f" % z, "%.3f" % ball[0], "%.3f" % ball[1],
                 "%.3f" % ball[2], car, label, "nan", "nan", "nan", ""] for h, tm, fp, x, z in pl]


def synth_csv(path, plan, seed):
    """plan: [(label, seconds, factor, factor_without)] -> one continuous positions.csv with a win column"""
    g = Synth(seed)
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(SYNTH_HEADER)
        for label, secs, fac, facw in plan:
            for _ in range(secs):
                w.writerows(g.rows(label, fac, facw))
    return path


def x1_plan(b1, a1, b2, a2, secs=90, without=1.0, settle=5):
    return [("pre", 5, b1, 1.0), ("B1", secs, b1, without if b1 != 1.0 else 1.0), ("x", settle, 1.0, 1.0), ("A1", secs, a1, 1.0),
            ("x", settle, b2, 1.0), ("B2", secs, b2, without if b2 != 1.0 else 1.0), ("x", settle, 1.0, 1.0), ("A2", secs, a2, 1.0)]


def self_test():
    import contextlib, io, tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    def x1(path, *extra):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--x1", "--csv", path, "--perm", "400", "--json", path + ".json"] + list(extra))
        txt = buf.getvalue()
        oc = [l for l in txt.splitlines() if l.startswith("OUTCOME")]
        res = json.load(open(path + ".json"))["x1"] if os.path.exists(path + ".json") else {}
        return rc, (int(oc[0].split()[1]) if oc else None), txt, res

    with tempfile.TemporaryDirectory() as d:
        p = synth_csv(os.path.join(d, "live.csv"), x1_plan(0.6, 1.0, 0.6, 1.0), 1)
        rc, oc, txt, _ = x1(p)
        check(rc == 0 and oc == 1 and "VERDICT    PASS" in txt, "0.6 in B1 and B2 (engine reads live): OUTCOME 1, PASS (exit %d)" % rc)
        p = synth_csv(os.path.join(d, "b1only.csv"), x1_plan(0.6, 1.0, 1.0, 1.0), 2)
        rc, oc, txt, _ = x1(p)
        check(rc == 0 and oc == 2, "0.6 in B1 only: OUTCOME 2, PASS (exit %d, outcome %s)" % (rc, oc))
        nulls = {}
        for s in range(3):
            p = synth_csv(os.path.join(d, "null%d.csv" % s), x1_plan(1.0, 1.0, 1.0, 1.0), 100 + s)
            rc, oc, txt, _ = x1(p)
            nulls[s] = (rc, oc)
        check(all(oc == 3 and rc != 0 for rc, oc in nulls.values()), "3 null runs (1.0 everywhere): OUTCOME 3, never PASS %s" % nulls)
        p = synth_csv(os.path.join(d, "b2only.csv"), x1_plan(1.0, 1.0, 0.6, 1.0), 3)
        rc, oc, txt, res = x1(p)
        check(oc == 3 and rc == 3 and any("contradictory" in n for n in res.get("notes", [])),
              "0.6 in B2 only: OUTCOME 3, INCONCLUSIVE, contradictory note (exit %d)" % rc)
        p = synth_csv(os.path.join(d, "state.csv"), x1_plan(0.6, 1.0, 0.6, 1.0, without=0.6), 4)
        rc, oc, txt, _ = x1(p)
        check(oc == 3 and rc != 0 and "without-ball shift" in txt, "without-ball width moves too (game state): not a PASS (exit %d)" % rc)
        p = synth_csv(os.path.join(d, "small.csv"), x1_plan(0.97, 1.0, 0.97, 1.0), 5)
        rc, oc, txt, _ = x1(p)
        check(oc == 3 and rc != 0, "a tiny effect (0.97, about -1 m): below the 3 m criterion, not a PASS (exit %d)" % rc)
        p = synth_csv(os.path.join(d, "latch.csv"), x1_plan(0.6, 0.6, 0.6, 0.6), 6)
        b = synth_csv(os.path.join(d, "base.csv"), [("A", 120, 1.0, 1.0)], 7)
        rc, oc, txt, res = x1(p, "--x1-baseline", b)
        check(oc == 3 and any("SETUP-LATCH SUSPECT" in n for n in res.get("notes", [])),
              "value latched at setup (0.6 in every window) + 1.0 baseline: OUTCOME 3 with the setup-latch advisory")
        p = synth_csv(os.path.join(d, "short.csv"), x1_plan(0.6, 1.0, 0.6, 1.0, secs=6), 8)
        rc, oc, txt, _ = x1(p)
        check(rc == 3 and oc != 1, "6 s windows (too few blocks): INCONCLUSIVE, no PASS (exit %d)" % rc)
        rc, oc, txt, res = x1(os.path.join(d, "live.csv"), "--stratum", "poss=own")
        check("NOT PRE-REGISTERED" in txt and "--stratum poss=own" in txt and res.get("preregistered") is False,
              "review minor 9: an extra filter in --x1 is labelled NOT PRE-REGISTERED")
        ns = argparse.Namespace(stratum="", stoppage=None, ball_still=None, drop_dead_ball=False, max_dt=1.5, block="auto", perm=5000,
                                seed=1, length=L_DEFAULT, width=W_DEFAULT)
        check(x1_deviations(ns) == [], "the default --x1 settings are the pre-registered ones")
        r_ = {"obs": -10.0, "p": 0.001, "mde95": 2.0, "blocks_a": 9, "blocks_b": 9, "mean_a": 40.0, "mean_b": 30.0, "n_a": 50, "n_b": 50}
        w_ = {"obs": 0.1, "p": 0.9}
        v, _, why = verdict({0: r_, 1: r_}, parse_criterion(X1_CRITERION), {0: 1.0, 1: 1.0}, {0: w_})
        check(v != "PASS" and any("without-ball control missing for team 1" in x for x in why),
              "review minor 10: a missing without-ball control is not a pass")
        v, _, _ = verdict({0: r_, 1: r_}, parse_criterion(X1_CRITERION), {0: 1.0, 1: 1.0}, {0: w_, 1: w_})
        check(v == "PASS", "... and with both controls present the same numbers pass")
        p = synth_csv(os.path.join(d, "noA2.csv"), x1_plan(0.6, 1.0, 0.6, 1.0)[:-1], 9)
        rc, oc, txt, _ = x1(p)
        check(rc == 3 and oc == 3 and "A2" in txt, "window A2 missing: INCONCLUSIVE, OUTCOME 3")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--csv", os.path.join(d, "live.csv"), "--A", "A1,A2", "--B", "B1,B2", "--perm", "400",
                       "--criterion", "width_with_ball pooled -8m 2x_noise p<0.05 without_half"])
        check(rc == 0 and "VERDICT    PASS" in buf.getvalue(), "plain A/B mode (T4 command form, pooled -8 m): PASS")
        # ---- G-T1 (pre-registered reading, see the GT1 block)
        def gt1(L_b, W_b, L_a, W_a, n=40, teams_b=None, vanilla=None):
            recs = []
            for w, (Lx, Wx) in (("B", (L_b, W_b)), ("A", (L_a, W_a))):
                for tm in (0, 1):
                    Lm, Wm = (teams_b[tm] if (teams_b and w == "B") else (Lx, Wx))
                    recs += [{"win": w, "team": tm, "length_with": Lm + (k % 3), "width_with": Wm - (k % 3)} for k in range(n)]
            return gt1_decide(recs, vanilla or {0: (50.0, 50.0), 1: (50.0, 50.0)}, say=lambda *a: None)
        check(gt1(25, 30, 49, 49)["outcome"] == "PASS-live", "G-T1: B compressed (50%/60%), A back at 98%: PASS-live")
        check(gt1(25, 30, 28, 33)["outcome"] == "PASS-setup", "G-T1: B compressed and A stays compressed: PASS-setup")
        check(gt1(49, 49, 49, 49)["outcome"] == "FAIL", "G-T1: B at ~98%/98% everywhere: FAIL")
        check(gt1(25, 30, 45, 42)["outcome"] == "PASS-setup", "G-T1: A at 90%/84% (width below 85%) is NOT 'returned': PASS-setup")
        check(gt1(25, 49, 49, 49)["outcome"] == "INCONCLUSIVE", "G-T1: length compresses, width does not: INCONCLUSIVE")
        check(gt1(25, 30, 49, 49, teams_b={0: (25, 30), 1: (49, 49)})["outcome"] == "INCONCLUSIVE", "G-T1: only one team compresses: INCONCLUSIVE")
        check(gt1(28, 33, 49, 49)["outcome"] == "PASS-live", "G-T1: just inside the edges (length ~58%, width ~64%): PASS-live")
        check(gt1(30, 33, 49, 49)["outcome"] == "INCONCLUSIVE", "G-T1: length ~62% (> 60%): not a pass, not a fail")
        check(gt1(25, 30, 49, 49, n=5)["outcome"] == "INCONCLUSIVE", "G-T1: 5 samples per window: INCONCLUSIVE")
        v = gt1_decide([], None, say=lambda *a: None)
        check(v["verdict"] == "INCONCLUSIVE" and "baseline" in v["notes"][0], "G-T1: no vanilla baseline: INCONCLUSIVE")
        check(parse_gt1_vanilla("length=60,width=52") == {0: (60.0, 52.0), 1: (60.0, 52.0)} and
              parse_gt1_vanilla("team0=60/52,team1=61/50")[1] == (61.0, 50.0), "G-T1: --gt1-vanilla forms parse")
        try:
            parse_gt1_vanilla("length=60")
            check(False, "G-T1: an incomplete --gt1-vanilla is refused")
        except ValueError:
            check(True, "G-T1: an incomplete --gt1-vanilla is refused")
        p = synth_csv(os.path.join(d, "gt1.csv"), [("B", 80, 0.5, 1.0), ("x", 5, 1.0, 1.0), ("A", 80, 1.0, 1.0)], 11)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--gt1", "--csv", p, "--gt1-vanilla", "length=40,width=50", "--json", p + ".json"])
        check(rc != 0 and "OUTCOME" in buf.getvalue(), "G-T1 CLI: synthetic width-only effect (length not compressed) is not a PASS (exit %d)" % rc)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--gt1", "--csv", p])
        check(rc == 3 and "no vanilla baseline" in buf.getvalue() and "FILTERS" in buf.getvalue(), "G-T1 CLI: no --gt1-vanilla: exit 3, the filter set is printed")
        pv = synth_csv(os.path.join(d, "van.csv"), [("V", 90, 1.0, 1.0)], 12)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = main(["--gt1", "--csv", p, "--gt1-vanilla-csv", pv])
        check("vanilla from" in buf.getvalue() and "OUTCOME" in buf.getvalue() and "no vanilla baseline" not in buf.getvalue(),
              "G-T1 CLI: --gt1-vanilla-csv computes the vanilla means through the same filters (exit %d)" % rc)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
