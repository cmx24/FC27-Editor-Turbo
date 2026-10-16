"""attrib_diff.py - OFFLINE, read-only. Compare a live attribulator dump (attrib_dump.py JSON) with a local value map (default
TO\\assessment_20261014\\MOD_MAP.json, kept outside git) and write TO\\attrib_live_vs_mod.md.

  python attrib_diff.py --dump DUMP.json [--dump2 LATER.json] [--map MAP.json] [--out FILE.md] [--json FILE.json] [--top N]
  python attrib_diff.py --self-test

Per map field (base copy; simulation-copy rows are listed as "not dumped") exactly one state:
  missing-group      the group is absent from the dump (path does not exist in the live databases)
  identity-fail      the group was found but its instance Name / vtable check failed (values not trusted)
  missing-field      the group is there, the field is not in the dump
  live=vanilla       scalar: live value equals the FC 27 default of the map (tolerance 1e-5 relative)
  live=mod           scalar: live value equals the map's value (the map's value is already in the game)
  live=other         scalar: neither; ratio live / FC 27 default is reported
  pointer-only       array / curve / enum / other pointer: the dump holds the raw pointer only - NOT compared, NOT missing
  not-dumped         the map row is for the simulation copy (the dump reads the base copy only)
--dump2 adds a second dump (e.g. at kick-off vs in the hub): scalars whose value changed between the two are listed (which DB is current).
No SaveToFile output format is assumed: only the attrib_dump.py JSON is read.
"""
import argparse, json, math, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402

DEFAULT_MAP = os.path.join(harness.TO, "assessment_20261014", "MOD_MAP.json")
STATES = ["live=vanilla", "live=mod", "live=other", "pointer-only", "not-dumped", "missing-field", "missing-group", "identity-fail"]


def close(a, b, rel=1e-5):
    try:
        a, b = float(a), float(b)
    except (TypeError, ValueError):
        return False
    return math.isclose(a, b, rel_tol=rel, abs_tol=1e-9)


def lg(x):
    """sort key: |log ratio|; ratios that are None / <= 0 sort last"""
    return -abs(math.log(x)) if isinstance(x, (int, float)) and x > 0 else 0


def classify(row, dump):
    """-> (state, live, ratio)"""
    if row.get("sim"):
        return "not-dumped", None, None
    g = dump["groups"].get(row["group"])
    if g is None:
        return "missing-group", None, None
    if not g.get("name_ok"):
        return "identity-fail", None, None
    f = g["fields"].get(row["field"])
    if f is None:
        return "missing-field", None, None
    if "value" not in f or row.get("kind") != "scalar":
        return "pointer-only", None, None
    live, van, mod = f["value"], row.get("fc27_default"), row.get("mod_value")
    ratio = None if van in (None, 0) else float(live) / float(van)
    if van is not None and close(live, van):
        return "live=vanilla", live, ratio
    if mod is not None and close(live, mod):
        return "live=mod", live, ratio
    return "live=other", live, ratio


def compare(dump, mp):
    out = []
    for row in mp["fields"]:
        st, live, ratio = classify(row, dump)
        out.append({"group": row["group"], "field": row["field"], "kind": row.get("kind"), "state": st, "live": live,
                    "fc27": row.get("fc27_default"), "mod": row.get("mod_value"), "ratio_live_vs_fc27": ratio,
                    "ratio_mod_vs_fc27": row.get("ratio_vs_fc27")})
    return out


def changed_between(d1, d2):
    ch = []
    for gn, g in d1["groups"].items():
        g2 = d2["groups"].get(gn)
        if not g2:
            continue
        for fn, f in g["fields"].items():
            f2 = g2["fields"].get(fn)
            if f2 is not None and "value" in f and "value" in f2 and not close(f["value"], f2["value"]):
                ch.append({"group": gn, "field": fn, "first": f["value"], "second": f2["value"]})
    return ch


def db_identity(d1, d2):
    """-> dict: current-DB address / name of both dumps, same object?, how many instance addresses moved"""
    def cur(d):
        return (d.get("mgr") or {}).get("current_db") or {}
    c1, c2 = cur(d1), cur(d2)
    moved = [gn for gn, g in d1["groups"].items() if gn in d2["groups"] and g.get("instance") != d2["groups"][gn].get("instance")]
    known = bool(c1.get("addr") and c2.get("addr"))
    return {"first": c1, "second": c2, "known": known, "same_db": (c1.get("addr") == c2.get("addr")) if known else None,
            "instances_moved": len(moved), "groups": len(d1["groups"]), "moved_names": moved[:10]}


