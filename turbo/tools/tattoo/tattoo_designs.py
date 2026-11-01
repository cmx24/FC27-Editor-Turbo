"""Tattoo design pictures for Turbo's tattoo pickers, from the FC 27 game files (READ-ONLY on the game).

Every tattoo N of the game is a decal texture content/character/tattoo/tattoo_N_color (a RES texture of a bundle; its pixels
are a chunk). This tool finds those textures with Turbo's own Frostbite readers (asset_catalog/frostbite.py: toc, cas, the
game's Oodle dll; kits3d/kit_chunks.py: bundle manifest with res + chunks, the 184-byte texture header), keeps mip 0 (or the
largest mip the chunk holds) as a DDS file and draws a 256x256 tile of the design:

  <turbo_output>\\pictures\\assets\\tattoo_design\\<id>.png      the tile the picker shows (ui_visual_picker.cpp)
  <turbo_output>\\cache\\tattoo_design_dds\\<id>.dds              the texture as found (for checks / a re-draw without the game)
  <turbo_output>\\cache\\tattoo_design_dds\\manifest.json         per id: format, size, mip used, toc, tile mode, or the failure

usage:
  python -I tattoo_designs.py extract [--game DIR] [--out TURBO_OUTPUT] [--le LE]     textures + tiles (tiles need Pillow)
  python -I tattoo_designs.py tiles   [--out TURBO_OUTPUT]                            tiles again from the DDS cache (no game)
"""
import argparse
import glob
import json
import os
import re
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LE_DEFAULT = r"C:\FC 27 Live Editor"
GAME_DEFAULT = r"C:\Program Files\EA Games\EA SPORTS FC 27"
# Turbo's readers: next to this file (a copy), the repo layout (turbo/tools/<x>), the installed LE (turbo\tools\asset_catalog)
for p in (HERE, os.path.join(HERE, "..", "asset_catalog"), os.path.join(HERE, "..", "kits3d"),
          os.path.join(LE_DEFAULT, "turbo", "tools", "asset_catalog")):
    if os.path.isdir(p) and p not in sys.path:
        sys.path.append(p)

TATTOO_RX = re.compile(r"^content/character/tattoo/tattoo_(\d+)_color$")
TEXTURE_RES = 0x6BDE20BA
TILE = 256
MARGIN = 14
SKIN = (214, 170, 132)  # neutral light-medium skin tone: the ink is dark, so the tile shows it on skin, as on a player

# Frostbite render format id -> DXGI format of the DDS (kit_chunks.FORMATS names the BC ids). The sRGB ids are written as their
# UNORM twin: the blocks are the same bytes, and Pillow decodes BC1/BC3/BC7 UNORM but refuses the SRGB tags (DXGI 72/78/99);
# the decoded values are the stored sRGB colours either way, which is what a PNG holds.
DXGI = {54: 71, 55: 71, 60: 77, 61: 77, 62: 80, 63: 83, 66: 98, 67: 98}
SRGB_TWIN = {72: 71, 78: 77, 99: 98}
BLOCK_BYTES = {71: 8, 72: 8, 80: 8, 81: 8, 77: 16, 78: 16, 83: 16, 84: 16, 98: 16, 99: 16, 74: 16, 75: 16}


def log(msg):
    print(msg, flush=True)


# ------------------------------------------------------------------ DDS
def dds_bytes(dxgi, w, h, data):
    """A one-mip DX10 DDS file of `data` (Pillow reads BC1..BC7 and RGBA8 from it)."""
    if dxgi in BLOCK_BYTES:
        linear = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * BLOCK_BYTES[dxgi]
        flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000
    else:
        linear = w * 4
        flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x8
    hdr = struct.pack("<4s7I44x", b"DDS ", 124, flags, h, w, linear, 0, 1)
    pf = struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    caps = struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    dx10 = struct.pack("<5I", dxgi, 3, 0, 1, 0)
    return hdr + pf + caps + dx10 + data[:linear * (1 if dxgi in BLOCK_BYTES else h)]


