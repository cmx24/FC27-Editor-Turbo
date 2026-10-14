"""Kit texture chunk resolver for Turbo 2.6 phase 2 (3D kits on the pitch): READ-ONLY on the game.

What it proves offline (see turbo_output\\assessment_20261014\\IMPL_kits3d.md): for a club's kit, which RES texture header
belongs to which chunk (by the GUID inside the Oodle-compressed 184-byte header, never by order), the texture's
format / size / mip sizes, where the chunk's Oodle blocks sit in the cas files, and a per-block key (compressed size,
raw size, FNV-1a 64 of the compressed bytes) that the in-process OodleLZ_Decompress hook matches. It also lists every other
texture that uses the same chunk GUID (the sharing guard) and every bundle, in every toc, that names the kit.

usage:
  python -I kit_chunks.py index   [--game DIR] [--out FILE]               scan every toc, write the kit index (cached by toc size+mtime)
  python -I kit_chunks.py resolve --team 95 [--kind home_0_0] [--slug leicester_city_95] [--game DIR] [--index FILE] [--json FILE]
  python -I kit_chunks.py census  [--index FILE]                           sharing statistics
"""
import argparse
import glob
import gzip
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "asset_catalog"))
import frostbite as fb  # noqa: E402

DEFAULT_GAME = r"C:\Program Files\EA Games\EA SPORTS FC 27"
KIT_MARK = "character/kit/kit_"
INDEX_VERSION = 1

# Frostbite render format ids found in the texture headers (RES offset 0x0C) -> DXGI name (inferred from bytes per pixel
# and verified by decoding: 55 BC1 sRGB jersey colour, 54 BC1 coeff, 63 BC5 normal, 66 BC7 taa, 61 BC3 sRGB, 60 BC3).
FORMATS = {54: "BC1_UNORM", 55: "BC1_UNORM_SRGB", 60: "BC3_UNORM", 61: "BC3_UNORM_SRGB", 62: "BC4_UNORM", 63: "BC5_UNORM",
           66: "BC7_UNORM", 67: "BC7_UNORM_SRGB"}