def render(rows, dump, mp, ch=None, top=40, ident=None):
    cnt = {s: 0 for s in STATES}
    for r in rows:
        cnt[r["state"]] += 1
    L = ["# live attribulator dump vs the value map", "",
         "Dump: %s (%s, %s groups: %s OK / %s identity FAIL)." % (dump.get("started", "?"), "MOCK" if dump.get("mock") else "live",
                                                              len(dump["groups"]), dump.get("ok"), dump.get("bad")),
         "Map rows: %d. Only SCALARS are compared; arrays and curves are 'pointer-only' (the dump holds the raw pointer, no list layout is assumed)." % len(rows), "",
         "| state | rows |", "|---|---|"] + ["| %s | %d |" % (s, cnt[s]) for s in STATES] + [""]
    sc = [r for r in rows if r["state"] in ("live=vanilla", "live=mod", "live=other")]
    L.append("Scalars compared: %d. Reading: `live=vanilla` everywhere = the live tables are FC 27's own; `live=mod` = the map's values are already in the "
             "game; `live=other` = something else wrote them (list below)." % len(sc))
    other = sorted([r for r in rows if r["state"] == "live=other"], key=lambda r: lg(r["ratio_live_vs_fc27"]))
    if other:
        L += ["", "## live=other (largest ratio first, top %d of %d)" % (min(top, len(other)), len(other)), "",
              "| group | field | FC 27 | live | mod | live/FC27 |", "|---|---|---|---|---|---|"]
        L += ["| %s | %s | %s | %s | %s | %s |" % (r["group"], r["field"], r["fc27"], r["live"], r["mod"],
                                                   "%.3f" % r["ratio_live_vs_fc27"] if r["ratio_live_vs_fc27"] is not None else "-") for r in other[:top]]
    for st, title in (("missing-group", "path missing: group"), ("missing-field", "path missing: field"), ("identity-fail", "identity FAIL")):
        sel = [r for r in rows if r["state"] == st]
        if sel:
            L += ["", "## %s (%d)" % (title, len(sel)), ""] + ["- %s . %s" % (r["group"], r["field"]) for r in sel[:top]]
            if len(sel) > top:
                L.append("- ... %d more" % (len(sel) - top))
    mods = sorted([r for r in sc if r["state"] == "live=vanilla" and r["ratio_mod_vs_fc27"] not in (None, 1)],
                  key=lambda r: lg(r["ratio_mod_vs_fc27"]))
    L += ["", "## scalars still at the FC 27 value that the map would move (top %d of %d)" % (min(top, len(mods)), len(mods)), "",
          "| group | field | FC 27 (= live) | map value | map/FC27 |", "|---|---|---|---|---|"]
    L += ["| %s | %s | %s | %s | %s |" % (r["group"], r["field"], r["fc27"], r["mod"], r["ratio_mod_vs_fc27"]) for r in mods[:top]]
    if ident is not None:
        L += ["", "## which database is current (dump vs dump2)", ""]
        if not ident["known"]:
            L.append("- the current-DB pointer is missing from a dump (older dump format): cannot say whether the DB object changed")
        else:
            L.append("- current DB: %s (%s) -> %s (%s): **%s**" % (ident["first"].get("addr"), ident["first"].get("name"), ident["second"].get("addr"),
                                                                 ident["second"].get("name"), "same object" if ident["same_db"] else "A DIFFERENT OBJECT"))
        L.append("- group instance addresses that moved: %d of %d%s" % (ident["instances_moved"], ident["groups"],
                                                                         " (e.g. " + ", ".join(ident["moved_names"][:3]) + ")" if ident["moved_names"] else ""))
        L.append("- equal scalar values do NOT mean the same DB: a copy holds the same numbers at other addresses")
    if ch is not None:
        L += ["", "## scalars that differ between the two dumps (%d)" % len(ch), ""]
        L += ["- %s . %s : %s -> %s" % (c["group"], c["field"], c["first"], c["second"]) for c in ch[:top]]
        if not ch:
            L.append("- none: both dumps read the same values")
    return "\n".join(L) + "\n", cnt


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump")
    ap.add_argument("--dump2")
    ap.add_argument("--map", default=DEFAULT_MAP)
    ap.add_argument("--out", default=os.path.join(harness.TO, "attrib_live_vs_mod.md"))
    ap.add_argument("--json", default=None)
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.dump:
        ap.error("--dump is required")
    dump = json.load(open(a.dump, encoding="utf-8"))
    mp = json.load(open(a.map, encoding="utf-8"))
    rows = compare(dump, mp)
    ch = changed_between(dump, json.load(open(a.dump2, encoding="utf-8"))) if a.dump2 else None
    d2 = json.load(open(a.dump2, encoding="utf-8")) if a.dump2 else None
    ident = db_identity(dump, d2) if d2 else None
    txt, cnt = render(rows, dump, mp, ch, a.top, ident)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    open(a.out, "w", encoding="utf-8").write(txt)
    if a.json:
        json.dump({"counts": cnt, "rows": rows, "changed_between_dumps": ch}, open(a.json, "w"), indent=1)
    print("wrote %s: %s" % (a.out, ", ".join("%s %d" % kv for kv in cnt.items() if kv[1])))
    return 0


