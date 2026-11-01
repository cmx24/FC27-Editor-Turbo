#!/usr/bin/env python3
"""Regenerates the "entries" of turbo/package/turbo/settings/slider_map_6AC07E31.json from the game's own slider table.

Source of the order (Turbo 2.12.1, research_20261016/SLIDER_MAP_FULL.md): the setting table in FC27.exe build 6AB9813C
(rows {name, index, id}, the ids start at 0x8D = byte 0 of the 125-byte Custom block; the three bytes seen on the EA Custom screen
(TEST_31 G-S1: CPU sprint 36, user sprint 80, user acceleration 81) sit where this table puts them, and the 125 rows fill the
block exactly: 16 CPU-AI opponent + 16 CPU-AI teammates + 4 group sliders + 37 CPU + 7 CPU positioning + 37 user + 7 user
positioning + 1 power bar). Everything except the three screen-proven bytes was "table" evidence until 2.12.1's one-launch proof.
Turbo X sliders (2026-11-01): the 122 others are "file" evidence (FILE-MAPPED): the P2121 marker file (2 + byte*13 mod 97, never 50) was kept
by the game and every value was seen at its label on the EA Custom screen on build 6AC07E31 (evidence/2026-10-16/P2121/SLIDERS.md 125/125,
again 10-19 L7b/L7c); on the running build 6AD922D1 only the 3 anchors were seen (SLIDERS_READ/marker_2041), so there they are file-mapped:
written to the Settings file with the game closed (turbo_settings_apply --apply), never live. verified_by_build 6AC07E31 lists all 125.
Usage: python tools/gen_slider_map.py [--check]   (rewrites the JSON; --check only compares)
       python tools/gen_slider_map.py --proof PRESET.json PENDING.json   (the 2.12.1 one-launch proof files)
"""
import json, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
PATH = os.path.join(HERE, "..", "turbo", "package", "turbo", "settings", "slider_map_6AC07E31.json")

