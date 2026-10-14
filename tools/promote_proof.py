"""promote_proof.py - Turbo 2.7: turns saved in-game evidence into the proof block of the gameplay tuning knobs file, for review. OFFLINE.

A knob becomes a slider only after (1) the deterministic engine-read check PASSED in game AND (2) a visible effect at an extreme value was seen
(tools/AGENT_RULES_25.md; scripts/knobs_guard.py enforces the evidence). This tool is the only thing that should edit those fields by hand-free
means: it reads an evidence folder with verdicts and, ONLY for a knob with engine == "PASS" AND effect == "PASS", writes into the knob
    "proof": {"session": <S>, "evidence": "EV/<date>/<folder>/<file>#<knob>", "note": ...}   (what scripts/knobs_guard.py requires)
    "engine_check": {"session", "evidence"}
    "slider": true
    and, when the verdict says the engine reads the value LIVE (mid-match write seen to take effect): "live": true, "applies": "live";
    otherwise (latched at match construction, written in the hub before kick-off): "applies": "next match setup"
It never sets "verified" (that is the locate run's), never touches a candidate, a knob that is not verified, a knob that already has a proof, or a
verdict that is INCONCLUSIVE / FAIL / NA / "effect only". The default run is a DRY RUN: it prints the refusals and a unified diff of the knobs file;
nothing is edited without --apply (and then the result is checked with the package guard and written atomically, LF).

Evidence folder (either or both):
  result.json     tools/battery_write.py's result ("rows": knob, engine, effect, live, decision)
  verdicts.json   {"session": "T29", "verdicts": [{"knob": "shape.def_line_range_max", "engine": "PASS", "effect": "PASS", "live": false,
                                                  "note": "free text"}]}      (checks the battery does not run: latched knobs, hub A/B)
A knob named in both with different verdicts is refused.

  python -I tools/promote_proof.py EVIDENCE_DIR --session S [--knobs FILE] [--ev-root DIR] [--knob ID ...] [--diff-out FILE] [--no-live] [--apply]
  --no-live: promote as latched whatever the verdict says (the way back when the in-game live test of a live promotion fails: revert the
             promotion commit, then repeat with --no-live --apply). It can only downgrade; it never sets live.
  python -I tools/promote_proof.py --self-test
Exit: 0 = done (dry run or applied), 1 = error / guard refused the result, 2 = usage, 3 = nothing could be promoted.
"""
import argparse, copy, difflib, importlib.util, json, os, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
KNOBS_DEFAULT = os.path.normpath(os.path.join(HERE, "..", "turbo", "package", "turbo", "tuning", "knobs_6AC07E31.json"))


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def guard_module():
    return _load("knobs_guard", os.path.join(HERE, "..", "scripts", "knobs_guard.py"))


def dumps(v, ind=0):
    """the knobs file's layout (the same as tools/knobs_v5.py dumps): 2-space indent, lists of plain values on one line"""
    pad = "  " * (ind + 1)
    if isinstance(v, dict):
        if not v:
            return "{}"
        return "{\n" + (",\n").join("%s%s: %s" % (pad, json.dumps(k, ensure_ascii=False), dumps(x, ind + 1)) for k, x in v.items()) + "\n" + "  " * ind + "}"
    if isinstance(v, list):
        if all(not isinstance(x, (dict, list)) for x in v) and len(json.dumps(v, ensure_ascii=False)) <= 100 + 100 * (ind == 1):
            return json.dumps(v, ensure_ascii=False)
        if not v:
            return "[]"
        return "[\n" + (",\n").join(pad + dumps(x, ind + 1) for x in v) + "\n" + "  " * ind + "]"
    return json.dumps(v, ensure_ascii=False)


# ------------------------------------------------------------------------------------------------------------- evidence
def find_ev_root(folder):
    """the nearest ancestor folder named 'evidence' (turbo_dev\\evidence): proof evidence is written relative to it as EV/..."""
    d = os.path.abspath(folder)
    while True:
        if os.path.basename(d).lower() == "evidence":
            return d
        up = os.path.dirname(d)
        if up == d:
            return None
        d = up