def guess_dxgi(fmt_id, w, h, mip0_size):
    """The DXGI format of a texture header: the known ids, else from bytes per pixel (marked as a guess)."""
    if fmt_id in DXGI:
        return DXGI[fmt_id], False
    bpp = mip0_size / float(max(1, w * h))
    if abs(bpp - 0.5) < 0.05:
        return 71, True
    if abs(bpp - 1.0) < 0.05:
        return 98, True
    if abs(bpp - 4.0) < 0.05:
        return 28, True  # R8G8B8A8_UNORM
    return None, True


# ------------------------------------------------------------------ the game
def tattoo_tocs(game, out):
    """Tocs to scan: those the asset catalog's EBX census cache says hold tattoos (fast), else every toc."""
    import build_asset_catalog as bac  # noqa: E402  (Turbo's own toc list: Data + Patch, no superbundle layouts)
    every = bac.list_tocs(game)
    cache = os.path.join(out, "asset_catalog", "cache")
    picked = []
    for p in every:
        c = os.path.join(cache, bac.cache_name(bac.rel(game, p)))
        try:
            with open(c, encoding="utf-8") as f:
                d = json.load(f)
        except (OSError, ValueError):
            return every, "every toc (no census cache for %s)" % bac.rel(game, p)
        st = os.stat(p)
        k = d.get("key") or {}
        if k.get("size") != st.st_size or k.get("mtime") != int(st.st_mtime):
            return every, "every toc (census cache outdated for %s)" % bac.rel(game, p)
        if (d.get("result") or {}).get("tattoo"):
            picked.append(p)
    return picked, "%d tocs the catalog census lists with tattoos" % len(picked)


def mip_slice(data, lo, hdr):
    """(mip index, w, h, bytes) of the largest mip the chunk data holds. lo = the chunk's logical offset in the bundle."""
    sizes = hdr["mip_sizes"]
    total = sum(sizes)
    start = 0 if len(data) >= total else (lo if lo else total - len(data))
    cum = 0
    for k, s in enumerate(sizes):
        if cum >= start and cum - start + s <= len(data):
            return k, max(1, hdr["w"] >> k), max(1, hdr["h"] >> k), data[cum - start:cum - start + s]
        cum += s
    raise ValueError("no whole mip in %d bytes (total %d, start %d)" % (len(data), total, start))


