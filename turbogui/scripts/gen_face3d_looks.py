#!/usr/bin/env python3
"""FC 27 LE Turbo GUI - generate the 3D looks of the real faces (core/face3d_looks.h).

Merges the classification CSVs of the game's 3D head renders (header
`id,skin,hair_colour,hair_length,hair_type,facial_hair,beard_colour,headwear[,conf]`, id = playerid = the head id of a real
face, values from the word lists below, conf = one letter per trait: h sure, m probable, l unsure; a row without conf is
sure in every trait) into
  turbogui/data/face3d_looks.csv           the merged copy kept in the repo (sorted by id, always with conf; regenerate
                                           from it with --repo)
  turbogui/src/core/face3d_looks_data.h    the table compiled into Turbo ({playerid, {skin, hair_colour, ...}, {conf...}}
                                           as indexes) and the hand check (data/face3d_looks_check.csv, same columns:
                                           heads checked one by one against their renders; the chooser compares the
                                           table with it for its confidence note)
Bad rows (wrong column count, bad id, unknown word, bad conf) are skipped and counted; when an id is classified twice the
last row wins (files in natural order: face3d_class_2 before face3d_class_10). The output depends only on the input rows
(sorted by id), so a rerun gives the same files.

usage: gen_face3d_looks.py csv-or-folder ...   (a folder: its face3d_class_*.csv files)
       gen_face3d_looks.py --repo              (regenerate the header from turbogui/data/face3d_looks.csv only)
       gen_face3d_looks.py --check             (exit 1 when the repo CSV or the header differ from what --repo writes)
"""
import csv, glob, os, re, sys

# Word lists: the order is the index stored in the header (core/face3d_looks.h enums). Append only.
TRAITS = [
    ("skin", ["very_light", "light", "medium", "olive", "brown", "dark"]),
    ("hair_colour", ["black", "dark_brown", "brown", "light_brown", "blonde", "red", "grey", "white", "dyed", "none"]),
    ("hair_length", ["bald", "buzz", "short", "medium", "long"]),
    ("hair_type", ["straight", "wavy", "curly", "afro", "braids", "dreads", "twists", "tied", "mohawk", "slicked", "spiky", "other"]),
    ("facial_hair", ["clean", "stubble", "moustache", "goatee", "short_beard", "full_beard", "long_beard"]),
    ("beard_colour", ["none", "black", "dark_brown", "brown", "blonde", "red", "grey"]),
    ("headwear", ["none", "headband", "hairband", "hat"]),
]
CONF = "hml"  # face3d::Conf: kConfHigh 0, kConfMedium 1, kConfLow 2 (the filters treat l as unknown)
HEADER = ["id"] + [t for t, _ in TRAITS] + ["conf"]

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
REPO_CSV = os.path.join(ROOT, "data", "face3d_looks.csv")
CHECK_CSV = os.path.join(ROOT, "data", "face3d_looks_check.csv")
HEADER_H = os.path.join(ROOT, "src", "core", "face3d_looks_data.h")


def natural(path):
    return [int(p) if p.isdigit() else p for p in re.split(r"(\d+)", os.path.basename(path).lower())]


def parse_row(row):
    """A CSV row -> (id, (index, ...), (conf, ...)) or None when the row is bad."""
    row = [c.strip().lower() for c in row]
    if len(row) not in (len(TRAITS) + 1, len(TRAITS) + 2):
        return None
    try:
        pid = int(row[0])
    except ValueError:
        return None
    if pid <= 0 or pid > 0x7FFFFFFF:
        return None
    vals = []
    for (name, words), w in zip(TRAITS, row[1:1 + len(TRAITS)]):
        if w not in words:
            return None
        vals.append(words.index(w))
    conf = [0] * len(TRAITS)
    if len(row) == len(TRAITS) + 2:
        c = row[-1]
        if len(c) != len(TRAITS) or any(ch not in CONF for ch in c):
            return None
        conf = [CONF.index(ch) for ch in c]
    return pid, tuple(vals), tuple(conf)


def read_csvs(paths):
    looks, bad, dup = {}, 0, 0
    for p in paths:
        with open(p, encoding="utf-8-sig", newline="") as fh:
            for n, row in enumerate(csv.reader(fh)):
                if not row or not "".join(row).strip():
                    continue
                if n == 0 and row[0].strip().lower() == "id":
                    continue
                r = parse_row(row)
                if r is None:
                    bad += 1
                    continue
                if r[0] in looks:
                    dup += 1
                looks[r[0]] = (r[1], r[2])
    return looks, bad, dup


