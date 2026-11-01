#!/usr/bin/env python3
"""Synthetic tests of fc27_commentary's pack discovery and per-set output (asset catalog WP-03). No game files: every
toc, layout and SBle here is made up in memory or in a temporary folder. Runs in well under 10 s.

usage: python -I turbo/tools/fc27_commentary/test_fc27_commentary.py
"""
import os
import struct
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fc27_commentary as fc  # noqa: E402

FAILS = []


def check(name, cond):
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAILS.append(name)


# ------------------------------------------------------------------------------------------- DbObject writer
def _varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _head(t, name):
    return bytes([t | (0x80 if name is None else 0)]) + (b"" if name is None else name.encode() + b"\0")


def dbo(name, v):
    if isinstance(v, dict):
        body = b"".join(dbo(k, x) for k, x in v.items()) + b"\0"
        return _head(2, name) + _varint(len(body)) + body
    if isinstance(v, list):
        body = b"".join(dbo(None, x) for x in v) + b"\0"
        return _head(1, name) + _varint(len(body)) + body
    if isinstance(v, bool):
        return _head(6, name) + bytes([v])
    if isinstance(v, int):
        return _head(8, name) + struct.pack("<i", v)
    if isinstance(v, str):
        b = v.encode() + b"\0"
        return _head(7, name) + _varint(len(b)) + b
    raise TypeError(v)


def layout_bytes(chunks):
    return dbo(None, {"superBundles": [{"name": "Win32/x"}], "installManifest": {
        "platform": "Win32", "installChunks": [{"id": "x", "name": n, "persistentIndex": pi, "alwaysInstalled": True}
                                               for n, pi in chunks]}})


# ------------------------------------------------------------------------------------------- SBle writer
def sble(selector="surname_ID", ids=(900002, 900762), prefix=(0, 0, 2, 2, 3), index_data=True):
    """fc._synthetic_sble with the selector, its two values and the prefix sum chosen: keys run over cm_sim (0, 1) x
    the selector's two values; rows of key k are [prefix[k], prefix[k+1]). index_data=False: an index without data
    over two parameters (FC 27 eng_us gm HIGH: 'index without data is not an identity')."""
    d = bytearray(0x400)
    d[0:4] = b"SBle"
    struct.pack_into("<H", d, 8, 1)
    struct.pack_into("<I", d, 0x18, 0x50)
    struct.pack_into("<II", d, 0x50, 0x70, 0)
    o = 0x70
    d[o:o + 4] = b"TESD"
    struct.pack_into("<III", d, o + 4, 0x100, fc.djb2x("Selection"), 0)
    struct.pack_into("<I", d, o + 0x38, 3)
    struct.pack_into("<HHHHI", d, o + 0x3C, 2, 1, 0x48, 0x78, 0x98)
    struct.pack_into("<5I", d, o + 0x48, fc.H_SEGMENT, 0x02000202, 0, 0, 0x200)
    struct.pack_into("<5I", d, o + 0x60, fc.djb2x("VariationId"), 0x04000202, 1000, 0, 0x210)
    struct.pack_into("<I", d, o + 0x78, 0x220 if index_data else 0)
    struct.pack_into("<I", d, o + 0x78 + 0x1C, 0x02000000)
    struct.pack_into("<5I", d, o + 0x98, fc.djb2x("cm_sim"), 2, 0, 1, 0)
    struct.pack_into("<5I", d, o + 0xB0, fc.djb2x(selector), 0x04000002, ids[0], ids[1], 0x230)
    struct.pack_into("<3H", d, 0x200, 931, 1419, 7)
    struct.pack_into("<3I", d, 0x210, 5, 6, 9)
    struct.pack_into("<5H", d, 0x220, *prefix)
    struct.pack_into("<2I", d, 0x230, 0, ids[1] - ids[0])
    return bytes(d)


