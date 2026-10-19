#!/usr/bin/env python3
"""FC 27 LE Turbo GUI - generate data/visual_names.json: field -> code -> in-game name (+ the RDBM26 picture file name).

Our own file of facts: names, codes and file NAMES only. No picture is read, copied or committed; the GUI loads the
pictures at runtime from the user's own RDBM26 install (Settings > picture folder, default C:\\FC_Tools\\RDBM26).

Sources (each code carries its source tag in "src"):
  enum:<asset>   FC 27 game enums, read offline from the game bundles (Patch\\Win32 toc, EBX enum members, 2026-10-19):
                 antifreeze/assets/gameplay/avatar/ssfrunstyle            -> players.runstylecode 0..23
                 antifreeze/assets/gameplay/choreo/enumasset_freekick_style -> players.animfreekickstartposcode 0..31
                 antifreeze/assets/gameplay/choreo/enumasset_penaltykick_style -> players.animpenaltiesstartposcode 0..31
                 RDBM26.exe writes runstyle_<n>.gif's n to runstylecode and fkanim_<n>.gif's n to
                 animfreekickstartposcode (its IL: textBoxrunstyleid -> "runstylecode", textBoxfkanimid ->
                 "animfreekickstartposcode"); runningcode1/2 have no RDBM pictures (RDBM names them
                 "<player> - Custom Style - <code>").
  loc:<key>      the game's language strings (LanguageStrings dump; TXT_CUSTOM_RUN_STYLE_*, TXT_FREE_KICK_STYLE_*,
                 TXT_PENALTY_KICK_STYLE_*, ID_HEAD_PRESET_HAIRCOLOR_*, CreatePlayerHairStyle_*, CreatePlayerFacialHairType_*).
                 The dump on this PC is FC 26's; FC 27 may rename a few.
  le:<key>       Live Editor's localization (loc\\eng_us\\localize.json), the names LE shows for the same codes.
  rdbm           RDBM26's own combo text (RDBM26.exe form resources), used only where the game has no string.
  fce            FC Editor's community hair names (tables\\hairnames.txt), only where the game has no string.

usage: gen_visual_names.py --rdbm <RDBM26\\Resources> --loc <LanguageStrings1.txt> [--loc ...] --le-loc <localize.json>
                           --fce <FC Editor tables dir> [--out data/visual_names.json]
"""
import argparse, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- FC 27 enums (EBX member name -> value), see the docstring
SSF_RUN_STYLE = ["Normal", "Darting_Striker", "The_General", "Big_Man", "T_Rex", "Morning_Sunshine", "Big_Bird",
                 "Freight_Train", "Head_Down", "Buzz", "Straight_Arms", "Elbows_Out", "Got_Game", "Arched_Back", "Norse",
                 "Dasher", "ElPato", "Rising_Star", "Captain", "Phonz", "Turtle", "Olympian", "Matilda", "Lioness"]
# enum member -> TXT_CUSTOM_RUN_STYLE_<key> where the key differs from the upper-cased member
RUN_KEY = {"Buzz": "THE_BUZZ", "Straight_Arms": "STRAIGHT_ARM", "Norse": "THE_NORSE", "ElPato": "EL_PATO",
           "Captain": "THE_CAPTAIN"}
FK_ENUM = ["NORMAL", "STRAIGHT_ON", "90DEGREE_FROM_BALL"] + ["STAR_%d" % i for i in range(1, 30)]
PK_ENUM = ["EDGE_OF_PENALTY_AREA", "CLOSE_TO_THE_BALL", "OUTSIDE_THE_PENALTY_AREA"] + ["STAR_%d" % i for i in range(1, 30)]
# the create-player menu names for the three generic stances (TXT_FREE_KICK_STYLE_*; STRAIGHT_ON is shown as
# "Far From Ball": same position in the menu, RDBM calls it "Long Run-up")
FK_KEY = {"NORMAL": "TXT_FREE_KICK_STYLE_NORMAL", "STRAIGHT_ON": "TXT_FREE_KICK_STYLE_FAR_FROM_BALL",
          "90DEGREE_FROM_BALL": "TXT_FREE_KICK_STYLE_90DEGREE_FROM_BALL"}
# RDBM26's penalty stance texts for the three generic stances (the game strings name only the star run-ups)
PK_GENERIC = {"EDGE_OF_PENALTY_AREA": "Edge of penalty box", "CLOSE_TO_THE_BALL": "Close to ball",
              "OUTSIDE_THE_PENALTY_AREA": "Outside penalty box"}


