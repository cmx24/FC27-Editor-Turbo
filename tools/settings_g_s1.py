"""settings_g_s1.py - OFFLINE analysis of gate G-S1 (PLAN_tactics_settings.md section 3): after the coordinator changed Gameplay Type -> CUSTOM and
set three distinct slider values in the EA menu (Sprint Speed User 37, Acceleration User 41, Sprint Speed CPU 63), where did the numbers go?
It reads ONLY the explicit paths it is given (copies the coordinator already made under EV\\<date>\\g_s1\\); it never lists, opens or writes
the live settings folder, the saves or the game, and it needs no dev service.

  python settings_g_s1.py --old OLD_SETTINGS --new NEW_SETTINGS [--gv GV.json|GV.txt] [--dump ATTRIB_DUMP.json]
                          [--md5-before FILE --md5-after FILE] [--targets sprint_user=37,accel_user=41,sprint_cpu=63] [--out FILE.md]
  python settings_g_s1.py --mock          the whole analysis over synthetic fixtures (in a temp folder)
  python settings_g_s1.py --self-test

Inputs
  --old / --new   two copies of the game's Settings<ts> file (FBCHUNKS, 772790 B, see game_settings.cpp). Compared as u32 words of the PAYLOAD
                  from payload+48 (the CRC32 word at payload+16 and the whole meta block - slot name, GUID, timestamps - are NOT part of the moved
                  list). A word "moves to" a target when its new value equals the target as u32, as float32, or as a 0..1 fraction (target/100).
  --gv            the game variables read through the existing gv bridge after the change: JSON {name: value} or lines "NAME = value".
  --dump          an attrib_dump.py JSON taken AFTER the change: scalars equal to a target (u32/i32/f32) are listed; groups whose name contains
                  'customization' are called out.
  --md5-before/--md5-after   md5 lists (lines "<md5>  <file>" or "<file>  <md5>") of the settings folder copy before / after: CmMgrC* entries must be
                  unchanged (the career was never saved); Settings<ts> entries may appear (the game writes one on leaving the screen).
Output: a Markdown report and the G-S1 branch row(s) of the plan. VERDICT PASS = at least one route moved, FAIL = nothing moved anywhere
(plan row 4), INCONCLUSIVE = a required input is missing or invalid. Exit 0 / 1 / 3.
"""
import argparse, json, os, re, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402

TOOL = "settings_g_s1"
HEADER, META = 18, 0x454
POFF = HEADER + META            # payload offset in the file
PSIZE = 0xBC650
FSIZE = POFF + PSIZE            # 772790
CRC_AT, CRC_FROM = 16, 48
DEFAULT_TARGETS = "sprint_user=37,accel_user=41,sprint_cpu=63"
GV_NAMES = {"sprint_user": "GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_USER", "accel_user": "GAMEPLAY_CUSTOMIZATION/ACCELERATION_USER",
            "sprint_cpu": "GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_CPU"}


class Bad(Exception):
    pass


def parse_targets(text):
    out = {}
    for part in text.split(","):
        if not part.strip():
            continue
        k, v = part.split("=", 1)
        out[k.strip()] = float(v)
    if len(out) < 1:
        raise Bad("no targets")
    return out


def load_settings(path):
    b = open(path, "rb").read()
    if len(b) != FSIZE:
        raise Bad("%s: %d bytes, expected %d (another layout)" % (path, len(b), FSIZE))
    if b[:10] != b"FBCHUNKS\x01\x00":
        raise Bad("%s: not an FBCHUNKS file" % path)
    if b[POFF:POFF + 15] != b"SaveType_Settin":
        raise Bad("%s: the payload is not SaveType_Settings" % path)
    stored = struct.unpack_from("<I", b, POFF + CRC_AT)[0]
    calc = zlib.crc32(b[POFF + CRC_FROM:POFF + PSIZE]) & 0xFFFFFFFF
    return b, stored == calc


