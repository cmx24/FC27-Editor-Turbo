#!/usr/bin/env python3
"""nav_capability_check.py - 2.20 IA wiring: nothing lost in the new navigation (brief 2.20-IA-wiring, "Capability check").

For every page of the OLD navigation (the base commit), the draw function it resolved to must still be REACHABLE from the NEW
navigation (this working tree): a symbol scan of the sources, no build and no game. Prints PASS or FAIL per capability and one RESULT
line; the exit code is 1 when anything fails.

How a capability is found (PITWALL\\REGRESSION.md has no "capabilities" section, so the list is derived):
  1. the OLD registry. At the base ref (default 2.20/base, else 1320785) the page table of ui_shell.cpp (page, area, label, legacy tab),
     every TURBO_REGISTER_PAGE / TURBO_REGISTER_NEW_PAGE line, and the legacy panel a page falls back to in draw_legacy_page (app.cpp).
     Each (page, draw function) pair is one capability: "Players > Edit a player: draw_player_edit".
  2. the builders' lists, when they exist: lines "capability: <words> => <function>[, <function>]" in PITWALL\\REGRESSION.md (a
     "capabilities" section), PITWALL\\capabilities\\*.md|txt and PITWALL\\board\\ia-*.md. Each line is one capability; it passes when
     a function it names is defined in the new sources and reachable.
  3. waivers: a retired SIGNPOST page (it only pointed at controls that live elsewhere) is checked by the controls it pointed at
     (RETIRED below), not by its own draw function.

REACHABLE = the function is the draw function of a page of the new table (either registry), or the legacy panel such a page falls back
to when nothing is registered for it, or is called / named (symbol scan of function bodies, transitive) by one of those. The escape
hatch Settings > "Use the old pages" is NOT counted: it would make every old panel reachable for ever.
Also checked: every page of the new table draws something (a registered function or a legacy panel), and no page is registered twice
in the same registry.

usage: py -3 -I tools/nav_capability_check.py [--base REF] [--src DIR] [--pitwall DIR] [--old-only] [--quiet] [--self-test]
  --src      the folder with the NEW sources (default: turbogui/src/ui of this repository, from the working tree)
  --base     the git ref of the OLD sources (default 2.20/base, else 1320785)
"""
import argparse
import os
import re
import subprocess
import sys

DEFAULT_BASES = ("2.20/base", "1320785")
UI_DIR = "turbogui/src/ui"
KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "else", "do", "static_assert", "decltype", "alignof"}

# Retired signpost pages (page identifier at the base ref) -> the controls they pointed at; all must stay reachable.
#   FacesHair  "Faces & hair": Edit a player > Appearance (real face chooser, 3D hair and beard) and Miniface
#   BodyGear   "Body, tattoos & gear": Edit a player > Appearance (tattoos, hair / boots / gloves galleries, the appearance fields)
# tattoo_editor is also called by the card view of the player page (draw_player_page), so removing it from the Appearance tab alone does not
# fail this check: the card view is a real page of the new navigation (Edit a player > Card view), the result is honest.
RETIRED = {
    "FacesHair": ["real_face_picker", "faces3d_gate_section", "miniface_editor"],
    "BodyGear": ["tattoo_editor", "item_galleries", "is_appearance_field"],
}