def fnv1a64(b):
    """FNV-1a 64 (offset 0xCBF29CE484222325, prime 0x100000001B3): the same value the C++ matcher computes."""
    h = 0xCBF29CE484222325
    for x in memoryview(b):
        h = ((h ^ x) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def block_headers(data):
    """Block stream of a chunk: [(dsize, csize, ctype, payload_offset)] with the 8-byte headers parsed (same as fb.read_blocks)."""
    out, p = [], 0
    while p < len(data):
        dsize = struct.unpack_from(">I", data, p)[0]
        code = struct.unpack_from("<H", data, p + 4)[0]
        csize = struct.unpack_from(">H", data, p + 6)[0] + (((code >> 8) & 0x0F) << 16)
        ctype = code & 0x7F
        out.append((dsize & 0xFFFFFF, csize, ctype, p + 8))
        p += 8 + csize
    return out, p


def parse_texture_header(h):
    """184-byte texture RES header (decompressed): format id, width, height, mips, chunk guid hex, mip sizes."""
    if len(h) < 0x38 + 15 * 4:
        raise ValueError("texture header too short (%d)" % len(h))
    fmt = struct.unpack_from("<I", h, 0x0C)[0]
    w, hh = struct.unpack_from("<HH", h, 0x16)
    mips = h[0x1E]
    guid = h[0x28:0x38].hex()
    sizes = list(struct.unpack_from("<15I", h, 0x38))[:mips]
    return {"fmt_id": fmt, "fmt": FORMATS.get(fmt, "?%d" % fmt), "w": w, "h": hh, "mips": mips, "guid": guid, "mip_sizes": sizes}


def parse_manifest(blob):
    """Bundle manifest: ebx [(name,size)], res [[name,size,type]], chunks [(guid hex, logicalOffset, logicalSize)]. Entries are in
    the toc location order manifest, ebx..., res..., chunks... (location k+1 is the k-th item)."""
    if struct.unpack_from("<I", blob, 4)[0] != fb.MANIFEST_MAGIC:
        raise ValueError("manifest magic")
    total, ne, nr, nc, so, _mo, _ms = struct.unpack_from("<7I", blob, 8)
    strings = 4 + so
    p = 0x24 + 20 * total
    ebx, res = [], []
    for _ in range(ne):
        no, osz = struct.unpack_from("<II", blob, p)
        ebx.append((fb._cstr(blob, strings + no), osz))
        p += 8
    for _ in range(nr):
        no, osz = struct.unpack_from("<II", blob, p)
        res.append([fb._cstr(blob, strings + no), osz, 0])
        p += 8
    for r in res:
        r[2] = struct.unpack_from("<I", blob, p)[0]
        p += 4
    p += 16 * nr + 8 * nr  # res meta (16 bytes) and rid (8 bytes) per res
    chunks = []
    for _ in range(nc):
        chunks.append((blob[p:p + 16].hex(), *struct.unpack_from("<II", blob, p + 16)))
        p += 24
    return ebx, res, chunks


def bundle_manifest_blob(game, locs):
    blob = game.raw(locs[0])
    if struct.unpack_from("<I", blob, 4)[0] != fb.MANIFEST_MAGIC:
        blob = fb.read_blocks(blob, game.oodle)
    return blob


def list_tocs(game_root):
    out = []
    for layer in ("Data", "Patch"):
        for p in sorted(glob.glob(os.path.join(game_root, layer, "Win32", "*.toc"))):
            out.append(p)
    return out


def toc_key(game_root, p):
    st = os.stat(p)
    return {"toc": os.path.relpath(p, game_root).replace("\\", "/"), "size": st.st_size, "mtime": int(st.st_mtime)}


def build_index(game_root, out_path, log=print):
    """Scan every toc: for each bundle whose manifest names a kit asset, record names (ebx/res), res->chunk guid mapping through
    the decompressed texture headers and the chunk locations. Cached per toc (size + mtime) inside the index file."""
    game = fb.Game(game_root)
    old = {}
    if os.path.isfile(out_path):
        try:
            with gzip.open(out_path, "rt", encoding="utf-8") as f:
                o = json.load(f)
            if o.get("version") == INDEX_VERSION:
                old = {t["key"]["toc"]: t for t in o["tocs"] if "key" in t}
        except Exception:
            old = {}
    tocs = []
    t0 = time.time()
    for p in list_tocs(game_root):
        k = toc_key(game_root, p)
        prev = old.get(k["toc"])
        if prev and prev["key"] == k:
            tocs.append(prev)
            log("%s: cached (%d kit bundles)" % (k["toc"], len(prev["bundles"])))
            continue
        with open(p, "rb") as f:
            d = f.read()
        try:
            bundles = fb.parse_toc_bundles(d)
        except Exception as e:
            log("%s: skipped (%s)" % (k["toc"], e))
            continue
        kit_bundles = []
        n_err = 0
        for i, locs in enumerate(bundles):
            if not locs:
                continue
            try:
                blob = bundle_manifest_blob(game, locs)
                ebx, res, chunks = parse_manifest(blob)
            except Exception:
                n_err += 1
                continue
            if not any(KIT_MARK in n for n, _ in ebx) and not any(KIT_MARK in r[0] for r in res):
                continue
            rec = {"i": i, "ebx": [n for n, _ in ebx], "res": [], "chunks": []}
            ne, nr = len(ebx), len(res)
            for k2, r in enumerate(res):
                item = {"name": r[0], "type": "%08x" % r[2]}
                if r[2] == 0x6BDE20BA:  # texture
                    try:
                        hdr = fb.read_blocks(game.raw(locs[1 + ne + k2]), game.oodle)
                        item.update(parse_texture_header(hdr))
                    except Exception as e:
                        item["err"] = str(e)[:80]
                rec["res"].append(item)
            for k3, (g, lo, ls) in enumerate(chunks):
                loc = locs[1 + ne + nr + k3]
                rec["chunks"].append({"guid": g, "logical": ls, "cas": loc[0], "off": loc[1], "size": loc[2]})
            kit_bundles.append(rec)
        tocs.append({"key": k, "bundles": kit_bundles, "errors": n_err})
        log("%s: %d bundles, %d kit bundles, %d unreadable (%.0fs)" % (k["toc"], len(bundles), len(kit_bundles), n_err, time.time() - t0))
    game.close()
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with gzip.open(out_path, "wt", encoding="utf-8") as f:
        json.dump({"version": INDEX_VERSION, "tocs": tocs}, f, separators=(",", ":"))
    return tocs


def load_index(path):
    with gzip.open(path, "rt", encoding="utf-8") as f:
        o = json.load(f)
    if o.get("version") != INDEX_VERSION:
        raise ValueError("index version")
    return o["tocs"]


def kit_of(name):
    """'content/character/kit/kit_0/leicester_city_95/home_0_0/jersey/jersey_95_0_0_color' ->
    ('leicester_city_95', 'home_0_0', 'jersey', 'jersey_95_0_0_color')"""
    p = name.split("/")
    try:
        i = p.index("character") + 2
        slug, kind = p[i + 1], p[i + 2]
        part = p[i + 3] if len(p) > i + 4 else ""
        return slug, kind, part, p[-1]
    except (ValueError, IndexError):
        return None


def team_of_slug(slug):
    t = slug.rsplit("_", 1)[-1]
    return int(t) if t.isdigit() else None


def textures_of(tocs, team, kind=None, slug=None):
    """All texture resources of a team's kit across every toc: dicts with toc, bundle, name, guid, header fields, chunk."""
    out = []
    for t in tocs:
        toc = t["key"]["toc"]
        for b in t["bundles"]:
            chunk_by = {c["guid"]: c for c in b["chunks"]}
            for r in b["res"]:
                if r.get("type") != "6bde20ba":
                    continue
                k = kit_of(r["name"])
                if not k or team_of_slug(k[0]) != team:
                    continue
                if kind and k[1] != kind:
                    continue
                if slug and k[0] != slug:
                    continue
                d = dict(r)
                d.update({"toc": toc, "bundle": b["i"], "slug": k[0], "kind": k[1], "part": k[2], "leaf": k[3]})
                d["chunk"] = chunk_by.get(r.get("guid"))
                out.append(d)
    return out


def guid_users(tocs):
    """chunk guid -> set of (toc, bundle, resource name) that reference it."""
    u = {}
    for t in tocs:
        for b in t["bundles"]:
            for r in b["res"]:
                g = r.get("guid")
                if g:
                    u.setdefault(g, set()).add((t["key"]["toc"], b["i"], r["name"]))
    return u


def chunk_blocks(game, chunk):
    """Blocks of one chunk read from the cas: [{dsize, csize, ctype, fnv of the compressed payload, head16, tail16}]."""
    data = game.raw((chunk["cas"], chunk["off"], chunk["size"]))
    hdrs, used = block_headers(data)
    if used != len(data):
        raise ValueError("block stream does not end at the chunk end (%d != %d)" % (used, len(data)))
    out = []
    for dsize, csize, ctype, po in hdrs:
        pay = data[po:po + csize]
        out.append({"dsize": dsize, "csize": csize, "ctype": ctype, "fnv": "%016x" % fnv1a64(pay),
                    "head": pay[:16].hex(), "tail": pay[-16:].hex()})
    return out


def resolve(game_root, tocs, team, kind=None, slug=None, with_blocks=True):
    game = fb.Game(game_root)
    tex = textures_of(tocs, team, kind, slug)
    users = guid_users(tocs)
    for d in tex:
        ch = d.get("chunk")
        d["expected_logical"] = sum(d.get("mip_sizes", []))
        d["layout_ok"] = bool(ch) and ch["logical"] == d["expected_logical"]
        d["shared_with"] = sorted("%s:%d:%s" % u for u in users.get(d.get("guid"), ())
                                  if not (u[0] == d["toc"] and u[1] == d["bundle"] and u[2] == d["name"]))
        if with_blocks and ch:
            d["blocks"] = chunk_blocks(game, ch)
            d["blocks_dsize_total"] = sum(b["dsize"] for b in d["blocks"])
    game.close()
    return tex


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["index", "resolve", "census"])
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--out", default=None)
    ap.add_argument("--index", default=None)
    ap.add_argument("--team", type=int)
    ap.add_argument("--kind")
    ap.add_argument("--slug")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    idx = a.index or a.out or os.path.join(os.environ.get("TEMP", "."), "kit_index.json.gz")
    if a.cmd == "index":
        build_index(a.game, idx)
        return 0
    tocs = load_index(idx)
    if a.cmd == "resolve":
        tex = resolve(a.game, tocs, a.team, a.kind, a.slug)
        for d in sorted(tex, key=lambda x: (x["kind"], x["part"], x["leaf"])):
            print("%-14s %-7s %-32s %-16s %4dx%-4d mips %2d logical %8d ok %-5s blocks %2s shared %d" % (
                d["kind"], d["part"], d["leaf"], d["fmt"], d["w"], d["h"], d["mips"], d["chunk"]["logical"] if d["chunk"] else -1,
                d["layout_ok"], len(d.get("blocks", [])), len(d["shared_with"])))
        if a.json:
            with open(a.json, "w") as f:
                json.dump(tex, f, indent=1)
        return 0
    users = guid_users(tocs)
    multi = {g: u for g, u in users.items() if len({x[2] for x in u}) > 1}
    print("chunks referenced:", len(users), "referenced by more than one resource name:", len(multi))
    return 0


if __name__ == "__main__":
    sys.exit(main())