def read_loc(paths):
    loc = {}
    for p in paths:
        with open(p, encoding="utf-8", errors="replace") as f:
            for line in f:
                c = line.rstrip("\r\n").split("\t")
                if len(c) >= 2 and c[0] and c[0] not in loc:
                    loc[c[0]] = c[1].strip()
    return loc


def files(d):
    try:
        return sorted(os.listdir(d))
    except OSError:
        return []


def codes_from(names, rx):
    out = {}
    for n in names:
        m = re.fullmatch(rx, n, re.I)
        if m:
            out[int(m.group(1))] = n
    return out


def entry(name, src, file=None, note=None):
    e = {"name": name, "src": src}
    if file:
        e["file"] = file
    if note:
        e["note"] = note
    return e


def build(a):
    loc = read_loc(a.loc)
    le = json.load(open(a.le_loc, encoding="utf-8"))["translations"]
    R = a.rdbm
    fields, sets = {}, {}

    def le_name(key):
        v = le.get(key)
        return v if v and v != key and not v.startswith("Unnamed") else None

    # run style (runstylecode): animated, enum + loc
    gif = codes_from(files(os.path.join(R, "runstyle")), r"runstyle_(\d+)\.gif")
    codes = {}
    for v, mem in enumerate(SSF_RUN_STYLE):
        key = "TXT_CUSTOM_RUN_STYLE_" + RUN_KEY.get(mem, mem.upper())
        if key in loc:
            codes[v] = entry(loc[key], "loc:" + key, gif.get(v))
        else:
            codes[v] = entry(mem.replace("_", " ").title(), "enum:ssfrunstyle." + mem, gif.get(v),
                             "no game string in the FC 26 dump")
    sets["runstyle"] = {"title": "Run style", "dir": "runstyle", "kind": "gif", "codes": codes}
    fields["runstylecode"] = "runstyle"

    # free kick stance (animfreekickstartposcode): animated
    gif = codes_from(files(os.path.join(R, "fkanim")), r"fkanim_(\d+)\.gif")
    codes = {}
    for v, mem in enumerate(FK_ENUM):
        if mem in FK_KEY and FK_KEY[mem] in loc:
            codes[v] = entry(loc[FK_KEY[mem]], "loc:" + FK_KEY[mem], gif.get(v))
        else:
            codes[v] = entry("Signature stance %s" % mem.split("_")[1], "enum:freekick_style." + mem, gif.get(v))
    sets["fkanim"] = {"title": "Free kick run-up", "dir": "fkanim", "kind": "gif", "codes": codes}
    fields["animfreekickstartposcode"] = "fkanim"

    # penalty stance (names only, no RDBM pictures)
    codes = {}
    for v, mem in enumerate(PK_ENUM):
        if mem in PK_GENERIC:
            codes[v] = entry(PK_GENERIC[mem], "rdbm")
        else:
            codes[v] = entry("Signature stance %s" % mem.split("_")[1], "enum:penaltykick_style." + mem)
    sets["pkstance"] = {"title": "Penalty run-up", "dir": "", "kind": "none", "codes": codes}
    fields["animpenaltiesstartposcode"] = "pkstance"

    # hair colour / beard colour: png 0..11, json 12..34 (colour swatch + game string id)
    for set_id, sub, stem, field, le_prefix in (("haircolor", "haircolor", "haircolor_", "haircolorcode", "haircolor_"),
                                                ("facialhaircolor", "facialhaircolor", "Facial_Hair_",
                                                 "facialhaircolorcode", "facialhaircolor_")):
        names = files(os.path.join(R, sub))
        codes = {}
        for n in names:
            m = re.fullmatch(re.escape(stem) + r"(\d+)\.(png|json)", n, re.I)
            if not m:
                continue
            v = int(m.group(1))
            nm, src = None, None
            if m.group(2).lower() == "json":
                try:
                    sid = json.load(open(os.path.join(R, sub, n), encoding="utf-8"))["LocalizationInfo"]["StringId"]
                    if sid in loc:
                        nm, src = loc[sid], "loc:" + sid
                except (OSError, ValueError, KeyError, TypeError):
                    pass
            if nm is None:
                key = le_prefix + str(v)  # facial hair has its own LE names (0..4); never borrow the hair ones
                if le_name(key):
                    nm, src = le_name(key), "le:" + key
            codes[v] = entry(nm or "Colour %d" % (v + 1), src or "ordinal", n,
                             "swatch" if m.group(2).lower() == "json" else None)
        sets[set_id] = {"title": "Hair colour" if set_id == "haircolor" else "Beard colour", "dir": sub,
                        "kind": "png", "codes": dict(sorted(codes.items()))}
        fields[field] = set_id

    # eye colour 1..10, skin tone (FC 27 10..100 = file n*10), complexion 1..10
    codes = {}
    for v, n in codes_from(files(os.path.join(R, "eyecolor")), r"eye_(\d+)\.png").items():
        nm = le_name("eyecolor_%d" % v)
        codes[v] = entry(nm or "Eyes %d" % v, "le:eyecolor_%d" % v if nm else "ordinal", n)
    sets["eyecolor"] = {"title": "Eye colour", "dir": "eyecolor", "kind": "png", "codes": dict(sorted(codes.items()))}
    fields["eyecolorcode"] = "eyecolor"

    codes = {}
    for v, n in codes_from(files(os.path.join(R, "skintone")), r"skintone_(\d+)_0\.png").items():
        nm = le_name("skintonecode_%d" % v)
        codes[v * 10] = entry(nm or "Skin tone %d" % v, "le:skintonecode_%d" % v if nm else "ordinal", n)
    sets["skintone"] = {"title": "Skin tone", "dir": "skintone", "kind": "png", "codes": dict(sorted(codes.items())),
                        "note": "FC 27 stores 10..100; RDBM's file number is the code / 10"}
    fields["skintonecode"] = "skintone"

    codes = {v: entry("Complexion %d" % v, "ordinal", n)
             for v, n in codes_from(files(os.path.join(R, "complexion")), r"complexion_(\d+)\.png").items()}
    sets["complexion"] = {"title": "Complexion", "dir": "complexion", "kind": "png", "codes": dict(sorted(codes.items()))}
    fields["skincomplexion"] = "complexion"

    # kit style sets (small, LE names)
    for set_id, sub, rx, field, le_prefix, title, fallback in (
            ("jerseyfit", "jerseyfit", r"kitFit_(\d+)\.png", "jerseyfit", "jerseyfit_", "Jersey fit", None),
            ("jerseysleeve", "jerseysleeve", r"kitSleeves_(\d+)\.png", "jerseysleevelengthcode", "jerseysleevelengthcode_",
             "Sleeves", None),
            ("socklength", "socklength", r"kitSock_(\d+)\.png", "socklengthcode", "socklengthcode_", "Sock length", None),
            ("jerseystyle", "jerseystyle", r"kitStyle_(\d+)\.png", "jerseystylecode", None, "Shirt", ["Tucked In", "Untucked"])):
        codes = {}
        for v, n in codes_from(files(os.path.join(R, sub)), rx).items():
            nm = le_name(le_prefix + str(v)) if le_prefix else None
            if nm:
                codes[v] = entry(nm, "le:" + le_prefix + str(v), n)
            elif fallback and v < len(fallback):
                codes[v] = entry(fallback[v], "rdbm", n)
            else:
                codes[v] = entry("%s %d" % (title, v + 1), "ordinal", n)
        sets[set_id] = {"title": title, "dir": sub, "kind": "png", "codes": dict(sorted(codes.items()))}
        fields[field] = set_id

    # accessories: Accessory_<code>.png (+ Accessory_<code>_<colour>.png variants)
    codes = {}
    for n in files(os.path.join(R, "Accessory")):
        m = re.fullmatch(r"Accessory_(\d+)\.png", n, re.I)
        if m:
            v = int(m.group(1))
            nm = le_name("accessorycode_%d" % v)
            codes[v] = entry(nm or "Accessory %d" % v, "le:accessorycode_%d" % v if nm else "ordinal", n)
    sets["accessory"] = {"title": "Accessory", "dir": "Accessory", "kind": "png", "codes": dict(sorted(codes.items()))}
    for k in range(1, 5):
        fields["accessorycode%d" % k] = "accessory"

    # big sets: pictures discovered at runtime by pattern; names where the game (or FC Editor) has them
    hair_names = {}
    p = os.path.join(a.fce, "hairnames.txt")
    if os.path.isfile(p):
        for line in open(p, encoding="utf-8", errors="replace"):
            c = line.rstrip("\r\n").split("\t")
            if len(c) >= 2 and c[0].isdigit():
                hair_names[int(c[0])] = c[1].strip().rstrip("*").strip()
    codes = {}
    for v, n in codes_from(files(os.path.join(R, "hairstyle")), r"item_(\d+)_0\.png").items():
        key = "CreatePlayerHairStyle_%d" % v
        if key in loc:
            codes[v] = entry(loc[key], "loc:" + key, n)
        elif v in hair_names:
            codes[v] = entry(hair_names[v], "fce", n)
        else:
            codes[v] = entry("", "none", n)
    sets["hairstyle"] = {"title": "Hair", "dir": "hairstyle", "kind": "png", "pattern": "item_{code}_0.png",
                         "codes": dict(sorted(codes.items()))}
    fields["hairtypecode"] = "hairstyle"

    codes = {}
    for v, n in codes_from(files(os.path.join(R, "facialhairstyle")), r"item_(\d+)\.png").items():
        key = "CreatePlayerFacialHairType_%d" % v
        codes[v] = entry(loc[key], "loc:" + key, n) if key in loc else entry("", "none", n)
    sets["facialhair"] = {"title": "Beard", "dir": "facialhairstyle", "kind": "png", "pattern": "item_{code}.png",
                          "codes": dict(sorted(codes.items()))}
    fields["facialhairtypecode"] = "facialhair"

    codes = {v: entry("", "none", n) for v, n in codes_from(files(os.path.join(R, "eyebrow")), r"brow_(\d+)\.png").items()}
    sets["eyebrow"] = {"title": "Eyebrows", "dir": "eyebrow", "kind": "png", "pattern": "brow_{code}.png",
                       "codes": dict(sorted(codes.items()))}
    fields["eyebrowcode"] = "eyebrow"

    codes = {v: entry("", "none", n) for v, n in codes_from(files(os.path.join(R, "tattoo")), r"item_(\d+)_0\.png").items()}
    sets["tattoo"] = {"title": "Tattoo", "dir": "tattoo", "kind": "png", "pattern": "item_{code}_0.png",
                      "codes": dict(sorted(codes.items()))}
    for part in ("head", "front", "back", "leftarm", "rightarm", "leftleg", "rightleg"):
        fields["tattoo" + part] = "tattoo"

    # boots / gloves: FC 26 picture sets (FC 27 moved some ids: the GUI keeps its own brand labels)
    for set_id, subs, rx, field, title in (("boots", ("FC26_shoes", "shoes"), r"shoe_(\d+)_0\.png", "shoetypecode", "Boots"),
                                           ("gloves", ("FC26_gkgloves", "gkgloves"), r"gkglove_(\d+)\.png", "gkglovetypecode",
                                            "GK gloves")):
        sub = next((s for s in subs if files(os.path.join(R, s))), subs[0])
        codes = {}
        for v, n in codes_from(files(os.path.join(R, sub)), rx).items():
            key = "CreatePlayerBoot_%d" % v
            codes[v] = entry("", "none", n)
        sets[set_id] = {"title": title, "dir": sub, "fallback_dirs": [s for s in subs if s != sub], "kind": "png",
                        "pattern": rx.replace(r"(\d+)", "{code}").replace("\\", ""), "codes": dict(sorted(codes.items())),
                        "note": "pictures are FC 26's; FC 27 moved some ids to other models"}
        fields[field] = set_id

    def jsonable(sets):
        return {k: dict(v, codes={str(c): e for c, e in v["codes"].items()}) for k, v in sets.items()}

    return {
        "schema": 1,
        "about": "field -> picture set -> code -> in-game name (+ RDBM26 picture file name). Names and codes only; "
                 "the pictures stay in the user's RDBM26 folder. Generated by scripts/gen_visual_names.py.",
        "fields": fields,
        "sets": jsonable(sets),
    }


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--rdbm", required=True)
    ap.add_argument("--loc", action="append", required=True)
    ap.add_argument("--le-loc", required=True)
    ap.add_argument("--fce", required=True)
    ap.add_argument("--out", default=os.path.join(HERE, "..", "data", "visual_names.json"))
    a = ap.parse_args(argv)
    doc = build(a)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, ensure_ascii=False, indent=1)
        f.write("\n")
    n = sum(len(s["codes"]) for s in doc["sets"].values())
    named = sum(1 for s in doc["sets"].values() for e in s["codes"].values() if e["name"])
    print("visual_names.json: %d fields, %d sets, %d codes, %d named" % (len(doc["fields"]), len(doc["sets"]), n, named))
    for k, s in doc["sets"].items():
        print("  %-16s %4d codes %4d named %4d pictures" % (k, len(s["codes"]), sum(1 for e in s["codes"].values() if e["name"]),
                                                          sum(1 for e in s["codes"].values() if e.get("file"))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
