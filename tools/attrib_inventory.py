"""attrib_inventory.py - Turbo 2.5 track A: the OFFLINE inventory of the game's gameplay attribute groups (the engine's tuning tables).

Reads FC 27's own data (never a mod's): the decoded attribulator assets of the installed game (one JSON: asset path -> flattened
values) and the FC 27 reflection type DB (one JSON: type name -> size, signature, fields with offsets), both made by
fc27_attrib_extract.py / fc27_attrib_types.py. Writes a compact inventory (docs/re/attrib_inventory_6AC07E31.json) and the readable report.
It never talks to the game, never writes outside its --out paths.

  python -I tools/attrib_inventory.py build --assets <fc27_assets.json> --typedb <typedb.json> [--out docs/re/attrib_inventory_6AC07E31.json]
  python -I tools/attrib_inventory.py report [--inv docs/re/attrib_inventory_6AC07E31.json] [--out INVENTORY.md]
  python -I tools/attrib_inventory.py --self-test

Per group (= one *_runtime asset of the base database; instance = the engine's AttribSchema object):
  db         gameplay | prematch               the attribute database it lives in (prematch groups are read at match setup)
  family     gp_positioning ...                the AttribGroupRuntime_<family> object (a slot of the database runtime object)
  family_slot / group_slot                     byte offsets: database runtime object -> family object -> instance pointer. These two
                                               numbers are the SAME chain the 10 located knobs use (manager -> database -> group ->
                                               instance); every group of the inventory has them, so a group needs a locator entry in the
                                               knobs file (data only), not new native code.
  type / sig / size / nfields                  the schema type for the L1 type check
  variants                                     databases that own a different copy (simulation = Authentic, small-sided modes)
  fields[]   [name, offset, kind, default, extra]
     kind  f32 i32 u32 bool i8 enum | af32 ai32 abool au32 (an array: a pointer to a list, `default` = its values) | curve (a pointer
           to a FloatCurve: `default` = [points, ymin, ymax]) | ptr (another pointer) | other
Classes of "scalable" (an offline judgement, never a proof): mult (a float > 0: times a factor), add (a float that is 0 or negative: add
an offset), int (a whole number: set within a range), switch (bool), curve (needs the curve locator of phase 2), no.
"""
import json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_INV = os.path.join(ROOT, "docs", "re", "attrib_inventory_6AC07E31.json")
BUILD = "6AC07E31-2145C000"
SCALAR = {"f32": "f32", "i32": "i32", "u32": "u32", "bool": "bool", "i8": "i8"}
ARRAY = {"C:List`1<f32>": "af32", "C:List`1<i32>": "ai32", "C:List`1<bool>": "abool", "C:List`1<u32>": "au32"}


def r6(v):
    """a float as FC 27 wrote it, rounded to 6 significant digits (the EBX holds a 32-bit float)"""
    if isinstance(v, float) and math.isfinite(v):
        return float("%.6g" % v)
    return v


def split_asset(path):
    """'fifa/attribulator/gameplay/groups/gp_positioning/x_runtime' -> (db, variant tuple, family, name); None for non-group assets"""
    p = path.split("/")
    if len(p) < 6 or p[0] != "fifa" or p[1] != "attribulator" or "groups" not in p:
        return None
    gi = p.index("groups")
    if len(p) != gi + 3 or not p[-1].endswith("_runtime"):
        return None
    return p[2], tuple(p[3:gi]), p[gi + 1], p[-1][:-len("_runtime")]


def group_key(family, name):
    """the inventory key of a group: its asset name; a prematch asset drops the family prefix (pm_teamtacticssettings / teamtacticssettings)"""
    if name.startswith(family):
        return name
    return family if family.endswith(name) else family + "_" + name


def slot_of(grp_type, name):
    """the field of an AttribGroupRuntime_<family> type that holds the instance of asset `name` (case-insensitive, the family prefix is
    sometimes dropped: prematch teamtacticssettings)"""
    low = name.lower()
    for f in grp_type["fields"]:
        if f["name"].lower() == low:
            return f
    for f in grp_type["fields"]:
        if low.endswith(f["name"].lower()) or f["name"].lower().endswith(low):
            return f
    return None


