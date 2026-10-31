#!/usr/bin/env python3
"""static_check.py <script.ps1>...  - the offline check the turbo-x scripts get where PowerShell cannot run.

It is NOT execution. It tokenizes each .ps1 (comments, '...' strings with '' escapes, "..." strings with `-escapes, here-strings),
then checks:
  * ( ) { } [ ] balance outside strings and comments
  * every string closed, every <# #> block comment closed
  * ASCII only, no tab characters
  * required lines: $ErrorActionPreference = 'Stop', Read-Host 'Press Enter to close', param( first
  * forbidden commands: Invoke-Expression / iex anywhere; Remove-Item / Move-Item / Rename-Item / Clear-Content / del / rm in the
    scripts listed in NO_DELETE (Protect must never delete or move anything)
  * every Test-Path / Get-ChildItem / Copy-Item / Get-Content / Get-FileHash / Get-Item uses -LiteralPath
  * the bare relative tokens '.\' or ' . ' are not used as a path
Exit 0 = every check passed, 1 = a check failed (details printed).
"""
import re
import sys

NO_DELETE = {"Protect-TurboFiles.ps1", "Restore-TurboFiles.ps1", "Record-BuildIdentity.ps1"}
DESTRUCTIVE = re.compile(r"(?<![\w-])(Remove-Item|Move-Item|Rename-Item|Clear-Content|Set-Content|Out-File|del|rm|rmdir|erase|ri|mv)(?![\w-])", re.I)
IEX = re.compile(r"(?<![\w-])(Invoke-Expression|iex)(?![\w-])", re.I)
LITERAL_CMDS = re.compile(r"(?<![\w-])(Test-Path|Get-ChildItem|Copy-Item|Get-Content|Get-FileHash|Get-Item|Select-String)(?![\w-])", re.I)


def strip(text):
    """Return (code_only, errors): code with strings/comments blanked (same length), plus unterminated-construct errors."""
    out = []
    errors = []
    i, n = 0, len(text)
    line = 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            out.append(c)
            i += 1
            continue
        if c == "<" and text.startswith("<#", i):
            j = text.find("#>", i + 2)
            if j < 0:
                errors.append("line %d: unterminated <# block comment" % line)
                j = n - 2
            seg = text[i:j + 2]
            line += seg.count("\n")
            out.append(" " * len(seg))
            i = j + 2
            continue
        if c == "#":
            j = text.find("\n", i)
            if j < 0:
                j = n
            out.append(" " * (j - i))
            i = j
            continue
        if c == "@" and i + 1 < n and text[i + 1] in "'\"" and text[i + 2:i + 3] in ("\r", "\n"):
            q = text[i + 1]
            end = "\n" + q + "@"
            j = text.find(end, i + 2)
            if j < 0:
                errors.append("line %d: unterminated here-string" % line)
                j = n - len(end)
            seg = text[i:j + len(end)]
            line += seg.count("\n")
            out.append("S" * len(seg))
            i = j + len(end)
            continue
        if c == "'":
            j = i + 1
            while True:
                k = text.find("'", j)
                if k < 0:
                    errors.append("line %d: unterminated single-quoted string" % line)
                    k = n - 1
                    break
                if k + 1 < n and text[k + 1] == "'":
                    j = k + 2
                    continue
                break
            seg = text[i:k + 1]
            line += seg.count("\n")
            out.append("S" * len(seg))
            i = k + 1
            continue
        if c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                if text[j] == "`":
                    j += 2
                    continue
                if text[j] == "\n":
                    line += 1
                j += 1
            if j >= n:
                errors.append("line %d: unterminated double-quoted string" % line)
            seg = text[i:j + 1]
            out.append("S" * len(seg))
            i = j + 1
            continue
        out.append(c)
        i += 1
    return "".join(out), errors


def check(path):
    name = path.replace("\\", "/").split("/")[-1]
    problems = []
    raw = open(path, "rb").read()
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as e:
        problems.append("non-ASCII byte at offset %d" % e.start)
        text = raw.decode("utf-8", "replace")
    if "\t" in text:
        problems.append("tab character on line %d" % (text[:text.index("\t")].count("\n") + 1))
    code, errs = strip(text)
    problems += errs
    pairs = {"(": ")", "{": "}", "[": "]"}
    stack = []
    line = 1
    for ch in code:
        if ch == "\n":
            line += 1
        elif ch in pairs:
            stack.append((ch, line))
        elif ch in pairs.values():
            if not stack or pairs[stack[-1][0]] != ch:
                problems.append("line %d: unexpected '%s'" % (line, ch))
                break
            stack.pop()
    for ch, ln in stack:
        problems.append("line %d: '%s' never closed" % (ln, ch))
    first_code = next((l for l in code.splitlines() if l.strip()), "")
    if not first_code.strip().lower().startswith("param("):
        problems.append("first statement is not param( : %r" % first_code.strip()[:40])
    if "$ErrorActionPreference = 'Stop'" not in text:
        problems.append("missing $ErrorActionPreference = 'Stop'")
    if "Read-Host 'Press Enter to close'" not in text:
        problems.append("missing Read-Host 'Press Enter to close'")
    for m in IEX.finditer(code):
        problems.append("line %d: Invoke-Expression/iex" % (code[:m.start()].count("\n") + 1))
    if name in NO_DELETE:
        for m in DESTRUCTIVE.finditer(code):
            problems.append("line %d: destructive/overwriting cmdlet %s" % (code[:m.start()].count("\n") + 1, m.group(1)))
    for ln_no, cl in enumerate(code.splitlines(), 1):
        for m in LITERAL_CMDS.finditer(cl):
            rest = cl[m.end():]
            # the arguments of this cmdlet end at the next pipe / semicolon / closing paren at depth 0
            depth = 0
            args = []
            for ch in rest:
                if ch in "([{":
                    depth += 1
                elif ch in ")]}":
                    if depth == 0:
                        break
                    depth -= 1
                elif ch in "|;" and depth == 0:
                    break
                args.append(ch)
            if "-LiteralPath" not in "".join(args):
                problems.append("line %d: %s without -LiteralPath" % (ln_no, m.group(1)))
        if re.search(r"(^|[\s'\"(])\.\\", cl) or re.search(r"[\s(]\.[\s)]", cl):
            problems.append("line %d: bare relative path token" % ln_no)
    return problems


def main(argv):
    bad = 0
    for p in argv:
        probs = check(p)
        if probs:
            bad += 1
            print("FAIL %s" % p)
            for x in probs:
                print("   " + x)
        else:
            print("PASS %s (balanced, ASCII, no tabs, no Invoke-Expression, -LiteralPath everywhere, pause-before-exit present)" % p)
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1:]))