def words(b):
    return struct.unpack_from("<%dI" % ((PSIZE - CRC_FROM) // 4), b, POFF + CRC_FROM)  # word i = payload offset 48 + 4 i


def hits_target(u, targets):
    """-> [(name, how)] for a u32 word value u"""
    f = struct.unpack("<f", struct.pack("<I", u))[0]
    out = []
    for n, t in targets.items():
        if u == int(t) and float(t).is_integer():
            out.append((n, "u32"))
        if f == f and abs(f - t) < 1e-3 and u != int(t):
            out.append((n, "f32"))
        if f == f and abs(f - t / 100.0) < 1e-4:
            out.append((n, "fraction f32 (target/100)"))
    return out


def diff_settings(old, new, targets):
    """-> dict(moved [{word, payload_off, old, new, hits}], meta_changed int, crc_changed bool, found {target: [(word, how)]})"""
    wo, wn = words(old), words(new)
    moved, found = [], {n: [] for n in targets}
    for i, (a, c) in enumerate(zip(wo, wn)):
        if a != c:
            h = hits_target(c, targets)
            moved.append({"word": i, "payload_off": CRC_FROM + 4 * i, "old": a, "new": c,
                          "old_f32": struct.unpack("<f", struct.pack("<I", a))[0], "new_f32": struct.unpack("<f", struct.pack("<I", c))[0], "hits": h})
            for n, how in h:
                found[n].append((i, how))
    meta = sum(1 for a, c in zip(old[:POFF], new[:POFF]) if a != c)
    crc = old[POFF + CRC_AT:POFF + CRC_AT + 4] != new[POFF + CRC_AT:POFF + CRC_AT + 4]
    return {"moved": moved, "meta_changed": meta, "crc_changed": crc, "found": found}


def read_gv(path):
    txt = open(path, encoding="utf-8", errors="replace").read()
    try:
        d = json.loads(txt)
        if isinstance(d, dict):
            return {str(k): v for k, v in d.items()}
    except ValueError:
        pass
    out = {}
    for line in txt.splitlines():
        m = re.match(r"\s*([A-Za-z0-9_/]+)\s*[=:]\s*(-?[0-9.]+)", line)
        if m:
            out[m.group(1)] = float(m.group(2))
    return out


def gv_check(gv, targets):
    res = {}
    for n, t in targets.items():
        key = GV_NAMES.get(n)
        v = None
        if key:
            for k, val in gv.items():
                if k.upper().endswith(key.split("/")[-1]) or k.upper() == key:
                    v = val
        res[n] = {"key": key, "value": v, "moved": v is not None and abs(float(v) - t) < 1e-3}
    return res


def dump_hits(dump, targets):
    custom, other = [], []
    want = list(targets.values())
    for gn, g in dump["groups"].items():
        for fn, f in g["fields"].items():
            v = f.get("value")
            if v is None or isinstance(v, bool):
                continue
            for n, t in targets.items():
                if abs(float(v) - t) < 1e-3:
                    (custom if "customization" in gn.lower() else other).append({"group": gn, "field": fn, "value": v, "target": n})
    return custom, other


def md5_lists(path):
    out = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.split()
        if len(parts) < 2:
            continue
        a, b = parts[0], parts[-1].lstrip("*")
        if re.fullmatch(r"[0-9a-fA-F]{32}", a):
            out[os.path.basename(b)] = a.lower()
        elif re.fullmatch(r"[0-9a-fA-F]{32}", b):
            out[os.path.basename(a)] = b.lower()
    return out


def md5_check(before, after):
    cm = {n: (before.get(n), after.get(n)) for n in sorted(set(before) | set(after)) if n.lower().startswith("cmmgrc")}
    bad = [n for n, (x, y) in cm.items() if x != y]
    new_settings = sorted(n for n in after if n.startswith("Settings") and n not in before)
    return {"cmmgr": cm, "cmmgr_changed": bad, "new_settings": new_settings}


def analyse(targets, old=None, new=None, gv=None, dump=None, md5=None):
    r = {"targets": targets, "routes": {}, "notes": []}
    if old is not None and new is not None:
        d = diff_settings(old, new, targets)
        r["file"] = d
        all3 = all(len(d["found"][n]) == 1 for n in targets)
        any_ = any(d["found"][n] for n in targets)
        r["routes"]["file"] = "moved (all targets, one word each)" if all3 else ("moved (partial / ambiguous)" if any_ else "no target value in a moved word")
        if d["moved"] and not any_:
            r["notes"].append("%d payload words moved but none holds a target value: the menu may store the value in another encoding" % len(d["moved"]))
    if gv is not None:
        r["gv"] = gv_check(gv, targets)
        r["routes"]["gv"] = "moved" if any(x["moved"] for x in r["gv"].values()) else "no target value in the game variables"
    if dump is not None:
        c, o = dump_hits(dump, targets)
        r["dump"] = {"customization": c, "other": o}
        r["routes"]["attribdb"] = "customization scalars hold a target" if c else ("only unrelated groups hold a target value (%d: probably coincidence)" % len(o) if o else "no scalar equals a target")
    if md5 is not None:
        r["md5"] = md5
        if md5["cmmgr_changed"]:
            r["notes"].append("CmMgrC* CHANGED (%s): the career was saved or touched - the launch is a FAIL (never-saved rule)" % ", ".join(md5["cmmgr_changed"]))
    moved_routes = [k for k, v in r["routes"].items() if v.startswith("moved") or v.startswith("customization")]
    br = []
    if "file" in r["routes"] and r["routes"]["file"].startswith("moved"):
        br.append("file indices moved: slot map proven for the moved ids (verified:true + proof); file route works for the next launch (G-S2a)")
    if "gv" in r["routes"] and r["routes"]["gv"] == "moved":
        br.append("gv moved too: the menu writes gv; write target = the profile store, not gv (channel A lesson)")
    if "attribdb" in r["routes"] and r["routes"]["attribdb"].startswith("customization"):
        br.append("attribdb holds the values: write through the same attribulator chain (G-S2b)")
    if not br and r["routes"]:
        br.append("nothing moved anywhere: RE the getter-by-id on 6AC07E31 (T3); G-S2c")
    r["branch"] = br
    if not r["routes"]:
        r["verdict"] = "INCONCLUSIVE"
    elif md5 is not None and md5["cmmgr_changed"]:
        r["verdict"] = "FAIL"
    else:
        r["verdict"] = "PASS" if moved_routes else "FAIL"
    return r


def render(r, old_ok=None, new_ok=None):
    L = ["# G-S1 analysis", "", "Targets: " + ", ".join("%s = %g" % kv for kv in r["targets"].items()), ""]
    L += ["## Routes", ""] + ["- %s: %s" % kv for kv in r["routes"].items()] + [""]
    if "file" in r:
        d = r["file"]
        L += ["## Settings file diff", "",
              "- payload words moved (CRC word and meta excluded): %d; meta bytes changed (slot name / timestamps, ignored): %d; CRC word changed: %s" % (
                  len(d["moved"]), d["meta_changed"], d["crc_changed"]),
              "- CRC valid: old %s, new %s" % (old_ok, new_ok), ""]
        for n, hs in d["found"].items():
            L.append("- %s: %s" % (n, ", ".join("word %d (payload+0x%X, %s)" % (w, CRC_FROM + 4 * w, how) for w, how in hs) or "NOT found in a moved word"))
        L += ["", "| word | payload offset | old | new | new as f32 | target hit |", "|---|---|---|---|---|---|"]
        for m in d["moved"][:60]:
            L.append("| %d | 0x%X | 0x%X | 0x%X | %.6g | %s |" % (m["word"], m["payload_off"], m["old"], m["new"], m["new_f32"],
                                                                 ", ".join("%s (%s)" % h for h in m["hits"]) or ""))
        if len(d["moved"]) > 60:
            L.append("| ... | | | | | %d more |" % (len(d["moved"]) - 60))
        L.append("")
    if "gv" in r:
        L += ["## Game variables (gv)", ""] + ["- %s (%s): %s -> %s" % (n, x["key"], x["value"], "MOVED" if x["moved"] else "not the target") for n, x in r["gv"].items()] + [""]
    if "dump" in r:
        L += ["## attribdb dump", "", "- customization groups holding a target: %d" % len(r["dump"]["customization"])]
        L += ["  - %(group)s . %(field)s = %(value)s (%(target)s)" % x for x in r["dump"]["customization"][:30]]
        L += ["- other groups holding a target value: %d (probably coincidence)" % len(r["dump"]["other"])]
        L += ["  - %(group)s . %(field)s = %(value)s (%(target)s)" % x for x in r["dump"]["other"][:20]] + [""]
    if "md5" in r:
        m = r["md5"]
        L += ["## Folder md5", "", "- CmMgrC*: %s" % ("UNCHANGED" if not m["cmmgr_changed"] else "CHANGED: " + ", ".join(m["cmmgr_changed"])),
              "- new Settings<ts> files after the change: %s" % (", ".join(m["new_settings"]) or "none"), ""]
    L += ["## Branch (plan section 3)", ""] + ["- " + b for b in r["branch"]] + [""] + ["- note: " + n for n in r["notes"]]
    L += ["", "VERDICT %s" % r["verdict"]]
    return "\n".join(L) + "\n"


def make_fixture(d, targets=(37, 41, 63), mode="u32", crc_only=False):
    """synthetic old/new settings files (valid layout + CRC); returns (old_path, new_path, word_indices)"""
    b = bytearray(FSIZE)
    b[:10] = b"FBCHUNKS\x01\x00"
    struct.pack_into("<II", b, 10, META, PSIZE)
    b[HEADER:HEADER + 8] = b"LEICESTR"
    b[POFF:POFF + 15] = b"SaveType_Settin"
    struct.pack_into("<I", b, POFF + 0x30, PSIZE - CRC_FROM)
    idx = [120, 121, 460]
    for k, i in enumerate(idx):
        struct.pack_into("<I", b, POFF + CRC_FROM + 4 * i, 35 + k)  # the Authentic values before

    def seal(x):
        struct.pack_into("<I", x, POFF + CRC_AT, zlib.crc32(bytes(x[POFF + CRC_FROM:POFF + PSIZE])) & 0xFFFFFFFF)
    seal(b)
    n = bytearray(b)
    n[HEADER + 8] = ord("X")  # a meta change (timestamp-like)
    if not crc_only:
        for t, i in zip(targets, idx):
            struct.pack_into("<I", n, POFF + CRC_FROM + 4 * i, t if mode == "u32" else struct.unpack("<I", struct.pack("<f", float(t)))[0])
    seal(n)
    po, pn = os.path.join(d, "old"), os.path.join(d, "new")
    open(po, "wb").write(bytes(b))
    open(pn, "wb").write(bytes(n))
    return po, pn, idx


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--old")
    ap.add_argument("--new")
    ap.add_argument("--gv")
    ap.add_argument("--dump")
    ap.add_argument("--md5-before")
    ap.add_argument("--md5-after")
    ap.add_argument("--targets", default=DEFAULT_TARGETS)
    ap.add_argument("--out", default=None)
    ap.add_argument("--mock", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    import tempfile
    try:
        targets = parse_targets(a.targets)
        if a.mock:
            with tempfile.TemporaryDirectory() as d:
                po, pn, _ = make_fixture(d)
                gvp = os.path.join(d, "gv.json")
                json.dump({GV_NAMES["sprint_user"]: 50, GV_NAMES["accel_user"]: 50, GV_NAMES["sprint_cpu"]: 50}, open(gvp, "w"))
                m1, m2 = os.path.join(d, "m1"), os.path.join(d, "m2")
                open(m1, "w").write("a" * 32 + "  CmMgrC1\n")
                open(m2, "w").write("a" * 32 + "  CmMgrC1\n" + "b" * 32 + "  Settings20261015101516976\n")
                return run(po, pn, gvp, None, m1, m2, targets, a.out)
        return run(a.old, a.new, a.gv, a.dump, a.md5_before, a.md5_after, targets, a.out)
    except (Bad, OSError, ValueError) as e:
        print("settings_g_s1: %s" % e)
        return harness.finish("INCONCLUSIVE", tool=TOOL)


def run(old_p, new_p, gv_p, dump_p, m1, m2, targets, out):
    old = new = None
    ok_o = ok_n = None
    if bool(old_p) != bool(new_p):
        raise Bad("--old and --new go together")
    if old_p:
        old, ok_o = load_settings(old_p)
        new, ok_n = load_settings(new_p)
    gv = read_gv(gv_p) if gv_p else None
    dump = json.load(open(dump_p, encoding="utf-8")) if dump_p else None
    md5 = md5_check(md5_lists(m1), md5_lists(m2)) if (m1 and m2) else None
    r = analyse(targets, old, new, gv, dump, md5)
    txt = render(r, ok_o, ok_n)
    if out:
        open(out, "w", encoding="utf-8").write(txt)
        print("wrote %s" % out)
    print(txt)
    return harness.finish(r["verdict"], tool=TOOL)


def self_test():
    import tempfile
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond
    T = parse_targets(DEFAULT_TARGETS)
    with tempfile.TemporaryDirectory() as d:
        po, pn, idx = make_fixture(d)
        o, ook = load_settings(po)
        n, nok = load_settings(pn)
        check(ook and nok, "fixtures: both CRCs valid")
        r = analyse(T, o, n)
        f = r["file"]["found"]
        check([f[k][0][0] for k in ("sprint_user", "accel_user", "sprint_cpu")] == idx and all(len(v) == 1 for v in f.values()),
              "u32 37/41/63 found at the three moved words (slot map for 3 ids)")
        check(len(r["file"]["moved"]) == 3 and r["file"]["crc_changed"] and r["file"]["meta_changed"] >= 1,
              "moved list holds exactly the 3 words: the CRC word and the meta change are NOT in it")
        check(r["verdict"] == "PASS" and "slot map proven" in r["branch"][0], "verdict PASS, branch 1")
        po2, pn2, idx2 = make_fixture(d, mode="f32")
        o2, _ = load_settings(po2)
        n2, _ = load_settings(pn2)
        r2 = analyse(T, o2, n2)
        check(all(r2["file"]["found"][k][0][1] == "f32" for k in T), "the same values stored as float32 are found as f32")
        po3, pn3, _ = make_fixture(d, crc_only=True)
        r3 = analyse(T, load_settings(po3)[0], load_settings(pn3)[0])
        check(r3["file"]["moved"] == [] and r3["verdict"] == "FAIL" and "nothing moved anywhere" in r3["branch"][0],
              "only the meta/CRC changed: nothing moved, branch 4, FAIL")
        # coincidence guard: a moved word that equals 41 by chance counts once per word; an unmoved 37 never counts
        b2 = bytearray(n)
        struct.pack_into("<I", b2, POFF + CRC_FROM + 4 * 900, 37)
        old2 = bytearray(o)
        struct.pack_into("<I", old2, POFF + CRC_FROM + 4 * 900, 37)  # unchanged 37 elsewhere
        rr = analyse(T, bytes(old2), bytes(b2))
        check(len(rr["file"]["found"]["sprint_user"]) == 1, "an UNCHANGED word that holds 37 is not a hit")
        gvp = os.path.join(d, "gv.txt")
        open(gvp, "w").write("GAMEPLAY_CUSTOMIZATION/SPRINT_SPEED_USER = 37\nGAMEPLAY_CUSTOMIZATION/ACCELERATION_USER = 50\n")
        g = analyse(T, gv=read_gv(gvp))
        check(g["gv"]["sprint_user"]["moved"] and not g["gv"]["accel_user"]["moved"] and g["verdict"] == "PASS", "gv text lines: sprint moved, accel not")
        dump = {"groups": {"gp_customization_customization": {"fields": {"SprintSpeedUser": {"value": 37.0}, "x": {"value": 1.0}}},
                           "gp_other": {"fields": {"y": {"value": 63}, "flag": {"value": True}}}}}
        dd = analyse(T, dump=dump)
        check(len(dd["dump"]["customization"]) == 1 and len(dd["dump"]["other"]) == 1 and dd["branch"][0].startswith("attribdb holds"),
              "attribdb dump: customization hit called out, unrelated hit counted separately, a bool never matches")
        m1, m2 = os.path.join(d, "m1"), os.path.join(d, "m2")
        open(m1, "w").write("a" * 32 + "  CmMgrC1\n" + "c" * 32 + " *Settings1\n")
        open(m2, "w").write("a" * 32 + "  CmMgrC1\n" + "c" * 32 + " *Settings1\n" + "d" * 32 + "  Settings20261015101516976\n")
        mk = md5_check(md5_lists(m1), md5_lists(m2))
        check(mk["cmmgr_changed"] == [] and mk["new_settings"] == ["Settings20261015101516976"], "md5: CmMgrC unchanged, the new Settings<ts> listed")
        open(m2, "w").write("e" * 32 + "  CmMgrC1\n")
        rm = analyse(T, o, n, md5=md5_check(md5_lists(m1), md5_lists(m2)))
        check(rm["verdict"] == "FAIL" and any("CmMgrC" in x for x in rm["notes"]), "CmMgrC changed: FAIL regardless of the other routes")
        open(os.path.join(d, "short"), "wb").write(b"x" * 100)
        try:
            load_settings(os.path.join(d, "short"))
            check(False, "a wrong-size file is refused")
        except Bad:
            check(True, "a wrong-size file is refused")
        check(analyse(T)["verdict"] == "INCONCLUSIVE", "no inputs at all: INCONCLUSIVE")
        txt = render(r, ook, nok)
        check("word 120" in txt and "VERDICT PASS" in txt, "the report names the moved words")
    rc = main(["--mock"])
    check(rc == 0, "--mock end to end (exit %d)" % rc)
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
