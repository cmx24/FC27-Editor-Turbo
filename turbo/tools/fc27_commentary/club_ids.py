"""Clubs with a recorded spoken name in one FC 27 commentary language.

FC 27 has no club-name bank: the commentator says a club's name from team-keyed SoundWave families whose Selection data
set is keyed by club-id parameters (home_ID, away_ID, asset_team_ID, ...). This tool reads every family of the language's
bank and lists the FC 27 team ids that have rows for at least one parameter of CLUB_PARAMS, the exact set the Turbo
voice-swap hook rewrites (turbogui/src/core/callname_voice.cpp is_club_param; team_venue_ID and *_gID are left out).

Output: turbo\\callnames\\clubs\\<lang>.json  {"lang", "game_build", "params": {name: count}, "ids": [...]}
The Turbo GUI offers these ids as donors in Club > Name, colours & crest > Commentary name (4.0 menus: Club & crest >
Identity > Commentary name).

    python -I club_ids.py --lang fre_fr --out ...\\turbo\\callnames\\clubs\\fre_fr.json
    python -I club_ids.py --selftest
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fc27_commentary as fc  # noqa: E402

# Keep in step with is_club_param (turbogui/src/core/callname_voice.cpp) and briefs\4.0-club-callnames.md.
CLUB_PARAMS = (
    "asset_team_ID", "home_ID", "away_ID", "winning_team_ID", "losing_team_ID", "ta_focus_team1_ID", "ta_focus_team2_ID",
    "ttp_team_a_ID", "lae_focus_team_1_ID", "lae_focus_team_2_ID", "lae_against_team_1_ID", "lae_against_team_2_ID",
    "lb_against_team_ID", "news_recap_team_ID", "tr_team_b_ID", "opposite_team_ID",
)
for _n in CLUB_PARAMS + ("team_gID", "team_venue_ID"):
    fc.NAMES[fc.djb2x(_n)] = _n


def selection_club_ids(sel):
    """{parameter: set(ids)} of one Selection data set, over the club parameters it is keyed by."""
    out, err = {}, None
    for ix in sorted(sel.indexes, key=lambda x: -len(x["params"])):
        names = [p["name"] for p in ix["params"] if p["name"] in CLUB_PARAMS]
        if not names:
            continue
        try:
            keys = sel.row_keys(ix)
        except fc.FormatError as ex:  # an index this reader cannot expand: the next one may cover the same parameters
            err = err or ex
            continue
        for p in names:
            out.setdefault(p, set()).update(k[p] for k in keys.values() if p in k and k[p] > 0)
    if not out and err:
        raise err
    return out


def collect(bank, log=print):
    per_param, errors, families = {}, [], 0
    for s in sorted(bank.sets):
        for f in bank.families(s):
            name = "%s/%s" % (s, f)
            try:
                res = bank.assets[bank.family_name(f, s)]["res"]
                sel = fc.parse_sble(bank.cas.read(*res[:2]), with_fields=False, only="Selection").get("Selection")
                if sel is None:
                    continue
                ids = selection_club_ids(sel)
            except (fc.FormatError, KeyError, struct.error) as ex:
                errors.append("%s: %s" % (name, ex))
                continue
            if ids:
                families += 1
            for p, v in ids.items():
                per_param.setdefault(p, set()).update(v)
    for e in errors:
        log("skipped " + e)
    return per_param, families, errors


def result_json(lang, build, per_param):
    ids = sorted(set().union(*per_param.values())) if per_param else []
    return {"lang": lang, "game_build": build, "params": {p: len(v) for p, v in sorted(per_param.items())}, "ids": ids}


# ---------------------------------------------------------------------------------------------------- self-test
def _fixture():
    """An SBle with one Selection DSET: 3 rows, an index over team_gID (0..1) x home_ID (values 1, 95)."""
    d = bytearray(0x400)
    d[0:4] = b"SBle"
    struct.pack_into("<H", d, 8, 1)
    struct.pack_into("<I", d, 0x18, 0x50)
    struct.pack_into("<II", d, 0x50, 0x70, 0)
    o = 0x70
    d[o:o + 4] = b"TESD"
    struct.pack_into("<III", d, o + 4, 0x100, fc.djb2x("Selection"), 0)
    struct.pack_into("<I", d, o + 0x38, 3)
    struct.pack_into("<HHHHI", d, o + 0x3C, 0, 1, 0x48, 0x78, 0x98)
    struct.pack_into("<I", d, o + 0x78, 0x220)
    struct.pack_into("<I", d, o + 0x78 + 0x1C, 0x02000000)
    struct.pack_into("<5I", d, o + 0x98, fc.djb2x("team_gID"), 2, 0, 1, 0)
    struct.pack_into("<5I", d, o + 0xB0, fc.djb2x("home_ID"), 0x04000002, 1, 95, 0x230)
    struct.pack_into("<5H", d, 0x220, 0, 0, 2, 2, 3)      # (0,1): -, (0,95): rows 0-1, (1,1): -, (1,95): row 2
    struct.pack_into("<2I", d, 0x230, 0, 94)
    return bytes(d)


def self_test():
    fails = []

    def check(cond, what):
        print(("PASS " if cond else "FAIL ") + what)
        if not cond:
            fails.append(what)

    check(len(CLUB_PARAMS) == 16 and len(set(CLUB_PARAMS)) == 16, "16 distinct club parameters")
    check("team_venue_ID" not in CLUB_PARAMS and not any(p.endswith("_gID") for p in CLUB_PARAMS),
          "team_venue_ID and *_gID left out")
    sel = fc.parse_sble(_fixture(), with_fields=False, only="Selection")["Selection"]
    ids = selection_club_ids(sel)
    check(ids == {"home_ID": {95}}, "fixture: home_ID keys hold 95 only (1 has no rows; team_gID ignored): %r" % ids)
    r = result_json("xx_xx", "B", {"home_ID": {95, 7}, "away_ID": {7, 1}})
    check(r["ids"] == [1, 7, 95] and r["params"] == {"away_ID": 2, "home_ID": 2}, "union sorted, counts per parameter")
    check(result_json("xx_xx", "B", {})["ids"] == [], "no families: empty list")
    print("selftest: %d FAIL" % len(fails))
    return 1 if fails else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lang", help="commentary language as the game names its pack, e.g. fre_fr")
    ap.add_argument("--game", default=fc.DEFAULT_GAME, help="game folder (default: %(default)s)")
    ap.add_argument("--out", help="output json")
    ap.add_argument("--selftest", action="store_true", help="run the offline checks and stop")
    a = ap.parse_args(argv)
    if a.selftest:
        return self_test()
    if not a.lang or not a.out:
        ap.error("--lang and --out are required")
    if os.path.exists(a.out):
        print("refusing to overwrite %s (move it aside first)" % a.out, file=sys.stderr)
        return 2
    bank = fc.Bank(a.game, a.lang)
    try:
        per_param, families, errors = collect(bank)
    finally:
        bank.close()
    res = result_json(a.lang, fc.game_build(a.game), per_param)
    if not res["ids"]:
        print("no club ids found for %s; nothing written" % a.lang, file=sys.stderr)
        return 2
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
        f.write("\n")
    print("%s: %d clubs from %d families, %s, %d skipped" % (a.lang, len(res["ids"]), families, res["params"], len(errors)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