def extract(game_root, out, le):
    import frostbite as fb  # noqa: E402
    import kit_chunks as kc  # noqa: E402
    t0 = time.time()
    dds_dir = os.path.join(out, "cache", "tattoo_design_dds")
    os.makedirs(dds_dir, exist_ok=True)
    tocs, how = tattoo_tocs(game_root, out)
    log("Turbo tattoo designs: game %s, scanning %s" % (game_root, how))
    game = fb.Game(game_root)
    found = {}      # id -> list of candidates (later toc first after the reverse below: Patch over Data)
    other = set()   # names with 'tattoo' that are not tattoo_N_color (cranium decals...)
    toc_chunks = None
    for p in tocs:
        with open(p, "rb") as f:
            d = f.read()
        try:
            bundles = fb.parse_toc_bundles(d)
        except Exception as e:  # noqa: BLE001
            log("  %s: skipped (%s)" % (p, e))
            continue
        n_tex = n_err = 0
        for locs in bundles:
            if not locs:
                continue
            try:
                ebx, res, chunks = kc.parse_manifest(kc.bundle_manifest_blob(game, locs))
            except Exception:  # noqa: BLE001
                n_err += 1
                continue
            for n, _s in ebx:
                if "tattoo" in n and not n.startswith("content/character/tattoo/"):
                    other.add(n)
            ne, nr = len(ebx), len(res)
            bchunks = {}
            for k3, (g, lo, ls) in enumerate(chunks):
                bchunks[g] = (locs[1 + ne + nr + k3], lo, ls)
            for k2, (name, _sz, rtype) in enumerate(res):
                m = TATTOO_RX.match(name)
                if not m:
                    if "tattoo" in name and not name.startswith("content/character/tattoo/"):
                        other.add(name)
                    continue
                tid = int(m.group(1))
                cand = {"toc": os.path.relpath(p, game_root).replace("\\", "/"), "name": name, "type": "%08x" % rtype}
                if rtype != TEXTURE_RES:
                    cand["err"] = "res type %08x is not a texture" % rtype
                else:
                    try:
                        hdr = kc.parse_texture_header(fb.read_blocks(game.raw(locs[1 + ne + k2]), game.oodle))
                        cand["hdr"] = hdr
                        cand["bchunk"] = bchunks.get(hdr["guid"])
                        n_tex += 1
                    except Exception as e:  # noqa: BLE001
                        cand["err"] = "texture header: %s" % str(e)[:120]
                found.setdefault(tid, []).append(cand)
        log("  %-60s %6d bundles, %4d tattoo textures, %d unreadable (%.0fs)" % (
            os.path.relpath(p, game_root), len(bundles), n_tex, n_err, time.time() - t0))
    manifest = {"built": time.strftime("%Y-%m-%dT%H:%M:%S"), "game": game_root, "tocs": how, "items": {}}
    ok = 0
    fails = {}
    for tid in sorted(found):
        cands = list(reversed(found[tid]))  # Patch tocs come after Data in list_tocs: try them first
        item = {"err": None}
        for c in cands:
            if "hdr" not in c:
                item["err"] = c.get("err")
                continue
            hdr = c["hdr"]
            locs = []
            if c.get("bchunk"):
                locs.append(("bundle", c["bchunk"][0], c["bchunk"][1]))
            if toc_chunks is None:
                toc_chunks = {}
                for tp in bac_list(game_root):
                    try:
                        with open(tp, "rb") as f:
                            for g, v in fb.parse_toc_chunks(f.read()).items():
                                toc_chunks.setdefault(g, v)  # Patch first (bac_list puts Patch first)
                    except Exception:  # noqa: BLE001
                        pass
            for g in (hdr["guid"], bytes.fromhex(hdr["guid"])[::-1].hex()):
                if g in toc_chunks:
                    locs.insert(0, ("toc", toc_chunks[g], 0))
                    break
            if not locs:
                item["err"] = "chunk %s not found (bundle or toc chunk tables)" % hdr["guid"]
                continue
            err = None
            for where, loc, lo in locs:
                try:
                    data = fb.read_blocks(game.raw(loc), game.oodle)
                    k, w, h, pix = mip_slice(data, lo, hdr)
                    dxgi, guessed = guess_dxgi(hdr["fmt_id"], w, h, len(pix))
                    if dxgi is None:
                        raise ValueError("format id %d (%s) not decodable" % (hdr["fmt_id"], hdr["fmt"]))
                    with open(os.path.join(dds_dir, "%d.dds" % tid), "wb") as f:
                        f.write(dds_bytes(dxgi, w, h, pix))
                    item = {"err": None, "toc": c["toc"], "fmt_id": hdr["fmt_id"], "fmt": hdr["fmt"], "dxgi": dxgi,
                            "dxgi_guessed": guessed, "w0": hdr["w"], "h0": hdr["h"], "mip": k, "w": w, "h": h,
                            "chunk_from": where}
                    break
                except Exception as e:  # noqa: BLE001
                    err = "%s chunk: %s" % (where, str(e)[:120])
            if item.get("err") is None and "dxgi" in item:
                break
            item["err"] = err
        manifest["items"][str(tid)] = item
        if item.get("err"):
            fails[tid] = item["err"]
        else:
            ok += 1
    game.close()
    manifest["other_tattoo_names"] = sorted(other)[:200]
    manifest["other_tattoo_names_count"] = len(other)
    write_json(os.path.join(dds_dir, "manifest.json"), manifest)
    log("TEXTURES %d of %d tattoo ids written as DDS, %d failed (%.0fs)" % (ok, len(found), len(fails), time.time() - t0))
    for tid, e in sorted(fails.items()):
        log("  FAILED %d: %s" % (tid, e))
    log("  other names with 'tattoo' (not tattoo_N_color): %d" % len(other))
    for n in sorted(other)[:30]:
        log("    %s" % n)
    catalog_check(out, set(found))
    return manifest


