#!/usr/bin/env python3
"""nav_capability_check.py - nothing lost in the new navigation (briefs 2.20-IA-wiring "Capability check", 2.22-T-capcheck).

For every page of the OLD navigation (the base ref, default the tag v2.14.0 = the last release whose DEFAULT navigation had the squad
table, the club browser, the managers table, the miniface editor and Turbo Tools), the draw function it resolved to must still be
REACHABLE BY DEFAULT from the NEW navigation (this working tree), and so must every function that the old default navigation reached
(the function level "lost" list). A symbol scan of the sources, no build and no game. Prints PASS or FAIL per capability and one
RESULT line; the exit code is 1 when anything fails.

How a capability is found (PITWALL\\REGRESSION.md has no "capabilities" section, so the list is derived):
  1. the OLD registry. At the base ref (default v2.14.0) the page table of ui_shell.cpp (page, area, label, legacy tab),
     every TURBO_REGISTER_PAGE / TURBO_REGISTER_NEW_PAGE line, and the legacy panel a page falls back to in draw_legacy_page (app.cpp).
     Each (page, draw function) pair is one capability: "Players > Edit a player: draw_player_edit".
  2. the builders' lists, when they exist: lines "capability: <words> => <function>[, <function>]" in PITWALL\\REGRESSION.md (a
     "capabilities" section), PITWALL\\capabilities\\*.md|txt and PITWALL\\board\\ia-*.md. Each line is one capability; it passes when
     a function it names is defined in the new sources and reachable.
  3. waivers: a retired SIGNPOST page (it only pointed at controls that live elsewhere) is checked by the controls it pointed at
     (RETIRED below), not by its own draw function.

REACHABLE BY DEFAULT = the function is the draw function of a page of the new table, or the legacy panel such a page falls back to when
nothing is registered for it, or is called / named (symbol scan of function bodies, transitive) by one of those. Two rules keep the scan
honest (the 2.22 false passes):
  - when a page is in both registries the NEW registry is the default root; the old one only answers with "Use the old pages" on, which
    is NOT counted (it would make every old panel reachable for ever);
  - generic member names (STOP_WORDS: draw, show, run, update ...) make no call edge: a local `draw(...)` call is not App::draw, which
    would pull draw_legacy_page and every legacy panel into the reachable set.
LOST = functions the base reached by default that the new navigation does not reach, minus the allow-list tools/nav_capability_allow.txt
(one line per function: `function | why it is gone on purpose | who decided`, # comments). Anything else lost FAILs.
Also checked: every page of the new table draws something (a registered function or a legacy panel), and no page is registered twice
in the same registry.

usage: py -3 -I tools/nav_capability_check.py [--base REF] [--src DIR] [--pitwall DIR] [--allow FILE] [--old-only] [--quiet] [--why FN] [--self-test]
  --src      the folder with the NEW sources (default: turbogui/src/ui of this repository, from the working tree)
  --base     the git ref of the OLD sources (default v2.14.0)
  --allow    the allow-list (default tools/nav_capability_allow.txt next to this script)
  --why FN   print a root-to-FN call path in the new navigation (or "not reachable") and exit
"""
import argparse
import os
import re
import subprocess
import sys

DEFAULT_BASES = ("v2.14.0",)
UI_DIR = "turbogui/src/ui"
KEYWORDS = {"if", "for", "while", "switch", "catch", "return", "sizeof", "else", "do", "static_assert", "decltype", "alignof"}

# Generic member names: no call edge is made to a function of one of these names (a call `x.draw()` / `show(...)` is almost never
# the single function of that name in the index, which merges every class). They still count when a page registers one as its draw function.
STOP_WORDS = frozenset("""draw show run update render init reset clear begin end apply open close load save add get set find size
empty count next name id ok text label value push pop insert erase remove start stop tick step poll build make create""".split())

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


_CASE = re.compile(r"\bcase\s+(?:shell::)?Page::(\w+)\s*:")


def switch_arms(body):
    """A page dispatcher (`switch (p) { case Page::X: ...; }`) -> {page: arm text}, else {}. Fewer than 3 `case Page::` labels is no dispatcher."""
    marks = list(_CASE.finditer(body))
    if len(marks) < 3:
        return {}
    arms = {}
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(body)
        arm = body[m.end():end]
        d = re.search(r"\bdefault\s*:", arm)
        arms[m.group(1)] = arm[:d.start()] if d else arm
    return arms


