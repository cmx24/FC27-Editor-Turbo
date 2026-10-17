"""Self-tests of the asset catalog builder. Synthetic bytes only: no game folder, no Live Editor files, no real names.
usage: python -I test_asset_catalog_builder.py   (a few seconds)"""
import json
import os
import re
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_asset_catalog as B  # noqa: E402
import families  # noqa: E402
import frostbite as fb  # noqa: E402
import legacy  # noqa: E402
import names as namesmod  # noqa: E402

FAILS = []


def check(cond, what):
    print(("ok   " if cond else "FAIL ") + what)
    if not cond:
        FAILS.append(what)


def test_fnv():
    # FNV-1 64 reference vectors (multiply then xor)
    check(legacy.fnv1("") == 0xCBF29CE484222325, "fnv1 empty = basis")
    check(legacy.fnv1("a") == 0xAF63BD4C8601B7BE, "fnv1 'a'")
    check(legacy.fnv1("foobar") == 0x340D8765A4DDA9C2, "fnv1 'foobar'")
    check(legacy.path_hash("Data/UI/X.dds") == legacy.fnv1("data/ui/x.dds"), "path hash is on the lower-case path")
    paths = ["data/ui/imgAssets/heads/p1.dds", "a", "", "Data/X/Y_Long_Name_12345.dds"]
    check(legacy.hash_many(paths) == [legacy.path_hash(p) for p in paths], "vectorised hash = scalar hash")


def test_brute():
    prefix = "data/ui/imgAssets/heads/p"
    want = {7, 90001, 1234567}
    game = {legacy.path_hash(prefix + str(i) + ".dds") for i in want} | {12345}
    gs = legacy.np.array(sorted(game), dtype=legacy.np.uint64) if legacy.np is not None else None
    hits = legacy.brute_hits(prefix, 0, 1500000, [".dds"], game, gs)
    check(sorted(i for i, _ in hits) == sorted(want), "brute force finds exactly the game ids")
    check(all(p.startswith(prefix) for _, p in hits), "brute force keeps the listed letter case")
    slow = legacy.brute_hits(prefix, 0, 100000, [".dds"], game, None)
    check(sorted(i for i, _ in slow) == [7, 90001], "pure-Python fallback agrees")


def manifest_chunk(version, hashes):
    eo = 0x40
    d = bytearray(eo + 28 * len(hashes))
    struct.pack_into("<4I", d, 0, version, 0x50, 0, len(hashes))
    struct.pack_into("<Q", d, 0x10, eo)
    for k, h in enumerate(hashes):
        struct.pack_into("<Q", d, eo + 28 * k, h)
    return bytes(d)


def test_manifest_chunk():
    hs = [1, 0xFFFFFFFFFFFFFFFF, 0x1234]
    check(legacy.parse_manifest_chunk(manifest_chunk(2, hs)) == hs, "v2 manifest chunk")
    check(legacy.parse_manifest_chunk(manifest_chunk(3, hs)) == hs, "v3 manifest chunk")
    check(legacy.parse_manifest_chunk(manifest_chunk(1, hs)) is None, "v1 refused")
    check(legacy.parse_manifest_chunk(manifest_chunk(2, hs)[:-10]) is None, "truncated chunk refused")


def bundle_manifest(names):
    """A minimal bundle manifest: magic at +4, 7 u32 at +8 (total, ebx, res, chunks, string offset, 0, 0)."""
    total = len(names)
    hdr_end = 0x24 + 20 * total + 8 * total
    strings = b""
    offs = []
    for n in names:
        offs.append(len(strings))
        strings += n.encode() + b"\0"
    so = hdr_end - 4
    d = bytearray(hdr_end) + strings
    struct.pack_into("<I", d, 4, fb.MANIFEST_MAGIC)
    struct.pack_into("<7I", d, 8, total, total, 0, 0, so, 0, 0)
    p = 0x24 + 20 * total
    for o in offs:
        struct.pack_into("<II", d, p, o, 0)
        p += 8
    return bytes(d)


