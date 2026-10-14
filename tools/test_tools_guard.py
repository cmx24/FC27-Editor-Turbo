"""test_tools_guard.py - the WP-C guard over tools/ (decision 3.3: no game data, no mod values, no addresses beyond the documented build
constants) plus the harness's structural rules. Read-only, offline.  python test_tools_guard.py --self-test

Checks (every text file under tools/, this file's own allow-list lines excluded):
  1. heap addresses: a hex literal >= 0x10000000 must be an FC27.exe image address of build 6AC07E31 (0x140000000 ..
     +0x2145C000), a "round" bound / mask (<= 3 significant hex digits, or all F), or on the allow-list below (with its reason).
  2. player ids: no list of 3+ comma-separated 5..7-digit numbers (a starting XI); with a local id source (TO\\turbo_table_*.csv
     playerid columns, outside git) no 5..7-digit number of the code may be a known player id.
  3. player names: with a local name source (the same tables), no name token (>= 5 letters, capitalised) appears in tools/.
  4. mod values: no mod / mod-author markers (fifamod, anth, fifer, gvsx) - Cassio's mods are never read or quoted here.
  5. Escape: no literal Escape send (a key op with vk 27 / 0x1B, a gk.ps1 'key esc' / 'key:escape' step, VK_ESCAPE, {ESC}).
  6. structure: every Python tool that imports dev2 calls harness.require_live; every tool passing allow_write=True has an
     --allow-write switch and a name containing 'write' or 'sentinels' (decision 3.3 names); every shell wrapper that runs a live tool
     installs an EXIT trap and has a kill path; no file in tools/ is CRLF.
"""
import csv, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402

IMG_LO, IMG_HI = 0x140000000, 0x140000000 + 0x2145C000  # FC27.exe 6AC07E31-2145C000
ALLOW = {  # value -> reason (guard allow-list)
    0x2145C000: "FC27.exe SizeOfImage of build 6AC07E31 (documented build constant)",
    0x5EED00000000: "harness.MOCK_HEAP: synthetic mock heap base, not a game address",
    0x7FFFFFFFFFFF: "user-space pointer upper bound",
    0x113EA820: "service registry id passed to Find (decision 4.3, a documented constant, not an address)",
}
TEXT_EXT = (".py", ".sh", ".ps1", ".md", ".txt", ".json", ".lua")
MOD_RE = re.compile(r"\b(fifamod|anth|fifer|gvsx)\b", re.I)
ESC_RES = [re.compile(p, re.I) for p in (r'"vk"\s*:\s*(27|0x1b)\b', r"\bvk\s*=\s*(27|0x1b)\b", r"\bkey[: ]+esc(ape)?\b", r"VK_ESCAPE",
                                         r"\{ESC\}")]
PID_LIST_RE = re.compile(r"\b\d{5,7}(\s*,\s*\d{5,7}){2,}\b")
HEX_RE = re.compile(r"\b0x([0-9A-Fa-f]{8,16})\b")
NAME_STOP = {"Total", "Turbo", "Editor", "Width", "Match", "Check", "Write", "Value", "Board", "Black", "White", "Green", "Young",
             "Story", "Power", "Price", "Smart", "Chain", "Tests", "Group", "Field", "Block", "Track", "Gates", "After", "Cross",
             "Stone", "Small", "Short", "Major", "Mason", "Baker", "Brown", "Hunter", "Walker", "Mode", "Phase", "State", "Start"}


def tool_files(root=HERE):
    out = []
    for dp, dn, fn in os.walk(root):
        dn[:] = [x for x in dn if x != "__pycache__"]
        for f in fn:
            if f.lower().endswith(TEXT_EXT):
                out.append(os.path.join(dp, f))
    return sorted(out)


def significant(v):
    h = "%X" % v
    return len(h.rstrip("0"))


def scan_hex(path, text):
    bad = []
    for ln, line in enumerate(text.splitlines(), 1):
        if os.path.abspath(path) == os.path.abspath(__file__):
            break  # the guard's own allow-list and planted (synthetic) test values are reviewed by hand
        for m in HEX_RE.finditer(line):
            v = int(m.group(1), 16)
            if v < 0x10000000 or IMG_LO <= v < IMG_HI or v in ALLOW or significant(v) <= 3 or set("%X" % v) == {"F"}:
                continue
            bad.append("%s:%d heap-like address 0x%X" % (os.path.basename(path), ln, v))
    return bad