def call_pages(body, name):
    """The `Page::X` literals of the arguments of every call of `name` in the body: a set, or None when some call has no literal."""
    pages, seen = set(), False
    for m in re.finditer(r"(?<![\w:])%s\s*\(" % re.escape(name), body):
        depth, i = 1, m.end()
        while i < len(body) and depth:
            depth += {"(": 1, ")": -1}.get(body[i], 0)
            i += 1
        lit = re.findall(r"Page::(\w+)", body[m.end():i])
        if not lit:
            return None
        pages.update(lit)
        seen = True
    return pages if seen else None


class Index:
    """The functions of a source set, one node per (file, name). A call resolves to the function of that name in the SAME file when there
    is one (a static / namespace function of one translation unit is never the same-named one of another: editor_changes exists in
    ui_changes.cpp and in ui_tools.cpp), else to every definition of the name in the other files. Overloads in one file are merged.
    A page dispatcher (switch over Page::X) called with a literal page only brings in that page's arm (node (file, name, page))."""

    def __init__(self, sources):
        self.nodes = {}   # (file, name) or (file, name, page) -> body
        self.by_name = {}  # name -> [file, ...]
        self.arms = {}    # (file, name) -> {page: text}
        for fn, text in sources.items():
            for name, body in function_bodies(strip_code(text)).items():
                self.nodes[(fn, name)] = body
                self.by_name.setdefault(name, []).append(fn)
                arms = switch_arms(body)
                if arms:
                    self.arms[(fn, name)] = arms
                    for pg, t in arms.items():
                        self.nodes[(fn, name, pg)] = t

    def __contains__(self, name):
        return name in self.by_name

    def __iter__(self):
        return iter(self.by_name)

    def targets(self, f, s, caller_body=""):
        base = (f, s) if (f, s) in self.nodes else None
        found = [base] if base else [(g, s) for g in self.by_name.get(s, [])]
        out = []
        for t in found:
            if t in self.arms:
                pages = call_pages(caller_body, s)
                if pages is not None:
                    out += [t + (pg,) for pg in sorted(pages) if pg in self.arms[t]]
                    continue
            out.append(t)
        return out


def build_index(sources):
    return Index(sources)


_IDENT = re.compile(r"[A-Za-z_]\w*")


def _walk(roots, idx):
    """-> {node: predecessor node or None} reached from the root names; generic STOP_WORDS make no edge."""
    prev = {}
    todo = []
    for r in roots:
        for f in idx.by_name.get(r, []):
            if (f, r) not in prev:
                prev[(f, r)] = None
                todo.append((f, r))
    while todo:
        node = todo.pop(0)
        f, body = node[0], idx.nodes[node]
        for s in sorted(set(_IDENT.findall(body))):
            if s in STOP_WORDS:
                continue
            for t in idx.targets(f, s, body):
                if t not in prev:
                    prev[t] = node
                    todo.append(t)
    return prev


def reachable(roots, idx):
    """The function names reachable from the root names by call / name edges (see Index.targets; generic STOP_WORDS make no edge)."""
    return {node[1] for node in _walk(roots, idx)}


def reach_path(roots, idx, target):
    """A root-to-target call path ([names]) or None; same edges as reachable()."""
    prev = _walk(roots, idx)
    node = next((n for n in prev if n[1] == target), None)
    out = []
    while node is not None:
        out.append(node[1])
        node = prev[node]
    return out[::-1] or None


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
    """The functions a page draws BY DEFAULT: the NEW registry's function, else the old registry's, else the legacy panel(s) it falls
    back to. A page in both registries counts only its new function: the old one answers with "Use the old pages" on (not counted)."""
    out = []
    for table in (nav.reg_new, nav.reg_old):
        if page in table:
            out.append(table[page])
            break
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


# ---------------------------------------------------------------- the allow-list
def parse_allow(text):
    """tools/nav_capability_allow.txt -> ({function: (why, who)}, [error lines]). A line is `function | why | who decided`."""
    allow, errors = {}, []
    for n, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = [x.strip() for x in line.split("|")]
        if len(parts) != 3 or not re.fullmatch(r"~?[A-Za-z_]\w*", parts[0]) or not parts[1] or not parts[2]:
            errors.append("line %d: expected `function | why it is gone on purpose | who decided`: %s" % (n, line[:80]))
        elif parts[0] in allow:
            errors.append("line %d: %s listed twice" % (n, parts[0]))
        else:
            allow[parts[0]] = (parts[1], parts[2])
    return allow, errors


def read_allow(path):
    if not path or not os.path.isfile(path):
        return {}, ["allow-list not found: %s" % path] if path else []
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        return parse_allow(f.read())


def default_roots(nav):
    roots = []
    for p in nav.order:
        roots += page_draw_functions(nav, p)[0]
    return roots