HEAD = "content/character/player/player_9/test_head_90001/var_%d/"
EBX = [HEAD % 0 + "face_90001_0_0_color", HEAD % 0 + "headmorphpreset_90001_0_0", HEAD % 1 + "face_90001_1_0_color",
       "content/character/player/player_9/test_head_90001/var_0_starhead_brt",
       "content/character/player/player_9/generic_90100/var_0/face_90100_0_0_color",
       "content/character/player/player_9/generic_90100/var_0/headmorphpreset_90100_0_0",
       "content/character/player/player_9/generic_90100/var_0_launch_starhead_brt",
       "content/character/player/player_9/test_head_90002/var_0/face_90002_0_0_color",
       "content/character/generichair/hairgen_4/x", "content/character/shoe/shoe_10/a", "content_ts/character/shoe/shoe_10/a",
       "content_ts/character/shoe/shoe_2200/a", "content_ts/character/base_assets/accessory/wrist_test_tape_6/m",
       "content/character/kit/kit_1/test_club_one_9001/home_40_0/k", "content/character/kit/kit_1/referee_6001/home_1_0/k",
       "content/character/cranium/presets/decals/eyebrows/eyebrows_3_preset", "content/other/thing"]


def test_bundle_manifest_and_classify():
    blob = bundle_manifest(EBX)
    check(fb.manifest_names(blob) == EBX, "bundle manifest names round trip")
    r = families.classify(fb.manifest_names(blob))
    check(sorted(r["head3d"]) == ["90001", "90002", "90100"], "head ids from the head regex")
    h = r["head3d"]["90001"]
    check(h["slugs"] == ["test_head"] and sorted(h["vars"]) == [0, 1], "head slug and vars")
    check(h["face"] and h["morph"] and h["star"], "head proof flags (face, morph, starhead)")
    check(r["head3d"]["90100"]["star"] == 1, "launch starhead counts")
    check(not r["head3d"]["90002"]["morph"], "head without morph preset")
    check(r["kit"]["9001"] == {"slug": "test_club_one", "kits": ["home_40_0"]}, "kit slug and kit folder")
    check(r["accessory"] == {"6": "wrist_test_tape"}, "descriptive accessory folder")
    check(set(r["boots"]) == {"10"} and set(r["boots_ts"]) == {"10", "2200"}, "boots content vs content_ts")
    m = families.merge([("Data/Win32/contentsb.toc", r), ("Patch/Win32/contentlaunchsb.toc", {"head3d": {"90001": h}})])
    check(m["head3d"][90001]["toc"] == {"L", "S"} and m["head3d"][90002]["toc"] == {"S"}, "toc letters L/S")
    check(families.head_proven(m["head3d"][90001]) and not families.head_proven(m["head3d"][90002]), "head write proof")
    check(families.head_is_generic(m["head3d"][90100]), "generic head")
    check(families.slug_name("doe_john") == "Doe John", "slug -> readable name")
    check(families.NON_TEAM_SLUG.match("referee_women") and not families.NON_TEAM_SLUG.match("test_club_one"), "non-team kit slugs")


def test_names_priority():
    heads = {90001: {"slugs": ["test_head"]}, 90003: {"slugs": ["test_slug_three"]}, 90100: {"slugs": ["generic_90100"]}}
    kits = {9001: {"slug": "test_club_one"}, 6001: {"slug": "referee_test"}}
    masters = [("ita_it", {"90001": "Test Master Name", "55555": "Not In Catalog"}, {"990001": "Testname"})]
    N = namesmod.build_names({90001, 90002, 90003, 90100, 92001}, {9001, 6001}, {990001}, heads, kits, masters,
                             {"90001": "Test Workbook Name", "90002": "Test Workbook Two"}, {"90003": "Test Extra Three"},
                             {92001: "Test Coach"})
    j = N.as_json()
    check(j["player"]["90001"] == ["Test Master Name", "master:ita_it"], "master beats workbook and slug")
    check(j["player"]["90002"] == ["Test Workbook Two", "workbook"], "workbook name")
    check(j["player"]["90003"] == ["Test Extra Three", "extra_names"], "extra names beat the slug")
    check("90100" not in j["player"], "generic slug is not a name")
    check("55555" not in j["player"], "ids outside the catalog are dropped")
    check(j["team"] == {"9001": ["Test Club One", "kit_slug"], "6001": ["Referee kit (referee_test, not a club)", "kind"]},
          "kit slug team name; a referee kit id is labelled by its kind")
    check(j["commentary"] == {"990001": ["Testname", "master:ita_it"]}, "commentary name")
    check(j["player"]["92001"] == ["Test Coach", "managers"], "managers name")