def bac_list(game_root):
    import build_asset_catalog as bac  # noqa: E402
    every = bac.list_tocs(game_root)
    return [p for p in every if os.sep + "Patch" + os.sep in p] + [p for p in every if os.sep + "Patch" + os.sep not in p]


def catalog_check(out, ids):
    cats = sorted(glob.glob(os.path.join(out, "asset_catalog", "catalog_*.json")), key=os.path.getmtime)
    if not cats:
        log("  (no asset catalog to compare with)")
        return
    with open(cats[-1], encoding="utf-8") as f:
        cat = json.load(f)
    model = set(int(x) for x in cat.get("families", {}).get("tattoo", {}).get("model", []))
    log("CATALOG %s: %d tattoo models; %d have a colour texture here; missing: %s" % (
        os.path.basename(cats[-1]), len(model), len(model & ids), sorted(model - ids)[:60]))


def write_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1, sort_keys=True)
    os.replace(tmp, path)


# ------------------------------------------------------------------ tiles
def tile_of(dds_path):
    """256x256 RGBA tile of one tattoo texture: the design cropped to its ink, fitted, on a skin-tone square.
    Mode 'alpha': the texture's alpha is the ink mask (colour over skin by alpha). Mode 'multiply': no alpha, ink dark on white
    (skin x texture). Mode 'mask': no alpha, light ink on black (dark ink by luminance)."""
    from PIL import Image, ImageChops, ImageOps, ImageStat  # noqa: E402
    with open(dds_path, "rb") as f:
        raw = bytearray(f.read())
    if raw[84:88] == b"DX10" and struct.unpack_from("<I", raw, 128)[0] in SRGB_TWIN:  # a cache written before the UNORM mapping
        struct.pack_into("<I", raw, 128, SRGB_TWIN[struct.unpack_from("<I", raw, 128)[0]])
    import io  # noqa: E402
    im = Image.open(io.BytesIO(bytes(raw)))
    im.load()
    im = im.convert("RGBA")
    r, g, b, a = im.split()
    lum = im.convert("L")
    amin, amax = a.getextrema()
    a_mean = ImageStat.Stat(a).mean[0]
    l_mean = ImageStat.Stat(lum).mean[0]
    if amax - amin > 32 and a_mean < 240:
        mode, mask = "alpha", a
    elif l_mean >= 128:
        mode, mask = "multiply", ImageOps.invert(lum)
    else:
        mode, mask = "mask", lum
    # a decal on a cylinder (arm, leg, head) can cross the texture's wrap seam: one design shows as two halves at the left and right
    # edges. Roll the texture (it wraps) so the widest empty band of columns / rows sits at the edge: the halves join again.
    ink = mask.point(lambda v: 255 if v > 24 else 0)
    dx, dy = _wrap_shift(ink.resize((ink.size[0], 1), Image.BOX)), _wrap_shift(ink.resize((1, ink.size[1]), Image.BOX))
    if dx or dy:
        im, mask, ink = (ImageChops.offset(x, -dx, -dy) for x in (im, mask, ink))
    bbox = ink.getbbox()
    if not bbox:
        return None, mode, "empty (no ink above 24/255)"
    pad = max(2, int(0.02 * max(im.size)))
    bbox = (max(0, bbox[0] - pad), max(0, bbox[1] - pad), min(im.size[0], bbox[2] + pad), min(im.size[1], bbox[3] + pad))
    rgb = im.convert("RGB").crop(bbox)
    m = mask.crop(bbox)
    skin = Image.new("RGB", rgb.size, SKIN)
    if mode == "alpha":
        art = Image.composite(rgb, skin, m)
    elif mode == "multiply":
        art = ImageChops.multiply(skin, rgb)
    else:
        art = Image.composite(Image.new("RGB", rgb.size, (28, 30, 36)), skin, m)
    side = TILE - 2 * MARGIN
    scale = min(side / float(art.size[0]), side / float(art.size[1]))
    nw, nh = max(1, int(round(art.size[0] * scale))), max(1, int(round(art.size[1] * scale)))
    art = art.resize((nw, nh), Image.LANCZOS)
    tile = Image.new("RGBA", (TILE, TILE), SKIN + (255,))
    tile.paste(art, ((TILE - nw) // 2, (TILE - nh) // 2))
    return tile, mode, "%dx%d ink box %s" % (im.size[0], im.size[1], bbox)


def _wrap_shift(line):
    """Offset that starts the line just after its longest empty run (circular), or 0 when that run already touches an edge."""
    v = list(line.tobytes())  # an "L" image: one byte per pixel
    n = len(v)
    if n < 2 or all(x == 0 for x in v) or all(x != 0 for x in v):
        return 0
    best_len, best_end, run = 0, 0, 0
    for i in range(2 * n):  # twice round: a run across the seam is counted whole
        if v[i % n] == 0:
            run += 1
            if run > best_len and run <= n:
                best_len, best_end = run, (i + 1) % n
        else:
            run = 0
    start = (best_end - best_len) % n
    if start == 0 or best_end == 0 or start > best_end:  # the empty run already reaches an edge (or wraps across it)
        return 0
    return best_end


def tiles(out):
    dds_dir = os.path.join(out, "cache", "tattoo_design_dds")
    dest = os.path.join(out, "pictures", "assets", "tattoo_design")
    try:
        import PIL  # noqa: F401,E402
    except ImportError:
        log("TILES skipped: Pillow is not installed for this Python (the DDS files are kept in %s)" % dds_dir)
        return 3
    os.makedirs(dest, exist_ok=True)
    mpath = os.path.join(dds_dir, "manifest.json")
    man = {}
    if os.path.isfile(mpath):
        with open(mpath, encoding="utf-8") as f:
            man = json.load(f)
    n_ok, fails, modes = 0, {}, {}
    files = sorted(glob.glob(os.path.join(dds_dir, "*.dds")), key=lambda p: int(os.path.basename(p)[:-4]))
    for p in files:
        tid = int(os.path.basename(p)[:-4])
        try:
            tile, mode, note = tile_of(p)
        except Exception as e:  # noqa: BLE001
            tile, mode, note = None, "?", "decode: %s" % str(e)[:120]
        it = man.setdefault("items", {}).setdefault(str(tid), {})
        it["tile_mode"], it["tile_note"] = mode, note
        if tile is None:
            fails[tid] = note
            it["tile"] = None
            continue
        tp = os.path.join(dest, "%d.png" % tid)
        tile.save(tp + ".tmp.png")
        os.replace(tp + ".tmp.png", tp)
        it["tile"] = "pictures/assets/tattoo_design/%d.png" % tid
        modes[mode] = modes.get(mode, 0) + 1
        n_ok += 1
    if man:
        write_json(mpath, man)
    log("TILES %d written to %s, %d failed; modes %s" % (n_ok, dest, len(fails), modes))
    for tid, e in sorted(fails.items()):
        log("  TILE FAILED %d: %s" % (tid, e))
    return 0 if n_ok else 4


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("cmd", choices=["extract", "tiles"])
    ap.add_argument("--game", default=GAME_DEFAULT)
    ap.add_argument("--le", default=LE_DEFAULT)
    ap.add_argument("--out", default=None, help="turbo_output folder (default <LE>\\turbo_output)")
    a = ap.parse_args(argv)
    out = a.out or os.path.join(a.le, "turbo_output")
    if a.cmd == "extract":
        if not os.path.isfile(os.path.join(a.game, "FC27.exe")):
            log("NO GAME at %s" % a.game)
            return 2
        man = extract(a.game, out, a.le)
        if not any(not v.get("err") for v in man["items"].values()):
            log("NO TEXTURE extracted")
            return 5
    rc = tiles(out)
    log("DONE rc=%d" % rc)
    return rc


if __name__ == "__main__":
    sys.exit(main())
