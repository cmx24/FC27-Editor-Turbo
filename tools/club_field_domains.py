"""club_field_domains.py - the value sets of the club, crowd, stadium and kit fields of 2.20 K3 (brief 2.20-K3-club-fields).

Reads a STOCK squad file (read-only, through squads_pack\\tools\\squaddb.py) and emits, for every field Turbo's "Crowd & presentation",
"Home ground" and kit-extras sections draw, the values the file holds and how many rows hold each. Invariant 4 of the fc27-modding
skill: a value Turbo writes must exist in the base file's value set for that column, never FC Editor's _temp export and never an FC 26 enum.

  py -3 -I tools/club_field_domains.py --squad "<squad file>" --emit            print the C++ block that core/club_fields.h embeds
  py -3 -I tools/club_field_domains.py --squad "<squad file>" --write           replace the block between the markers of core/club_fields.h
  py -3 -I tools/club_field_domains.py --squad "<squad file>" --check           exit 1 when the header's block differs from the file's values
  py -3 -I tools/club_field_domains.py --header-check                           offline: the header block is well formed (no squad file needed)
  py -3 -I tools/club_field_domains.py --squad "<squad file>" --team 95         print one club's values and its home stadium
  py -3 -I tools/club_field_domains.py --self-test                              synthetic columns, no squad file, no game

The generated block names its source file and md5, so a header built from another file is visible in the diff. The squad file is only
read; nothing under the game's settings folder or turbo_output is written (bridge_meta.json, which squaddb reads, is read too).
"""
import argparse
import collections
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HEADER = os.path.join(ROOT, "turbogui", "src", "core", "club_fields.h")
SQUADDB_DIR = r"C:\FC 27 Live Editor\turbo_dev\squads_pack\tools"
DEFAULT_SQUAD = os.path.expandvars(r"%LOCALAPPDATA%\EA SPORTS FC 27\settings\Squads20261004161718434")

BEGIN = "// BEGIN GENERATED club_field_domains.py"
END = "// END GENERATED club_field_domains.py"

# (table, field): the fields of the three sections, in the order the header lists them. teamkittypetechid is here only for its labels.
FIELDS = [
    ("teams", f)
    for f in (
        "hastifo isbannerenabled crowdregion crowdskintonecode hasstandingcrowd hasvikingclap haslargeflag skinnyflags "
        "iscompetitionscarfenabled iscompetitionpoleflagenabled iscompetitioncrowdcardsenabled stanchionflamethrower "
        "flamethrowercannon hassubstitutionboard pitchwear stadiummowpattern_code playsurfacetype presassetone presassettwo "
        "ballid trainingstadium"
    ).split()
] + [
    ("stadiums", f)
    for f in (
        "tifo supportsunlicensedtifos supportsunlicensedbanners seatcolor colorregion adboardtype policetypecode defaultweather "
        "tod1weather tod2weather tod3weather tod4weather hasmexicanwave pitchtarps goalnetstanchiontype stadiumgoalnettype "
        "stadiumgoalnettension stadiumgoalpoststyle"
    ).split()
] + [
    ("teamkits", f)
    for f in (
        "isgeneric jerseyfit shortstyle jerseycollargeometrytype jerseyrestriction armbandtype captainarmband hasadvertisingkit "
        "jerseyfrontnumberplacementcode jerseybacknameplacementcode shortsnumberplacementcode jerseybacknamefontcase teamkittypetechid"
    ).split()
]


def domain(values):
    """[(value, rows holding it)] ascending by value"""
    return sorted(collections.Counter(values).items())