def test_team_names():
    with tempfile.TemporaryDirectory() as tmp:
        loc = os.path.join(tmp, "LanguageStrings1.txt")
        with open(loc, "w", encoding="utf-8-sig") as f:
            f.write("stringid\tsourcetext\thashid\n")
            f.write("TeamName_9001\tTest Club One FC\t-1\n")
            f.write("TeamName_9002\tTest Club Two\t2\n")
            f.write("TeamName_Abbr3_9002\tTCT\t3\n")
            f.write("TeamName_9003_FUT-Custom\tTeamName_Abbr15_9003\t4\n")
        csvp = os.path.join(tmp, "custom_team_names.csv")
        with open(csvp, "w", encoding="utf-8") as f:
            f.write("key;value\nTeamName_9004;Test Renamed Four\nTeamName_Abbr3_9004;TRF\n")
        tj = os.path.join(tmp, "team_names.json")
        with open(tj, "w", encoding="utf-8") as f:
            json.dump({"teams": [{"teamid": 9005, "name": "Test Turbo Five"}]}, f)
        locn = namesmod.load_loc_team_names([loc])
        check(locn == {9001: "Test Club One FC", 9002: "Test Club Two"}, "loc strings: TeamName_<id> rows only %s" % locn)
        le = namesmod.load_le_team_names(csvp)
        tt = namesmod.load_turbo_team_names(tj)
        check(le == {9004: "Test Renamed Four"} and tt == {9005: "Test Turbo Five"}, "LE csv and Turbo json team names")
        kits = {9001: {"slug": "test_club_one"}, 9004: {"slug": "test_club_four"}, 0: {"slug": "_default"},
                111: {"slug": "otp_generic"}}
        N = namesmod.build_names(set(), {0, 111, 9001, 9002, 9004, 9005, 9006}, set(), {}, kits,
                                 team_sources=[("loc_strings", locn), ("le_team_names", le), ("turbo_team_names", tt)])
        t = N.as_json()["team"]
        check(t["9001"] == ["Test Club One FC", "loc_strings"] and t["9002"] == ["Test Club Two", "loc_strings"],
              "the game's team names beat the kit slug")
        check(t["9004"] == ["Test Club Four", "kit_slug"] and t["9005"] == ["Test Turbo Five", "turbo_team_names"],
              "renames only fill gaps")
        check(t["0"][1] == "kind" and "not a club" in t["0"][0] and t["111"] == ["Generic team (otp_generic, not a club)", "kind"],
              "non-team ids labelled by kind")
        check("9006" not in t, "no source: no name (the GUI shows Team #id)")


