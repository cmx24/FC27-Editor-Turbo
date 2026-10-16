"""ebx_block_kinds.py - OFFLINE, read-only. For every attrib EBX asset in the installed FC 27 toc (Patch then Data),
report the block kind of its stored bytes (Oodle 0x11/0x15/0x19, raw 0x00, other), compressed and raw size.
usage: python -I ebx_block_kinds.py [--regex RX] [--out FILE.md] [--json FILE.json] [--mock] [--selftest]
Reuses toc_bundles (parse_toc/raw/manifest); never opens saves/settings."""
import sys, os, re, json, struct, collections
sys.path.insert(0, r"C:\FC 27 Live Editor\turbo_output\asset_research\scripts")

MANIFEST_MAGIC = int("9D798ED6", 16)  # asset-manifest magic (not an address)
KIND = {0x00: "raw", 0x11: "oodle", 0x15: "oodle", 0x19: "oodle"}


def scan_blocks(data):
    """Walk block headers of one stored asset WITHOUT decompressing. Returns [(ctype, csize, dsize)]."""
    out, p, n = [], 0, len(data)
    while p < n:
        if p + 8 > n:
            raise ValueError("truncated block header at %d" % p)
        dsize = struct.unpack_from(">I", data, p)[0]
        code = struct.unpack_from("<H", data, p + 4)[0]
        csize = struct.unpack_from(">H", data, p + 6)[0] + (((code >> 8) & 0x0F) << 16)
        out.append((code & 0x7F, csize, dsize & 0xFFFFFF))
        p += 8 + csize
    if p != n:
        raise ValueError("block payload overruns asset (%d != %d)" % (p, n))
    return out


def summarize(blocks):
    kinds = sorted({KIND.get(c, "other(%#x)" % c) for c, _, _ in blocks})
    return {"n_blocks": len(blocks), "kind": "+".join(kinds) if kinds else "empty",
            "ctypes": sorted({"%#x" % c for c, _, _ in blocks}),
            "comp": sum(cs for _, cs, _ in blocks), "raw": sum(ds for _, _, ds in blocks)}


def family(name):
    n = name.lower()
    for k in ("prematch", "simulation", "gameplay"):
        if k in n:
            return k
    return "other"


def scan_game(rx):
    import toc_bundles as tb
    rows, seen = [], set()
    for layer in ("Patch", "Data"):
        toc = os.path.join(tb.GAME, layer, "Win32", "fc", "fcgame", "fcgame.toc")
        _h, bundles = tb.parse_toc(toc)
        for bi, locs in enumerate(bundles):
            if not locs:
                continue
            try:
                blob = tb.raw(locs[0])
                if struct.unpack_from("<I", blob, 4)[0] != MANIFEST_MAGIC:
                    blob = tb.read_blocks(blob)
                _m, ebx, _r, _c = tb.manifest(blob)
            except Exception:
                continue
            for i, name in enumerate(ebx):
                if not rx.search(name):
                    continue
                shadowed = name.lower() in seen
                try:
                    s = summarize(scan_blocks(tb.raw(locs[1 + i])))
                except Exception as e:
                    s = {"kind": "error", "err": repr(e)[:120], "n_blocks": 0, "ctypes": [], "comp": 0, "raw": 0}
                s.update(name=name, layer=layer, bundle=bi, family=family(name), shadowed=shadowed)
                rows.append(s)
                seen.add(name.lower())
    return rows


def mock_rows():
    def mk(n, k, c, r, nb=1):
        return dict(name=n, layer="Patch", bundle=1, family=family(n), shadowed=False, kind=k,
                    ctypes=["0x15" if k == "oodle" else "0x0"], n_blocks=nb, comp=c, raw=r)
    return [mk("fifa/attribulator/gameplay/gp_x", "oodle", 100, 400),
            mk("fifa/attribulator/simulation/sim_x", "raw", 300, 300),
            mk("fifa/attribulator/prematch/pm_x", "oodle", 50, 90, 2)]