# (id stem, label, game name) in the table's order, k = 0..36 per side
SLIDERS = [
    ("sprint_speed", "Sprint speed", "SPRINT_SPEED"), ("acceleration", "Acceleration", "ACCELERATION"),
    ("shot_error", "Shot error", "REGULAR_SHOT_ERROR"), ("header_shot_accuracy", "Header shot accuracy", "HEADER_SHOT_ACCURACY"),
    ("pass_error", "Pass error", "GROUND_PASS_ERROR"), ("header_pass_accuracy", "Header pass accuracy", "HEADER_PASS_ACCURACY"),
    ("trap_error", "First touch control error", "TRAP_ERROR"), ("intercept_trap_error", "Intercept control", "INTERCEPT_CONTROL"),
    ("tackle_accuracy", "Tackle accuracy", "TACKLE_ACCURACY"), ("gk_ball_deflection_control", "Goalkeeper ball deflection control", "GK_BALL_DEFLECTION_CONTROL"),
    ("shot_speed", "Shot speed", "REGULAR_SHOT_SPEED"), ("pass_speed", "Pass speed", "GROUND_PASS_SPEED"),
    ("injury_frequency", "Injury frequency", "INJURY_FREQUENCY"), ("injury_severity", "Injury severity", "INJURY_SEVERITY"),
    ("finesse_shot_error", "Finesse shot error", "FINESSE_SHOT_ERROR"), ("chip_shot_error", "Chip shot error", "CHIP_SHOT_ERROR"),
    ("low_driven_shot_error", "Low driven shot error", "LOW_DRIVEN_NORMAL_SHOT_ERROR"), ("power_shot_error", "Power shot error", "POWER_SHOT_ERROR"),
    ("through_pass_error", "Through pass error", "THROUGH_PASS_ERROR"), ("lob_pass_error", "Lob pass error", "LOB_PASS_ERROR"),
    ("cross_error", "Cross error", "CROSS_ERROR"), ("lobbed_through_pass_error", "Lobbed through pass error", "LOBBED_THROUGH_PASS_ERROR"),
    ("finesse_shot_speed", "Finesse shot speed", "FINESSE_SHOT_SPEED"), ("chip_shot_speed", "Chip shot speed", "CHIP_SHOT_SPEED"),
    ("low_driven_shot_speed", "Low driven shot speed", "LOW_DRIVEN_NORMAL_SHOT_SPEED"), ("power_shot_speed", "Power shot speed", "POWER_SHOT_SPEED"),
    ("outfield_ball_deflection_control", "Outfield ball deflection control", "OUTFIELD_PLAYERS_BALL_DEFLECTION_CONTROL"),
    ("lob_pass_speed", "Lob pass speed", "LOB_PASS_SPEED"), ("cross_speed", "Cross speed", "CROSS_SPEED"),
    ("lobbed_through_pass_speed", "Lobbed through pass speed", "LOBBED_THROUGH_PASS_SPEED"),
    ("dribble_jog_touch_interval", "Dribble jog touch interval", "DRIBBLE_JOG_TOUCH_INTERVAL"),
    ("dribble_sprint_touch_interval", "Dribble sprint touch interval", "DRIBBLE_SPRINT_TOUCH_INTERVAL"),
    ("dribble_controlled_touch_interval", "Dribble controlled touch interval", "DRIBBLE_CONTROLLED_TOUCH_INTERVAL"),
    ("through_pass_speed", "Through pass speed", "THROUGH_PASS_SPEED"), ("regular_jockey_speed", "Regular jockey speed", "REGULAR_JOCKEY_SPEED"),
    ("sprint_jockey_speed", "Sprint jockey speed", "SPRINT_JOCKEY_SPEED"), ("physicality_impact", "Physicality impact", "PHYSICALITY_IMPACT"),
]
POSITIONING = [
    ("pos_marking", "Positioning: marking", "POS_MARKING"), ("pos_line_length", "Positioning: line length", "POS_LINE_LENGTH"),
    ("pos_line_width", "Positioning: line width", "POS_LINE_WIDTH"), ("pos_def_line_height", "Positioning: line height", "POS_DEFENSIVE_LINE_HEIGHT"),
    ("pos_run_frequency", "Positioning: run frequency", "POS_RUN_FREQUENCY"), ("pos_full_back", "Positioning: full back positioning", "POS_FULLBACK_POSITIONING"),
    ("gk_ability", "Goalkeeper ability", "GK_ABILITY"),
]
# CPU Sliders tab: 16 per side (the _CPU names = CPU opponent at bytes 0..15, the _USER names = CPU teammates at bytes 16..31)
CPU_AI = [
    ("defending_aggression", "Defending aggression", "DEFENDING_AGGRESSION"), ("stand_tackle_frequency", "Stand tackle frequency", "STAND_TACKLE_FREQUENCY"),
    ("slide_tackle_frequency", "Slide tackle frequency", "SLIDE_TACKLE_FREQUENCY"), ("professional_foul_frequency", "Professional foul frequency", "PROFESSIONAL_FOUL_FREQUENCY"),
    ("normal_shot_frequency", "Normal shot frequency", "NORMAL_SHOT_FREQUENCY"), ("buildup_speed", "Build-up speed", "BUILDUP_SPEED"),
    ("skill_move_frequency", "Skill move frequency", "SKILL_MOVE_FREQUENCY"), ("normal_crossing_frequency", "Normal crossing frequency", "NORMAL_CROSS_FREQUENCY"),
    ("early_crossing_frequency", "Early crossing frequency", "EARLY_CROSS_FREQUENCY"), ("first_touch_pass_frequency", "First touch pass frequency", "FIRST_TOUCH_PASS_FREQUENCY"),
    ("dribble_frequency", "Dribble frequency", "DRIBBLE_FREQUENCY"), ("finesse_shot_frequency", "Finesse shot frequency", "FINESSE_SHOT_FREQUENCY"),
    ("chip_shot_frequency", "Chip shot frequency", "CHIP_SHOT_FREQUENCY"), ("low_driven_shot_frequency", "Low driven shot frequency", "LOW_DRIVEN_SHOT_FREQUENCY"),
    ("power_shot_frequency", "Power shot frequency", "POWER_SHOT_FREQUENCY"), ("long_shot_frequency", "Long shot frequency", "LONG_SHOT_FREQUENCY"),
]
META = [("all_pass_speed", "All pass speed (group)", "ALL_PASS_SPEED_SLIDERS"), ("all_pass_error", "All pass error (group)", "ALL_PASS_ERROR_SLIDERS"),
        ("all_shot_speed", "All shot speed (group)", "ALL_SHOT_SPEED_SLIDERS"), ("all_shot_error", "All shot error (group)", "ALL_SHOT_ERROR_SLIDERS")]