def field_kind(f, T):
    pt = f.get("ptype") or ""
    if pt in SCALAR:
        return SCALAR[pt]
    if pt in ARRAY:
        return ARRAY[pt]
    if pt == "V:PointerRef":
        return "curve" if f.get("type") == "FloatCurve" else "ptr"
    if pt.startswith("V:"):
        t = T.get(pt[2:])
        return "enum" if t is not None and not t.get("fields") and t.get("size", 8) <= 4 else "other"
    return "other"


def flat_default(kind, name, values):
    """FC 27's value of one field from the flattened asset values"""
    if kind in SCALAR.values():
        return r6(values.get(name))
    if kind.startswith("a"):
        n = values.get(name + ".#")
        if n is None:
            return None
        return [r6(values.get("%s[%d]" % (name, i))) for i in range(int(n))]
    if kind == "curve":
        n = values.get(name + ".Points.#")
        if n is None:
            return None
        ys = [values.get("%s.Points[%d].Y" % (name, i)) for i in range(int(n))]
        ys = [y for y in ys if isinstance(y, (int, float))]
        return [int(n), r6(min(ys)) if ys else None, r6(max(ys)) if ys else None]
    if kind == "enum":
        return values.get(name)
    return None


def build(assets, typedb):
    T = typedb["types"]
    groups = {}
    for path, a in assets.items():
        sp = split_asset(path)
        if not sp or sp[0] not in ("gameplay", "prematch") or a.get("error"):
            continue
        db, variant, family, name = sp
        if variant:
            continue
        gkey = group_key(family, name)
        t = T.get(a.get("type"))
        dbt = T.get("AttribDBRuntime_%sattribdb" % db)
        gt = T.get("AttribGroupRuntime_" + family)
        if not (t and dbt and gt):
            continue
        fam_f = next((f for f in dbt["fields"] if f["name"].lower() == family.lower()), None)
        slot_f = slot_of(gt, name)
        vals = a["values"]
        fields = []
        for f in sorted(t["fields"], key=lambda x: x["offset"]):
            kind = field_kind(f, T)
            fields.append([f["name"], f["offset"], kind, flat_default(kind, f["name"], vals)])
        groups[gkey] = {"db": db, "family": family, "asset": path, "guid": a.get("partition"), "type": a["type"], "sig": t["sig"], "size": t["size"],
                        "nfields": len(t["fields"]), "family_slot": fam_f["offset"] if fam_f else None,
                        "group_slot": slot_f["offset"] if slot_f else None, "db_sig": dbt["sig"], "group_type": "AttribGroupRuntime_" + family,
                        "group_sig": gt["sig"], "variants": {}, "fields": fields}
    # the other databases' own copies: which databases override the group and which scalar values differ from the base copy
    for path, a in assets.items():
        sp = split_asset(path)
        if not sp or sp[0] != "gameplay" or not sp[1] or a.get("error"):
            continue
        _, variant, family, name = sp
        g = groups.get(group_key(family, name))
        if not g:
            continue
        vname = "/".join(v for v in variant if v != "databases")
        diff = {}
        for fname, off, kind, dv in g["fields"]:
            if kind in SCALAR.values():
                v = r6(a["values"].get(fname))
                if v != dv:
                    diff[fname] = v
        g["variants"][vname] = {"asset": path, "guid": a.get("partition"), "differs": diff}
    return {"build": BUILD, "source": "FC 27 installed game data (attribulator EBX) + FC 27 reflection type data; offline",
            "groups": dict(sorted(groups.items()))}


