#!/usr/bin/env python3
"""4.0 S1: "one component" lint for the Turbo GUI page template (src/ui/ui_page.h).

Turbo 4.0 GUI overhaul (brief 4.0-gui-desk.md, S1): a page that uses the template is a function that constructs a `page::Frame`
(src/ui/ui_page.h). On such TEMPLATE PAGES this lint FAILS on:
  T1  a raw BeginTable: a table must be drawn in the registry form, i.e. its flags name `tbl::` (src/ui/table_registry.h, the
      DataTable policy that tests/native/test_ux_resize.h already guards: resizable / reorderable / hideable columns, stable id);
  T2  an own undo store (a vector / stack / struct named *undo*): Undo is the global Changes list (src/ui/ui_changes.h), which the
      frame's Undo button already calls;
  T3  a label (a visible control label, the Frame title) with the whole word classic / old / new: a converted page replaces the old
      one, it is not offered next to it ("Use the old pages", "New ..." labels belong to a cutover that the template does not have).
The label length / wording rules (R3 R4 R5 R6 R9, copy deck) live in the native label guard (tests/native/gui_label_scan.h
template_problems, run by tests/native/test_gui_labels.h); this script is the structural half, and needs no build.

Tree-wide ADVISORY (never fails): every raw BeginTable site, every own-undo-store site and every classic / old / new label in
turbogui/src, so a conversion slice sees the work still to do. It prints the counts as one line (for the board row) and the list
after it. The span detection mirrors the C++ one (page::Frame ident( ... ) -> the enclosing function body).

Usage: python lint_one_component.py [repo_root] [--list] [--self-test]
Exit 0 = PASS, 1 = FAIL (every violation is printed with file:line), 2 = no sources. Source scan only: no build, no game.
Known limit: a table / undo store inside a helper that a template page CALLS is not seen (only the page function's own body).
"""
import os
import re
import sys

# visible-label calls: the first string argument is what a player reads (same set as gui_label_scan.h visible_label_callees)
LABEL_CALLEES = ("Button", "SmallButton", "Checkbox", "RadioButton", "Selectable", "MenuItem", "TreeNode", "TreeNodeEx",
                 "CollapsingHeader", "BeginTabItem", "BeginMenu", "checkbox_right", "primary_button")
LABEL_RE = re.compile(r"\b(?:%s)\s*\(\s*\"" % "|".join(LABEL_CALLEES))
CUTOVER_WORD = re.compile(r"\b(classic|old|new)\b", re.I)
# an own undo store: a container / struct / class / variable whose name has "undo" in it and that holds history
UNDO_DECL = re.compile(
    r"(?:std::(?:vector|deque|stack|list|map)\s*<[^;{}]*>\s*&?\s*\w*[Uu]ndo\w*"      # std::vector<Step> undo_stack
    r"|\b(?:struct|class)\s+\w*[Uu]ndo\w*\s*\{"                                       # struct UndoStep {
    r"|\b\w*[Uu]ndo_?(?:stack|history|log|store|steps|list|buf|buffer)\w*\s*(?:;|=|\{|\[))")  # undo_stack;  undoHistory{
FRAME_KEY = "page::Frame"


def read(path):
    with open(path, "rb") as f:
        return f.read().decode("utf-8", "replace")


