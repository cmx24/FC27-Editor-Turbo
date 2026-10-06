#!/usr/bin/env python3
"""FC 27 LE Turbo GUI - generate src/core/appearance_labels_data.h (core/field_labels.h) and the native tests' fixture
tests/native/fixture_player_codes.h.

From Turbo's own export of the FC 27 players table (Turbo Tools > Exports > Export tables: turbo_table_players.csv) and
the name texts Live Editor decodes for Turbo (bridge_names.txt, "nameid<TAB>text"):
  * kUsers: who uses each player-specific code: body types outside Live Editor's generic ones ("Unique: Lionel Messi"),
    free kick / penalty stances and running animations other than the standard 0. The three best-rated players first,
    then how many more.
From FC Editor's FC 27 brand maps (tables/bootmap27.txt, glovesmap27.txt: one column of ids per brand):
  * kBoots / kGloves: the brand of each boot (shoetypecode) and GK glove (gkglovetypecode) id. The game's own boot and
    glove names on disk are FC 26's (LanguageStrings), and FC 27 moved many ids to other brands, so no model names.
The fixture: every value each code-like players field holds (codes only, no player data), so the tests can run every
label over every value the real database uses.

usage: gen_appearance_labels.py <export dir with turbo_table_players.csv and bridge_names.txt> <FC Editor tables dir>
"""
import csv, collections, datetime, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.join(HERE, "..", "src", "core")
TESTS = os.path.join(HERE, "..", "tests", "native")

GENERIC_BODIES = {1, 2, 3, 4, 5, 6, 7, 8, 9, 11}  # Live Editor's bodytype_N names (field_labels.h)
SIGNATURE = ["animfreekickstartposcode", "animpenaltiesstartposcode", "runningcode1", "runningcode2"]  # 0 = standard
# fields the fixture leaves out: attributes, ratings, ids, dates, money (numbers, not codes)
NUMBERS = {"acceleration", "sprintspeed", "agility", "balance", "jumping", "stamina", "strength", "reactions", "aggression",
           "composure", "interceptions", "positioning", "vision", "ballcontrol", "crossing", "dribbling", "finishing",
           "freekickaccuracy", "headingaccuracy", "longpassing", "shortpassing", "defensiveawareness", "shotpower", "longshots",
           "standingtackle", "slidingtackle", "volleys", "curve", "penalties", "gkdiving", "gkhandling", "gkkicking",
           "gkreflexes", "gkpositioning", "overallrating", "potential", "modifier", "height", "weight", "birthdate",
           "playerjointeamdate", "contractvaliduntil", "playerid", "firstnameid", "lastnameid", "commonnameid",
           "playerjerseynameid", "headassetid", "nationality", "wage", "releaseclause", "trait1", "trait2", "icontrait1",
           "icontrait2", "pacdiv", "shohan", "paskic", "driref", "defspe", "phypos", "finishingcode1", "finishingcode2"}


def read_names(path):
    names = {}
    for line in open(path, "rb").read().split(b"\n"):
        p = line.rstrip(b"\r").split(b"\t")
        if len(p) >= 2 and p[0].lstrip(b"-").isdigit():
            names[int(p[0])] = p[1].decode("utf-8", "replace").strip()
    return names


def player_name(r, names):
    common = names.get(int(r["commonnameid"]), "") if int(r["commonnameid"] or 0) else ""
    if common:
        return common
    return " ".join(x for x in (names.get(int(r["firstnameid"]), ""), names.get(int(r["lastnameid"]), "")) if x) or "#" + r["playerid"]