def local_sources():
    """player ids and name tokens from the local (outside git) table exports, if present"""
    ids, names, used = set(), set(), []
    for f in ("turbo_table_editedplayernames.csv", "turbo_table_playernames.csv", "turbo_table_players.csv"):
        p = os.path.join(harness.TO, f)
        if not os.path.exists(p):
            continue
        used.append(f)
        try:
            with open(p, newline="", encoding="utf-8", errors="replace") as fh:
                for row in csv.DictReader(fh):
                    for k, v in row.items():
                        if not v:
                            continue
                        if k in ("playerid",) and v.isdigit() and 5 <= len(v) <= 7:
                            ids.add(v)
                        if k in ("firstname", "surname", "commonname", "playerjerseyname", "name"):
                            for tok in re.findall(r"[A-Za-zÀ-ɏ]{5,}", v):
                                if tok[0].isupper() and tok not in NAME_STOP:
                                    names.add(tok)
        except OSError:
            pass
    return ids, names, used


def scan(root=HERE):
    """-> (problems, notes)"""
    probs, notes = [], []
    files = tool_files(root)
    ids, names, used = local_sources()
    notes.append("local id/name sources: %s (%d ids, %d name tokens)" % (", ".join(used) or "none found", len(ids), len(names)))
    name_re = re.compile(r"\b(" + "|".join(re.escape(n) for n in sorted(names)) + r")\b") if names else None
    me = os.path.abspath(__file__)
    for p in files:
        raw = open(p, "rb").read()
        text = raw.decode("utf-8", errors="replace")
        b = os.path.basename(p)
        if b"\r\n" in raw:
            probs.append("%s: CRLF line endings (tools/ is LF)" % b)
        probs += scan_hex(p, text)
        own = os.path.abspath(p) == me
        for ln, line in enumerate(text.splitlines(), 1):
            if own:
                continue
            if PID_LIST_RE.search(line):
                probs.append("%s:%d a list of player-id-like numbers" % (b, ln))
            for num in re.findall(r"\b\d{5,7}\b", line):
                if num in ids:
                    probs.append("%s:%d %s is a player id of the local tables" % (b, ln, num))
            if name_re:
                m = name_re.search(line)
                if m:
                    probs.append("%s:%d a player name token of the local tables" % (b, ln))
            if MOD_RE.search(line):
                probs.append("%s:%d mod marker %r" % (b, ln, MOD_RE.search(line).group(0)))
            for rx in ESC_RES:
                if rx.search(line):
                    probs.append("%s:%d literal Escape send %r" % (b, ln, rx.search(line).group(0)))
        if own:
            continue
        if b.endswith(".py") and b not in ("dev2.py", "harness.py") and re.search(r"^\s*import dev2|^\s*from dev2|dev2\.call", text, re.M):
            if "require_live(" not in text:
                probs.append("%s: talks to the dev service but never calls harness.require_live" % b)
        if b.endswith(".py") and "allow_write=True" in text and b not in ("dev2.py", "harness.py"):
            if "--allow-write" not in text:
                probs.append("%s: writes (allow_write=True) without an --allow-write switch" % b)
            if "write" not in b and "sentinels" not in b:
                probs.append("%s: writes but its name says neither write nor sentinels" % b)
        if b.endswith(".sh") and re.search(r"x1_write|x2_|x3_|match_measure|chain_check", text):
            if not re.search(r"trap\s+\S+\s+EXIT", text) or "taskkill" not in text:
                probs.append("%s: runs live tools without an EXIT trap + kill path" % b)
    return probs, notes


def self_test():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    probs, notes = scan()
    for n in notes:
        print("  note  " + n)
    for p in probs:
        print("  FAIL  " + p)
    check(not probs, "tools/ is clean: no heap addresses, player ids / names, mod markers, Escape sends; structure rules hold")
    with tempfile.TemporaryDirectory() as d:  # the guard must catch each kind of leak
        bad = {"a.py": 'X = 0x3A5B7C9D1E\nimport dev2\ndev2.call({"op": "write"}, allow_write=True)\n',
               "b.sh": 'python x1_write.py set 0.6\n', "c.md": "pids 100001, 100002, 100003\nthe gvsx values\n",
               "d.ps1": 'gk key esc\n', "e.py": 'r = {"op": "key", "vk": 27}\r\n'}
        for f, t in bad.items():
            with open(os.path.join(d, f), "w", newline="") as fh:
                fh.write(t)
        probs, _ = scan(d)
        joined = "\n".join(probs)
        for what, needle in (("heap address", "heap-like address 0x3A5B7C9D1E"), ("missing require_live", "never calls harness.require_live"),
                             ("write without --allow-write", "without an --allow-write"), ("write tool name", "neither write nor"),
                             ("wrapper without trap", "without an EXIT trap"), ("player-id list", "player-id-like"),
                             ("mod marker", "mod marker"), ("gk Escape", "literal Escape send 'key esc'"),
                             ("dev Escape", "literal Escape send '\"vk\": 27'"), ("CRLF", "CRLF")):
            check(needle in joined, "the guard catches a planted %s" % what)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        sys.exit(self_test())
    probs, notes = scan()
    for n in notes:
        print(n)
    for p in probs:
        print(p)
    print("VERDICT " + ("PASS" if not probs else "FAIL"))
    sys.exit(0 if not probs else 1)