def repo_csv_text(looks):
    out = [",".join(HEADER)]
    for pid in sorted(looks):
        vals, conf = looks[pid]
        out.append(",".join([str(pid)] + [TRAITS[i][1][v] for i, v in enumerate(vals)] + ["".join(CONF[c] for c in conf)]))
    return "\n".join(out) + "\n"


def table_lines(name, looks):
    if not looks:
        return ["constexpr Look %s[] = {{0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}}};  // no data: count 0" % name]
    out = ["constexpr Look %s[] = {" % name]
    line = "   "
    for pid in sorted(looks):
        vals, conf = looks[pid]
        cell = " {%d, {%s}" % (pid, ",".join(str(v) for v in vals))
        cell += (", {%s}}," % ",".join(str(c) for c in conf)) if any(conf) else "},"  # all sure: the zero default
        if len(line) + len(cell) > 118:
            out.append(line)
            line = "   "
        line += cell
    out += [line, "};"]
    return out


def header_text(looks, checks):
    unsure = sum(1 for v, c in looks.values() for x in c if x == 2)
    out = ["// FC 27 LE Turbo GUI - 3D looks of the real faces (core/face3d_looks.h). GENERATED by scripts/gen_face3d_looks.py",
           "// from data/face3d_looks.csv and data/face3d_looks_check.csv: do not edit. %d heads classified from the game's 3D"
           % len(looks),
           "// renders (%d unsure trait values), %d heads in the hand check." % (unsure, len(checks)),
           "// {playerid, {skin, hair_colour, hair_length, hair_type, facial_hair, beard_colour, headwear}, {confidence of each}}:",
           "// indexes into the word lists of the script (face3d::Skin, HairColour, ... ; face3d::Conf 0 sure, 1 probable,",
           "// 2 unsure; a look without the second list is sure in every trait).",
           "#pragma once", "", "namespace turbo::face3d::data {", ""]
    out += table_lines("kLooks", looks)
    out.append("constexpr size_t kCount = %s;" % ("sizeof(kLooks) / sizeof(kLooks[0])" if looks else "0"))
    out += ["", "// the hand check: heads checked one by one against their renders"]
    out += table_lines("kChecks", checks)
    out.append("constexpr size_t kCheckCount = %s;" % ("sizeof(kChecks) / sizeof(kChecks[0])" if checks else "0"))
    out += ["", "}  // namespace turbo::face3d::data", ""]
    return "\n".join(out)


def write_text(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)


def read_text(path):
    try:
        with open(path, encoding="utf-8", newline="") as fh:
            return fh.read()
    except OSError:
        return None


def main(argv):
    args = argv[1:]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    check = args == ["--check"]
    if args in (["--repo"], ["--check"]):
        paths = [REPO_CSV]
    else:
        paths = []
        for a in args:
            if os.path.isdir(a):
                paths += sorted(glob.glob(os.path.join(a, "face3d_class_*.csv")), key=natural)
            elif os.path.isfile(a):
                paths.append(a)
            else:
                print("not found: %s" % a, file=sys.stderr)
                return 2
    if not paths:
        print("no face3d_class_*.csv found", file=sys.stderr)
        return 2
    looks, bad, dup = read_csvs(paths)
    checks, cbad, _ = read_csvs([CHECK_CSV]) if os.path.isfile(CHECK_CSV) else ({}, 0, 0)
    csv_text, h_text = repo_csv_text(looks), header_text(looks, checks)
    if check:
        stale = [p for p, t in ((REPO_CSV, csv_text), (HEADER_H, h_text)) if read_text(p) != t]
        for p in stale:
            print("STALE: %s (run gen_face3d_looks.py --repo)" % p)
        print("face3d looks: %d heads, hand check %d heads, %d + %d bad rows: %s"
              % (len(looks), len(checks), bad, cbad, "stale" if stale else "up to date"))
        return 1 if stale or bad or cbad else 0
    write_text(REPO_CSV, csv_text)
    write_text(HEADER_H, h_text)
    print("face3d looks: %d heads from %d CSV file(s), %d bad rows skipped, %d duplicates (last wins); hand check %d heads"
          " (%d bad rows)" % (len(looks), len(paths), bad, dup, len(checks), cbad))
    print("  %s\n  %s" % (REPO_CSV, HEADER_H))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