# ---------------------------------------------------------------- reading sources
def run_git(args, cwd):
    git = "git"
    if os.name == "nt" and not any(os.path.exists(os.path.join(d, "git.exe")) for d in os.environ.get("PATH", "").split(os.pathsep)):
        for c in ("C:/Program Files/Git/cmd/git.exe", "C:/Program Files/Git/bin/git.exe"):  # MSYS2's PATH has no git
            if os.path.exists(c):
                git = c
                break
    p = subprocess.run([git] + args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        raise RuntimeError("git %s: %s" % (" ".join(args), p.stderr.decode("utf-8", "replace").strip()))
    return p.stdout


def read_old_sources(repo, ref):
    """{filename: text} of turbogui/src/ui at the ref."""
    names = run_git(["ls-tree", "--name-only", ref, UI_DIR + "/"], repo).decode().split()
    out = {}
    for n in names:
        if n.endswith((".cpp", ".h")):
            out[os.path.basename(n)] = run_git(["show", "%s:%s" % (ref, n)], repo).decode("utf-8", "replace")
    return out


def read_new_sources(src):
    out = {}
    for n in sorted(os.listdir(src)):
        if n.endswith((".cpp", ".h")):
            with open(os.path.join(src, n), encoding="utf-8", errors="replace") as f:
                out[n] = f.read()
    return out


def strip_code(text):
    """The text with comments, string and character literals blanked (same length, newlines kept)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + re.sub(r"[^\n]", " ", text[i + 1:j - 1]) + (c if j - i >= 2 else ""))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


# ---------------------------------------------------------------- the function index
_HDR = re.compile(r"(~?[A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*(?:(?:const|noexcept|override|final)\s*)*(?:->\s*[\w:<>,\s\*&]+)?\s*$", re.S)


def header_name(code, brace_pos):
    """The function name when the `{` at brace_pos opens a function body (a header `name(args) [const] {`), else None."""
    head = code[max(0, brace_pos - 700):brace_pos]
    cut = max(head.rfind(";"), head.rfind("{"), head.rfind("}"))
    head = head[cut + 1:]
    m = _HDR.search(head.rstrip())
    if not m:
        return None
    name = m.group(1)
    if name in KEYWORDS:
        return None
    pre = head[:m.start()].rstrip()
    if pre.endswith(("=", "(", ",", "return", "?")):
        return None  # a lambda or an expression, not a definition
    return name


def function_bodies(code):
    """{name: body text} of every function defined in the (stripped) text. Lambdas stay inside their enclosing function."""
    bodies = {}
    stack = []
    in_func = 0
    starts = {}
    for i, c in enumerate(code):
        if c == "{":
            name = header_name(code, i) if in_func == 0 else None
            stack.append(name)
            if name:
                in_func += 1
                starts[len(stack)] = (name, i + 1)
        elif c == "}" and stack:
            name = stack.pop()
            if name:
                in_func -= 1
                nm, st = starts.pop(len(stack) + 1)
                bodies.setdefault(nm, []).append(code[st:i])
    return {k: "\n".join(v) for k, v in bodies.items()}


def build_index(sources):
    """{function name: body text} across all sources (overloads and same names merged)."""
    idx = {}
    for text in sources.values():
        for name, body in function_bodies(strip_code(text)).items():
            idx[name] = idx[name] + "\n" + body if name in idx else body
    return idx


def reachable(roots, idx):
    ident = re.compile(r"[A-Za-z_]\w*")
    todo = [r for r in roots if r in idx]
    seen = set(todo)
    while todo:
        f = todo.pop()
        for s in set(ident.findall(idx[f])):
            if s in idx and s not in seen:
                seen.add(s)
                todo.append(s)
    return seen


# ---------------------------------------------------------------- the navigation of a source set
_PAGE_ROW = re.compile(r'\{\s*Page::(\w+)\s*,\s*Area::(\w+)\s*,\s*"([^"]*)"\s*,\s*(-?\d+)\s*\}')
_REG = re.compile(r"TURBO_REGISTER_(NEW_)?PAGE\(\s*(?:turbo::)?(?:shell::)?Page::(\w+)\s*,\s*([\w:]+)\s*\)")
_AREA_LABEL = re.compile(r'case\s+Area::(\w+)\s*:\s*return\s*"([^"]*)"')


class Nav:
    pass


def blank_comments(text):
    """Comments blanked but string literals kept (the page table and the registration lines carry strings)."""
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def brace_block(text, open_pos):
    depth = 0
    for i in range(open_pos, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_pos + 1:i]
    return text[open_pos + 1:]


def parse_legacy(app_cpp):
    """draw_legacy_page of app.cpp: {legacy tab: function}, {page: [functions of its special block]}, {page: tab override}."""
    code = strip_code(app_cpp)
    m = re.search(r"\bdraw_legacy_page\s*\([^)]*\)\s*\{", code)
    if not m:
        return {}, {}, {}
    body = brace_block(code, m.end() - 1)
    cases = {int(c.group(1)): c.group(2) for c in re.finditer(r"case\s+(\d+)\s*:\s*(\w+)\s*\(", body)}
    override = {o.group(1): int(o.group(2)) for o in re.finditer(r"p\s*==\s*(?:shell::)?Page::(\w+)\s*\?\s*(\d+)\s*:", body)}
    extra = {}
    for e in re.finditer(r"if\s*\(\s*p\s*==\s*(?:shell::)?Page::(\w+)\s*\)\s*\{", body):
        blk = brace_block(body, e.end() - 1)
        extra[e.group(1)] = [s for s in re.findall(r"\b([a-z_]\w*)\s*\(", blk) if s not in KEYWORDS]
    return cases, extra, override


def parse_nav(sources):
    nav = Nav()
    shell = blank_comments(sources.get("ui_shell.cpp", ""))
    nav.pages, nav.order = {}, []
    for m in _PAGE_ROW.finditer(shell):
        nav.pages[m.group(1)] = (m.group(2), m.group(3), int(m.group(4)))
        nav.order.append(m.group(1))
    nav.area_labels = {m.group(1): m.group(2) for m in _AREA_LABEL.finditer(shell)}
    nav.reg_old, nav.reg_new, nav.dupes = {}, {}, []
    for fn, text in sources.items():
        if not fn.endswith(".cpp"):
            continue
        for m in _REG.finditer(blank_comments(text)):
            table = nav.reg_new if m.group(1) else nav.reg_old
            fnname = m.group(3).split("::")[-1]
            if m.group(2) in table:
                nav.dupes.append((m.group(2), "new" if m.group(1) else "old", fnname))
            else:
                table[m.group(2)] = fnname
    nav.legacy_case, nav.legacy_extra, nav.legacy_tab_override = parse_legacy(sources.get("app.cpp", ""))
    return nav


def page_draw_functions(nav, page):
    """The functions a page draws: its registered ones, else the legacy panel(s) it falls back to."""
    out = []
    for table in (nav.reg_new, nav.reg_old):
        if page in table and table[page] not in out:
            out.append(table[page])
    if out:
        return out, True
    tab = nav.pages.get(page, (None, "", -1))[2]
    tab = nav.legacy_tab_override.get(page, tab)
    out += nav.legacy_extra.get(page, [])
    if tab in nav.legacy_case:
        out.append(nav.legacy_case[tab])
    return out, False


def page_title(nav, page):
    area, label, _ = nav.pages[page]
    return "%s > %s" % (nav.area_labels.get(area, area), label)


# ---------------------------------------------------------------- builders' capability lists
_CAP = re.compile(r"^\s*(?:[-*]\s*)?(?:capability|cap)\s*:\s*(.+?)\s*(?:=>|->)\s*(.+?)\s*$", re.I)


def read_builder_caps(pitwall):
    files = []
    if pitwall and os.path.isdir(pitwall):
        reg = os.path.join(pitwall, "REGRESSION.md")
        if os.path.isfile(reg):
            files.append(reg)
        for sub, prefix in (("capabilities", None), ("board", "ia-")):
            d = os.path.join(pitwall, sub)
            if os.path.isdir(d):
                for n in sorted(os.listdir(d)):
                    if n.endswith((".md", ".txt")) and (prefix is None or n.startswith(prefix)):
                        files.append(os.path.join(d, n))
    caps = []
    for f in files:
        try:
            with open(f, encoding="utf-8-sig", errors="replace") as fh:
                for line in fh:
                    m = _CAP.match(line)
                    if m:
                        caps.append((os.path.basename(f), m.group(1), re.findall(r"[A-Za-z_]\w*", m.group(2))))
        except OSError:
            pass
    return caps, files


# ---------------------------------------------------------------- the check
def check(old_src, new_src, pitwall=None):
    """-> (list of (status, name, detail), number of builder capability lines, files read)."""
    old, new = parse_nav(old_src), parse_nav(new_src)
    idx = build_index(new_src)
    results = []

    roots = []
    for p in new.order:
        roots += page_draw_functions(new, p)[0]
    reach = reachable(roots, idx)

    # 1. the new table: every page draws something, nothing registered twice in one registry
    for p in new.order:
        fns, _ = page_draw_functions(new, p)
        ok = any(f in idx for f in fns)
        why = ("draws with " + ", ".join(fns)) if ok else "nothing to draw: no registered function and no legacy panel"
        results.append(("PASS" if ok else "FAIL", "page draws: " + page_title(new, p), why))
    for page, which, fn in new.dupes:
        results.append(("FAIL", "registered once: %s (%s registry)" % (page, which), "second registration of %s" % fn))
    for p in new.order:
        if p in new.reg_old and p in new.reg_new:
            results.append(("PASS", "double registration listed: " + page_title(new, p),
                            "new page %s wins over old %s (the old one answers when 'Use the old pages' is on)" % (new.reg_new[p], new.reg_old[p])))

    # 2. the old navigation: every (page, draw function) must still be reachable
    for p in old.order:
        fns, _ = page_draw_functions(old, p)
        title = page_title(old, p)
        if p in RETIRED and p not in new.pages:
            miss = [s for s in RETIRED[p] if s not in reach]
            results.append(("PASS" if not miss else "FAIL", "retired signpost %s" % title,
                            ("its controls stay reachable: " + ", ".join(RETIRED[p])) if not miss else "no longer reachable: " + ", ".join(miss)))
            continue
        for f in fns:
            if f in reach:
                where = [page_title(new, q) for q in new.order if f in page_draw_functions(new, q)[0]]
                results.append(("PASS", "%s: %s" % (title, f), "reached from " + (", ".join(where) if where else "a page that calls it")))
            elif f not in idx:
                results.append(("FAIL", "%s: %s" % (title, f), "the function no longer exists in the sources"))
            else:
                results.append(("FAIL", "%s: %s" % (title, f), "defined but no page of the new navigation reaches it"))

    # 3. the builders' lists
    caps, files = read_builder_caps(pitwall)
    for src, name, syms in caps:
        defined = [s for s in syms if s in idx]
        hit = [s for s in defined if s in reach]
        if hit:
            results.append(("PASS", "[%s] %s" % (src, name), "reached through " + hit[0]))
        elif defined:
            results.append(("FAIL", "[%s] %s" % (src, name), "defined but no page reaches: " + ", ".join(defined)))
        else:
            results.append(("FAIL", "[%s] %s" % (src, name), "none of these functions exists: " + ", ".join(syms)))
    return results, len(caps), files


def print_results(results, ncaps, files, quiet=False):
    npass = sum(1 for r in results if r[0] == "PASS")
    nfail = len(results) - npass
    for st, name, why in results:
        if quiet and st == "PASS":
            continue
        print("%s  %s  (%s)" % (st, name, why))
    print("builders' capability lines read: %d from %d file(s)%s" % (
        ncaps, len(files), "" if ncaps else " (none yet: add lines 'capability: <words> => <function>' to PITWALL\\capabilities\\<track>.md)"))
    if not ncaps:
        print("REGRESSION.md has no capability lines: the list above is derived from the old registry")
    print("RESULT %d pass, %d fail" % (npass, nfail))
    return nfail


# ---------------------------------------------------------------- self test
SELF_SHELL = '''
const PageInfo kPages[] = {
    {Page::A, Area::One, "Alpha", 0},
    {Page::B, Area::One, "Beta", -1},
    {Page::C, Area::Two, "Gamma", 1},
};
const char* area_label(Area a) { switch (a) { case Area::One: return "First"; case Area::Two: return "Second"; default: return ""; } }
'''
SELF_APP = '''
static void draw_legacy_page(App& app, shell::Page p) {
    const int lt = p == shell::Page::C ? 5 : shell::legacy_tab_of(p);
    switch (lt) { case 0: draw_old_panel(app); break; case 5: draw_tools(app); break; default: break; }
}
'''
SELF_PANEL = "void draw_old_panel(App&) { deep_control(); }\nvoid deep_control() {}\n"
SELF_OLD = {"ui_shell.cpp": SELF_SHELL, "app.cpp": SELF_APP,
            "reg.cpp": "TURBO_REGISTER_PAGE(shell::Page::B, page_beta);\nvoid page_beta(App& a) { helper_x(a); }\nvoid helper_x(App&) {}\n"
                       + SELF_PANEL + "void draw_tools(App&) {}\n"}
SELF_NEW_OK = {"ui_shell.cpp": SELF_SHELL, "app.cpp": SELF_APP,
               "reg.cpp": "TURBO_REGISTER_NEW_PAGE(shell::Page::B, page_beta2);\nTURBO_REGISTER_PAGE(shell::Page::B, page_beta);\n"
                          "void page_beta(App& a) { helper_x(a); }\nvoid page_beta2(App& a) { if (x) { helper_x(a); } }\nvoid helper_x(App&) {}\n"
                          + SELF_PANEL + "void draw_tools(App&) {}\n"}
SELF_NEW_LOST = dict(SELF_NEW_OK, **{"reg.cpp": SELF_NEW_OK["reg.cpp"].replace(SELF_PANEL, "")})


def self_test():
    bad = 0

    def ok(cond, what):
        nonlocal bad
        print("  %s  nav_capability_check: %s" % ("ok" if cond else "FAILED", what))
        bad += 0 if cond else 1

    idx = build_index({"x.cpp": 'void f(int a) { if (a) { g(); } auto l = [&]() { h(); }; }\nstruct S { int m() const { return k(); } };\n'
                                'int k() { return 1; }\nvoid g(){}\nvoid h(){}\nvoid u() {}\n'})
    ok(set(idx) == {"f", "g", "h", "k", "m", "u"}, "function index finds functions and methods, ifs and lambdas stay inside: " + ",".join(sorted(idx)))
    ok(reachable(["f"], idx) == {"f", "g", "h"} and reachable(["m"], idx) == {"m", "k"}, "reachability follows calls and lambdas")
    nav = parse_nav(SELF_OLD)
    ok(nav.order == ["A", "B", "C"] and nav.pages["C"] == ("Two", "Gamma", 1) and nav.area_labels["One"] == "First", "page table and area labels parsed")
    ok(nav.reg_old == {"B": "page_beta"} and nav.legacy_case == {0: "draw_old_panel", 5: "draw_tools"}, "registrations and legacy cases parsed")
    ok(page_draw_functions(nav, "A")[0] == ["draw_old_panel"] and page_draw_functions(nav, "C")[0] == ["draw_tools"], "legacy fallback with the tab override (C -> tab 5)")
    res, _, _ = check(SELF_OLD, SELF_NEW_OK)
    ok(all(r[0] == "PASS" for r in res), "nothing lost: all PASS (%d rows)" % len(res))
    res, _, _ = check(SELF_OLD, SELF_NEW_LOST)
    ok(any(r[0] == "FAIL" and "draw_old_panel" in r[1] for r in res), "a deleted panel function FAILs")
    res, _, _ = check(SELF_OLD, dict(SELF_NEW_OK, **{"ui_shell.cpp": SELF_SHELL.replace('    {Page::A, Area::One, "Alpha", 0},\n', "")}))
    ok(any(r[0] == "FAIL" and "Alpha" in r[1] for r in res), "a page dropped from the table FAILs (nothing reaches its panel any more)")
    print("SELF-TEST " + ("PASSED" if not bad else "FAILED"))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", default=None)
    ap.add_argument("--src", default=None)
    ap.add_argument("--pitwall", default=None)
    ap.add_argument("--old-only", action="store_true", help="skip the builders' capability lists (chunk 1: their pages are not wired yet)")
    ap.add_argument("--quiet", action="store_true", help="print the FAIL lines and the RESULT only")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    src = a.src or os.path.join(repo, UI_DIR)
    pitwall = a.pitwall or os.environ.get("TURBO_PITWALL") or os.path.abspath(os.path.join(repo, "..", "..", "PITWALL"))
    old_src, last = None, "empty tree"
    for ref in ([a.base] if a.base else list(DEFAULT_BASES)):
        try:
            got = read_old_sources(repo, ref)
            if got.get("ui_shell.cpp"):
                old_src = got
                print("old navigation: %s (%d ui files); new navigation: %s" % (ref, len(got), src))
                break
        except RuntimeError as e:
            last = str(e)
    if not old_src:
        print("FAIL  cannot read the old sources at %s: %s" % (a.base or "/".join(DEFAULT_BASES), last))
        print("RESULT 0 pass, 1 fail")
        return 1
    results, ncaps, files = check(old_src, read_new_sources(src), None if a.old_only else pitwall)
    return 1 if print_results(results, ncaps, files, a.quiet) else 0


if __name__ == "__main__":
    sys.exit(main())