# ------------------------------------------------------------------------------------------------ semantics (plain language)
# group prefix -> (area, plain meaning). The most specific (longest) prefix wins. Offline judgement [A], never a measurement.
AREAS = [
    ("gp_positioningformation_attacking", "Shape with the ball", "how wide and long the team stands while it has the ball"),
    ("gp_positioningformation_defensive", "Shape without the ball", "how wide and long the team stands without the ball"),
    ("gp_positioningformation_", "Shape", "the formation's base positions per line"),
    ("gp_positioning_zonal_defense_playoffsides", "Offside trap", "when the back line steps up to play the opposition offside / holds the line"),
    ("gp_positioning_zonal_defense_cutpassinglanes", "Pressing", "defenders cutting the passing lanes of the ball carrier"),
    ("gp_positioning_zonal_defense_attribute", "Marking", "reaction time / target speed of the zonal defenders by attribute"),
    ("gp_positioning_zonal_defense_manmarkneargoal", "Marking", "man-marking near the own goal"),
    ("gp_positioning_zonal_defense", "Shape without the ball", "line extents, gaps between lines and the depth thresholds of zone pressing / marking"),
    ("gp_positioning_defense_general", "Pressing", "line holding depth, interception depth, team press switches"),
    ("gp_positioning_partialteampress", "Pressing", "how far, how fast and how long the partial team press runs"),
    ("gp_positioning_defender_stepup", "Pressing", "defenders stepping out of the line to engage the ball carrier"),
    ("gp_positioning_defending_attributes", "Pressing", "pressing and zonal-marking tightness by player attribute"),
    ("gp_positioning_contain", "Pressing", "the ball-nearest defender's contain (jockey) duration and intensity"),
    ("gp_positioning_marking", "Marking", "man-marking tightness, speed and distance"),
    ("gp_positioning_attackingaggressiveness", "Attacking runs", "how often players make attacking / support runs"),
    ("gp_positioning_attacking_general", "Attacking runs", "when decisive runs start"),
    ("gp_positioning_attacker", "Attacking runs", "support / run positioning of the attackers"),
    ("gp_positioning_rundist", "Attacking runs", "how far a player may leave his spot"),
    ("gp_positioning_tacticalrole", "Roles", "full-back / wing-back width and depth, role work rates and speeds"),
    ("gp_positioning_goalkeeper", "Goalkeeper", "goalkeeper positioning"),
    ("gp_positioning_", "Shape", "positioning of the players"),
    ("gp_customization_", "Shape", "ranges the in-game positioning sliders map onto"),
    ("gp_cpuai_callforaction", "CPU AI", "when a team mate asks for the ball and the CPU releases it"),
    ("gp_cpuai_", "CPU AI", "decisions of the computer-controlled players"),
    ("gp_defense_tackling", "Defending", "tackle decisions, ranges and success"),
    ("gp_defense_jockey", "Defending", "jockeying speed and reaction"),
    ("gp_defense_blockintercept", "Defending", "blocks and interceptions"),
    ("gp_defense_", "Defending", "defending actions"),
    ("gp_kickpass", "Passing", "pass power, accuracy and decisions"),
    ("gp_kickshot", "Shooting", "shot power, accuracy and decisions"),
    ("gp_kickerror", "Passing", "error of passes and shots (accuracy)"),
    ("gp_kickcommon", "Passing", "common ball-striking rules"),
    ("gp_goalkeeper_", "Goalkeeper", "goalkeeper saves, reach, reactions and distribution"),
    ("gp_ballhandler", "Dribbling", "dribbling and ball control"),
    ("gp_protectball", "Dribbling", "shielding the ball"),
    ("gp_skillmoves", "Dribbling", "skill moves"),
    ("gp_locomotion", "Physics", "player movement"),
    ("gp_physics", "Physics", "ball and body physics"),
    ("gp_collision", "Physics", "collisions"),
    ("gp_playerattribute", "Player attributes", "how the player attributes feed the simulation"),
    ("gp_player_", "Players", "per-player behaviour hints"),
    ("gp_playerbehavior", "Players", "player behaviour"),
    ("gp_team_", "Team", "momentum / chemistry and team traits"),
    ("gp_emotionengine2", "Mood", "emotion / reaction engine (animation and morale)"),
    ("gp_evaluation", "AI evaluation", "scoring weights the AI uses to rate passes, shots and runs"),
    ("gp_setplay", "Set pieces", "free kicks, corners, throw-ins, kick-off"),
    ("gp_rules", "Rules", "match rules (fouls, advantage, end of half)"),
    ("gp_actor", "Players", "action / movement base data"),
    ("gp_animation", "Animation", "animation quality versus responsiveness"),
    ("gp_gestures", "Animation", "gestures"),
    ("gp_dpad", "Interface", "d-pad commands"),
    ("gp_haptics", "Interface", "controller feedback"),
    ("gp_volta", "Other modes", "Volta"),
    ("gp_choreo", "Animation", "scripted choreography"),
    ("gp_school", "Other", "training school"),
    ("gp_flow", "Other", "game flow"),
    ("gp_twoplayerheader", "Other", "two-player header"),
    ("gp_contextbaseattribmodifiers", "Player attributes", "context modifiers of the base attributes"),
    ("pm_tacticalrole", "Roles", "one of the 90 tactical roles (position, line, width, step-up, work-rate range)"),
    ("pm_teamtacticssettings", "Shape without the ball", "maps the team's defensive depth (1-100) to the four defensive styles"),
    ("pm_formation", "Shape", "prematch formation data"),
    ("pm_attributeboosts", "Player attributes", "positional / chemistry boosts at match setup"),
    ("pm_killswitches", "Other", "engine kill switches"),
    ("pm_simmodesettings", "Other", "simulation-mode settings"),
    ("pm_playerattribute", "Player attributes", "player attribute curves at match setup"),
    ("pm_tacticalsummary", "Roles", "tactical summary"),
]