def self_test():
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("  ok    " if cond else "  FAIL  ") + what)
        ok = ok and cond

    def grp(fields, name_ok=True):
        return {"name_ok": name_ok, "fields": fields}
    dump = {"ok": 3, "bad": 1, "groups": {
        "g_van": grp({"a": {"kind": "f32", "value": 0.5}, "b": {"kind": "f32", "value": 2.0}, "c": {"kind": "f32", "value": 3.5},
                      "cur": {"kind": "curve", "ptr": "0x1000"}, "en": {"kind": "enum", "raw": 1}}),
        "g_bad": grp({"a": {"kind": "f32", "value": 1.0}}, name_ok=False)}}
    mp = {"fields": [
        {"group": "g_van", "field": "a", "kind": "scalar", "sim": False, "fc27_default": 0.5, "mod_value": 1.0, "ratio_vs_fc27": 2.0},
        {"group": "g_van", "field": "b", "kind": "scalar", "sim": False, "fc27_default": 4.0, "mod_value": 2.0, "ratio_vs_fc27": 0.5},
        {"group": "g_van", "field": "c", "kind": "scalar", "sim": False, "fc27_default": 1.0, "mod_value": 9.0, "ratio_vs_fc27": 9.0},
        {"group": "g_van", "field": "cur", "kind": "curve", "sim": False, "fc27_values": [1, 2], "mod_values": [2, 4]},
        {"group": "g_van", "field": "en", "kind": "scalar", "sim": False, "fc27_default": 1, "mod_value": 2, "ratio_vs_fc27": 2},
        {"group": "g_van", "field": "nope", "kind": "scalar", "sim": False, "fc27_default": 1, "mod_value": 2, "ratio_vs_fc27": 2},
        {"group": "g_gone", "field": "a", "kind": "scalar", "sim": False, "fc27_default": 1, "mod_value": 2, "ratio_vs_fc27": 2},
        {"group": "g_bad", "field": "a", "kind": "scalar", "sim": False, "fc27_default": 1.0, "mod_value": 2, "ratio_vs_fc27": 2},
        {"group": "g_van", "field": "a", "kind": "scalar", "sim": True, "fc27_default": 0.5, "mod_value": 1.0, "ratio_vs_fc27": 2.0}]}
    rows = compare(dump, mp)
    st = [r["state"] for r in rows]
    check(st == ["live=vanilla", "live=mod", "live=other", "pointer-only", "pointer-only", "missing-field", "missing-group", "identity-fail", "not-dumped"],
          "nine rows, nine states (got %s)" % st)
    check(abs(rows[2]["ratio_live_vs_fc27"] - 3.5) < 1e-9, "live=other carries the ratio live / FC 27 (3.5)")
    check(rows[3]["state"] == "pointer-only" and rows[3]["state"] != "missing-field", "a curve is 'pointer only', never 'missing'")
    txt, cnt = render(rows, dump, mp, None)
    check(cnt["pointer-only"] == 2 and "## path missing: group (1)" in txt and "## live=other" in txt, "report: counts and sections")
    dumpa = dict(dump, mgr={"current_db": {"addr": "0x1000", "name": "gameplayattribdb"}})
    for g in dumpa["groups"].values():
        g["instance"] = "0x2000"
    d2 = json.loads(json.dumps(dumpa))
    d2["groups"]["g_van"]["fields"]["a"]["value"] = 0.75
    ch = changed_between(dump, d2)
    check(len(ch) == 1 and ch[0]["field"] == "a" and ch[0]["second"] == 0.75, "dump vs dump2: only the changed scalar is listed")
    check(db_identity(dumpa, d2)["same_db"] is True and db_identity(dumpa, d2)["instances_moved"] == 0, "same DB object, no moved instance: reported as the same")
    d3 = json.loads(json.dumps(dumpa))
    d3["mgr"]["current_db"]["addr"] = "0x5000"
    for g in d3["groups"].values():
        g["instance"] = "0x6000"
    idn = db_identity(dumpa, d3)
    t3, _ = render(rows, dumpa, mp, changed_between(dumpa, d3), 40, idn)
    check(idn["same_db"] is False and idn["instances_moved"] == 2 and "A DIFFERENT OBJECT" in t3 and "none: both dumps read the same values" in t3,
          "a swapped DB holding identical values: scalars say 'none', the DB section says DIFFERENT OBJECT")
    check(db_identity(dump, d3)["known"] is False, "an old dump without the pointer: 'cannot say', never 'same'")
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        dp, mpth, out = os.path.join(d, "d.json"), os.path.join(d, "m.json"), os.path.join(d, "o.md")
        json.dump(dump, open(dp, "w"))
        json.dump(mp, open(mpth, "w"))
        rc = main(["--dump", dp, "--map", mpth, "--out", out])
        check(rc == 0 and os.path.exists(out) and "pointer-only" in open(out).read(), "CLI writes the report")
    print("SELF-TEST " + ("PASSED" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
