"""selftest_all.py - runs every tools/ self-test (offline, mocks and synthetic data only; never the game) for scripts/check_all.sh's
PY step. AUTO-DISCOVERY: every tools/*.py that handles `--self-test` (or `--selftest`) - an argparse flag, an argv check - and every
tools/*.sh with a non-comment line handling it are run; a new check script of any track (s_a1_check.py, s_b1_check.py,
s_cleanup_check.sh, ...) is covered without editing this file. Each runs in its own process (python: this interpreter; shell: the bash
of TOOLS_BASH / Git for Windows / MSYS2, never the WSL launcher); PASS = exit 0 and no "SELF-TEST FAILED" line. Its "  ok" / "  FAIL"
lines are re-printed with the tool name, then one "SELF-TEST PASSED|FAILED" line (the PY step greps both).
  python tools/selftest_all.py --self-test [--quick] [--list]
--quick skips the slow end-to-end wrapper tests (SLOW below); --list prints what would run.
"""
import os, re, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
SLOW = {"test_x1_run.py"}  # about 90 s: run last, skipped by --quick
PY_FLAG = re.compile(r"""add_argument\(\s*["']--self-?test["']|["']--self-?test["']\s+(?:not\s+)?in\s+(?:sys\.)?argv|"""
                     r"""==\s*\[\s*["']--self-?test["']\s*\]""")
SH_FLAG = re.compile(r"--self-?test")


def flag_of(text):
    return "--selftest" if "--selftest" in text and "--self-test" not in text else "--self-test"


def discover(folder=HERE):
    """-> [(file, kind 'py'|'sh', flag)] sorted, slow ones last; this runner itself excluded"""
    found = []
    for f in sorted(os.listdir(folder)):
        p = os.path.join(folder, f)
        if not os.path.isfile(p) or f == os.path.basename(__file__):
            continue
        try:
            text = open(p, encoding="utf-8", errors="replace").read()
        except OSError:
            continue
        if f.endswith(".py") and PY_FLAG.search(text):
            found.append((f, "py", flag_of(text)))
        elif f.endswith(".sh"):
            code = "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#"))
            if SH_FLAG.search(code):
                found.append((f, "sh", flag_of(code)))
    return sorted(found, key=lambda x: (x[0] in SLOW, x[0]))


def find_bash():
    for c in (os.environ.get("TOOLS_BASH"), r"C:\Program Files\Git\bin\bash.exe", r"C:\msys64\usr\bin\bash.exe", shutil.which("bash")):
        if c and os.path.exists(c) and "system32" not in c.lower() and "windowsapps" not in c.lower():
            return c
    return None


def discovery_check():
    """the discovery itself on a temp folder: picks the flag forms, ignores mentions in comments / prose"""
    with tempfile.TemporaryDirectory() as d:
        files = {"a.py": 'ap.add_argument("--self-test", action="store_true")\n', "b.py": 'print("no test here")\n',
                 "c.sh": 'case "$1" in --self-test) echo ok ;; esac\n', "d.sh": "# mentions --self-test in a comment only\n",
                 "e.py": 'if "--selftest" in sys.argv:\n    pass\n', "f.py": 'if sys.argv[1:2] == ["--self-test"]:\n    pass\n',
                 "g.md": "--self-test\n", "selftest_all.py": 'add_argument("--self-test")\n'}
        for f, t in files.items():
            with open(os.path.join(d, f), "w") as fh:
                fh.write(t)
        got = discover(d)
    want = [("a.py", "py", "--self-test"), ("c.sh", "sh", "--self-test"), ("e.py", "py", "--selftest"), ("f.py", "py", "--self-test")]
    return got == want, got


def main(argv):
    quick = "--quick" in argv
    tests = discover()
    if "--list" in argv:
        for f, kind, flag in tests:
            print("%s %s %s%s" % (kind, f, flag, " (slow)" if f in SLOW else ""))
        return 0
    ok_disc, got = discovery_check()
    print(("  ok  " if ok_disc else "  FAIL  ") + "selftest_all: discovery finds the python / shell flag forms and ignores comments %s" % (
        "" if ok_disc else got))
    ok, total = ok_disc, 1
    bash = find_bash()
    for f, kind, flag in tests:
        p = os.path.join(HERE, f)
        if f in SLOW and quick:
            print("  skip  %s (--quick)" % f)
            continue
        if kind == "sh" and not bash:
            print("  FAIL  %s: no bash found (set TOOLS_BASH)" % f)
            ok = False
            continue
        cmd = [sys.executable, p, flag] if kind == "py" else [bash, p, flag]
        t = time.time()
        try:
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=600, cwd=HERE)
            out, rc = r.stdout + r.stderr, r.returncode
        except subprocess.TimeoutExpired:
            out, rc = "  FAIL  timeout", 3
        lines = [l for l in out.splitlines() if l.startswith(("  ok", "  FAIL"))]
        for l in lines:
            print(("  ok  " if l.startswith("  ok") else "  FAIL  ") + f.rsplit(".", 1)[0] + ": " + l[6:].strip())
        passed = rc == 0 and "SELF-TEST FAILED" not in out
        if not passed:
            tail = [l for l in out.splitlines() if "Traceback" in l or "Error" in l or "FAIL" in l][-3:]
            print("  FAIL  %s: exit %d, %s" % (f, rc, "; ".join(tail) or "see its output"))
        total += len(lines)
        ok = ok and passed
        print("        %s: %s, %d checks, %.0f s" % (f, "passed" if passed else "FAILED", len(lines), time.time() - t))
        sys.stdout.flush()
    print("        %d tools discovered: %s" % (len(tests), ", ".join(f for f, _, _ in tests)))
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