def verdict(rows):
    live = [r for r in rows if not r["shadowed"]]
    c = collections.Counter(r["kind"] for r in live)
    multi = sum(1 for r in live if r["n_blocks"] > 1)
    return live, c, multi


def render(rows, le_info, le_note=None):
    live, c, multi = verdict(rows)
    bad = [k for k in c if k != "oodle"]
    L = ["# attrib EBX block kinds (offline, read-only)", "",
         "## Verdict", "",
         "- Block kind: %s." % ("EVERY effective attrib EBX asset (%d) is exactly one Oodle block (ctype %s) in the stored bytes; none raw, none "
                               "multi-block" % (len(live), "/".join(sorted({t for r in live for t in r["ctypes"]}))) if live and not bad and not multi
                               else "NOT uniform: %s, multi-block %d (see tables)" % (dict(c), multi)),
         "- Same shape as the asset class the kit hotspot patcher already handles (`kit_swap.cpp` patch_hotspot_ebx accepts exactly one block of ctype "
         "0x11/0x15/0x19; Turbo reads the asset from the TOC itself, decompresses with Oodle, patches, and registers a replacement table item). "
         "STATIC match only.",
         "- NOT proven: that the game's bundle loader consults Turbo's table for attrib assets, and the load timing of the attrib bundles vs the DLL. "
         "Needs the observe-mode line in the first (b) launch.",
         "- NOT known: whether the EBX patch can change an array/curve LENGTH (new bytes carry their own csize/dsize in the table, so probably yes, "
         "but unproven); same-size scalar edits are the safe minimum.",
         "- Live Editor natives: " + (le_note or "see the last section") if le_info is not None else "- Live Editor natives: not scanned (mock)",
         "",
         "Assets scanned: %d (%d effective, rest shadowed by an earlier layer)." % (len(rows), len(live)), "",
         "Kind counts (effective): " + ", ".join("%s=%d" % kv for kv in sorted(c.items())),
         "Assets with more than one block: %d (the hotspot patcher handles one-Oodle-block assets only)." % multi, ""]
    for fam in ("gameplay", "simulation", "prematch", "other"):
        fr = [r for r in live if r["family"] == fam]
        if not fr:
            continue
        L += ["## %s (%d)" % (fam, len(fr)), "",
              "| asset | layer | kind | blocks | ctypes | comp B | raw B |", "|---|---|---|---|---|---|---|"]
        for r in sorted(fr, key=lambda r: r["name"]):
            L.append("| %s | %s | %s | %d | %s | %d | %d |" % (
                r["name"], r["layer"], r["kind"], r["n_blocks"], ",".join(r["ctypes"]), r["comp"], r["raw"]))
        L.append("")
    if le_info is not None:
        L += ["## Live Editor DLL strings", ""] + le_info + [""]
    return "\n".join(L)


NATIVES = ["GameplayAttribulatorSaveToFile", "GameplayAttribulatorSetVar", "GameplayAttribulatorGetVarType",
           "GameplayAttribulatorSetFloat", "GameplayAttribulatorGetFloat"]


def pe_sections(d):
    e = struct.unpack_from("<I", d, 0x3C)[0]
    ns, so = struct.unpack_from("<H", d, e + 6)[0], struct.unpack_from("<H", d, e + 20)[0]
    return [d[e + 24 + so + i * 40:e + 24 + so + i * 40 + 8].rstrip(b"\x00").decode("latin1") for i in range(ns)]


def control_names(root):
    """native names the stock LE Lua calls (control set: how many of THEM show up as plain strings in the DLL)"""
    names = set()
    base = os.path.join(root, "lua", "libs", "v2", "imports")
    for dp, dn, fn in os.walk(base):
        if os.sep + "turbo" in dp:
            continue
        for f in fn:
            if f.endswith(".lua"):
                try:
                    txt = open(os.path.join(dp, f), errors="ignore").read()
                except OSError:
                    continue
                names.update(re.findall(r"\b((?:Get|Set|Is|Has)[A-Z][A-Za-z]{6,40})\(", txt))
    return sorted(names)


