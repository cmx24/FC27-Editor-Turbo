#!/usr/bin/env python3
"""Checks every database table/field name Turbo uses against the REAL FC 27 database schema.

The schema comes from the user's own game: run lua\\scripts\\turbo_probe.lua in game (Live Editor's Lua Engine); it writes
turbo_output\\fc27_db_schema.json (GetDBMeta: every table and field with type, range and bit depth). Copy it to
turbo/le27/fc27_db_schema.json (gitignored, like Live Editor's own files). Names are collected exactly like
scripts/check_field_names.py does. A name the game does not have is reported with where it is used; names in OPTIONAL are
looked up with has()/field() and simply skipped when absent (FC 26-only fields kept for older/newer builds).
Usage: python3 scripts/check_fc27_schema.py [schema.json]
       python3 scripts/check_fc27_schema.py --coverage [schema.json]

Coverage mode lists the fields of the players, teams and manager tables that appear nowhere in turbogui/src/ui/*.cpp or
turbogui/src/core/field_labels.h (a field is "covered" when its name occurs there as a whole word). It is informational
(exit 0). Without the schema file it prints a hint and exits 0, so it works offline and in CI.
"""
import re
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from check_field_names import used_names  # noqa: E402

# Used only behind a presence check (the GUI/Lua side skips them when the table lacks the field)
OPTIONAL = {
    "career_playercontract": "FC 26 contract table; FC 27 has none. extend_user_contracts / squad_role use it only if present",
    "contract_date": "career_playercontract field (see above)",
    "contract_status": "career_playercontract field (see above)",
    "duration_months": "career_playercontract field (see above)",
    "last_status_change_date": "career_playercontract field (see above)",
    "playerrole": "career_playercontract field (see above); FC 27 squad roles are written through memory",
    "marking": "FC 26 name; FC 27 calls it defensiveawareness (both listed, the GUI shows the one present)",
    "attackingworkrate": "FC 26 only; FC 27 replaced work rates by player roles (role1..9)",
    "defensiveworkrate": "same",
    "physioaccess_senior": "teams overview field of other builds; FC 27 has none",
}


COVERAGE_TABLES = ("players", "teams", "manager")
COVERAGE_SOURCES = ("turbogui/src/ui/*.cpp", "turbogui/src/core/field_labels.h")


def coverage(path):
    import glob
    if not os.path.exists(path):
        print("coverage: schema missing (%s); nothing to compare. Run turbo_probe.lua in game and copy "
              "turbo_output\\fc27_db_schema.json there." % path)
        return 0
    try:
        schema = json.load(open(path, encoding="utf-8"))
        tables = schema["tables"]
    except (ValueError, KeyError, OSError) as e:
        print("coverage: cannot read schema (%s); skipped" % e)
        return 0
    text = ""
    nfiles = 0
    for pat in COVERAGE_SOURCES:
        for f in glob.glob(os.path.join(ROOT, pat)):
            with open(f, encoding="utf-8", errors="replace") as fh:
                text += fh.read() + "\n"
            nfiles += 1
    words = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text))
    total = 0
    for tname in COVERAGE_TABLES:
        t = tables.get(tname)
        if t is None:
            print("%s: table not in schema" % tname)
            continue
        names = [f["name"] for f in t.get("fields", [])]
        missing = [n for n in names if n not in words]
        total += len(missing)
        print("%s: %d fields, %d not referenced in the GUI sources" % (tname, len(names), len(missing)))
        for n in missing:
            print("    " + n)
    print("coverage: %d source files scanned, %d uncovered fields" % (nfiles, total))
    return 0


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    default = os.path.join(ROOT, "turbo", "le27", "fc27_db_schema.json")
    if "--coverage" in sys.argv[1:]:
        return coverage(args[0] if args else default)
    path = args[0] if args else default
    if not os.path.exists(path):
        print("schema missing: run turbo_probe.lua in game and copy turbo_output\\fc27_db_schema.json to " + path)
        return 2
    schema = json.load(open(path, encoding="utf-8"))
    tables = set(schema["tables"])
    fields = {f["name"] for t in schema["tables"].values() for f in t["fields"]}
    used = used_names()
    missing = []
    for name in sorted(used):
        if name in tables or name in fields:
            continue
        missing.append(name)
        tag = "optional" if name in OPTIONAL else "MISSING "
        print("  %s %-26s used in %s%s" % (tag, name, ", ".join(sorted(used[name])),
                                           (" - " + OPTIONAL[name]) if name in OPTIONAL else ""))
    hard = [n for n in missing if n not in OPTIONAL]
    print("FC 27 schema (%s, %d tables): %d names checked, %d present, %d optional absent, %d missing"
          % (schema.get("le_version", "?"), len(tables), len(used), len(used) - len(missing), len(missing) - len(hard), len(hard)))
    return 1 if hard else 0


if __name__ == "__main__":
    sys.exit(main())