# field-name keyword -> (category, meaning), tested in order on the lower-cased field name (plain language, offline judgement [A])
FIELD_WORDS = [
    (("pressing", "press"), "Pressing", "pressing"),
    (("mark",), "Marking", "marking"),
    (("tackl",), "Defending", "tackling"),
    (("interce",), "Defending", "interception"),
    (("jockey", "contain"), "Defending", "containing / jockeying"),
    (("offside",), "Offside trap", "offside line behaviour"),
    (("stepup", "step_up"), "Pressing", "stepping out of the line"),
    (("workrate",), "Work rate", "work rate (how much a player runs)"),
    (("run", "sprint"), "Attacking runs", "runs"),
    (("linewidth", "width"), "Shape", "width"),
    (("linelength", "length"), "Shape", "length"),
    (("depth", "lineoffset", "line"), "Shape", "line depth / spacing"),
    (("shot", "shoot"), "Shooting", "shots"),
    (("pass", "cross"), "Passing", "passing"),
    (("dribbl", "trap", "touch"), "Dribbling", "ball control"),
    (("save", "dive", "keeper", "gk"), "Goalkeeper", "goalkeeper"),
    (("speed", "accel"), "Speed", "speed"),
    (("time", "duration", "delay", "interval"), "Timing", "timing"),
    (("threshold",), "Thresholds", "a decision threshold"),
    (("weight", "score", "bias"), "AI evaluation", "a scoring weight"),
    (("error", "accuracy"), "Accuracy", "accuracy"),
    (("power",), "Power", "power"),
    (("distance", "dist", "radius", "range"), "Range", "a distance"),
]
RISKY = re.compile(r"(hash|guid|index|id$|count|enum|type$|mode$|flags?$|mask|version|bone|anim|sound|audio|fx|camera|haptic|rumble)", re.I)


def area_of(name):
    best = None
    for pre, area, meaning in AREAS:
        if name.startswith(pre) and (best is None or len(pre) > len(best[0])):
            best = (pre, area, meaning)
    return (best[1], best[2]) if best else ("Other", "unclassified")


def field_category(group, fname, kind):
    """-> (category, plain word) from the field name, falling back to the group's area"""
    low = fname.lower()
    for words, cat, plain in FIELD_WORDS:
        if any(w in low for w in words):
            return cat, plain
    return area_of(group)[0], "a value of " + area_of(group)[0].lower()


def scale_class(kind, default, fname):
    """an offline judgement of how a value may be changed: mult / add / int / switch / curve / no"""
    if kind == "bool":
        return "switch"
    if kind == "curve":
        return "curve"
    if kind in ("f32",):
        if RISKY.search(fname):
            return "no"
        if not isinstance(default, (int, float)) or not math.isfinite(default):
            return "no"
        return "mult" if default > 0 else "add"
    if kind in ("i32", "u32", "i8"):
        return "no" if RISKY.search(fname) else "int"
    return "no"


def human_label(fname):
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", fname).replace("_", " ")
    s = re.sub(r"\s+", " ", s).strip()
    return s[:1].upper() + s[1:]