def mask(s):
    """Blank comments, string and character literal contents (keep newlines and the quote characters, keep offsets)."""
    o = list(s)
    n = len(s)
    i = 0
    while i < n:
        c = s[i]
        if c == "/" and i + 1 < n and s[i + 1] == "/":
            while i < n and s[i] != "\n":
                o[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and s[i + 1] == "*":
            o[i] = o[i + 1] = " "
            i += 2
            while i + 1 < n and not (s[i] == "*" and s[i + 1] == "/"):
                if s[i] != "\n":
                    o[i] = " "
                i += 1
            if i + 1 < n:
                o[i] = o[i + 1] = " "
            i += 2
        elif c == '"' or c == "'":
            if c == "'" and 0 < i < n - 1 and s[i - 1] in "0123456789abcdefABCDEF" and s[i + 1] in "0123456789abcdefABCDEF":
                i += 1  # digit separator
                continue
            q = c
            i += 1
            while i < n and s[i] != q:
                if s[i] == "\\" and i + 1 < n:
                    o[i] = " "
                    i += 1
                if s[i] != "\n":
                    o[i] = " "
                i += 1
            i += 1
        else:
            i += 1
    return "".join(o)


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def ident_char(c):
    return c.isalnum() or c == "_"


def own_frame_prefix(m, pos):
    """True when the page::Frame at pos is ours: bare, `turbo::page::Frame` or `::turbo::page::Frame`. Any other qualifier
    (`shell::page::Frame`, `x::turbo::...` is fine only for turbo) or a glued identifier (`xpage::Frame`) is not."""
    if pos == 0:
        return True
    c = m[pos - 1]
    if ident_char(c):
        return False
    if c != ":":
        return True
    if pos < 2 or m[pos - 2] != ":":
        return False
    e = pos - 2
    b = e
    while b > 0 and ident_char(m[b - 1]):
        b -= 1
    return m[b:e] == "turbo"


def template_spans(src):
    """[(fn_name, body_start_pos, body_end_pos, frame_call_start, frame_call_end)] of the functions that construct a page::Frame."""
    m = mask(src)
    n = len(m)
    out = []
    pos = m.find(FRAME_KEY)
    while pos != -1:
        nxt = m.find(FRAME_KEY, pos + len(FRAME_KEY))
        if not own_frame_prefix(m, pos):
            pos = nxt
            continue
        p = pos + len(FRAME_KEY)
        while p < n and m[p].isspace():
            p += 1
        id0 = p
        while p < n and ident_char(m[p]):
            p += 1
        if p == id0:
            pos = nxt
            continue
        while p < n and m[p].isspace():
            p += 1
        if p >= n or m[p] != "(":
            pos = nxt
            continue
        d = 0
        q = p
        while q < n:
            if m[q] == "(":
                d += 1
            elif m[q] == ")":
                d -= 1
                if d == 0:
                    break
            q += 1
        fn_open = -1
        depth = 0
        i = pos
        while i > 0:
            i -= 1
            if m[i] == "}":
                depth += 1
            elif m[i] == "{":
                if depth > 0:
                    depth -= 1
                    continue
                j = i
                while j > 0 and m[j - 1].isspace():
                    j -= 1
                if j > 0 and m[j - 1] == ")":
                    fn_open = i
        if fn_open == -1:
            pos = nxt
            continue
        depth = 0
        fn_close = fn_open
        while fn_close < n:
            if m[fn_close] == "{":
                depth += 1
            elif m[fn_close] == "}":
                depth -= 1
                if depth == 0:
                    break
            fn_close += 1
        # the function name: the identifier before the '(' matching the ')' before fn_open
        j = fn_open
        while j > 0 and m[j - 1].isspace():
            j -= 1
        pd = 0
        k = j
        while k > 0:
            k -= 1
            if m[k] == ")":
                pd += 1
            elif m[k] == "(":
                pd -= 1
                if pd == 0:
                    break
        while k > 0 and m[k - 1].isspace():
            k -= 1
        b = k
        while b > 0 and ident_char(m[b - 1]):
            b -= 1
        name = m[b:k]
        if not any(s[1] == fn_open for s in out):
            out.append((name, fn_open, fn_close, pos, q))
        pos = nxt
    return out


def literal_at(src, m, start):
    """The string literal whose opening quote is at start-1 (m has blanked contents): its text, or None."""
    end = m.find('"', start)
    if end == -1:
        return None
    return src[start:end]


def table_calls(src, m):
    """[(pos, args_text)] of every ImGui::BeginTable( call."""
    out = []
    for mt in re.finditer(r"\bBeginTable\s*\(", m):
        p = mt.end() - 1
        d = 0
        q = p
        while q < len(m):
            if m[q] == "(":
                d += 1
            elif m[q] == ")":
                d -= 1
                if d == 0:
                    break
            q += 1
        out.append((mt.start(), src[p:q + 1]))
    return out


def label_literals(src, m):
    """[(pos, text)] of the first string literal of every visible-label call."""
    out = []
    for mt in LABEL_RE.finditer(m):
        t = literal_at(src, m, mt.end())
        if t is not None:
            out.append((mt.end(), t))
    return out


def frame_title(src, m, span):
    """The page::Frame title (the second string literal of its spec) or None."""
    lits = []
    i = span[3]
    while i < span[4]:
        if m[i] == '"':
            t = literal_at(src, m, i + 1)
            if t is None:
                break
            lits.append((i + 1, t))
            i = m.find('"', i + 1) + 1
        else:
            i += 1
    return lits[1] if len(lits) > 1 else None


def analyse(src):
    """(template_failures, advisory) for one source: lists of (line, rule, message)."""
    m = mask(src)
    spans = template_spans(src)
    fails, adv = [], []
    tcalls = table_calls(src, m)
    labels = label_literals(src, m)
    # tree-wide advisory
    for pos, args in tcalls:
        if "tbl::" not in args:
            adv.append((line_of(src, pos), "BeginTable", "raw BeginTable (no tbl:: registry flags)"))
    for mt in UNDO_DECL.finditer(m):
        adv.append((line_of(src, mt.start()), "undo", "own undo store: " + mt.group(0).strip()[:60]))
    for pos, text in labels:
        w = CUTOVER_WORD.search(text)
        if w:
            adv.append((line_of(src, pos), "label", 'label with "%s": %s' % (w.group(1), text[:50])))
    # template pages
    for name, a, b, _, _ in spans:
        for pos, args in tcalls:
            if a <= pos <= b and "tbl::" not in args:
                fails.append((line_of(src, pos), "T1", "%s: raw BeginTable on a template page (use the registry form: tbl::kData / kGrid flags)" % name))
        for mt in UNDO_DECL.finditer(m, a, b):
            fails.append((line_of(src, mt.start()), "T2", "%s: own undo store (%s): Undo is the global Changes list" % (name, mt.group(0).strip()[:40])))
        for pos, text in labels:
            if a <= pos <= b:
                w = CUTOVER_WORD.search(text)
                if w:
                    fails.append((line_of(src, pos), "T3", '%s: label "%s" has the word "%s"' % (name, text[:40], w.group(1))))
    for span in spans:
        t = frame_title(src, m, span)
        if t and CUTOVER_WORD.search(t[1]):
            fails.append((line_of(src, t[0]), "T3", '%s: page title "%s" has a cutover word' % (span[0], t[1][:40])))
    return fails, adv, spans


def source_files(base):
    out = []
    for d, _, fs in os.walk(os.path.join(base, "turbogui", "src")):
        for fn in fs:
            if fn.endswith((".cpp", ".h", ".hpp")):
                out.append(os.path.join(d, fn))
    return sorted(out)


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("ok   " if cond else "FAIL ") + what)
        ok = ok and cond

    clean = (
        'void good(App& app) {\n'
        '  page::Frame f(app, {"id", "Title", "help", nullptr});\n'
        '  if (ImGui::BeginTable("##chgrows", 3, tbl::kData)) { ImGui::EndTable(); }\n'
        '  if (ImGui::Button("Open the folder##x")) {}\n'
        '  f.end();\n'
        '}\n')
    fl, adv, sp = analyse(clean)
    check(len(sp) == 1 and sp[0][0] == "good" and not fl and not adv, "a clean template page: one span, no failure, no advisory")
    bad = (
        'void other(App& app) {\n'
        '  ImGui::BeginTable("##t", 2, ImGuiTableFlags_RowBg);\n'
        '  ImGui::Button("Use the old pages");\n'
        '}\n'
        'void bad(App& app) {\n'
        '  page::Frame f(app, {"id", "The new page", "help", nullptr});\n'                      # line 6
        '  if (ImGui::BeginTable("##t", 2, ImGuiTableFlags_RowBg)) {}\n'                       # 7  T1
        '  std::vector<Step> undo_stack;\n'                                                      # 8  T2
        '  struct UndoStep { int a; };\n'                                                        # 9  T2
        '  ImGui::Checkbox("Classic view##c", &v);\n'                                            # 10 T3
        '  // ImGui::Button("old in a comment");\n'
        '  const char* s = "ImGui::Button(\\"old\\")";\n'
        '  f.end();\n'
        '}\n')
    fl, adv, sp = analyse(bad)
    rules = sorted((r, ln) for ln, r, _ in fl)
    check(len(sp) == 1 and sp[0][0] == "bad", "only the function with page::Frame is a span (not 'other')")
    check(("T1", 7) in rules and ("T2", 8) in rules and ("T2", 9) in rules and ("T3", 10) in rules and ("T3", 6) in rules,
          "T1 raw table, T2 undo store x2, T3 label + title are each caught: %s" % rules)
    check(not any(ln in (2, 3) for _, ln in rules), "the same table / label in a non-template function is not a failure")
    check(not any(ln in (11, 12) for _, ln in rules), "comments and strings are ignored")
    advrules = sorted((r, ln) for ln, r, _ in adv)
    check(("BeginTable", 2) in advrules and ("label", 3) in advrules, "the advisory sees the non-template raw table and the old label: %s" % advrules)
    tricky = '// page::Frame fake(app, {});\nvoid t() {\n  const char* s = "} { page::Frame x(app, {});";\n  char c = \'{\';\n}\n'
    check(template_spans(tricky) == [], "page::Frame inside a comment or a string makes no span")
    shell = 'void t(App& app) {\n  shell::page::Frame f(app);\n  ImGui::BeginTable("##t", 2, ImGuiTableFlags_RowBg);\n}\n'
    fl, adv, sp = analyse(shell)
    check(sp == [] and fl == [], "shell::page::Frame (the existing two-pane frame) is not the template")
    # round 1 fix 1: the qualified form of the usage example in ui_page.h is a template page too
    for form in ("turbo::page::Frame", "::turbo::page::Frame"):
        q = ('void qbad(App& app) {\n'
             '  %s f(app, {"id", "The new page", "help", nullptr});\n'
             '  if (ImGui::BeginTable("##t", 2, ImGuiTableFlags_RowBg)) {}\n'
             '  std::vector<Step> undo_stack;\n'
             '}\n' % form)
        fl, adv, sp = analyse(q)
        rules = sorted(r for _, r, _ in fl)
        check(len(sp) == 1 and sp[0][0] == "qbad" and rules == ["T1", "T2", "T3"], "%s is a template page and its violations are caught: %s" % (form, rules))
    for form in ("shell::page::Frame", "turbo::shell::page::Frame", "xpage::Frame"):
        q = 'void other(App& app) {\n  %s f(app);\n}\n' % form
        check(template_spans(q) == [], "%s is not the template" % form)
    # round 1 fix 2: a tree with no template page FAILS (a lint that checks nothing must not pass)
    import tempfile, io, contextlib
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, "turbogui", "src", "ui"))
        with open(os.path.join(d, "turbogui", "src", "ui", "x.cpp"), "w") as f:
            f.write('void plain(App& app) { ImGui::Button("Open##x"); }\n')
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc_empty = scan_tree(d)
        check(rc_empty == 1 and "T0" in buf.getvalue(), "a tree with zero template pages fails (rc=%d)" % rc_empty)
        with open(os.path.join(d, "turbogui", "src", "ui", "y.cpp"), "w") as f:
            f.write('void good(App& app) {\n  turbo::page::Frame f(app, {"id", "Title", "help", nullptr});\n}\n')
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc_one = scan_tree(d)
        check(rc_one == 0, "the same tree with one clean template page (written turbo::page::Frame) passes (rc=%d)" % rc_one)
    return ok


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--self-test" in sys.argv:
        return 0 if self_test() else 1
    base = os.path.abspath(args[0]) if args else os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    if os.path.basename(base) == "turbogui":
        base = os.path.dirname(base)
    return scan_tree(base)


