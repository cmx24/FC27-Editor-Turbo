"""Re-extract the game-variable names the FC 27 image reads (Turbo 2.0 phase 0, task 0c). Offline, read-only: it only reads the
dumped image (FC27_IMAGE, default turbo_output\\fc27_image.bin), never the running game.

    bash scripts/re/py.sh scripts/re/gv_names.py [--out turbo_output/gamevars_fc27.txt] [--fn 0x140856DB4 ...] [--grep REGEX]

For every direct call to the global-variable getters (default: GetInt 0x140856DB4, docs/re/match_setup.md section 1) it looks a
few instructions back for a `lea rcx|rdx, [rip+disp32]` that points at an upper-case NUL-terminated name and collects
name -> read sites. Output: the names (one per line, with the number of read sites and one example site), then the names that
match the tactics / gameplay keyword groups (PASS, SHOT, FOUL, REF, SPEED, CARD, ERROR, TRAP, CONTROL, ... see GROUPS), so the
phase 0 review can promote or demote the tactic sliders of docs/TURBO_2_0_PLAN.md section 3.
Names found are only names the code READS: whether one can be overridden at runtime (like OVERRIDE/WEATHER) still needs the
in-game differential test (plan task 0e).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rx_jobs as rx  # noqa: E402

GETINT = 0x140856DB4
BACK = 96          # bytes searched before the call for the lea of the name
NAME_RE = re.compile(rb"^[A-Z0-9_/\.\- ]{3,120}$")

GROUPS = [
    ("pass", r"PASS"), ("shot", r"SHOT|SHOOT"), ("foul", r"FOUL|TACKLE"), ("referee", r"REF(?:EREE)?\b|REFEREE|OFFSIDE|ADVANTAGE"),
    ("speed", r"SPEED|PACE|SPRINT"), ("card", r"CARD|YELLOW|RED_"), ("error", r"ERROR|MISTAKE|MISCONTROL"),
    ("trap", r"TRAP|CONTROL|FIRST_TOUCH"), ("press", r"PRESS|DEFEND|LINE|WIDTH|DEPTH|COMPACT"),
    ("gameplay", r"GAMEPLAY|ATTRIBULATOR|TACTIC|MENTALIT|FORMATION"), ("injury/fatigue", r"INJUR|FATIGUE|STAMINA"),
    ("override", r"^OVERRIDE|OVERRIDE_"),
]


def call_sites(fn):
    refs = rx.rip_refs([fn])[fn]
    out = []
    for (r, tail) in refs:
        if tail == 0 and rx.u8(r - 1) == 0xE8:
            out.append(r - 1)
    return sorted(set(out))


def name_before(call_va):
    code = rx.read(call_va - BACK, BACK)
    # scan from the call backwards: the closest lea wins
    i = len(code) - 7
    while i >= 0:
        b0, b1, b2 = code[i], code[i + 1], code[i + 2]
        if b0 in (0x48, 0x4C) and b1 == 0x8D and (b2 & 0xC7) == 0x05:    # lea reg, [rip + disp32]
            disp = int.from_bytes(code[i + 3:i + 7], "little", signed=True)
            target = call_va - BACK + i + 7 + disp
            try:
                s = rx.cstr(target, 130)
            except Exception:
                s = b""
            if NAME_RE.match(s):
                return s.decode("ascii")
        i -= 1
    return None


def main(argv):
    out = None
    fns = []
    grep = None
    i = 0
    while i < len(argv):
        if argv[i] == "--out":
            out = argv[i + 1]
            i += 2
        elif argv[i] == "--fn":
            fns.append(int(argv[i + 1], 16))
            i += 2
        elif argv[i] == "--grep":
            grep = re.compile(argv[i + 1], re.I)
            i += 2
        else:
            i += 1
    fns = fns or [GETINT]
    names = {}
    total = 0
    for fn in fns:
        for site in call_sites(fn):
            total += 1
            n = name_before(site)
            if n:
                e = names.setdefault(n, [])
                e.append(site)
    lines = ["# game variable names read by the image: %d call sites, %d names (functions %s)" %
             (total, len(names), ", ".join("0x%X" % f for f in fns))]
    for n in sorted(names):
        lines.append("%s\t%d\t0x%X" % (n, len(names[n]), names[n][0]))
    lines.append("")
    lines.append("# keyword groups")
    for label, rgx in GROUPS:
        r = re.compile(rgx)
        hit = [n for n in sorted(names) if r.search(n)]
        lines.append("## %s (%d)" % (label, len(hit)))
        lines.extend("  " + n for n in hit)
    if grep:
        lines.append("## grep %s" % grep.pattern)
        lines.extend("  " + n for n in sorted(names) if grep.search(n))
    text = "\n".join(lines) + "\n"
    if out:
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("wrote %s: %d names from %d call sites" % (out, len(names), total))
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main(sys.argv[1:])