def flat_fields(inv):
    """every field of the inventory as dicts (group, name, offset, kind, default, area, category, scale)"""
    out = []
    for gname, g in inv["groups"].items():
        area, _ = area_of(gname)
        for fname, off, kind, dv in g["fields"]:
            cat, _ = field_category(gname, fname, kind)
            out.append({"group": gname, "field": fname, "offset": off, "kind": kind, "default": dv, "area": area, "category": cat,
                        "scale": scale_class(kind, dv, fname)})
    return out


def counts(inv):
    c = {"groups": len(inv["groups"]), "fields": 0, "scalar": 0, "curve": 0, "array": 0, "enum": 0, "ptr": 0, "other": 0, "mult": 0, "add": 0,
         "int": 0, "switch": 0}
    for f in flat_fields(inv):
        c["fields"] += 1
        k = f["kind"]
        c["scalar" if k in SCALAR.values() else "array" if k.startswith("a") and k != "other" else k if k in c else "other"] += 1
        if f["scale"] in ("mult", "add", "int", "switch"):
            c[f["scale"]] += 1
    return c


# ------------------------------------------------------------------------------------------------ report
def fmt(v):
    if isinstance(v, float):
        return "%g" % v
    if isinstance(v, list):
        return "[" + ", ".join(fmt(x) for x in v[:8]) + (", ..." if len(v) > 8 else "") + "]"
    return str(v)


def hexs(v):
    return "0x%x" % v if isinstance(v, int) else "?"