PROVEN = {"user.sprint_speed": 80, "user.acceleration": 81, "cpu.sprint_speed": 36}
PROOF = "evidence/2026-10-15/TEST_31.md"
# the per-byte file evidence of the 122 others (Turbo X sliders, 2026-11-01; see the docstring)
FILE_PROOF = ("evidence/2026-10-16/P2121: the marker block 2+(byte*13 mod 97) applied to the Settings file was kept by the game "
              "(P2121run/post Settings20261016151900674, md5 00d17694; again 10-19 L6b/L7a, block 53968757) and the EA Custom screen showed "
              "this byte's value at this label (P2121/SLIDERS.md 125/125, mosaic/*.jpg; 10-19 L7b/L7c 125/125) on build 6AC07E31. "
              "6AD922D1: same layout, anchors 0x24/0x50/0x51 = Cassio's 42/41/43 (evidence/2026-10-28/SLIDERS_READ/marker_2041); "
              "this byte not yet seen on the 6AD922D1 screen")
BUILD_PROOF_6AC07E31 = ("evidence/2026-10-16/P2121/SLIDERS.md: 125 of 125 Custom bytes = the marker values on the EA Custom screen "
                        "(screenshots P2121/drive*/, mosaic/*.jpg), repeated 10-19 L7b/L7c (play_G3_L7b/RESULTS.md, play_G3_L7c/RESULTS.md)")
TABLE = "game slider table (FC27.exe 6AB9813C, rows {name,index,id}): byte = id - 0x8D"


def build_entries():
    rows = []  # (custom_index, id, label, group, game_name)
    for k, (s, l, g) in enumerate(CPU_AI):
        rows.append((k, "cpu." + s, l + " (CPU opponent)", "cpu", g + "_CPU"))
        rows.append((16 + k, "user." + s, l + " (CPU teammates)", "user", g + "_USER"))
    for k, (s, l, g) in enumerate(META):
        rows.append((32 + k, "meta." + s, l, "meta", g))
    for side, base in (("cpu", 36), ("user", 80)):
        suf = "_CPU" if side == "cpu" else "_USER"
        for k, (s, l, g) in enumerate(SLIDERS):
            rows.append((base + k, side + "." + s, l, side, g + suf))
        for j, (s, l, g) in enumerate(POSITIONING):
            rows.append((base + 37 + j, side + "." + s, l, side, g + suf))
    rows.append((124, "user.power_bar_speed", "Power bar speed", "user", "POWER_BAR_USER"))
    rows.sort()
    assert [r[0] for r in rows] == list(range(125)), "the table must fill the 125-byte block exactly"
    out = []
    for ci, eid, label, grp, gname in rows:
        e = {"id": eid, "label": label, "group": grp, "index": None, "custom_index": ci, "game_name": gname}
        if eid in PROVEN:
            assert PROVEN[eid] == ci
            e.update(verified=True, proof=PROOF, evidence="screen",
                     note="Custom block byte 0x%02X (file 0x%X). OBSERVED on the EA Custom screen (TEST_31 G-S1); engine use in a match not proven." % (ci, 0xB8DDC + ci))
        else:
            e.update(verified=False, proof=FILE_PROOF, evidence="file",
                     note="Custom block byte 0x%02X (file 0x%X), order from the %s. FILE-MAPPED: seen on the EA screen on 6AC07E31 (P2121), not yet "
                          "on 6AD922D1; Turbo writes it to the Settings file with the game closed, never live." % (ci, 0xB8DDC + ci, TABLE))
        out.append(e)
    # the two old names with no slot in the 125-byte block (the builder never reads index 9 and forces index 10 to 50)
    for side in ("user", "cpu"):
        out.append({"id": side + ".dribble_error", "label": "Dribble error", "group": side, "index": None, "verified": False, "proof": "",
                    "note": "no slot in the 125-byte Custom block (builder slider index 9 is never read): cannot be set"})
        out.append({"id": side + ".pass_angle_error", "label": "Pass angle error", "group": side, "index": None, "verified": False, "proof": "",
                    "note": "no slot in the 125-byte Custom block (builder slider index 10 is forced to 50): cannot be set"})
    return out