# ---------------------------------------------------------------- the check
def check(old_src, new_src, pitwall=None, allow=None):
    """-> (list of (status, name, detail), number of builder capability lines, files read, sorted LOST names).
    LOST = functions the base reached by default that the new navigation does not, minus the allow-list."""
    allow = allow or {}
    old, new = parse_nav(old_src), parse_nav(new_src)
    idx = build_index(new_src)
    results = []

    roots = default_roots(new)
    reach = reachable(roots, idx)
    old_idx = build_index(old_src)
    old_reach = reachable(default_roots(old), old_idx)
    lost_all = sorted(f for f in old_reach if f not in reach and f not in STOP_WORDS)
    lost = [f for f in lost_all if f not in allow]

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
            elif f in allow:
                results.append(("PASS", "%s: %s" % (title, f), "allow-listed: %s (%s)" % allow[f]))
            elif f not in idx:
                results.append(("FAIL", "%s: %s" % (title, f), "the function no longer exists in the sources"))
            else:
                results.append(("FAIL", "%s: %s" % (title, f), "defined but no default page of the new navigation reaches it"))

    # 2b. the function level: everything the base reached by default (helpers, sections, tables), not only the page draw functions
    reported = {r[1].rsplit(": ", 1)[-1] for r in results if r[0] == "FAIL"}
    old_pages = {}
    for p in old.order:
        pr = reachable(page_draw_functions(old, p)[0], old_idx)
        for f in pr:
            old_pages.setdefault(f, []).append(page_title(old, p))
    for f in lost:
        if f in reported:
            continue
        where = "; ".join(old_pages.get(f, [])[:3])
        state = "defined but no default page reaches it" if f in idx else "no longer exists in the sources"
        results.append(("FAIL", "lost by default: " + f, "%s; on %s it was reached from: %s" % (state, "the base", where or "?")))
    for f in sorted(allow):
        if f in lost_all:
            results.append(("PASS", "lost on purpose: " + f, "allow-listed: %s (%s)" % allow[f]))
        elif f not in old_reach:
            results.append(("PASS", "allow-list entry not needed: " + f, "the base did not reach it by default either (remove the line)"))
        else:
            results.append(("PASS", "allow-list entry not needed: " + f, "reachable again in the new navigation (remove the line)"))

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
    return results, len(caps), files, lost