def report(inv, locators=None):
    """the readable inventory; `locators` = {group: True} for groups the shipped knobs file already names"""
    c = counts(inv)
    L = ["# INVENTORY - the game's gameplay attribute groups (FC 27 build %s)" % inv["build"], "",
         "Source: FC 27's own installed data (attribulator EBX of the Patch/Data layers) decoded with FC 27's reflection type data;",
         "offline, read-only. Labels: [V] decoded from those files, [A] assessed (names and neighbours, never measured), [U] unknown.", "",
         "## Totals [V]", "",
         "- %d groups (gameplay + prematch databases, base copy), %d fields: %d single values (float/int/bool), %d arrays, %d curves, %d enums, %d other pointers" %
         (c["groups"], c["fields"], c["scalar"], c["array"], c["curve"], c["enum"], c["ptr"] + c["other"]),
         "- single values by how they may be changed [A]: %d floats > 0 (times a factor), %d floats <= 0 (add an offset), %d whole numbers (set inside a range), %d switches" %
         (c["mult"], c["add"], c["int"], c["switch"]), ""]
    by_fam = {}
    for gname, g in inv["groups"].items():
        by_fam.setdefault((g["db"], g["family"]), []).append(gname)
    L += ["## Groups by family", "", "| db | family | groups | fields | family slot | note |", "|---|---|---|---|---|---|"]
    for (db, fam), names in sorted(by_fam.items()):
        nf = sum(len(inv["groups"][n]["fields"]) for n in names)
        slot = inv["groups"][names[0]]["family_slot"]
        L.append("| %s | %s | %d | %d | %s | %s |" % (db, fam, len(names), nf, hexs(slot), area_of(names[0])[1]))
    L += ["", "## Reach: same chain or new locator", "",
          "Every group has a family slot (database runtime object -> family object) and a group slot (family object -> instance pointer) in FC 27's",
          "type data [V]. That is the chain Turbo's locators already walk (attribute manager -> database -> group -> instance); a group is one more",
          "locator entry (type, signature, size, field count, the two slots, the instance name) and its knobs carry the field offset. No new native code",
          "for single values. What is NOT reached by the same chain without more work:", "",
          "- **curves** (a pointer to a FloatCurve object): one more hop, then the Points array; a different write (phase 2).",
          "- **arrays** (a pointer to a list): one more hop; element writes only with a count check.",
          "- **groups with more than one copy**: Authentic (simulation) and the small-sided databases own their own copy of some groups; a knob must write",
          "  every copy that the played mode uses (`copy_defaults` / variants), see the table below.",
          "- **90 tactical roles**: one instance per role, 90 slots of the same family object (a per-role locator, or a loop in the generator).", ""]
    ov = {}
    for gname, g in inv["groups"].items():
        for v, d in g["variants"].items():
            ov.setdefault(v, []).append((gname, len(d["differs"])))
    L += ["## Groups that other databases copy [V]", "", "| database copy | groups copied | of which with different single values |", "|---|---|---|"]
    for v, lst in sorted(ov.items(), key=lambda x: -len(x[1])):
        L.append("| %s | %d | %d |" % (v, len(lst), sum(1 for _, n in lst if n)))
    L.append("")
    sim = sorted((n, d) for n, g in inv["groups"].items() for v, d in g["variants"].items() if v == "simulation" and d["differs"])
    L += ["Authentic (simulation) copies whose single values differ from the base copy (the played mode may use the simulation one):", ""]
    for n, d in sim:
        L.append("- %s: %s" % (n, ", ".join("%s=%s" % (k, fmt(v)) for k, v in list(d["differs"].items())[:6]) + (" ..." if len(d["differs"]) > 6 else "")))
    L.append("")
    L += ["## Every group with its fields", "",
          "Groups of one schema type are listed once (the same fields, each group has its own values). A field reads `name @offset kind = FC 27 value [how it",
          "may be changed]`; the value shown is the first group's. Arrays and curves are counted, not listed. Full data: docs/re/attrib_inventory_6AC07E31.json.", ""]
    for (db, fam), names in sorted(by_fam.items()):
        L += ["### %s / %s" % (db, fam), ""]
        clusters = {}
        for n in sorted(names):
            clusters.setdefault(inv["groups"][n]["type"], []).append(n)
        for tname, members in sorted(clusters.items(), key=lambda x: x[1][0]):
            g = inv["groups"][members[0]]
            area, meaning = area_of(members[0])
            marks = [n for n in members if locators and n in locators]
            copies = sorted({v for n in members for v in inv["groups"][n]["variants"]})
            head = "**%s**" % (members[0] if len(members) == 1 else "%d groups of type %s" % (len(members), tname.replace("AttribSchema_", "")))
            L.append("%s%s - %s: %s. size %s, %d fields, slots %s/%s%s" % (
                head, " [in the shipped knobs file]" if marks else "", area, meaning, hexs(g["size"]), g["nfields"], hexs(g["family_slot"]), hexs(g["group_slot"]),
                ", copies in: " + ", ".join(copies) if copies else ""))
            if len(members) > 1:
                L.append("")
                L.append("  groups: " + ", ".join(members[:12]) + (" ... (+%d)" % (len(members) - 12) if len(members) > 12 else ""))
            L.append("")
            scal = [f for f in g["fields"] if f[2] in ("f32", "i32", "u32", "bool", "i8")]
            ncurve = sum(1 for f in g["fields"] if f[2] == "curve")
            narr = sum(1 for f in g["fields"] if f[2].startswith("a"))
            L.append("  %d single values, %d curves, %d arrays, %d other" % (len(scal), ncurve, narr, len(g["fields"]) - len(scal) - ncurve - narr))
            for fname, off, kind, dv in scal[:70]:
                L.append("  - %s @%s %s = %s [%s]" % (fname, hexs(off), kind, fmt(dv), scale_class(kind, dv, fname)))
            if len(scal) > 70:
                L.append("  - ... %d more single values" % (len(scal) - 70))
            L.append("")
    return "\n".join(L) + "\n"


