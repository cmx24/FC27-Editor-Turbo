#!/usr/bin/env python3
"""Make a Turbo spoken-callname list (turbo\\callnames\\spoken_<lang>.txt) from a commentary bank export.

The Turbo GUI (Players > Callname) needs to know which commentary ids have spoken audio in the commentary language
the game loaded. The banks are Frostbite superbundles Turbo does not parse; the ids come from an export of the
language's generic surname family (pSIMPLE_SURNAME) made with the FIFA Editor Tool ("Export Data Set" of the family's
selection table) or from any CSV that has a column with the commentary ids (commentaryid / surname_ID / cm_sim).

usage: python scripts/callnames_spoken_list.py <lang> <export.csv> [more.csv ...] -o "<Live Editor>/turbo/callnames/spoken_<lang>.txt"
       column auto-detected (first of: commentaryid, surname_ID, surname_id, cm_sim); --column NAME to force one
Only ids in 900000..965000 are kept (the player-name range of commentarynames). Output: "#turbo-spoken <lang> <count>"
then one id per line, sorted. Encoding of the inputs: UTF-8 (with or without BOM) or UTF-16 (FET / squad exports).
"""
import argparse
import csv
import io
import sys

CANDIDATES = ["commentaryid", "surname_ID", "surname_id", "cm_sim"]
LO, HI = 900000, 965000


def read_rows(path):
    raw = open(path, "rb").read()
    if raw[:2] in (b"\xff\xfe", b"\xfe\xff"):
        text = raw.decode("utf-16")
    else:
        text = raw.decode("utf-8-sig")
    sample = text[:4096]
    delim = "\t" if sample.count("\t") > sample.count(",") else ","
    return list(csv.DictReader(io.StringIO(text), delimiter=delim))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("lang", help="language code as the game names its pack, e.g. ita_it, por_br, eng_us")
    ap.add_argument("files", nargs="+", help="CSV/TSV exports")
    ap.add_argument("-o", "--out", required=True, help="output file (spoken_<lang>.txt)")
    ap.add_argument("--column", help="column holding the commentary ids")
    a = ap.parse_args()
    ids = set()
    for f in a.files:
        rows = read_rows(f)
        if not rows:
            print(f"{f}: no rows", file=sys.stderr)
            continue
        col = a.column or next((c for c in CANDIDATES if c in rows[0]), None)
        if not col:
            print(f"{f}: no commentary-id column among {list(rows[0].keys())}; use --column", file=sys.stderr)
            return 2
        n = 0
        for r in rows:
            try:
                v = int(str(r[col]).strip())
            except (TypeError, ValueError):
                continue
            if LO <= v <= HI:
                ids.add(v)
                n += 1
        print(f"{f}: column {col}, {n} ids in range")
    if not ids:
        print("no ids found", file=sys.stderr)
        return 1
    with open(a.out, "w", encoding="utf-8", newline="\n") as o:
        o.write(f"#turbo-spoken {a.lang.lower()} {len(ids)}\n")
        for v in sorted(ids):
            o.write(f"{v}\n")
    print(f"wrote {a.out}: {len(ids)} spoken commentary ids for {a.lang}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