def print_results(results, ncaps, files, quiet=False, lost=()):
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
    if lost:
        print("LOST BY DEFAULT (%d, not allow-listed): %s" % (len(lost), " ".join(lost)))
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
                          "void page_beta(App& a) { helper_x(a); }\nvoid page_beta2(App& a) { if (x) { page_beta(a); } }\nvoid helper_x(App&) {}\n"
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
    res, _, _, _ = check(SELF_OLD, SELF_NEW_OK)
    ok(all(r[0] == "PASS" for r in res), "nothing lost: all PASS (%d rows)" % len(res))
    res, _, _, _ = check(SELF_OLD, SELF_NEW_LOST)
    ok(any(r[0] == "FAIL" and "draw_old_panel" in r[1] for r in res), "a deleted panel function FAILs")
    res, _, _, _ = check(SELF_OLD, dict(SELF_NEW_OK, **{"ui_shell.cpp": SELF_SHELL.replace('    {Page::A, Area::One, "Alpha", 0},\n', "")}))
    ok(any(r[0] == "FAIL" and "Alpha" in r[1] for r in res), "a page dropped from the table FAILs (nothing reaches its panel any more)")
    # 2.22: generic names make no edge; the new registry is the default root; lost list + allow-list
    leak = {"a.cpp": "void page_new(App& a) { thing.draw(); }\nvoid draw() { draw_legacy_page(); }\nvoid draw_legacy_page() { draw_tools(); }\nvoid draw_tools() {}\n"}
    ok(reachable(["page_new"], build_index(leak)) == {"page_new"}, "a local draw() call makes no edge to App::draw -> draw_legacy_page -> draw_tools")
    ok(reachable(["draw"], build_index(leak)) == {"draw", "draw_legacy_page", "draw_tools"}, "a registered draw function is still a root")
    both = parse_nav({"ui_shell.cpp": SELF_SHELL, "reg.cpp": "TURBO_REGISTER_NEW_PAGE(shell::Page::B, page_new);\nTURBO_REGISTER_PAGE(shell::Page::B, page_old);\n"})
    ok(page_draw_functions(both, "B")[0] == ["page_new"], "a page in both registries: the NEW function is the default root, the old one is not")
    res, _, _, lost = check(SELF_OLD, SELF_NEW_LOST)
    ok("draw_old_panel" in lost and "deep_control" in lost and "draw_tools" not in lost, "lost list names the deleted panel and its helper: " + ",".join(lost))
    res, _, _, lost = check(SELF_OLD, SELF_NEW_LOST, None, {"draw_old_panel": ("w", "x"), "deep_control": ("w", "x")})
    ok(lost == [] and not any(r[0] == "FAIL" and "draw_old_panel" in r[1] for r in res), "allow-listed losses pass")
    # the real shape of the 2.21 failure: a panel (kept_section) the base reached by default through draw_tools, which no new default page reaches
    kept = "void draw_tools(App& a) { kept_section(a); }\nvoid kept_section(App&) {}"
    base = dict(SELF_OLD, **{"reg.cpp": SELF_OLD["reg.cpp"].replace("void draw_tools(App&) {}", kept)})
    unwired = dict(SELF_NEW_OK, **{"reg.cpp": SELF_NEW_OK["reg.cpp"].replace("void draw_tools(App&) {}", kept),
                                   "app.cpp": SELF_APP.replace("p == shell::Page::C ? 5 : ", "")})
    wired = dict(unwired, **{"reg.cpp": unwired["reg.cpp"].replace("void page_beta2(App& a) { if (x)", "void page_beta2(App& a) { kept_section(a); if (x)")})
    res, _, _, lost = check(base, unwired)
    ok("kept_section" in lost and "draw_tools" in lost and any(r[0] == "FAIL" for r in res), "lost by default: a panel only the legacy route reaches FAILs")
    res, _, _, lost = check(base, wired)
    ok("kept_section" not in lost and "draw_tools" in lost, "kept_section wired into a new default page is not lost: " + ",".join(lost))
    # same-file resolution and page dispatchers
    two = {"a.cpp": "void ed(App&) { only_a(); }\nvoid pa(App& a) { ed(a); }\nvoid only_a() {}\n",
           "b.cpp": "void ed(App&) { only_b(); }\nvoid only_b() {}\n"}
    ok(reachable(["pa"], build_index(two)) == {"pa", "ed", "only_a"}, "a call resolves to the same-named function of its own file first")
    disp = {"d.cpp": "bool dispatch(App& a, Page p) { switch (p) { case Page::X: px(a); return true; case Page::Y: py(a); return true; case Page::Z: pz(a); return true; default: return false; } }\n"
                     "void px(App&) {}\nvoid py(App&) {}\nvoid pz(App&) {}\nvoid caller(App& a) { dispatch(a, shell::Page::Y); }\nvoid any_page(App& a, Page q) { dispatch(a, q); }\n"}
    ok(reachable(["caller"], build_index(disp)) == {"caller", "dispatch", "py"}, "a dispatcher called with Page::Y brings in only that arm")
    ok({"px", "py", "pz"} <= reachable(["any_page"], build_index(disp)), "a dispatcher called with a variable page brings in every arm")
    al, er = parse_allow("# c\nfoo | replaced by bar | Pit wall CHANGE 21:44\n\nbad line\nfoo | again | x\n")
    ok(list(al) == ["foo"] and len(er) == 2, "allow-list parser: comments skipped, 3 fields required, duplicates refused: %d errors" % len(er))
    real, errs = read_allow(os.path.join(os.path.dirname(os.path.abspath(__file__)), "nav_capability_allow.txt"))
    ok(not errs, "tools/nav_capability_allow.txt parses (%d entries): %s" % (len(real), "; ".join(errs) or "no errors"))
    print("SELF-TEST " + ("PASSED" if not bad else "FAILED"))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", default=None)
    ap.add_argument("--src", default=None)
    ap.add_argument("--pitwall", default=None)
    ap.add_argument("--allow", default=None, help="the allow-list (default tools/nav_capability_allow.txt)")
    ap.add_argument("--why", default=None, metavar="FN", help="print a root-to-FN call path in the new navigation and exit")
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
    new_src = read_new_sources(src)
    if a.why:
        path = reach_path(default_roots(parse_nav(new_src)), build_index(new_src), a.why)
        print(" -> ".join(path) if path else "%s: not reachable by default" % a.why)
        return 0
    allow_path = a.allow or os.path.join(os.path.dirname(os.path.abspath(__file__)), "nav_capability_allow.txt")
    allow, errs = read_allow(allow_path)
    results, ncaps, files, lost = check(old_src, new_src, None if a.old_only else pitwall, allow)
    results = [("FAIL", "allow-list", e) for e in errs] + results
    return 1 if print_results(results, ncaps, files, a.quiet, lost) else 0


if __name__ == "__main__":
    sys.exit(main())