def read_verdicts(folder):
    """-> ({knob: {engine, effect, live, file, note, source}}, [problem lines]). A knob in two sources with different verdicts is conflicting"""
    out, probs = {}, []

    def add(knob, engine, effect, live, fname, note, decision=None):
        v = {"engine": engine, "effect": effect, "live": live, "file": fname, "note": note, "decision": decision}
        old = out.get(knob)
        if old is not None and (old["engine"], old["effect"], old["live"]) != (engine, effect, live):
            old["conflict"] = "%s and %s give different verdicts" % (old["file"], fname)
            return
        if old is None:
            out[knob] = v

    p = os.path.join(folder, "result.json")
    if os.path.isfile(p):
        try:
            doc = json.load(open(p, encoding="utf-8"))
            for r in doc.get("rows", []):
                add(r.get("knob"), r.get("engine"), r.get("effect"), r.get("live"), "result.json", "", r.get("decision"))
        except Exception as e:  # noqa: BLE001
            probs.append("result.json: not readable (%s)" % e)
    p = os.path.join(folder, "verdicts.json")
    session = None
    if os.path.isfile(p):
        try:
            doc = json.load(open(p, encoding="utf-8"))
            session = doc.get("session")
            for r in doc.get("verdicts", []):
                add(r.get("knob"), r.get("engine"), r.get("effect"), r.get("live"), "verdicts.json", str(r.get("note") or ""))
        except Exception as e:  # noqa: BLE001
            probs.append("verdicts.json: not readable (%s)" % e)
    if not os.path.isfile(os.path.join(folder, "result.json")) and not os.path.isfile(os.path.join(folder, "verdicts.json")):
        probs.append("no result.json and no verdicts.json in %s" % folder)
    return out, probs, session


# ------------------------------------------------------------------------------------------------------------- the decision
def refusal(k, v):
    """why this knob cannot be promoted ('' = it can); every reason, joined. k: the catalogue knob or None"""
    if v.get("conflict"):
        return "conflicting verdicts: " + v["conflict"]
    if k is None:
        return "not in the knobs file"
    if k.get("candidate") is True:
        return "an inventory candidate (unverified by definition)"
    why = []
    if k.get("verified") is not True:
        why.append("not verified in the running game (the locate run sets that, not this tool)")
    if isinstance(k.get("proof"), dict) and k["proof"]:
        why.append("already has a proof (%s)" % k["proof"].get("session"))
    if v["engine"] != "PASS":
        why.append("engine-read check is %s, not PASS" % v["engine"])
    if v["effect"] != "PASS":
        why.append("visible-effect check is %s, not PASS" % v["effect"])
    if v.get("decision") is not None and v["decision"] != "YES" and v["engine"] == "PASS" and v["effect"] == "PASS":
        why.append("the battery's decision was %r, not YES" % v["decision"])
    if not why:
        if not isinstance(k.get("min"), (int, float)) or not isinstance(k.get("max"), (int, float)) or not isinstance(k.get("default"), (int, float)):
            if k.get("type") != "bool" or not isinstance(k.get("default"), (int, float)):
                why.append("the catalogue has no complete min / max / default for it")
        if not k.get("locator") or k.get("offset") in (None, "", -1):
            why.append("no locator / offset in the catalogue")
    return "; ".join(why)


def promote(doc, verdicts, session, evroot_rel, only=None, allow_live=True):
    """-> (new doc, [(knob, note)] promoted, [(knob, why)] refused). evroot_rel(file) -> the 'EV/...' path of an evidence file"""
    new = copy.deepcopy(doc)
    by = {k.get("id"): k for k in new.get("knobs", [])}
    promoted, refused = [], []
    for kid in sorted(verdicts):
        if only and kid not in only:
            continue
        v = verdicts[kid]
        k = by.get(kid)
        why = refusal(k, v)
        if why:
            refused.append((kid, why))
            continue
        ev = "%s#%s" % (evroot_rel(v["file"]), kid)
        live = allow_live and v.get("live") is True  # --no-live can only downgrade to latched, never set live
        proof = {"session": session, "evidence": ev}
        if v.get("note"):
            proof["note"] = v["note"]
        k["engine_check"] = {"session": session, "evidence": ev}
        k["proof"] = proof
        k["slider"] = True
        if live:
            k["live"] = True
            k["applies"] = "live"
        else:
            k.pop("live", None)
            k["applies"] = "next match setup"
        promoted.append((kid, "live (written mid-match)" if live else "latched (written in the hub before kick-off)"))
    return new, promoted, refused


def render(doc):
    return dumps(doc) + "\n"


def write_atomic(path, text):
    d = os.path.dirname(os.path.abspath(path))
    fd, tmp = tempfile.mkstemp(prefix=".promote_", suffix=".tmp", dir=d)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise


