"""battery_extra.py - battery_write.py with the consumers track's extra knob probes merged in (tools/; a thin launcher, it adds no write logic).

battery_lib.PROBES knows 23 catalogue knobs; battery_write refuses any other id as "unknown knob". consumers_obs.probes_extra() builds PROBES-style
entries (route-C metric, expected signs, engine-safe range = the catalogue's, setup-only for latched copies) for the knobs of docs/re/consumers_6AC07E31.json;
this launcher merges them (never replacing an existing probe) and hands the command line to battery_write.main unchanged, so the owner rule, the identity
check before and after every write, the read-back with rollback, the kill switch and the restore at the end are battery_write's own.

  python -I tools/battery_extra.py plan --knobs FILE
  python -I tools/battery_extra.py hub  --knobs FILE [--prewrite knob=value --allow-write]
  python -I tools/battery_extra.py run  --knobs FILE --allow-write [--prewritten knob=value]
  python -I tools/battery_extra.py --self-test
battery_run.sh calls battery_write.py itself: for these knobs call this launcher directly (same flags), or ask the battery track to merge probes_extra() into
battery_lib.PROBES (one line) so that battery_run.sh works unchanged. The engine-read checks of the new knobs are NOT battery_engine kinds: they are in
consumers_obs.py (press-watch, latched-copy, probe) and are run by the coordinator next to the battery.
"""
import contextlib, io, os, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import battery_lib as bl  # noqa: E402
import consumers_obs as co  # noqa: E402


def merge_probes():
    """-> the ids added (existing probes are never replaced)"""
    add = {k: v for k, v in co.probes_extra().items() if k not in bl.PROBES}
    bl.PROBES.update(add)
    return sorted(add)


def main(argv=None):
    merge_probes()
    import battery_write as bw
    return bw.main(argv if argv is not None else sys.argv[1:])


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        sys.stdout.flush()
        ok = ok and cond

    import battery_mock as bm
    saved = dict(bl.PROBES)
    try:
        added = merge_probes()
        check(len(added) >= 15 and all(a not in saved for a in added), "merge_probes adds %d new probes and replaces none" % len(added))
        check(merge_probes() == [], "a second merge adds nothing")
        d = tempfile.mkdtemp()
        kf = os.path.join(d, "knobs.txt")
        with open(kf, "w") as f:
            f.write("pressing.stepup_distance_bonus\nshape.def_line_range_max\n")
        common = ["--knobs", kf, "--mock", "--state", os.path.join(d, "s.json"), "--wlog", os.path.join(d, "w.log"), "--off", os.path.join(d, "off"),
                  "--ab-state", os.path.join(d, "ab.json"), "--evidence", d]
        import battery_write as bw
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = bw.main(["plan", "--knobs", kf], world=None)
        out = buf.getvalue()
        check(rc == 0 and "pressing.stepup_distance_bonus" in out and "deferred / skipped: shape.def_line_range_max" in out,
              "plan: the effect-only knob is scheduled, the latched one is reported as set-up only (exit %d)" % rc)
        w = bm.MockWorld(scale=0.02, match_delay=1.0)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = bw.main(["hub", "--prewrite", "pressing.partial_min_penalty=0", "--allow-write"] + common, world=w)
        out = buf.getvalue()
        check(rc == 0 and "BATTERY HUB PREFLIGHT: PASS" in out and abs(w.knob_value("pressing.partial_min_penalty")) < 1e-9,
              "hub --prewrite on a merged knob: located, owner rule ok, written 0 and read back (exit %d)" % rc)
        w2 = bm.MockWorld(scale=0.02, match_delay=1.0)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = bw.main(["hub", "--prewrite", "pressing.partial_min_penalty=999", "--allow-write"] + common, world=w2)
        check(rc != 0 and abs(w2.knob_value("pressing.partial_min_penalty") - 7.0) < 1e-9, "a value outside the engine-safe range is refused and nothing is written")
    finally:
        bl.PROBES.clear()
        bl.PROBES.update(saved)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    sys.exit(main())