class FakeBank(fc.Bank):
    """A Bank over in-memory family resources {asset name: SBle bytes} (no toc, no cas, no Oodle)."""

    def __init__(self, lang, res, team=None):
        self.game, self.lang, self.res = "", lang, res
        self.assets = {n: {"res": (None, None, {"size": len(b)})} for n, b in res.items()}
        self._index_sets(team)

    def load_res(self, name):
        return {"name": name, "res": self.res[name], "sets": fc.parse_sble(self.res[name])}


def main():
    t0 = time.time()
    print("layout.toc (DbObject):")
    raw = layout_bytes([("superbundlelayout/commentaryfull_eng_us", 0x1234), ("superbundlelayout/installpackage_licensed",
                                                                              -2026045265)])
    _n, v, end = fc.read_dbobject(raw, 0)
    check("DbObject: object, list, int, string, bool read back",
          end == len(raw) and v["installManifest"]["installChunks"][1]["persistentIndex"] == -2026045265
          and v["installManifest"]["installChunks"][0]["alwaysInstalled"] is True
          and v["superBundles"] == [{"name": "Win32/x"}])
    lay = fc.Layout("G", datas={"Data": raw})
    check("Layout: persistentIndex as 8 hex digits (negative ones too)",
          lay.chunks.get("00001234") == "superbundlelayout/commentaryfull_eng_us"
          and lay.chunks.get("873d00af") == "superbundlelayout/installpackage_licensed")
    check("Layout: cas id layer 0 -> Data\\Win32\\<chunk>\\cas_NN.cas",
          lay.cas_path("0000000012340002") == os.path.join("G", "Data", "Win32", "superbundlelayout",
                                                           "commentaryfull_eng_us", "cas_02.cas"))
    check("Layout: cas id layer 1 -> Patch", lay.cas_path("0001873d00af0001") == os.path.join(
        "G", "Patch", "Win32", "superbundlelayout", "installpackage_licensed", "cas_01.cas"))
    check("Layout: unknown install chunk -> None", lay.cas_path("00000000beef0001") is None)
    signed = fc.TOC_MAGIC + b"\0" * (fc.TOC_DATA - 4) + raw
    check("Layout: a signed layout.toc (data at 0x22C) reads the same",
          fc.Layout("G", datas={"Patch": signed}).chunks == lay.chunks)

    print("pack discovery and cas files:")
    with tempfile.TemporaryDirectory() as g:
        def touch(*parts):
            p = os.path.join(g, *parts)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            open(p, "wb").close()
            return p
        def put(*parts, size=1):
            p = os.path.join(g, *parts)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(b"x" * size)
            return p

        def live_pack(lang, root="Data", full=5000, launch=900, cas=1, kinds=("full", "launch")):
            """A live pack in <root>\\Win32: tocs, superbundlelayout\\commentaryfull_<lang>\\cas_NN.cas."""
            out = {}
            if "full" in kinds:
                out["full"] = put(root, "Win32", "commentaryfull_%s.toc" % lang, size=full)
                for i in range(1, cas + 1):
                    put(root, "Win32", "superbundlelayout", "commentaryfull_%s" % lang, "cas_%02d.cas" % i, size=64)
            if "launch" in kinds:
                out["launch"] = put(root, "Win32", "commentarylaunch_%s.toc" % lang, size=launch)
            return out

        def backup_pack(lang, size=5000, kinds=("full", "launch"), cas=True):
            out = {}
            for k in kinds:
                out[k] = put("commentary", "commentary%s_%s.toc" % (k, lang), size=size)
                if cas:
                    put("commentary", "commentary%s_%s" % (k, lang), "cas_01.cas", size=64)
            return out

        # live only: installed, bytes from the live copy
        live_pack("eng_us")
        put("Patch", "Win32", "commentarylicensed_eng_us.toc", size=300)
        eng = fc.pack_data_source(g, "eng_us")
        check("pack_data_source: live only -> installed, source live, tocs Data full + launch + Patch licensed",
              eng["installed"] and eng["source"] == "live" and not eng["backup_only"] and eng["tocs"] == [
                  os.path.join(g, "Data", "Win32", "commentaryfull_eng_us.toc"),
                  os.path.join(g, "Data", "Win32", "commentarylaunch_eng_us.toc"),
                  os.path.join(g, "Patch", "Win32", "commentarylicensed_eng_us.toc")])
        patch_launch = put("Patch", "Win32", "commentarylaunch_eng_us.toc", size=3)
        check("find_tocs: Patch over Data for the live copy", fc.find_tocs(g, "eng_us")[1] == patch_launch)
        os.remove(patch_launch)
        # backup only: NOT installed, bytes unused, never listed
        backup_pack("fre_fr")
        fre = fc.pack_data_source(g, "fre_fr")
        check("pack_data_source: backup only -> NOT installed, no tocs, backup_only",
              not fre["installed"] and fre["backup_only"] and fre["tocs"] == [] and fre["source"] is None)
        check("find_tocs: backup only gives nothing", fc.find_tocs(g, "fre_fr") == [])
        check("installed_languages: a backup-only language is not listed", fc.installed_languages(g) == ["eng_us"])
        # both: installed, bytes from the backup
        live_pack("ita_it")
        bk = backup_pack("ita_it")
        ita = fc.pack_data_source(g, "ita_it")
        check("pack_data_source: both -> installed, source backup, tocs the backup's",
              ita["installed"] and ita["source"] == "backup" and ita["kinds"] == {"commentaryfull": "backup",
                                                                                  "commentarylaunch": "backup"}
              and ita["tocs"] == [bk["full"], bk["launch"]])
        check("find_tocs: both -> the backup tocs; their cas = the folder named like the toc (resolver)",
              fc.find_tocs(g, "ita_it") == [bk["full"], bk["launch"]] and fc.make_resolver(None)(
                  bk["full"], "0000000000010001") == os.path.join(g, "commentary", "commentaryfull_ita_it", "cas_01.cas"))
        # both, but the backup has no cas folder for one kind -> that kind falls back to live (mixed source, named)
        live_pack("spa_es")
        backup_pack("spa_es", kinds=("full",))
        put("commentary", "commentarylaunch_spa_es.toc", size=900)        # toc but no cas folder: unreadable
        spa = fc.pack_data_source(g, "spa_es")
        check("pack_data_source: a backup toc without its cas folder is skipped (live for that kind), source backup+live",
              spa["source"] == "backup+live" and spa["kinds"]["commentarylaunch"] == "live"
              and spa["kinds"]["commentaryfull"] == "backup")
        # neither
        check("pack_data_source: neither -> not installed, not backup_only, no tocs",
              fc.pack_data_source(g, "ger_de") == {"lang": "ger_de", "installed": False, "backup_only": False,
                                                   "source": None, "kinds": {}, "tocs": [], "notes": []})
        check("find_tocs: neither gives nothing; a bad language code gives nothing", fc.find_tocs(g, "ger_de") == []
              and fc.find_tocs(g, "../x") == [] and not fc.is_installed(g, ""))
        # the installed test is the apply script's step (a)
        live_pack("dut_nl", full=262391, launch=923, cas=2)
        put("Data", "Win32", "superbundlelayout", "commentarylaunch_dut_nl", "cas_01.cas", size=4096)
        check("installed: Dutch (launch toc 923 B) is installed, no size floor on the launch side", fc.is_installed(g, "dut_nl"))
        live_pack("por_br", full=4096)
        check("installed: a full toc of exactly 4096 B (EA-app stub) is NOT installed", not fc.is_installed(g, "por_br"))
        live_pack("pol_pl", cas=0)
        check("installed: no cas under superbundlelayout\\commentaryfull_<lang> -> NOT installed",
              not fc.is_installed(g, "pol_pl"))
        put("Data", "Win32", "superbundlelayout", "commentaryfull_pol_pl", "cas_01.cas", size=0)
        check("installed: only an empty cas -> NOT installed", not fc.is_installed(g, "pol_pl"))
        live_pack("swe_se", kinds=("full",))
        check("installed: no launch toc -> NOT installed", not fc.is_installed(g, "swe_se"))
        live_pack("tur_tr", root="Patch")
        check("installed: a pack live in Patch\\Win32 counts", fc.is_installed(g, "tur_tr"))
        check("installed_languages: only the live ones, sorted; no stub, no backup-only",
              fc.installed_languages(g) == ["dut_nl", "eng_us", "ita_it", "spa_es", "tur_tr"])
        # Bank refuses a language that is not installed (before reading anything), naming the backup when it holds it
        try:
            fc.Bank(g, "fre_fr")
            check("Bank: a backup-only language is refused", False)
        except fc.FormatError as ex:
            check("Bank: a backup-only language is refused, the error says a backup never makes it installed",
                  "not installed" in str(ex) and "backup never makes" in str(ex))
        # the language list does not change with the source; without the backup the live tocs are read
        pre = fc.installed_languages(g)
        os.remove(bk["full"])
        os.remove(bk["launch"])
        check("installed_languages: the same with or without the backup copy", fc.installed_languages(g) == pre)
        check("find_tocs: backup gone -> the live tocs", [os.path.basename(p) for p in fc.find_tocs(g, "ita_it")]
              == ["commentaryfull_ita_it.toc", "commentarylaunch_ita_it.toc"])
        data_lic = os.path.join(g, "Patch", "Win32", "commentarylicensed_eng_us.toc")
        data_it = os.path.join(g, "Data", "Win32", "commentaryfull_ita_it.toc")
        dl_launch = os.path.join(g, "commentary", "commentarylaunch_eng_us.toc")
        lay = fc.Layout(g, datas={"Data": layout_bytes([("superbundlelayout/installpackage_licensed", 7)])})
        res = fc.make_resolver(lay)
        lic_cas = touch("Data", "Win32", "superbundlelayout", "installpackage_licensed", "cas_01.cas")
        check("resolver: a Win32 toc's cas through layout.toc (another package's folder)",
              res(data_lic, "0000000000070001") == lic_cas)
        check("resolver: not in layout.toc -> Win32\\superbundlelayout\\<toc stem>\\cas_NN.cas",
              res(data_it, "00000000aaaa0002") == os.path.join(g, "Data", "Win32", "superbundlelayout",
                                                               "commentaryfull_ita_it", "cas_02.cas"))
        check("resolver: layout names a missing file -> the toc-stem folder",
              res(data_it, "0000000000070003") == os.path.join(g, "Data", "Win32", "superbundlelayout",
                                                               "commentaryfull_ita_it", "cas_03.cas"))
        check("resolver: a downloaded pack keeps the folder named like its toc (as before)",
              res(dl_launch, "0000000000070001") == os.path.join(g, "commentary", "commentarylaunch_eng_us",
                                                                 "cas_01.cas"))

    print("set names:")
    check("loccommentary <lang>_full", fc.parse_set_name(
        "sound/speech/loccommentary/ita_it/soundwaves/ita_it_full/psimple_surname", "ita_it") ==
        ("ita_it_full", "", "full", "psimple_surname"))
    check("loccommentary <lang>_demo", fc.parse_set_name(
        "sound/speech/loccommentary/ita_it/soundwaves/ita_it_demo/pplayer_names_high", "ita_it") ==
        ("ita_it_demo", "", "demo", "pplayer_names_high"))
    check("commentary <lang>_<team>_<kind>", fc.parse_set_name(
        "sound/speech/commentary/eng_us/soundwaves/eng_us_gm_licensed/pplayer_names_link", "eng_us") ==
        ("eng_us_gm_licensed", "gm", "licensed", "pplayer_names_link"))
    check("another language, another kind or a deeper path -> None", all(fc.parse_set_name(n, "eng_us") is None for n in (
        "sound/speech/loccommentary/ita_it/soundwaves/ita_it_full/psimple_surname",
        "sound/speech/commentary/eng_us/soundwaves/eng_us_dr_preview/psimple_surname",
        "sound/speech/commentary/eng_us/soundwaves/eng_us_dr_full/psimple_surname/extra",
        "sound/speech/commentary/eng_us/soundwaves/fre_fr_dr_full/psimple_surname")))

    print("sets, teams, union:")
    pre = "sound/speech/commentary/eng_us/soundwaves/"
    banks = {
        pre + "eng_us_dr_full/psimple_surname": sble("surname_ID", (900002, 900762), (0, 1, 2, 2, 3)),
        pre + "eng_us_dr_full/pplayer_names_simple": sble("player_db_pID", (51, 239598), (0, 1, 2, 2, 3)),
        pre + "eng_us_dr_full/pplayer_names_link": sble("player_db_pID", (51, 300), (0, 0, 2, 2, 3)),
        pre + "eng_us_dr_full/pplayer_names_high": sble("player_db_pID", (51, 52), (0, 1, 2, 2, 3)),
        pre + "eng_us_dr_demo/pplayer_names_simple": sble("player_db_pID", (51, 777), (0, 0, 2, 2, 3)),
        pre + "eng_us_gm_full/pplayer_names_simple": sble("player_db_pID", (51, 888), (0, 1, 2, 2, 3)),
        pre + "eng_us_gm_full/pplayer_names_high": sble("player_db_pID", (51, 888), index_data=False),
        pre + "eng_us_gm_full/pmatch_intro": sble("player_db_pID", (1, 2)),   # not a name family: ignored
        pre + "eng_us_gmsr_full/pmatch_intro": sble("player_db_pID", (1, 2)),  # a set without name families
    }
    bank = FakeBank("eng_us", banks)
    check("sets indexed by name", sorted(bank.sets) == ["eng_us_dr_demo", "eng_us_dr_full", "eng_us_gm_full",
                                                        "eng_us_gmsr_full"]
          and bank.sets["eng_us_gm_full"]["team"] == "gm" and bank.sets["eng_us_dr_demo"]["kind"] == "demo")
    check("primary set: <lang>_<dr>_full by default", bank.primary == "eng_us_dr_full")
    check("families() / family_name() of the primary set",
          bank.families() == ["pplayer_names_high", "pplayer_names_link", "pplayer_names_simple", "psimple_surname"]
          and bank.family_name("psimple_surname") == pre + "eng_us_dr_full/psimple_surname")
    check("primary set: --team gm", FakeBank("eng_us", banks, team="gm").primary == "eng_us_gm_full")
    try:
        FakeBank("eng_us", banks, team="xx")
        check("primary set: an unknown team is refused", False)
    except fc.FormatError:
        check("primary set: an unknown team is refused", True)
    log = []
    sets, teams, union = fc.collect_sets(bank, log.append)
    dr = sets["eng_us_dr_full"]["ids"]
    check("dr_full ids per family", dr["generic"] == [900002, 900762] and dr["simple"] == [51, 239598]
          and dr["link"] == [300] and dr["high"] == [51, 52] and dr["generic_high"] == [])
    gm = sets["eng_us_gm_full"]
    check("an unreadable HIGH is recorded in its set's errors, the run goes on",
          "not an identity" in gm["errors"].get("pplayer_names_high", "") and gm["ids"]["simple"] == [51, 888]
          and gm["ids"]["high"] == [] and any("errors" in x for x in log))
    check("teams: the union of each team's sets", teams["dr"]["simple"] == [51, 777, 239598]
          and teams["gm"]["simple"] == [51, 888] and teams["dr"]["generic"] == [900002, 900762]
          and teams["gm"]["generic"] == [])
    check("a set without name families is left out of sets and teams", "eng_us_gmsr_full" not in sets
          and sorted(teams) == ["dr", "gm"])
    check("union: every set of every team", union["simple"] == [51, 777, 888, 239598] and union["link"] == [300]
          and union["high"] == [51, 52])
    # An index without data over SEVERAL parameters is an identity when it has one row per key combination (FC 27 spa_es
    # psimple_surname: cm_sim x surname_ID x player_intensity = 2 x 2841 x 1 rows): rows follow the prefix-sum order,
    # last parameter fastest. A count that does not match stays an error (the eng_us gm HIGH fixture above).
    ds = object.__new__(fc.DataSet)
    ds.name, ds._d, ds.rows = fc.djb2x("Selection"), b"", 4
    ds.indexes = [{"ptr": 0, "params": [{"name": "cm_sim", "values": [0, 1]},
                                        {"name": "surname_ID", "values": [900002, 900762]},
                                        {"name": "player_intensity", "values": [2]}]}]
    keys = ds.row_keys()
    check("an index without data over several parameters: one row per key, last parameter fastest",
          [(keys[r]["cm_sim"], keys[r]["surname_ID"], keys[r]["player_intensity"]) for r in range(4)]
          == [(0, 900002, 2), (0, 900762, 2), (1, 900002, 2), (1, 900762, 2)])
    ds.rows = 5
    try:
        ds.row_keys()
        check("an index without data whose key count is not the row count is refused", False)
    except fc.FormatError as ex:
        check("an index without data whose key count is not the row count is refused", "not an identity" in str(ex))
    ita = FakeBank("ita_it", {
        "sound/speech/loccommentary/ita_it/soundwaves/ita_it_full/pplayer_names_simple": sble("player_db_pID", (5, 6)),
        "sound/speech/loccommentary/ita_it/soundwaves/ita_it_demo/pplayer_names_simple": sble("player_db_pID", (6, 9)),
    }, team="gm")
    s2, t2, _u2 = fc.collect_sets(ita, lambda *_: None)
    check("a language without teams: primary <lang>_full whatever --team says, team \"\"",
          ita.primary == "ita_it_full" and list(t2) == [""] and t2[""]["simple"] == [6, 9]
          and s2["ita_it_full"]["ids"]["simple"] == [6])
    try:
        FakeBank("eng_us", {pre + "eng_us_dr_demo/pplayer_names_simple": sble("player_db_pID", (5, 6))}, team="gm")
        check("no full set for the team asked -> refused", False)
    except fc.FormatError:
        check("no full set for the team asked -> refused", True)
    only_gm = FakeBank("eng_us", {pre + "eng_us_gm_full/pplayer_names_simple": sble("player_db_pID", (5, 6))})
    check("default team missing -> the first full set", only_gm.primary == "eng_us_gm_full")
    # --set: another set's rows, wavs under encoded segment numbers (callname_coverage.py SET_INDEX)
    check("set_index: full 0, licensed 1, demo 2 (no team / dr); gm +3; unknown None",
          (fc.set_index("", "full"), fc.set_index("", "demo"), fc.set_index("dr", "licensed"), fc.set_index("gm", "demo"),
           fc.set_index("drsr", "full"), fc.set_index("", "wc")) == (0, 2, 1, 5, None, None))
    ita_demo = FakeBank("ita_it", ita.res)
    ita_demo._index_sets(None, "ita_it_demo")
    check("--set ita_it_demo: that set is the primary one", ita_demo.primary == "ita_it_demo"
          and ita_demo.families() == ["pplayer_names_simple"])
    try:
        ita_demo._index_sets(None, "ita_it_licensed")
        check("--set of a set the bank does not have is refused", False)
    except fc.FormatError:
        check("--set of a set the bank does not have is refused", True)

    took = time.time() - t0
    print("test_fc27_commentary: %s (%.2f s)" % ("ok" if not FAILS else "%d failed" % len(FAILS), took))
    return 0 if not FAILS else 1


if __name__ == "__main__":
    sys.exit(main())
