"""test_ps1_gates.py - offline checks of the two PowerShell helpers (gk.ps1, launch_fc27.ps1). Safe by construction:
  - both files are only PARSED (PowerShell's own parser, nothing executed) and checked statically: the GAME_STATE gate comes before
    any input / process / launch code, Escape is refused by name and for VK 0x1B in the SendInput layer, `type` refuses control chars;
  - gk.ps1 is run ONCE, with an UNKNOWN command and a GAME_STATE file that says CLOSED: it must refuse (exit 3); even a broken gate
    would only print the usage for an unknown command (no key, no click, no window change);
  - launch_fc27.ps1 is never executed.
python test_ps1_gates.py --self-test
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PS = os.environ.get("TOOLS_POWERSHELL", "powershell")


def parse_errors(path):
    cmd = ("$e = $null; $t = $null; [void][System.Management.Automation.Language.Parser]::ParseFile('%s', [ref]$t, [ref]$e); "
           "if ($e) { $e | ForEach-Object { $_.Message } }; 'PARSED'") % path.replace("'", "''")
    r = subprocess.run([PS, "-NoProfile", "-NonInteractive", "-Command", cmd], capture_output=True, text=True, timeout=60)
    out = r.stdout.strip().splitlines()
    return [l for l in out if l != "PARSED"], "PARSED" in out


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    gk = os.path.join(HERE, "gk.ps1")
    la = os.path.join(HERE, "launch_fc27.ps1")
    for p in (gk, la):
        errs, parsed = parse_errors(p)
        check(parsed and not errs, "%s parses without errors %s" % (os.path.basename(p), errs[:2]))
    g = open(gk, encoding="utf-8").read()
    gate = g.find("-notcontains $tok")
    check(0 < gate < g.find("Add-Type") and gate < g.find("switch ($cmd)"), "gk.ps1: the GAME_STATE gate runs before Add-Type and the commands")
    check(re.search(r'if \(\$cmd -ne "shot"\)', g) is not None, "gk.ps1: every command except shot is gated")
    check(g.count("if (vk == 0x1B)") == 2, "gk.ps1: VK 0x1B refused in both SendInput paths (Key, VkRaw)")
    check('$k -eq "escape" -or $k -eq "esc"' in g and g.count("REFUSED escape") == 1, "gk.ps1: Escape refused by name (key and seq)")
    check("control characters are never typed" in g and "c < (char)0x20" in g, "gk.ps1: type refuses control characters (ESC is 0x1B)")
    code = [l for l in open(la, encoding="utf-8").read().splitlines() if not l.lstrip().startswith("#")]
    first = lambda word: next((i for i, l in enumerate(code) if word in l), 10 ** 6)  # noqa: E731
    check(0 < first("exit 6") < first("Get-Process") and first("exit 6") < first("schtasks"),
          "launch_fc27.ps1: the LAUNCHING gate (exit 6) runs before any process / task code")
    with tempfile.TemporaryDirectory() as d:
        st = os.path.join(d, "GAME_STATE.txt")
        with open(st, "w") as f:
            f.write("state: CLOSED (21:00) - offline test\n")
        r = subprocess.run([PS, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", gk, "selftest-noop"],
                           capture_output=True, text=True, timeout=60, env=dict(os.environ, TURBO_GAME_STATE=st))
        check(r.returncode == 3 and "refused - GAME_STATE is CLOSED" in r.stdout,
              "gk.ps1 (unknown command) while GAME_STATE is CLOSED: refused, exit %d" % r.returncode)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    print(__doc__)
    sys.exit(2)