def test_banks_auto():
    with tempfile.TemporaryDirectory() as tmp:
        for rel in (("Data", "Win32", "commentaryfull_eng_us.toc"), ("Data", "Win32", "commentarylaunch_eng_us.toc"),
                    ("commentary", "commentaryfull_ita_it.toc"), ("Data", "Win32", "commentarywc_fre_fr.toc")):
            os.makedirs(os.path.join(tmp, *rel[:-1]), exist_ok=True)
            open(os.path.join(tmp, *rel), "wb").close()
        check(B.installed_languages(tmp) == ["eng_us", "ita_it"], "installed languages: full / launch packs, downloaded copy")
        src = os.path.join(tmp, "Data", "Win32", "commentaryfull_eng_us.toc")
        st = os.stat(src)
        import datetime as dt
        bank = {"turbo_bank": 1, "lang": "eng_us", "game_build": "1.0.1", "sets": {},
                "source_files": [{"path": src, "size": st.st_size,
                                  "modified": dt.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds")}]}
        bp = os.path.join(tmp, "eng_us_bank.json")
        with open(bp, "w", encoding="utf-8") as f:
            json.dump(bank, f)
        check(B.bank_outdated(bp, "1.0.1") == "", "bank up to date")
        check(B.bank_outdated(bp, "1.0.2").startswith("built for game"), "another game build: outdated")
        with open(src, "wb") as f:
            f.write(b"x")
        check(B.bank_outdated(bp, "1.0.1").startswith("a source file changed"), "a changed toc: outdated")
        check(B.bank_outdated(os.path.join(tmp, "none.json"), "1.0.1") == "missing", "missing bank")
        os.makedirs(os.path.join(tmp, "out", "banks"))
        os.makedirs(os.path.join(tmp, "le", "turbo_output", "commentary_banks"))
        for p in (os.path.join(tmp, "out", "banks", "eng_us_bank.json"),
                  os.path.join(tmp, "le", "turbo_output", "commentary_banks", "eng_us_old.json"),
                  os.path.join(tmp, "le", "turbo_output", "commentary_banks", "ita_it_old.json")):
            open(p, "w").close()
        got = [os.path.basename(p) for p in B.default_banks(os.path.join(tmp, "le"), os.path.join(tmp, "out"))]
        check(got == ["eng_us_bank.json", "ita_it_old.json"], "built banks first; older files only for other languages %s" % got)


def fake_pe(stamp, size):
    d = bytearray(0x200)
    d[:2] = b"MZ"
    struct.pack_into("<I", d, 0x3C, 0x80)
    d[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<I", d, 0x80 + 8, stamp)
    struct.pack_into("<I", d, 0x80 + 24 + 56, size)
    return bytes(d)


def test_build_key():
    check(fb.build_key(fake_pe(0x6AC07E31, 0x2145C000)) == "6AC07E31-2145C000", "build key from a fake PE header")
    check(fb.build_key(fake_pe(0x1, 0xABC)) == "00000001-00000ABC", "build key is zero padded")
    try:
        fb.build_key(b"XX" + b"\0" * 100)
        check(False, "non-PE refused")
    except ValueError:
        check(True, "non-PE refused")


def test_cache_skips_unchanged():
    with tempfile.TemporaryDirectory() as tmp:
        game = os.path.join(tmp, "game")
        os.makedirs(os.path.join(game, "Data", "Win32"))
        toc = os.path.join(game, "Data", "Win32", "contentsb.toc")
        with open(toc, "wb") as f:
            f.write(b"x" * 10)
        cache = os.path.join(tmp, "cache")
        os.makedirs(cache)
        calls = []
        real = B.scan_toc
        B.scan_toc = lambda g, p: calls.append(p) or {"result": {"hair": {"4": 1}}, "bundles": 1, "errors": 0}
        try:
            c1, s1, src = B.ebx_census(game, None, cache, lambda s: None)
            c2, s2, _ = B.ebx_census(game, None, cache, lambda s: None)
            with open(toc, "ab") as f:
                f.write(b"y")
            B.ebx_census(game, None, cache, lambda s: None)
        finally:
            B.scan_toc = real
        check(len(calls) == 2 and s1["scanned"] == 1 and s2["cached"] == 1, "cache skips an unchanged toc, rescans a changed one")
        check(c1 == c2 and c1["hair"] == {4: 1}, "cached result equals the scanned one")
        check(list(src) == ["Data/Win32/contentsb.toc"] and set(src["Data/Win32/contentsb.toc"]) == {"size", "mtime"}, "sources keyed by game-relative path")


def test_bank_reader():
    with tempfile.TemporaryDirectory() as tmp:
        a = os.path.join(tmp, "a.json")
        with open(a, "w") as f:
            json.dump({"lang": "ita_it", "generic": [{"commentaryid": 990001}], "real": [{"playerid": 90001}, {"playerid": 90004}],
                       "real_high": [{"playerid": 90002}], "real_link": []}, f)
        b = os.path.join(tmp, "b.json")
        with open(b, "w") as f:
            json.dump({"lang": "eng_us", "sets": {"eng_us_dr_full": {"real": [{"playerid": 90001}], "generic": [{"commentaryid": 990002}]},
                                                  "eng_us_dr_demo": {"real_link": [{"playerid": 90003}]},
                                                  "eng_us_gm_licensed": {"real": [{"playerid": 90001}]}}}, f)
        # fc27_commentary's own output (WP-03): per set {team, kind, ids: {kind: [ids]}}, flat rows of the primary set too
        c = os.path.join(tmp, "c.json")
        with open(c, "w") as f:
            json.dump({"turbo_bank": 1, "lang": "eng_us", "primary_set": "eng_us_dr_full",
                       "generic": [{"commentaryid": 990002}], "real": [{"playerid": 90001}],
                       "sets": {"eng_us_dr_full": {"team": "dr", "kind": "full", "ids": {"generic": [990002], "simple": [90001], "link": [],
                                                                                          "high": [], "generic_high": []},
                                                   "selection_rows": {}, "errors": {}},
                                "eng_us_dr_demo": {"team": "dr", "kind": "demo", "ids": {"link": [90003]}, "errors": {}},
                                "eng_us_gm_full": {"team": "gm", "kind": "full", "ids": {"simple": [90005]}, "errors": {"high": "x"}}},
                       "teams": {"dr": {"generic": [990002], "simple": [90001], "link": [90003]}, "gm": {"simple": [90005]}}}, f)
        la, ta, _ = B.read_bank(a, tmp)
        lb, tb_, _ = B.read_bank(b, tmp)
        lc, tc, _ = B.read_bank(c, tmp)
    check(la == "ita_it" and ta == {"": {"generic": {990001}, "simple": {90001, 90004}, "high": {90002}, "link": set()}}, "flat bank -> team ''")
    check(lb == "eng_us" and tb_["dr"] == {"simple": {90001}, "generic": {990002}, "link": {90003}} and tb_["gm"] == {"simple": {90001}},
          "per-set bank -> teams dr/gm, sets merged")
    check(lc == "eng_us" and tc["dr"] == {"generic": {990002}, "simple": {90001}, "link": {90003}, "high": set()} and
          tc["gm"] == {"simple": {90005}}, "fc27_commentary bank (sets with team + ids) -> teams dr/gm, demo merged into dr")


def is_sorted_unique(x):
    return isinstance(x, list) and all(isinstance(i, int) for i in x) and all(a < b for a, b in zip(x, x[1:]))


def validate_v1(cat):
    """Python mirror of the schema v1 rules (turbogui/tests/catalog validate_v1)."""
    errs = []
    if cat.get("schema") != "turbo-asset-catalog/1":
        errs.append("schema")
    for k in ("build_key", "game_version", "built_at", "builder"):
        if not isinstance(cat.get(k), str):
            errs.append(k)
    for k, v in cat["sources"].items():
        if not (isinstance(v.get("size"), int) and isinstance(v.get("mtime"), int)):
            errs.append("sources." + k)
    F = cat["families"]
    if set(F) != set(B.FAMILY_KEYS):
        errs.append("family keys")
    for k, v in F.items():
        if k in B.MODEL_FAMILIES:
            if not is_sorted_unique(v["model"]):
                errs.append(k + ".model")
            for i, p in v["preview"].items():
                if not (i.isdigit() and p.startswith("data/") and ".." not in p and len(p) <= 200):
                    errs.append(k + ".preview")
        elif k == "kit":
            for i, t in v["teams"].items():
                if not (i.isdigit() and isinstance(t.get("slug"), str) and isinstance(t.get("kits"), list)):
                    errs.append("kit")
        elif k == "callnames":
            for lang, l in v.items():
                for team, kinds in l["teams"].items():
                    for kind, ids in kinds.items():
                        if kind not in ("generic", "simple", "link", "high") or not is_sorted_unique(ids):
                            errs.append("callnames")
        elif not is_sorted_unique(v.get("ids")):
            errs.append(k + ".ids")
    h = F["head3d"]
    hs = set(h["ids"])
    if not all(int(i) in hs for i in list(h["vars"]) + list(h["toc"])) or not set(h["generic"]) <= hs:
        errs.append("head3d keys")
    # slug (optional, builder 2): one EBX folder slug per head id, lower-case letters, digits and "_"
    for i, s in (h.get("slug") or {}).items():
        if not (i.isdigit() and int(i) in hs and isinstance(s, str) and re.fullmatch(r"[a-z0-9_]{1,64}", s)):
            errs.append("head3d.slug")
    for table, t in cat["names"].items():
        for i, n in t.items():
            if not (i.isdigit() and len(n) == 2 and n[0] and (n[1] in ("workbook", "extra_names", "slug", "kit_slug", "managers", "kind",
                                                                   "loc_strings", "le_team_names", "turbo_team_names") or n[1].startswith("master:"))):
                errs.append("names")
    return errs


def test_assemble_schema():
    census = families.merge([("Data/Win32/contentsb.toc", families.classify(EBX)),
                             ("Data/Win32/contentlaunchsb.toc", families.classify([EBX[0], EBX[1], EBX[3]]))])
    leg = {"families": {"miniface": {"90001": "data/ui/imgAssets/heads/p90001.dds"},
                        "hair": {"4": "data/ui/imgAssets/craniumhair/hair_4_0.dds"},
                        "hair_old": {"4": "data/ui/imgAssets/hairstyle/item_4_0.dds", "5": "data/ui/imgAssets/hairstyle/item_5_0.dds"},
                        "crest": {"9001": "data/ui/imgAssets/crest/light/l9001.dds",
                                  "99010101": "data/ui/imgAssets/crest/custom/l99010101.dds"}}}
    banks = {"ita_it": {"": {"simple": {90004, 90001}, "generic": {990001}}}}
    cat = B.assemble("0000AAAA-0000BBBB", "0.0.1.1", "2026-10-11T12:00:00-04:00", {"Data/Win32/contentsb.toc": {"size": 1, "mtime": 2}},
                     census, leg, banks)
    player, team, commentary = B.catalog_ids(cat)
    cat["names"] = namesmod.build_names(player, team, commentary, census["head3d"], census["kit"]).as_json()
    errs = validate_v1(cat)
    check(not errs, "assembled catalog follows schema v1 %s" % errs)
    F = cat["families"]
    check(F["head3d"]["ids"] == [90001, 90100] and F["head3d"]["unproven"] == [90002], "only proven heads are head3d ids")
    check(F["head3d"]["toc"]["90001"] == "LS" and F["head3d"]["generic"] == [90100], "head toc letters and generic")
    check(F["head3d"]["slug"] == {"90001": "test_head", "90100": "generic"} and "90002" not in F["head3d"]["slug"],
          "one EBX slug per proven head")
    check(F["hair"]["preview"] == {"4": "data/ui/imgAssets/craniumhair/hair_4_0.dds", "5": "data/ui/imgAssets/hairstyle/item_5_0.dds"},
          "new preview folder preferred, older one fills gaps")
    check(F["boots"]["model"] == [10] and F["boots"]["model_ts_only"] == [2200], "boots gate = content/ ids")
    check(F["callnames"]["ita_it"]["teams"][""]["simple"] == [90001, 90004], "bank ids sorted")
    check(F["crest"]["ids"] == [9001] and "99010101" not in F["crest"]["paths"], "create-a-club crest templates left out")
    check(cat["names"]["player"]["90001"] == ["Test Head", "slug"], "slug name in the catalog")
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "turbogui", "tests", "fixtures",
                           "asset_catalog_v1_small.json"), encoding="utf-8") as f:
        fx = json.load(f)
    check(not validate_v1(fx), "the WP-01 fixture passes the same checks")


def test_classify_path():
    r = legacy.classify_path("data/ui/imgAssets/crest512x512/dark/l9001.dds")
    r2 = legacy.classify_path("data/ui/imgAssets/crest/light/l9001.dds")
    check(r[0] == "crest" and r[1] == 9001 and r2[2] < r[2], "crest light/default size preferred")
    check(legacy.classify_path("data/ui/imgAssets/heads/p250.dds")[:2] == ("miniface", 250), "miniface path")
    check(legacy.classify_path("data/ui/imgAssets/cmLegendaryPlayers/small/legend_7.dds")[:2] == ("legendface", 7), "legend path")
    check(legacy.classify_path("data/ui/imgAssets/gkglove/gkglove_20.dds")[:2] == ("gkglove", 20), "gk glove path")
    check(legacy.classify_path("data/ui/imgAssets/genericManagerHeads/gmh90200.dds")[:2] == ("staffgeneric", 90200),
          "generic manager head picture (counted only)")
    check(legacy.classify_path("data/ui/imgAssets/clubInfo/manager/manager_9001.dds")[:2] == ("clubmanager", 9001),
          "club-info manager picture (counted only)")
    check(legacy.classify_path("data/ui/other/p1.dds") is None, "unrelated path")


if __name__ == "__main__":
    import time
    t = time.time()
    for fn in (test_fnv, test_brute, test_manifest_chunk, test_bundle_manifest_and_classify, test_names_priority,
               test_team_names, test_banks_auto, test_build_key, test_cache_skips_unchanged, test_bank_reader,
               test_assemble_schema, test_classify_path):
        fn()
    print("%d failed, %.1fs" % (len(FAILS), time.time() - t))
    sys.exit(1 if FAILS else 0)