# ------------------------------------------------------------------------------------------------ self test
def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        ok = ok and bool(cond)
        print("  %s %s" % ("ok  " if cond else "FAIL", what))

    # synthetic sources: a gameplay group with two copies, a prematch group, a curve, an array, an enum
    T = {"types": {
        "AttribDBRuntime_gameplayattribdb": {"sig": 11, "size": 0x100, "fields": [{"name": "gp_demo", "offset": 0x38, "ptype": "V:AttribGroupRuntime_gp_demo", "type": "x"}]},
        "AttribDBRuntime_prematchattribdb": {"sig": 12, "size": 0x70, "fields": [{"name": "pm_demo", "offset": 0x40, "ptype": "V:AttribGroupRuntime_pm_demo", "type": "x"}]},
        "AttribGroupRuntime_gp_demo": {"sig": 13, "size": 0x30, "fields": [{"name": "gp_demo_press", "offset": 0x20, "ptype": "V:AttribSchema_demo", "type": "x"}]},
        "AttribGroupRuntime_pm_demo": {"sig": 14, "size": 0x20, "fields": [{"name": "demo", "offset": 0x18, "ptype": "V:AttribSchema_pm_demo", "type": "x"}]},
        "AttribSchema_demo": {"sig": 21, "size": 0x60, "fields": [
            {"name": "pressDistance", "offset": 0x28, "ptype": "f32", "type": "Float32"},
            {"name": "pressOn", "offset": 0x2c, "ptype": "bool", "type": "Boolean"},
            {"name": "pressCount", "offset": 0x30, "ptype": "i32", "type": "Int32"},
            {"name": "pressCurve", "offset": 0x38, "ptype": "V:PointerRef", "type": "FloatCurve"},
            {"name": "pressList", "offset": 0x40, "ptype": "C:List`1<f32>", "type": "x"},
            {"name": "pressKind", "offset": 0x48, "ptype": "V:DemoEnum", "type": "DemoEnum"}]},
        "DemoEnum": {"sig": 31, "size": 4, "fields": []},
        "AttribSchema_pm_demo": {"sig": 22, "size": 0x30, "fields": [{"name": "maxDepth", "offset": 0x28, "ptype": "i32", "type": "Int32"}]}}}
    base = "fifa/attribulator/gameplay/groups/gp_demo/gp_demo_press_runtime"
    sim = "fifa/attribulator/gameplay/databases/simulation/groups/gp_demo/gp_demo_press_runtime"
    A = {base: {"type": "AttribSchema_demo", "values": {"pressDistance": 8.0, "pressOn": True, "pressCount": 3, "pressCurve.Points.#": 2,
                                                        "pressCurve.Points[0].Y": 0.5, "pressCurve.Points[1].Y": 2.0, "pressList.#": 2,
                                                        "pressList[0]": 1.5, "pressList[1]": 2.5, "pressKind": "K_A"}},
         sim: {"type": "AttribSchema_demo", "values": {"pressDistance": 9.0, "pressOn": True, "pressCount": 3}},
         "fifa/attribulator/prematch/groups/pm_demo/demo_runtime": {"type": "AttribSchema_pm_demo", "values": {"maxDepth": 30}},
         "fifa/attribulator/gameplay/gameplayattribdb": {"type": "AttribDB", "values": {}}}
    inv = build(A, T)
    g = inv["groups"].get("gp_demo_press")
    check(set(inv["groups"]) == {"gp_demo_press", "pm_demo"}, "build: the two groups, no database asset")
    check(g and g["family_slot"] == 0x38 and g["group_slot"] == 0x20 and g["size"] == 0x60 and g["sig"] == 21 and g["nfields"] == 6, "build: slots, size, signature, field count")
    check(inv["groups"]["pm_demo"]["family_slot"] == 0x40 and inv["groups"]["pm_demo"]["group_slot"] == 0x18 and inv["groups"]["pm_demo"]["db"] == "prematch",
          "build: a prematch group; its slot field named without the family prefix")
    kinds = {f[0]: (f[2], f[3]) for f in g["fields"]}
    check(kinds["pressDistance"] == ("f32", 8.0) and kinds["pressOn"] == ("bool", True) and kinds["pressCount"] == ("i32", 3), "build: single values")
    check(kinds["pressCurve"] == ("curve", [2, 0.5, 2.0]) and kinds["pressList"] == ("af32", [1.5, 2.5]) and kinds["pressKind"][0] == "enum", "build: curve, array, enum")
    check(g["variants"].get("simulation", {}).get("differs") == {"pressDistance": 9.0}, "build: the simulation copy and the value that differs")
    check([f[1] for f in g["fields"]] == sorted(f[1] for f in g["fields"]), "build: fields in offset order")
    check(split_asset(base) == ("gameplay", (), "gp_demo", "gp_demo_press") and split_asset(sim)[1] == ("databases", "simulation") and split_asset("fifa/attribulator/gameplay/gameplayattribdb") is None,
          "split_asset: base, a database copy, a non-group asset")
    check(scale_class("f32", 8.0, "pressDistance") == "mult" and scale_class("f32", 0.0, "x") == "add" and scale_class("i32", 3, "pressCount") == "no" and
          scale_class("i32", 30, "maxDepth") == "int" and scale_class("bool", True, "x") == "switch" and scale_class("curve", [2, 0, 1], "x") == "curve" and
          scale_class("f32", 1.0, "soundHash") == "no", "scale_class: float > 0, float 0, risky names, int, switch, curve")
    check(area_of("gp_positioning_zonal_defense_playoffsides")[0] == "Offside trap" and area_of("gp_positioning_zonal_defense")[0] == "Shape without the ball" and
          area_of("nope")[0] == "Other", "area_of: the longest prefix wins")
    check(field_category("g", "partialTeamPressMaxSpeed", "f32")[0] == "Pressing" and human_label("TacticDepthThresholdForZonePressing") == "Tactic Depth Threshold For Zone Pressing",
          "field_category / human_label")
    rep = report(inv)
    check("gp_demo_press" in rep and "simulation" in rep and "Reach" in rep, "report: renders groups, copies, the reach section")
    c = counts(inv)
    check(c["groups"] == 2 and c["fields"] == 7 and c["curve"] == 1 and c["array"] == 1 and c["enum"] == 1 and c["scalar"] == 4 and c["mult"] == 1 and c["switch"] == 1, "counts: %s" % c)
    # the committed inventory (if the data file is there): consistent with the 10 located knobs' locators in the shipped knobs file
    if os.path.exists(DEFAULT_INV):
        inv2 = json.load(open(DEFAULT_INV, encoding="utf-8"))
        check(inv2["build"] == BUILD and len(inv2["groups"]) > 800, "the committed inventory: build and size (%d groups)" % len(inv2["groups"]))
        bad = [n for n, x in inv2["groups"].items() if x["family_slot"] is None or x["group_slot"] is None]
        check(not bad, "the committed inventory: every group has its chain slots (%d without)" % len(bad))
        width = {"f32": 4, "i32": 4, "u32": 4, "bool": 1, "i8": 1}
        bad = [n for n, x in inv2["groups"].items() if any(o + width[k] > x["size"] for _, o, k, _ in x["fields"] if k in width)]
        check(not bad, "the committed inventory: every single value sits inside its type (%d outside)" % len(bad))
        kn = os.path.join(ROOT, "turbo", "package", "turbo", "tuning", "knobs_6AC07E31.json")
        if os.path.exists(kn):
            kd = json.load(open(kn, encoding="utf-8"))
            bad = []
            for loc in kd["locators"]:
                if loc.get("kind") != "attrib":
                    continue
                grp = loc["variants"][0]["instance"].rsplit("/", 1)[1][:-len("_runtime")]
                x = inv2["groups"].get(grp) or inv2["groups"].get("pm_" + grp)
                if not x or hexs(x["family_slot"]) != loc["family_slot"] or hexs(x["group_slot"]) != loc["group_slot"] or hexs(x["size"]) != loc["type_size"] or \
                        x["nfields"] != loc["type_fields"] or hexs(x["sig"]) != loc["type_signature"]:
                    bad.append(loc["name"])
            check(not bad, "the inventory reproduces the shipped locators' slots, size, signature and field count (%s)" % ", ".join(bad))
    print("SELF-TEST", "PASSED" if ok else "FAILED")
    return 0 if ok else 1


def main(argv):
    if argv[1:2] == ["--self-test"]:
        return self_test()
    if len(argv) < 2 or argv[1] not in ("build", "report"):
        print(__doc__)
        return 2
    args = {}
    i = 2
    while i < len(argv) - 1:
        args[argv[i]] = argv[i + 1]
        i += 2
    if argv[1] == "build":
        if "--assets" not in args or "--typedb" not in args:
            print(__doc__)
            return 2
        inv = build(json.load(open(args["--assets"], encoding="utf-8")), json.load(open(args["--typedb"], encoding="utf-8")))
        out = args.get("--out", DEFAULT_INV)
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            json.dump(inv, f, separators=(",", ":"), sort_keys=False)
            f.write("\n")
        print("inventory:", counts(inv), "->", out)
        return 0
    inv = json.load(open(args.get("--inv", DEFAULT_INV), encoding="utf-8"))
    text = report(inv)
    if "--out" in args:
        with open(args["--out"], "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("report ->", args["--out"])
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
