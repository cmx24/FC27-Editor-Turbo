"""knobs_guard.py - the package guard of the gameplay tuning knobs files (docs/TACTICS_OVERHAUL_DECISION.md 3.1 "Honest UI", WP-A2).

A knob is offered as a slider in Turbo's view only after its effect was measured in a played match (its "proof"). This guard refuses a
knobs file in which:
  - a knob asks for a slider ("slider": true) without a proof,
  - a proof misses its session or its evidence, or its evidence is not under EV/ (turbo_dev\\evidence\\<date>\\...),
  - a proven knob is not verified (a knob that is not located in the running game cannot have been measured),
  - a knob is "live": true without a proof (a mid-match write needs the measured live effect of gate X1 outcome 1),
  - the file does not parse as JSON.
Usage: python -I scripts/knobs_guard.py <folder with knobs_*.json>   (exit 0 = ok, 1 = refused)
       python -I scripts/knobs_guard.py --self-test
"""
import json, os, sys


def problems(doc):
    out = []
    for k in doc.get("knobs", []) if isinstance(doc, dict) else []:
        kid = k.get("id", "?")
        proof = k.get("proof")
        has_proof = isinstance(proof, dict) and bool(proof.get("session")) and bool(proof.get("evidence"))
        if proof is not None and not has_proof:
            out.append("%s: a proof needs its session and its evidence" % kid)
        if has_proof and not str(proof.get("evidence")).replace("\\", "/").startswith("EV/"):
            out.append("%s: the proof's evidence must be under EV/ (turbo_dev\\evidence): %s" % (kid, proof.get("evidence")))
        if k.get("slider") is True and not has_proof:
            out.append("%s: a slider without a proof (its effect was never measured in a played match)" % kid)
        if has_proof and k.get("verified") is not True:
            out.append("%s: proven but not verified in the running game" % kid)
        if k.get("live") is True and not has_proof:
            out.append("%s: live without a proof" % kid)
    return out


def check_folder(folder):
    files = sorted(f for f in os.listdir(folder) if f.startswith("knobs_") and f.endswith(".json")) if os.path.isdir(folder) else []
    bad = []
    proven = knobs = 0
    for f in files:
        try:
            doc = json.load(open(os.path.join(folder, f), encoding="utf-8"))
        except Exception as e:  # noqa: BLE001
            bad.append("%s: not JSON (%s)" % (f, e))
            continue
        for p in problems(doc):
            bad.append("%s: %s" % (f, p))
        for k in doc.get("knobs", []):
            knobs += 1
            proven += 1 if isinstance(k.get("proof"), dict) and k["proof"].get("session") and k["proof"].get("evidence") else 0
    if bad:
        print("knobs guard: refused:")
        for b in bad:
            print("  " + b)
        return 1
    print("knobs guard: ok (%d file(s), %d knobs, %d proven in a played match: sliders)" % (len(files), knobs, proven))
    return 0


def self_test():
    ok = True

    def expect(name, doc, n):
        nonlocal ok
        got = problems(doc)
        good = len(got) == n
        ok = ok and good
        print("  %s %s (%d problem(s))" % ("ok  " if good else "FAIL", name, len(got)))

    proof = {"session": "P1", "evidence": "EV/2026-10-14/TEST_25.md#width_in"}
    expect("read-only knobs", {"knobs": [{"id": "a", "verified": True}, {"id": "b"}]}, 0)
    expect("a proven slider", {"knobs": [{"id": "a", "verified": True, "slider": True, "proof": proof}]}, 0)
    expect("a slider without a proof", {"knobs": [{"id": "a", "verified": True, "slider": True}]}, 1)
    expect("a proof without evidence", {"knobs": [{"id": "a", "verified": True, "slider": True, "proof": {"session": "P1"}}]}, 2)
    expect("evidence outside EV/", {"knobs": [{"id": "a", "verified": True, "proof": {"session": "P1", "evidence": "notes.txt"}}]}, 1)
    expect("proven but unverified", {"knobs": [{"id": "a", "proof": proof}]}, 1)
    expect("live without a proof", {"knobs": [{"id": "a", "verified": True, "live": True}]}, 1)
    here = os.path.dirname(os.path.abspath(__file__))
    shipped = os.path.join(here, "..", "turbo", "package", "turbo", "tuning")
    good = check_folder(shipped) == 0
    print("  %s the shipped knobs file" % ("ok  " if good else "FAIL"))
    ok = ok and good
    print("SELF-TEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        sys.exit(self_test())
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(check_folder(sys.argv[1]))