def brand_map(path):
    raw = open(path, "rb").read()
    text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("utf-8", "replace")
    rows = [l.rstrip("\r").split("\t") for l in text.split("\n") if l.strip()]
    brands = [h.strip() for h in rows[0]]
    ids = {}
    for r in rows[1:]:
        for col, cell in enumerate(r):
            if cell.strip().isdigit():
                ids[int(cell)] = col
    return brands, ids


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def wrap(cells, indent="    ", width=118):
    out, line = [], indent
    for c in cells:
        if len(line) + len(c) + 1 > width and line.strip():
            out.append(line.rstrip())
            line = indent
        line += c + " "
    if line.strip():
        out.append(line.rstrip())
    return out


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    export, fce = argv[1], argv[2]
    rows = list(csv.DictReader(open(os.path.join(export, "turbo_table_players.csv"), encoding="utf-8")))
    names = read_names(os.path.join(export, "bridge_names.txt"))
    stamp = datetime.date.fromtimestamp(os.path.getmtime(os.path.join(export, "turbo_table_players.csv"))).isoformat()

    # who uses each player-specific code, best-rated first
    users = []
    for field in ["bodytypecode"] + SIGNATURE:
        by = collections.defaultdict(list)
        for r in rows:
            v = int(r[field])
            if (field == "bodytypecode" and v not in GENERIC_BODIES) or (field != "bodytypecode" and v != 0):
                by[v].append((-int(r["overallrating"]), int(r["playerid"]), player_name(r, names)))
        for v in sorted(by):
            best = sorted(by[v])
            users.append((field, v, len(best), ", ".join(n for _, _, n in best[:3])))

    boot_brands, boots = brand_map(os.path.join(fce, "bootmap27.txt"))
    glove_brands, gloves = brand_map(os.path.join(fce, "glovesmap27.txt"))

    out = ["// FC 27 LE Turbo GUI - appearance label data (core/field_labels.h). GENERATED by scripts/gen_appearance_labels.py: do not edit.",
           "// kUsers: Turbo's export of the FC 27 players table (%d players, %s) with the names Live Editor decodes" % (len(rows), stamp),
           "// (bridge_names.txt): the three best-rated players using each player-specific code and how many use it.",
           "// kBoots / kGloves: brand of each id from FC Editor's FC 27 maps (bootmap27.txt, glovesmap27.txt).",
           "#pragma once", "#include <cstdint>", "", "namespace turbo::labels::data {", "",
           "struct Users {", "    const char* field;", "    int32_t code;", "    int32_t players;  // players using the code",
           "    const char* names;  // the three best-rated of them", "};", "", "constexpr Users kUsers[] = {"]
    out += ["    {%s, %d, %d, %s}," % (c_str(f), v, n, c_str(s)) for f, v, n, s in users]
    out += ["};", "", "struct Brand {", "    int16_t id;", "    uint8_t brand;  // index in the brand names", "};", ""]
    for name, brands, ids in (("Boot", boot_brands, boots), ("Glove", glove_brands, gloves)):
        out.append("constexpr const char* k%sBrandNames[] = {%s};" % (name, ", ".join(c_str(b) for b in brands)))
        out.append("constexpr Brand k%ss[] = {" % name)
        out += wrap(["{%d,%d}," % (i, ids[i]) for i in sorted(ids)])
        out += ["};", ""]
    out += ["}  // namespace turbo::labels::data", ""]
    with open(os.path.join(CORE, "appearance_labels_data.h"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out))

    # the fixture: every value of every code-like field
    fields = [f for f in rows[0].keys() if f not in NUMBERS]
    fx = ["// FC 27 LE Turbo GUI native tests - every value the FC 27 players table holds in its code-like fields (Turbo's export",
          "// of %s, %d players): codes only, no player data. GENERATED by scripts/gen_appearance_labels.py: do not edit." % (stamp, len(rows)),
          "#pragma once", "#include <cstdint>", "#include <vector>", "", "struct FixtureCodes {", "    const char* field;",
          "    std::vector<int64_t> values;", "};", "", "static const std::vector<FixtureCodes> kPlayerCodes = {"]
    nvals = 0
    for f in sorted(fields):
        vals = sorted({int(r[f]) for r in rows if r[f].lstrip("-").isdigit()})
        if not vals or len(vals) > 1000:
            continue
        nvals += len(vals)
        cells = ["%d," % v for v in vals]
        cells[-1] = cells[-1].rstrip(",")
        fx.append("    {%s, {" % c_str(f))
        fx += wrap(cells, indent="        ")
        fx.append("    }},")
    fx += ["};", ""]
    with open(os.path.join(TESTS, "fixture_player_codes.h"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(fx))

    print("appearance_labels_data.h: %d player-specific codes, %d boot ids (%d brands), %d glove ids (%d brands)"
          % (len(users), len(boots), len(boot_brands), len(gloves), len(glove_brands)))
    print("fixture_player_codes.h: %d values in %d fields" % (nvals, sum(1 for l in fx if l.startswith("    {\""))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
