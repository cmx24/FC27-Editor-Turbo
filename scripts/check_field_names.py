#!/usr/bin/env python3
"""Checks every database table/field name Turbo uses against independent sources:

  1. EA's official FIFA database schema (db_meta.xml shipped with xAranaktu's FIFA 21 Live Editor, GPL-3.0 repo)
  2. field and table names used by xAranaktu's own FC 24 / FC 25 / FC 26 Live Editor Lua scripts (newer fields)

Names Turbo uses are taken from the GUI sources (field lists and field lookups) and the Lua modules (table/field
calls and FIELDS lists). A name found in neither source fails the check unless it is listed in ALLOWED with a reason.
Needs git and network access (GitHub). Usage: python3 scripts/check_field_names.py [cache dir]
"""
import glob
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "turbogui", "build", "schema_sources")
REPOS = ["FIFA-21-Live-Editor", "FC-24-Live-Editor", "FC-25-Live-Editor", "FC-26-Live-Editor"]

# Names not in either source, kept on purpose. The GUI drops a field silently when the game's table lacks it.
ALLOWED = {
    "physioaccess_senior": "teams overview: newer CM facility field, unverified; shown only if FC 27 has it",
    "preferredposition5": "FC 26 added preferred positions 5-7 (LE v26.3.2); shown only if present",
    "preferredposition6": "same",
    "preferredposition7": "same",
    "cm_teamsheets": "read by FC 27 Live Editor's own career_mode/helpers.lua",
}


def fetch():
    os.makedirs(CACHE, exist_ok=True)
    for r in REPOS:
        d = os.path.join(CACHE, r)
        if not os.path.isdir(d):
            subprocess.run(["git", "clone", "-q", "--depth", "1", "https://github.com/xAranaktu/%s.git" % r, d], check=True)


def known_names():
    meta = ET.parse(os.path.join(CACHE, "FIFA-21-Live-Editor", "data", "db_meta.xml")).getroot()
    tables, fields = set(), set()
    for t in meta.iter("table"):
        tables.add(t.get("name"))
        for f in t.iter("field"):
            fields.add(f.get("name"))
    for p in glob.glob(os.path.join(CACHE, "FC-2*", "**", "*.lua"), recursive=True):
        s = open(p, encoding="utf-8", errors="replace").read()
        fields.update(re.findall(r'(?:Get|Set)RecordFieldValue\([^,]+,\s*"([a-z0-9_]+)"', s))
        fields.update(re.findall(r'\b([a-z0-9_]+)\s*=\s*entry\["', s))
        tables.update(re.findall(r'GetTable\("([a-z0-9_]+)"\)', s))
    return tables, fields


def used_names():
    used = {}

    def add(name, where):
        used.setdefault(name, set()).add(os.path.relpath(where, ROOT))

    gui = glob.glob(os.path.join(ROOT, "turbogui", "src", "**", "*.cpp"), recursive=True)
    for p in gui:
        s = open(p).read()
        for m in re.finditer(r'(?:field|has|table)\("([a-z][a-z0-9_]+)"\)', s):
            add(m.group(1), p)
        for m in re.finditer(r'get_int\([^,]+,\s*(?:[^,]+,\s*)?"([a-z][a-z0-9_]+)"', s):
            add(m.group(1), p)
        # field lists: string literals inside the editor field-list functions and field_grid calls
        for block in re.findall(r'(?:fields\(\)\s*|field_grid\(app,[^{]*)\{([^}]*)\}', s, flags=re.S):
            for q in re.findall(r'"([a-z][a-z0-9_]+)"', block):
                add(q, p)
        for block in re.findall(r'attribute_groups\(\)[^;]*?=\s*\{(.*?)\};', s, flags=re.S):
            for q in re.findall(r'\{"[A-Z][a-z]+",\s*\{([^}]*)\}', block):
                for f in re.findall(r'"([a-z][a-z0-9_]+)"', q):
                    add(f, p)
    lua = glob.glob(os.path.join(ROOT, "turbo", "package", "lua", "libs", "v2", "imports", "turbo", "**", "*.lua"), recursive=True)
    call = re.compile(r'(?:get_table|db\.get|db\.set|db\.find|has_field|field_info|GetTable|GetRecordFieldValue|SetRecordFieldValue)\s*\(([^)]*)\)')
    for p in lua:
        s = open(p).read()
        for m in call.finditer(s):
            for q in re.findall(r'"([a-z][a-z0-9_]+)"', m.group(1)):
                add(q, p)
        for block in re.findall(r'FIELDS\s*=\s*\{([^}]*)\}', s):
            for q in re.findall(r'"([a-z][a-z0-9_]+)"', block):
                add(q, p)
    return used


def main():
    fetch()
    tables, fields = known_names()
    used = used_names()
    bad = 0
    for name in sorted(used):
        if name in tables or name in fields:
            continue
        if name in ALLOWED:
            print("  allowed  %-24s %s" % (name, ALLOWED[name]))
            continue
        bad += 1
        print("  UNKNOWN  %-24s used in %s" % (name, ", ".join(sorted(used[name]))))
    print("%d names checked: %d known, %d allowed, %d unknown" % (len(used), len(used) - bad - sum(n in ALLOWED for n in used), sum(n in ALLOWED for n in used), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