def run(a, out=print):
    folder = os.path.abspath(a.evidence)
    if not os.path.isdir(folder):
        out("promote_proof: no such evidence folder: " + folder)
        return 2
    evroot = os.path.abspath(a.ev_root) if a.ev_root else find_ev_root(folder)
    if not evroot:
        out("promote_proof: %s is not under a folder named 'evidence' (turbo_dev\\evidence); pass --ev-root" % folder)
        return 2
    rel = os.path.relpath(folder, evroot).replace("\\", "/")
    if rel.startswith(".."):
        out("promote_proof: the evidence folder is outside --ev-root")
        return 2
    verdicts, probs, vsession = read_verdicts(folder)
    for p in probs:
        out("promote_proof: " + p)
    session = a.session or vsession
    if not session:
        out("promote_proof: give --session (the P-session name, e.g. T29-B1) or a \"session\" in verdicts.json")
        return 2
    if not verdicts:
        out("promote_proof: no verdicts found: nothing to do")
        return 3
    text = open(a.knobs, encoding="utf-8", newline="").read()
    try:
        doc = json.loads(text)
    except Exception as e:  # noqa: BLE001
        out("promote_proof: the knobs file is not JSON: %s" % e)
        return 1
    guard = guard_module()
    before = guard.problems(doc)
    if before:
        out("promote_proof: the knobs file already fails the guard (%d problem(s)): fix that first:" % len(before))
        for b in before:
            out("  " + b)
        return 1

    def evrel(fname):
        if not os.path.isfile(os.path.join(folder, fname)):
            return None
        return "EV/%s/%s" % (rel, fname) if rel != "." else "EV/" + fname

    ok_files = {v["file"] for v in verdicts.values() if evrel(v["file"])}
    for kid, v in list(verdicts.items()):
        if v["file"] not in ok_files:
            v["conflict"] = "evidence file %s is missing" % v["file"]
    new, promoted, refused = promote(doc, verdicts, session, lambda f: evrel(f), set(a.knob) if a.knob else None, allow_live=not a.no_live)
    out("promote_proof: evidence %s (EV/%s), session %s, knobs file %s" % (folder, rel, session, a.knobs))
    for kid, why in refused:
        out("  REFUSED  %-36s %s" % (kid, why))
    for kid, mode in promoted:
        out("  PROMOTE  %-36s -> slider, %s" % (kid, mode))
    if not promoted:
        out("promote_proof: nothing can be promoted; the knobs file is unchanged")
        return 3
    after = guard.problems(new)
    if after:
        out("promote_proof: the result would fail the package guard, nothing written:")
        for b in after:
            out("  " + b)
        return 1
    newtext = render(new)
    if render(doc) != text:
        out("  NOTE: the knobs file is not in the canonical layout; the diff below includes the re-layout")
    diff = "".join(difflib.unified_diff(text.splitlines(True), newtext.splitlines(True), "a/" + os.path.basename(a.knobs), "b/" + os.path.basename(a.knobs), n=2))
    out(diff.rstrip("\n"))
    if a.diff_out:
        with open(a.diff_out, "w", encoding="utf-8", newline="\n") as f:
            f.write(diff)
        out("promote_proof: diff saved to " + a.diff_out)
    if not a.apply:
        out("promote_proof: DRY RUN - nothing edited (review the diff, then repeat with --apply)")
        return 0
    write_atomic(a.knobs, newtext)
    chk = guard.problems(json.loads(open(a.knobs, encoding="utf-8").read()))
    if chk:  # cannot happen (checked above); restore anyway
        write_atomic(a.knobs, text)
        out("promote_proof: guard refused the written file; restored the original")
        return 1
    out("promote_proof: APPLIED %d knob(s). Next: python -I scripts/knobs_guard.py %s, rebuild, 'Read the knobs file again' in the page" % (
        len(promoted), os.path.dirname(a.knobs)))
    return 0


def build_parser():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("evidence", nargs="?")
    ap.add_argument("--knobs", default=KNOBS_DEFAULT)
    ap.add_argument("--session")
    ap.add_argument("--ev-root")
    ap.add_argument("--knob", action="append")
    ap.add_argument("--diff-out")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--no-live", action="store_true", help="promote as latched (applies next match setup, no live) whatever the verdict says live")
    ap.add_argument("--self-test", action="store_true")
    return ap