def le_scan(root=r"C:\FC 27 Live Editor"):
    """-> (lines, inconclusive_note). The plain-string result is only meaningful if the control names are mostly visible."""
    lines, note = [], None
    p = os.path.join(root, "FCLiveEditor.DLL")
    if not os.path.isfile(p):
        return ["- no FCLiveEditor.DLL under %s" % root], "no DLL found"
    d = open(p, "rb").read()
    hits = {n: (n.encode() in d or n.encode("utf-16-le") in d) for n in NATIVES}
    ctl = control_names(root)
    seen = [n for n in ctl if n.encode() in d]
    vers = sorted({m.decode() for m in re.findall(rb"27\.\d+\.\d+", d)})
    lines.append("- `%s` (%d B), PE sections: %s" % (p, len(d), ", ".join(pe_sections(d))))
    lines.append("- Live Editor version strings inside the DLL: %s (installed LE = the highest, `version_info.json` only lists the "
                 "versions that exist)" % ", ".join(vers))
    for n, h in hits.items():
        lines.append("- `%s` as a plain string (ASCII or UTF-16): **%s**" % (n, "found" if h else "NOT found"))
    lines.append("- CONTROL: of %d native names the stock Lua calls (Get*/Set*/Is*/Has*), only %d appear as plain strings in the DLL."
                 % (len(ctl), len(seen)))
    if ctl and len(seen) * 2 < len(ctl):
        note = ("INCONCLUSIVE: most natives that certainly exist are not plain strings in this DLL (no packer section: names are built, "
                "hashed or obfuscated), so a miss proves nothing. Only the boot probe `type(_G.GameplayAttribulatorSaveToFile)` in launch 1 decides.")
    elif not any(hits.values()):
        note = "the names are not in the DLL and the control says they would be: the natives are probably absent."
    lines.append("- READING: " + (note or "found: the natives are registered by this DLL."))
    return lines, note


def selftest():
    def blk(ct, payload, dsize):
        return struct.pack(">I", dsize) + struct.pack("<H", ct & 0x7F) + struct.pack(">H", len(payload)) + payload
    d = blk(0x15, b"x" * 10, 40) + blk(0x00, b"y" * 5, 5)
    b = scan_blocks(d)
    assert b == [(0x15, 10, 40), (0, 5, 5)], b
    s = summarize(b)
    assert s["kind"] == "oodle+raw" and s["comp"] == 15 and s["raw"] == 45
    try:
        scan_blocks(d[:-3])
        raise SystemExit("overrun not detected")
    except ValueError:
        pass
    assert family("fifa/attribulator/prematch/x") == "prematch" and family("zz") == "other"
    txt = render(mock_rows(), ["- mock"], "INCONCLUSIVE: x")
    assert "## Verdict" in txt and "INCONCLUSIVE: x" in txt and "oodle=2" in txt and "raw=1" in txt and "## prematch (1)" in txt
    print("ebx_block_kinds selftest PASS")
    return 0


def main(a):
    if "--selftest" in a:
        return selftest()
    def g(k, d=None):
        return a[a.index(k) + 1] if k in a else d
    rx = re.compile(g("--regex", r"^fifa/attribulator/"), re.I)
    mock = "--mock" in a
    rows = mock_rows() if mock else scan_game(rx)
    le, note = (["- (mock)"], None) if mock else le_scan()
    txt = render(rows, le, note)
    out = g("--out")
    if out:
        open(out, "w", encoding="utf-8").write(txt)
    if g("--json"):
        json.dump(rows, open(g("--json"), "w"), indent=1)
    print("wrote %s (%d assets)" % (out, len(rows)) if out else txt)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
