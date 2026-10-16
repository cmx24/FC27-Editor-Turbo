"""ebx_block_kinds.py - OFFLINE, read-only. For every attrib EBX asset in the installed FC 27 toc (Patch then Data),
report the block kind of its stored bytes (Oodle 0x11/0x15/0x19, raw 0x00, other), compressed and raw size.
usage: python -I ebx_block_kinds.py [--regex RX] [--out FILE.md] [--json FILE.json] [--mock] [--selftest]
Reuses toc_bundles (parse_toc/raw/manifest); never opens saves/settings."""
import sys, os, re, json, struct, collections
sys.path.insert(0, r"C:\FC 27 Live Editor\turbo_output\asset_research\scripts")

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
                if struct.unpack_from("<I", blob, 4)[0] != 0x9D798ED6:
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


def render(rows, le_info):
    live, c, multi = verdict(rows)
    L = ["# attrib EBX block kinds (offline, read-only)", "",
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


def le_scan(root=r"C:\FC 27 Live Editor"):
    lines = []
    skip = ("turbo_dev", "turbo_output", ".git", "saves", "settings", "backup", "backups")
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d.lower() not in skip]
        for f in fn:
            fl = f.lower()
            if fl.endswith((".dll", ".asi", ".exe")) and ("live" in fl or "editor" in fl or "lua" in fl):
                p = os.path.join(dp, f)
                try:
                    d = open(p, "rb").read()
                except OSError:
                    continue
                hits = {n: (n.encode() in d or n.encode("utf-16-le") in d) for n in NATIVES}
                lines.append("- `%s` (%d B): " % (p, len(d)) +
                             ", ".join("%s=%s" % (n, "YES" if h else "no") for n, h in hits.items()))
            elif fl in ("version.txt", "le_version.txt"):
                try:
                    lines.append("- version file `%s`: %s" % (os.path.join(dp, f), open(os.path.join(dp, f), errors="replace").read(80).strip()))
                except OSError:
                    pass
    return lines or ["- no Live Editor DLL found under %s" % root]


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
    txt = render(mock_rows(), ["- mock"])
    assert "oodle=2" in txt and "raw=1" in txt and "## prematch (1)" in txt
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
    txt = render(rows, ["- (mock)"] if mock else le_scan())
    out = g("--out")
    if out:
        open(out, "w", encoding="utf-8").write(txt)
    if g("--json"):
        json.dump(rows, open(g("--json"), "w"), indent=1)
    print("wrote %s (%d assets)" % (out, len(rows)) if out else txt)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