def proof_value(k):
    """A value per Custom byte that is never 50 (the default) and repeats only 97 bytes apart: a swapped or shifted byte shows as the wrong number on its label."""
    v = 2 + (k * 13) % 97
    return 51 if v == 50 else v


def write_proof(preset_path, pending_path):
    """The one-launch proof files (research_20261016/P2121_PROOF.md): a tactic preset (settings block only, include_unverified) and the staged
    settings_pending.json the tool turbo_settings_apply.exe --apply reads (no Turbo window needed)."""
    j = json.load(open(PATH, encoding="utf-8"))
    vals = {e["id"]: proof_value(e["custom_index"]) for e in j["entries"] if e.get("custom_index") is not None}
    preset = {"turbo_tactic_preset": 1, "name": "P2121 proof", "note": "every Custom slider = 2 + (byte*13) mod 97 (never 50)",
              "created": "2026-10-16T00:00", "game_build": "6AC07E31-2145C000",
              "settings": {"layout": "fbchunks-454-bc650", "slider_map_build": "6AC07E31", "values": vals, "include_unverified": True}}
    unver = [e["id"] for e in j["entries"] if e.get("evidence") in ("table", "file")]
    pending = {"turbo_settings_pending": 1, "preset": "P2121 proof", "staged": "2026-10-16T00:00:00", "source_md5": "", "values": vals, "unverified": unver}
    for path, obj in ((preset_path, preset), (pending_path, pending)):
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(obj, indent=1) + "\n")
    print("proof files:", preset_path, pending_path, len(vals), "values,", len(unver), "unverified")


def main():
    if "--proof" in sys.argv:
        i = sys.argv.index("--proof")
        write_proof(sys.argv[i + 1], sys.argv[i + 2])
        return
    j = json.load(open(PATH, encoding="utf-8"))
    keep = [e for e in j["entries"] if e["group"] in ("match", "player")]
    j["entries"] = keep + build_entries()
    vb = j.setdefault("verified_by_build", {})
    vb["6AC07E31"] = {"note": "Turbo X sliders (2026-11-01): every Custom byte seen on the EA Custom screen on this build (marker values, P2121 10-16 and L7b/L7c 10-19).",
                      "proof": BUILD_PROOF_6AC07E31,
                      "ids": [e["id"] for e in j["entries"] if e.get("custom_index") is not None]}
    j["note"] = ("FC 27 Settings<timestamp> slider map. ALL 125 bytes of the Custom block (GameplayTypeCustomData, payload 0xB8976 = file 0xB8DDC) are named from the game's "
                 "own slider table (16+16 CPU-AI, 4 group sliders, 37+7 CPU, 37+7 user, power bar), see turbo_dev/research_20261016/SLIDER_MAP_FULL.md. "
                 "Build 6AC07E31: all 125 seen on the EA Custom screen with marker values (P2121 10-16, L7b/L7c 10-19; verified_by_build). "
                 "Build 6AD922D1 (title update 10-27): only cpu.sprint_speed 0x24, user.sprint_speed 0x50, user.acceleration 0x51 seen on the screen "
                 "(verified, written live); the other 122 are evidence \"file\" (FILE-MAPPED: same layout, anchors hold; written to the Settings file "
                 "with the game closed by turbo_settings_apply --apply, never live). That the engine READS this block in a match is NOT proven "
                 "(G-S3 FAIL). The u32 array (array_base) is NOT what the Custom screen shows: no known UI change ever moved it.")
    text = json.dumps(j, indent=1, ensure_ascii=False) + "\n"
    if "--check" in sys.argv:
        print("up to date" if open(PATH, encoding="utf-8").read() == text else "DIFFERS")
        return
    with open(PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("entries", len(j["entries"]), "custom-mapped", sum(1 for e in j["entries"] if e.get("custom_index") is not None))


main()