def scan_tree(base):
    if not os.path.isdir(os.path.join(base, "turbogui", "src", "ui")):
        print("lint_one_component: no turbogui/src/ui under %s" % base)
        return 2
    rel = lambda p: os.path.relpath(p, base).replace("\\", "/")
    failures, adv_all, pages = [], [], 0
    for p in source_files(base):
        src = read(p)
        fl, adv, sp = analyse(src)
        pages += len(sp)
        failures += ["FAIL %s %s:%d  %s" % (r, rel(p), ln, msg) for ln, r, msg in fl]
        if "/src/ui/" in "/" + rel(p):  # the GUI only: core/ keeps the data-layer records the Changes list reads
            adv_all += [(rel(p), ln, r, msg) for ln, r, msg in adv]
    if pages == 0:
        # the same rule as the native guard (test_gui_labels.h): a scanner that finds no template page checks nothing, so it must not pass
        failures.append("FAIL T0 turbogui/src/ui  no template page found (no function constructs a page::Frame): the lint would check nothing")
    for f in failures:
        print(f)
    counts = {}
    for _, _, r, _ in adv_all:
        counts[r] = counts.get(r, 0) + 1
    print("ADVISORY (tree-wide, never fails): raw BeginTable=%d own-undo-store=%d classic/old/new-label=%d | template pages=%d | failures=%d"
          % (counts.get("BeginTable", 0), counts.get("undo", 0), counts.get("label", 0), pages, len(failures)))
    if "--list" in sys.argv:
        for f, ln, r, msg in sorted(adv_all):
            print("  advisory %-10s %s:%d  %s" % (r, f, ln, msg))
    print("lint_one_component: %s (%d template page(s) checked)" % ("FAIL" if failures else "PASS", pages))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
