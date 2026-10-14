"""selftest_all.py - runs every tools/ self-test (offline, mocks and synthetic data only; never the game) for scripts/check_all.sh's
PY step. Each tool runs in its own process; its "  ok" / "  FAIL" lines are re-printed with the tool name, then one
"SELF-TEST PASSED|FAILED" line (the PY step greps both).  python tools/selftest_all.py --self-test [--quick]
--quick skips the slow end-to-end wrapper tests (test_x1_run.py, about 55 s).
"""
import os, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
# (file, slow) - every tool with a --self-test; new tools are added here
TESTS = [("dev2.py", False), ("x1_write.py", False), ("mm_stats.py", False), ("match_measure.py", False),
         ("test_tools_guard.py", False), ("chain_check.py", False), ("x2_readout.py", False), ("x2_sentinels.py", False),
         ("x3_sentinels.py", False), ("x3_readback.py", False), ("test_ps1_gates.py", False),
         ("test_x1_run.py", True)]


def main(argv):
    quick = "--quick" in argv
    ok, total = True, 0
    for f, slow in TESTS:
        p = os.path.join(HERE, f)
        if not os.path.exists(p):
            continue
        if slow and quick:
            print("  skip  %s (--quick)" % f)
            continue
        t = time.time()
        try:
            r = subprocess.run([sys.executable, p, "--self-test"], capture_output=True, text=True, timeout=600, cwd=HERE)
            out, rc = r.stdout + r.stderr, r.returncode
        except subprocess.TimeoutExpired:
            out, rc = "  FAIL  timeout", 3
        lines = [l for l in out.splitlines() if l.startswith(("  ok", "  FAIL"))]
        for l in lines:
            print(l[:6] + f[:-3] + ": " + l[6:].strip())
        passed = rc == 0 and "SELF-TEST PASSED" in out
        if not passed:
            tail = [l for l in out.splitlines() if "Traceback" in l or "Error" in l][-3:]
            print("  FAIL  %s: exit %d, %s" % (f, rc, "; ".join(tail) or "no SELF-TEST PASSED line"))
        total += len(lines)
        ok = ok and passed
        print("        %s: %s, %d checks, %.0f s" % (f, "passed" if passed else "FAILED", len(lines), time.time() - t))
        sys.stdout.flush()
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