def md5_of(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def emit_block(columns, source_name, source_md5, counts):
    """The text between (and including) the two marker lines. `columns` = {(table, field): [values]}."""
    out = [BEGIN + " - do not edit by hand: py -3 tools/club_field_domains.py --squad <file> --write",
           "// source: %s  md5 %s  rows: %s" % (source_name, source_md5, ", ".join("%s %d" % (t, counts[t]) for t in sorted(counts))),
           "// per field: the values the stock file holds (ascending) and how many rows hold each"]
    for i, (table, field) in enumerate(FIELDS):
        dom = domain(columns[(table, field)])
        out.append("static const int kDomV%d[] = {%s};  // %s.%s" % (i, ", ".join(str(v) for v, _ in dom), table, field))
        out.append("static const int kDomN%d[] = {%s};" % (i, ", ".join(str(n) for _, n in dom)))
    out.append("static const GenDomain kGenDomains[] = {")
    for i, (table, field) in enumerate(FIELDS):
        out.append('    {"%s", "%s", %d, kDomV%d, kDomN%d},' % (table, field, len(domain(columns[(table, field)])), i, i))
    out.append("};")
    out.append(END)
    return "\n".join(out) + "\n"


def read_columns(sq):
    cols, counts = {}, {}
    for table, field in FIELDS:
        t = sq[table]
        counts[table] = t.valid
        cols[(table, field)] = [t.get(i, field) for i in range(t.valid)]
    return cols, counts


def load_squad(path):
    sys.path.insert(0, SQUADDB_DIR)
    import squaddb  # noqa: E402
    return squaddb.SquadFile(path)


def block_in(text):
    a, b = text.find(BEGIN), text.find(END)
    if a < 0 or b < a:
        return None
    return text[a:b + len(END)] + "\n"


def cmd_header_check():
    text = open(HEADER, encoding="utf-8").read()
    blk = block_in(text)
    if blk is None:
        print("FAIL header has no generated block")
        return 1
    n_dom = len(re.findall(r"^static const int kDomV\d+\[\]", blk, re.M))
    n_row = len(re.findall(r'^    \{"(?:teams|stadiums|teamkits)", "', blk, re.M))
    ok = n_dom == len(FIELDS) and n_row == len(FIELDS)
    print("%s header block: %d value arrays, %d domain rows, %d fields in the tool" % ("PASS" if ok else "FAIL", n_dom, n_row, len(FIELDS)))
    return 0 if ok else 1


def self_test():
    fails = []

    def check(c, m):
        print(("  ok   " if c else "  FAIL ") + m)
        if not c:
            fails.append(m)

    check(domain([20, 70, 20, 90, 40, 20]) == [(20, 3), (40, 1), (70, 1), (90, 1)], "domain: ascending values with their row counts")
    check(domain([]) == [], "domain: empty column")
    cols = {k: [0, 1, 1] for k in FIELDS}
    cols[("teams", "crowdskintonecode")] = [20, 40, 70, 90, 20]
    blk = emit_block(cols, "Fixture", "0" * 32, {"teams": 5, "stadiums": 3, "teamkits": 3})
    check(blk.startswith(BEGIN) and blk.rstrip().endswith(END), "emit: the block is bracketed by the two markers")
    check("kDomV%d[] = {20, 40, 70, 90};  // teams.crowdskintonecode" % FIELDS.index(("teams", "crowdskintonecode")) in blk,
          "emit: crowdskintonecode keeps its four values")
    check(blk.count("static const int kDomV") == len(FIELDS) and blk.count('    {"') == len(FIELDS), "emit: one array and one row per field")
    check(block_in("x\n" + blk + "y\n") == blk, "block_in finds the block again")
    check(block_in("no block here") is None, "block_in: none when the markers are missing")
    check(len(set(FIELDS)) == len(FIELDS), "the field list has no duplicates")
    print("SELF-TEST " + ("FAILED" if fails else "PASSED"))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--squad", default=DEFAULT_SQUAD)
    ap.add_argument("--emit", action="store_true")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--header-check", action="store_true")
    ap.add_argument("--team", type=int)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()
    if a.self_test:
        return self_test()
    if a.header_check:
        return cmd_header_check()
    if not (a.emit or a.write or a.check or a.team):
        ap.print_help()
        return 2
    sq = load_squad(a.squad)
    if a.team:
        t = sq["teams"]
        rows = [i for i in range(t.valid) if t.get(i, "teamid") == a.team]
        if not rows:
            print("no team %d in %s" % (a.team, a.squad))
            return 1
        for table, field in FIELDS:
            if table == "teams":
                print("teams.%s = %d" % (field, t.get(rows[0], field)))
        sl = sq["teamstadiumlinks"]
        sid = [sl.get(i, "stadiumid") for i in range(sl.valid) if sl.get(i, "teamid") == a.team]
        print("home stadium id(s): %s" % sid)
        st = sq["stadiums"]
        for s in sid:
            for i in range(st.valid):
                if st.get(i, "stadiumid") == s:
                    for table, field in FIELDS:
                        if table == "stadiums":
                            print("stadiums.%s = %d" % (field, st.get(i, field)))
        return 0
    cols, counts = read_columns(sq)
    blk = emit_block(cols, os.path.basename(a.squad), md5_of(a.squad), counts)
    if a.emit:
        sys.stdout.write(blk)
    if a.check or a.write:
        text = open(HEADER, encoding="utf-8").read()
        cur = block_in(text)
        if cur is None:
            print("the header has no generated block (%s)" % HEADER)
            return 1
        same = cur == blk
        if a.check:
            print("%s the header's block %s the values of %s" % ("PASS" if same else "FAIL", "equals" if same else "differs from", os.path.basename(a.squad)))
            return 0 if same else 1
        a0 = text.find(BEGIN)
        b0 = text.find(END) + len(END) + 1
        open(HEADER, "w", encoding="utf-8", newline="\n").write(text[:a0] + blk + text[b0:])
        print("wrote the block into " + HEADER)
    return 0


if __name__ == "__main__":
    sys.exit(main())