# ------------------------------------------------------------------------------------------------------------- self-test
def self_test():
    ok = True

    def check(c, name):
        nonlocal ok
        ok = ok and bool(c)
        print("  %s %s" % ("ok  " if c else "FAIL", name))

    guard = guard_module()
    shipped = open(KNOBS_DEFAULT, encoding="utf-8", newline="").read()
    sdoc = json.loads(shipped)
    check(render(sdoc) == shipped, "the shipped knobs file round-trips through this tool's layout (no re-layout noise in a diff)")
    check(sum(1 for k in sdoc["knobs"] if k.get("proof")) == 0, "the shipped catalogue has 0 proven knobs (this tool is not run on it here)")
    with tempfile.TemporaryDirectory() as tmp:
        ev = os.path.join(tmp, "evidence")
        kf = os.path.join(tmp, "knobs_6AC07E31.json")
        with open(kf, "w", encoding="utf-8", newline="\n") as f:
            f.write(shipped)

        def mk(name, result=None, verdicts=None):
            d = os.path.join(ev, "2026-10-20", name)
            os.makedirs(d, exist_ok=True)
            if result is not None:
                json.dump({"rows": result}, open(os.path.join(d, "result.json"), "w"))
            if verdicts is not None:
                json.dump(verdicts, open(os.path.join(d, "verdicts.json"), "w"))
            return d

        def row(knob, engine, effect, live, decision):
            return {"knob": knob, "engine": engine, "effect": effect, "live": live, "decision": decision}

        def go(d, *extra, knobs=kf):
            lines = []
            a = build_parser().parse_args([d, "--knobs", knobs, "--session", "TS1"] + list(extra))
            rc = run(a, lines.append)
            return rc, "\n".join(lines)

        # 1. a decisive battery: width_in live PASS/PASS, width_out engine PASS but effect INCONCLUSIVE, cb effect-only, tightness FAIL, a candidate
        cand = next(k["id"] for k in sdoc["knobs"] if k.get("candidate"))
        d1 = mk("battery_010101", result=[
            row("shape.width_in", "PASS", "PASS", True, "YES"),
            row("shape.width_out", "PASS", "INCONCLUSIVE", True, "DEFER"),
            row("shape.width_in_cb", "NA", "PASS", True, "YES (effect only)"),
            row("marking.tightness", "PASS", "FAIL", None, "NO"),
            row(cand, "PASS", "PASS", True, "YES"),
            row("shape.length_in", "PASS", "PASS", None, "YES"),
            row("not.a.knob", "PASS", "PASS", True, "YES")])
        rc, out = go(d1)
        check(rc == 0 and "DRY RUN" in out and open(kf, encoding="utf-8", newline="").read() == shipped, "dry run: prints a diff, edits nothing")
        check("PROMOTE  shape.width_in " in out and "live (written mid-match)" in out, "width_in (engine PASS + effect PASS, live) is promoted as live")
        check("PROMOTE  shape.length_in " in out and "latched" in out, "length_in (PASS + PASS, live undecided) is promoted as latched, never live")
        for kid, frag in (("shape.width_out", "visible-effect check is INCONCLUSIVE"), ("shape.width_in_cb", "engine-read check is NA"),
                          ("marking.tightness", "visible-effect check is FAIL"), (cand, "inventory candidate"), ("not.a.knob", "not in the knobs file")):
            check(("REFUSED  %-36s" % kid) in out and frag in out, "refused: %s (%s)" % (kid, frag))
        check('+      "slider": true' in out or '+      "slider": true,' in out, "the diff shows the slider line")
        rc, out = go(d1, "--apply")
        new = open(kf, encoding="utf-8", newline="").read()
        nd = json.loads(new)
        kn = {k["id"]: k for k in nd["knobs"]}
        w = kn["shape.width_in"]
        check(rc == 0 and "APPLIED 2 knob(s)" in out, "apply: 2 knobs")
        check(w["proof"]["session"] == "TS1" and w["proof"]["evidence"] == "EV/2026-10-20/battery_010101/result.json#shape.width_in" and w["slider"] is True
              and w["live"] is True and w["applies"] == "live" and w["engine_check"] == w["proof"] | {} and w["verified"] is True,
              "width_in: the proof block (session, EV/ evidence), engine_check, slider, live, applies live, verified untouched")
        l = kn["shape.length_in"]
        check(l["slider"] is True and "live" not in l and l["applies"] == "next match setup" and l["proof"]["evidence"].endswith("result.json#shape.length_in"),
              "length_in: slider, latched (applies next match setup), no live")
        check(sum(1 for k in nd["knobs"] if k.get("proof")) == 2 and guard.problems(nd) == [] and "\r" not in new and new.endswith("}\n"),
              "exactly 2 proven, the guard is happy, LF line ends")
        others = [k for k in nd["knobs"] if k["id"] not in ("shape.width_in", "shape.length_in")]
        check(others == [k for k in sdoc["knobs"] if k["id"] not in ("shape.width_in", "shape.length_in")], "every other knob is byte-for-byte the same data")
        rc, out = go(d1)
        check("already has a proof" in out and rc == 3, "a second run refuses to overwrite a proof")
        # 1b. --no-live: the same live PASS/PASS evidence promoted as latched; a guard-clean result; --no-live never sets live
        with open(kf, "w", encoding="utf-8", newline="\n") as f:
            f.write(shipped)
        rc, out = go(d1, "--no-live", "--apply", "--knob", "shape.width_in")
        nd = json.loads(open(kf, encoding="utf-8", newline="").read())
        w = {k["id"]: k for k in nd["knobs"]}["shape.width_in"]
        check(rc == 0 and "latched" in out and w["slider"] is True and "live" not in w and w["applies"] == "next match setup" and guard.problems(nd) == [],
              "--no-live: live PASS/PASS evidence is promoted as latched (no live), the guard is happy")
        # 2. verdicts.json: a latched knob (hub A/B), conflicting sources, missing session
        with open(kf, "w", encoding="utf-8", newline="\n") as f:
            f.write(shipped)
        d2 = mk("hub_ab", result=[row("shape.length_out", "PASS", "PASS", True, "YES")],
                verdicts={"session": "TV9", "verdicts": [{"knob": "shape.length_out", "engine": "PASS", "effect": "FAIL", "live": True},
                                                          {"knob": "shape.width_in_cb", "engine": "PASS", "effect": "PASS", "live": False, "note": "A/B in hub"},
                                                          {"knob": "shape.gap_dmf", "engine": "PASS", "effect": "PASS", "live": False}]})
        a = build_parser().parse_args([d2, "--knobs", kf])
        lines = []
        rc = run(a, lines.append)
        out = "\n".join(lines)
        check(rc == 0 and "conflicting verdicts" in out and "PROMOTE  shape.width_in_cb " in out and "session TV9" in out, "verdicts.json: session from the file, a knob with conflicting sources refused")
        # gap_dmf in the shipped catalogue is NOT verified: it must be refused
        check("REFUSED  shape.gap_dmf" in out and "not verified" in out, "an unverified knob is refused even with PASS / PASS")
        a = build_parser().parse_args([d2, "--knobs", kf])
        a.session = None
        d3 = mk("no_session", result=[row("shape.width_in", "PASS", "PASS", True, "YES")])
        lines = []
        a = build_parser().parse_args([d3, "--knobs", kf])
        check(run(a, lines.append) == 2 and "--session" in "\n".join(lines), "no session -> usage error, nothing written")
        # 3. folder outside an evidence root, empty folder, a deleted evidence file
        outside = os.path.join(tmp, "elsewhere")
        os.makedirs(outside)
        lines = []
        check(run(build_parser().parse_args([outside, "--knobs", kf, "--session", "S"]), lines.append) == 2 and "evidence" in "\n".join(lines), "a folder outside 'evidence' is refused without --ev-root")
        empty = os.path.join(ev, "2026-10-20", "empty")
        os.makedirs(empty)
        lines = []
        check(run(build_parser().parse_args([empty, "--knobs", kf, "--session", "S"]), lines.append) == 3, "an empty evidence folder promotes nothing")
        # 4. the real TEST_27 evidence (if present) must refuse width_in: it was INCONCLUSIVE
        real = os.path.normpath(os.path.join(HERE, "..", "..", "..", "evidence", "2026-10-14", "battery_122701"))
        if os.path.isfile(os.path.join(real, "result.json")):
            lines = []
            rc = run(build_parser().parse_args([real, "--knobs", kf, "--session", "S"]), lines.append)
            check(rc == 3 and "REFUSED  shape.width_in" in "\n".join(lines), "the real TEST_27 evidence (battery_122701): width_in is INCONCLUSIVE -> refused")
        else:
            print("  skip the real TEST_27 evidence is not on this machine")
        # 5. a guard failure of the result is refused and nothing is written (a knob with verified false cannot reach here; force via a doc)
        bad = copy.deepcopy(sdoc)
        for k in bad["knobs"]:
            if k["id"] == "shape.width_in":
                k["live"] = True  # a hand edit: live without proof/applies
        with open(kf, "w", encoding="utf-8", newline="\n") as f:
            f.write(render(bad))
        lines = []
        rc = run(build_parser().parse_args([d1, "--knobs", kf, "--session", "S"]), lines.append)
        check(rc == 1 and "already fails the guard" in "\n".join(lines), "a knobs file that already fails the guard is not touched")
    print("SELF-TEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    args = build_parser().parse_args()
    if args.self_test:
        sys.exit(self_test())
    if not args.evidence:
        print(__doc__)
        sys.exit(2)
    sys.exit(run(args))
