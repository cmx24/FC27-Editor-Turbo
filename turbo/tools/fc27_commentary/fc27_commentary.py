#!/usr/bin/env python3
"""Extract the FC 27 commentary speech bank of one language from disk (track: commentary bank, docs/re/fc27-commentary-bank.md).

What it gives, for a downloaded commentary pack (<game>\\commentary\\commentaryfull_<lang>.toc + cas files):
  * every row of the pSIMPLE_SURNAME selection data set: SegmentID, VariationId, surname_ID (= commentary id);
  * every row of pPLAYER_NAMES_SIMPLE (SegmentID, VariationId, player_db_pID = player id) and of pPLAYER_NAMES_LINK;
  * with --wav, each segment's audio as a 48 kHz 16-bit mono PCM wav, named like FIFA Editor Tool's exports
    (generic\\pSIMPLE_SURNAME_<seg>_<seg>.wav, real\\pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav).

usage: python fc27_commentary.py --lang ita_it [--game DIR] [--out FILE.json] [--wav DIR] [--ffmpeg EXE] [--list]
       python fc27_commentary.py --self-test [--game DIR]

The layers, all read from the game's files (nothing is guessed):
  toc        signed toc (00 D1 CE 01, data at 0x22C, big-endian header of 15 ints): inline bundles + chunk table;
  bundles    each inline bundle lists cas locations; its first entry is the binary bundle manifest (magic 0x9D798ED6):
             ebx / res names and sizes, in the order of the remaining locations;
  blocks     Frostbite blocks (8-byte header: decompressed size, code, compressed size), type 0x00 stored, 0x19 Oodle
             (oo2core_9_win64.dll from the game folder, loaded at run time, never copied);
  SBle       the res of a SoundWave family: data sets (DSET) with hashed names (djb2-xor), column fields and a
             prefix-sum index over the selector parameters (cm_sim, surname_ID / player_db_pID, player_intensity);
  audio      one chunk per family (its GUID is in the family's EBX); each segment is an EA SNS stream
             (H block: SNR header, D blocks: u32 samples + one Opus packet, E block); --wav wraps the packets in Ogg
             and decodes them with ffmpeg (found on PATH or given with --ffmpeg).

Pure Python 3.11 standard library + ctypes. Read-only on the game folder.
"""
import argparse
import concurrent.futures
import ctypes
import datetime
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import wave

DEFAULT_GAME = r"C:\Program Files\EA Games\EA SPORTS FC 27"
TOC_MAGIC = b"\x00\xd1\xce\x01"
TOC_DATA = 0x22C                    # the toc data starts after the signature block
BUNDLE_MAGICS = (0x9D798ED6,)       # binary bundle manifest (little-endian, with sha1s)
INLINE_BUNDLE = 0x40000000          # toc bundle size flag: the bundle's location table is inside the toc
OODLE_TYPES = (0x11, 0x15, 0x19)    # block compression types handled by Oodle
OPUS_RATE = 48000

FAMILIES = {                        # output key -> (family name in the bank, selector parameter, wav prefix)
    "generic": ("psimple_surname", "surname_ID", "pSIMPLE_SURNAME"),
    "real": ("pplayer_names_simple", "player_db_pID", "pPLAYER_NAMES_SIMPLE"),
    "real_link": ("pplayer_names_link", "player_db_pID", "pPLAYER_NAMES_LINK"),
}
WAV_FAMILIES = ("generic", "real")


class FormatError(Exception):
    pass


def djb2x(name):
    """The hash the speech system uses for data set, field and parameter names (h = 5381; h = h * 33 ^ c)."""
    h = 5381
    for c in name.encode("ascii"):
        h = ((h * 33) & 0xFFFFFFFF) ^ c
    return h


# Names resolved by hashing the strings of the game's own image (turbo_output\fc27_image.bin), see the doc.
NAMES = {djb2x(n): n for n in (
    "Selection", "Variations", "Segments", "Chunks", "VariationId", "SegmentCount", "StreamChunkIndex", "Duration",
    "ChunkSize", "ChunkId", "ChunkIndex", "cm_sim", "surname_ID", "player_db_pID", "player_intensity")}
H_SEGMENT = 0x6AC4E4EA        # the Selection column FIFA Editor Tool calls SegmentID (name not found in the image)
H_SEG_OFFSET = 0xE8E591DD     # Segments: byte offset of the segment's stream in the family chunk (low 2 bits = 3)
H_SEG_CHUNK = 0xD506D74E      # Segments: index of the chunk (constant 0 in the ita_it families)


def hname(h):
    return NAMES.get(h, "%08x" % h)


# ---------------------------------------------------------------------------------------------------------- Oodle
class Oodle:
    def __init__(self, game):
        path = os.path.join(game, "oo2core_9_win64.dll")
        if not os.path.isfile(path):
            raise FormatError("Oodle not found: %s (the game folder's oo2core_9_win64.dll is needed)" % path)
        self.path = path
        self.dll = ctypes.WinDLL(path)
        fn = self.dll.OodleLZ_Decompress
        fn.restype = ctypes.c_int64
        fn.argtypes = [ctypes.c_void_p, ctypes.c_int64, ctypes.c_void_p, ctypes.c_int64, ctypes.c_int, ctypes.c_int,
                       ctypes.c_int, ctypes.c_void_p, ctypes.c_int64, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                       ctypes.c_int64, ctypes.c_int]
        self.fn = fn

    def decompress(self, src, rawlen):
        out = ctypes.create_string_buffer(rawlen)
        n = self.fn(src, len(src), out, rawlen, 1, 0, 0, None, 0, None, None, None, 0, 3)
        if n != rawlen:
            raise FormatError("Oodle returned %d bytes, expected %d" % (n, rawlen))
        return out.raw


def read_blocks(data, oodle):
    """Frostbite block stream -> bytes. Header: u32 BE size (low 24 bits), u16 LE code (low 7 bits = type, bits 8..11 =
    compressed size bits 16..19), u16 BE compressed size (low 16 bits)."""
    out, p, n = [], 0, len(data)
    while p < n:
        if p + 8 > n:
            raise FormatError("truncated block header at %d" % p)
        dsize = struct.unpack_from(">I", data, p)[0]
        code = struct.unpack_from("<H", data, p + 4)[0]
        csize = struct.unpack_from(">H", data, p + 6)[0] + (((code >> 8) & 0x0F) << 16)
        if dsize >> 24:
            raise FormatError("block at %d uses a dictionary (flags %02x): not supported" % (p, dsize >> 24))
        ctype = code & 0x7F
        p += 8
        if ctype == 0:
            if csize != dsize:
                raise FormatError("stored block at %d: sizes %d / %d differ" % (p, csize, dsize))
            out.append(data[p:p + dsize])
        elif ctype in OODLE_TYPES:
            if oodle is None:
                raise FormatError("Oodle block without Oodle")
            out.append(oodle.decompress(data[p:p + csize], dsize))
        else:
            raise FormatError("block at %d: compression type 0x%02x not supported" % (p - 8, ctype))
        p += csize
    return b"".join(out)


# ------------------------------------------------------------------------------------------------------------ toc
class Toc:
    """A signed FC 24+ toc: inline bundles (their cas location tables) and the chunk table."""

    def __init__(self, path, data=None):
        self.path = path
        d = data if data is not None else open(path, "rb").read()
        if d[:4] != TOC_MAGIC or len(d) < TOC_DATA + 60:
            raise FormatError("%s: not a signed toc (magic %s)" % (path, d[:4].hex()))
        h = struct.unpack_from(">15I", d, TOC_DATA)
        if h[0] != 0x3C:
            raise FormatError("%s: toc header size %#x, expected 0x3c" % (path, h[0]))
        self.header = h
        self.bundles = []                       # [ [(cas id hex, offset, size), ...] ]  entry 0 = bundle manifest
        for i in range(h[2]):
            _name, size, off = struct.unpack_from(">IIQ", d, TOC_DATA + h[1] + i * 16)
            if not size & INLINE_BUNDLE:
                raise FormatError("%s: bundle %d is not inline (size %#x): not supported" % (path, i, size))
            self.bundles.append(self._inline_bundle(d, TOC_DATA + off, size & 0x3FFFFFFF, i))
        self.chunks = {}                        # guid (EBX byte order, hex) -> (cas id hex, offset, size)
        nch, gofs, eofs = h[5], TOC_DATA + h[4], TOC_DATA + h[6]
        for k in range(nch):
            o = gofs + k * 20
            guid = d[o:o + 16][::-1].hex()      # the toc stores the GUID bytes reversed
            v = struct.unpack_from(">I", d, o + 16)[0]
            idx = (v & 0x00FFFFFF) // 4         # offset in 4-byte units into 16-byte entries
            if idx >= nch:
                raise FormatError("%s: chunk %d points at entry %d of %d" % (path, k, idx, nch))
            e = eofs + idx * 16
            self.chunks[guid] = (d[e:e + 8].hex(), *struct.unpack_from(">II", d, e + 8))
        # A cas id is 8 bytes: bytes 2..5 name the install package, the last 2 the cas file number. The pack's own
        # package is the one its bundles use; a few chunks point into base-game packages (other folders).
        self.package = {loc[0][:12] for b in self.bundles for loc in b}
        if len(self.package) != 1:
            raise FormatError("%s: bundles use %d cas packages" % (path, len(self.package)))
        self.package = self.package.pop()

    def own(self, cas_id):
        return cas_id[:12] == self.package

    def _inline_bundle(self, d, o, size, i):
        bd = d[o:o + size]
        hdr = struct.unpack_from(">9I", bd, 0)
        flags_off, count, p = hdr[2], hdr[3], hdr[4]
        flags = bd[flags_off:flags_off + count]
        if len(flags) != count:
            raise FormatError("%s: bundle %d flag table truncated" % (self.path, i))
        out, cas = [], None
        for k in range(count):
            fl = flags[k]
            if fl not in (0x00, 0x84):
                raise FormatError("%s: bundle %d entry %d has unknown flags %#x" % (self.path, i, k, fl))
            if fl & 0x80:
                cas = bd[p:p + 8].hex()
                p += 8
            if cas is None:
                raise FormatError("%s: bundle %d entry %d has no cas id" % (self.path, i, k))
            off, sz = struct.unpack_from(">II", bd, p)
            p += 8
            out.append((cas, off, sz))
        if p != flags_off:
            raise FormatError("%s: bundle %d location table ends at %#x, flags at %#x" % (self.path, i, p, flags_off))
        return out


def cas_path(toc_path, cas_id):
    """A downloaded pack keeps its cas files in the folder named like its toc: <stem>\\cas_NN.cas (NN = the cas id's
    low 16 bits)."""
    stem = os.path.splitext(toc_path)[0]
    return os.path.join(stem, "cas_%02d.cas" % int(cas_id[-4:], 16))


class CasReader:
    def __init__(self, oodle):
        self.oodle = oodle
        self.files = {}

    def raw(self, toc_path, loc):
        cas, off, size = loc
        p = cas_path(toc_path, cas)
        f = self.files.get(p)
        if f is None:
            if not os.path.isfile(p):
                raise FormatError("cas file not found: %s" % p)
            f = self.files[p] = open(p, "rb")
        f.seek(off)
        data = f.read(size)
        if len(data) != size:
            raise FormatError("%s: short read at %d" % (p, off))
        return data

    def read(self, toc_path, loc):
        return read_blocks(self.raw(toc_path, loc), self.oodle)

    def close(self):
        for f in self.files.values():
            f.close()


# ----------------------------------------------------------------------------------------------- binary bundle
def _cstr(d, o):
    e = d.index(b"\0", o)
    return d[o:e].decode("utf-8", "replace")


def parse_binary_bundle(blob):
    """Manifest: u32 BE size, u32 LE magic, 7 x u32 LE (total, ebx, res, chunk counts, strings offset, meta offset,
    meta size), sha1 x total, ebx (name, size), res (name, size), res types, res meta (16 bytes), res ids, chunks."""
    if len(blob) < 0x24:
        raise FormatError("bundle manifest too short")
    magic = struct.unpack_from("<I", blob, 4)[0]
    if magic not in BUNDLE_MAGICS:
        raise FormatError("bundle magic %#x not supported" % magic)
    total, ne, nr, nc, so, _mo, _ms = struct.unpack_from("<7I", blob, 8)
    if total != ne + nr + nc:
        raise FormatError("bundle counts %d != %d + %d + %d" % (total, ne, nr, nc))
    p = 0x24 + 20 * total
    strings = 4 + so
    ebx, res, chunks = [], [], []
    for _ in range(ne):
        no, osz = struct.unpack_from("<II", blob, p)
        ebx.append({"name": _cstr(blob, strings + no), "size": osz})
        p += 8
    for _ in range(nr):
        no, osz = struct.unpack_from("<II", blob, p)
        res.append({"name": _cstr(blob, strings + no), "size": osz})
        p += 8
    for r in res:
        r["type"] = struct.unpack_from("<I", blob, p)[0]
        p += 4
    for r in res:
        r["meta"] = blob[p:p + 16].hex()
        p += 16
    for r in res:
        r["rid"] = struct.unpack_from("<Q", blob, p)[0]
        p += 8
    for _ in range(nc):
        chunks.append({"guid": blob[p:p + 16].hex(), "offset": struct.unpack_from("<I", blob, p + 16)[0],
                       "size": struct.unpack_from("<I", blob, p + 20)[0]})
        p += 24
    return {"ebx": ebx, "res": res, "chunks": chunks}


# --------------------------------------------------------------------------------------------------------- SBle
def _u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def _u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def _ints(d, o, n, width):
    fmt = {1: "B", 2: "H", 4: "I"}.get(width)
    if fmt is None:
        raise FormatError("value width %d not supported" % width)
    if o + n * width > len(d):
        raise FormatError("array at %#x (%d x %d) runs past the end" % (o, n, width))
    return struct.unpack_from("<%d%s" % (n, fmt), d, o)


class DataSet:
    """One DSET of an SBle resource.

    +0x00 'DSET' (stored 'TESD'), +0x04 header size, +0x08 name hash, +0x0C type hash, +0x38 rows,
    +0x3C u16 fields, +0x3E u16 indexes, +0x40 u16 field table, +0x42 u16 index table, +0x44 u32 parameter table
    (offsets from the DSET). Pointers are stored as (u32 offset from the resource start, u32 next pointer to fix up).
    field (0x18): hash, flags (bits 24..31 = value width in bytes, 0 = constant), base, 0, data pointer;
                  a value is (base + stored) mod 2^32, a float field (flags low byte 5) is that as float bits.
    index (0x20): prefix-sum array pointer, 16 bytes 0, u32, u32 (bits 24..31 = parameter count, low byte = first
                  parameter); keys run over the product of the parameters' value sets in parameter order (row-major,
                  the first parameter outermost); rows of key k are [prefix[k], prefix[k+1]).
    parameter (0x18): hash, flags (low 24 bits = value count, bits 24..31 = width of the sorted value list),
                  min, max, value list pointer (values are min + stored; no list = every value min..max).
    """

    def __init__(self, d, o):
        if d[o:o + 4] != b"TESD":
            raise FormatError("no DSET at %#x" % o)
        self.offset = o
        self.size, self.name, self.type = _u32(d, o + 4), _u32(d, o + 8), _u32(d, o + 12)
        self.rows = _u32(d, o + 0x38)
        nf, ni = _u16(d, o + 0x3C), _u16(d, o + 0x3E)
        fo, io, po = _u16(d, o + 0x40), _u16(d, o + 0x42), _u32(d, o + 0x44)
        self.fields = {}
        self.field_flags = {}
        for k in range(nf):
            h, fl, base, _pad, ptr = struct.unpack_from("<5I", d, o + fo + k * 0x18)
            width = fl >> 24
            if ptr == 0 or width == 0:
                vals = [base] * self.rows
            else:
                vals = [(base + x) & 0xFFFFFFFF for x in _ints(d, ptr, self.rows, width)]
            if (fl & 0xFF) == 5:
                vals = [struct.unpack("<f", struct.pack("<I", v))[0] for v in vals]
            self.fields[h] = vals
            self.field_flags[h] = fl
        self.indexes = []
        for k in range(ni):
            e = o + io + k * 0x20
            ptr = _u32(d, e)
            sel = _u32(d, e + 0x1C)
            first, count = sel & 0xFF, sel >> 24
            params = [self._param(d, o + po + (first + j) * 0x18) for j in range(count)]
            self.indexes.append({"ptr": ptr, "params": params})
        self._d = d

    def _param(self, d, o):
        h, fl, mn, mx, ptr = struct.unpack_from("<5I", d, o)
        n, width = fl & 0xFFFFFF, fl >> 24
        if ptr:
            vals = [mn + x for x in _ints(d, ptr, n, width)]
        else:
            vals = list(range(mn, mx + 1))
            if len(vals) != n:
                raise FormatError("parameter %s: %d values for %d..%d" % (hname(h), n, mn, mx))
        if vals and (vals[0] != mn or vals[-1] != mx or any(b <= a for a, b in zip(vals, vals[1:]))):
            raise FormatError("parameter %s: value list not sorted from min to max" % hname(h))
        return {"hash": h, "name": hname(h), "values": vals}

    def row_keys(self, index=None):
        """{row: {parameter name: value}} through the index with the most parameters (or the one given)."""
        if index is None:
            index = max(self.indexes, key=lambda x: len(x["params"]))
        params = index["params"]
        total = 1
        for p in params:
            total *= len(p["values"])
        if index["ptr"] == 0:
            if len(params) != 1 or total != self.rows:
                raise FormatError("DSET %s: index without data is not an identity" % hname(self.name))
            return {r: {params[0]["name"]: params[0]["values"][r]} for r in range(self.rows)}
        width = 2 if self.rows <= 0xFFFF else 4
        pref = _ints(self._d, index["ptr"], total + 1, width)
        if pref[0] != 0 or pref[-1] != self.rows or any(b < a for a, b in zip(pref, pref[1:])):
            raise FormatError("DSET %s: index is not a prefix sum over %d rows" % (hname(self.name), self.rows))
        out = {}
        sizes = [len(p["values"]) for p in params]
        for k in range(total):
            a, b = pref[k], pref[k + 1]
            if a == b:
                continue
            key, rest = {}, k
            for p, s in zip(reversed(params), reversed(sizes)):
                key[p["name"]] = p["values"][rest % s]
                rest //= s
            for r in range(a, b):
                if r in out:
                    raise FormatError("DSET %s: row %d has two keys" % (hname(self.name), r))
                out[r] = key
        if len(out) != self.rows:
            raise FormatError("DSET %s: index covers %d of %d rows" % (hname(self.name), len(out), self.rows))
        return out


def parse_sble(d):
    """SBle resource: 'SBle', u32 size, u16 data set count, ..., +0x18 pointer to the data set pointer array."""
    if d[:4] != b"SBle":
        raise FormatError("not an SBle resource (%s)" % d[:4].hex())
    n, arr = _u16(d, 8), _u32(d, 0x18)
    sets = [DataSet(d, _u32(d, arr + 8 * k)) for k in range(n)]
    return {hname(s.name): s for s in sets}


# ---------------------------------------------------------------------------------------------------- the bank
class Bank:
    def __init__(self, game, lang):
        self.game, self.lang = game, lang
        self.oodle = Oodle(game)
        self.cas = CasReader(self.oodle)
        self.tocs = []
        for kind in ("commentaryfull", "commentarylaunch"):
            p = os.path.join(game, "commentary", "%s_%s.toc" % (kind, lang))
            if os.path.isfile(p):
                self.tocs.append(Toc(p))
        if not self.tocs:
            raise FormatError("no downloaded commentary pack for %s under %s\\commentary (only downloaded packs are "
                              "supported: the base game's eng_us keeps its cas data in the install chunks)" % (lang, game))
        self.assets = {}                  # name -> {"ebx": (toc path, loc), "res": (toc path, loc, meta)}
        for toc in self.tocs:
            for entries in toc.bundles:
                man = parse_binary_bundle(self.cas.raw(toc.path, entries[0]))     # the manifest is stored as is
                ne, nr, nc = len(man["ebx"]), len(man["res"]), len(man["chunks"])
                if len(entries) != 1 + ne + nr + nc:
                    raise FormatError("%s: bundle has %d locations for %d assets" % (toc.path, len(entries), ne + nr + nc))
                for i, e in enumerate(man["ebx"]):
                    self.assets.setdefault(e["name"], {})["ebx"] = (toc.path, entries[1 + i])
                for i, r in enumerate(man["res"]):
                    self.assets.setdefault(r["name"], {})["res"] = (toc.path, entries[1 + ne + i], r)

    def family_name(self, family):
        return "sound/speech/loccommentary/%s/soundwaves/%s_full/%s" % (self.lang, self.lang, family)

    def families(self):
        pre = "sound/speech/loccommentary/%s/soundwaves/%s_full/" % (self.lang, self.lang)
        return sorted(n[len(pre):] for n, a in self.assets.items() if n.startswith(pre) and "res" in a)

    def load_family(self, family):
        name = self.family_name(family)
        a = self.assets.get(name)
        if not a or "res" not in a or "ebx" not in a:
            raise FormatError("family %s not found in the %s bank" % (family, self.lang))
        toc_path, loc, r = a["res"]
        res = self.cas.read(toc_path, loc)
        if len(res) != r["size"]:
            raise FormatError("%s: res is %d bytes, the manifest says %d" % (family, len(res), r["size"]))
        ebx = self.cas.read(*a["ebx"])
        return {"name": name, "res": res, "ebx": ebx, "sets": parse_sble(res)}

    def chunk_guids(self, ebx):
        """GUIDs of the toc chunks this EBX refers to, in the order they appear."""
        found = []
        for toc in self.tocs:
            for o in range(0, len(ebx) - 15):
                g = ebx[o:o + 16].hex()
                if g in toc.chunks and g not in [x[0] for x in found]:
                    found.append((g, toc))
        return found

    def close(self):
        self.cas.close()


def selection_rows(fam, selector):
    """The family's selection rows: [{segment, variation, <selector>, cm_sim, player_intensity}]."""
    sel = fam["sets"].get("Selection")
    if sel is None:
        raise FormatError("%s: no Selection data set" % fam["name"])
    if H_SEGMENT not in sel.fields or djb2x("VariationId") not in sel.fields:
        raise FormatError("%s: Selection has no SegmentID / VariationId column" % fam["name"])
    keys = sel.row_keys()
    seg, var = sel.fields[H_SEGMENT], sel.fields[djb2x("VariationId")]
    rows = []
    for r in range(sel.rows):
        k = keys[r]
        if selector not in k:
            raise FormatError("%s: Selection is not keyed by %s (%s)" % (fam["name"], selector, list(k)))
        rows.append({"segment": seg[r], "variation": var[r], "id": k[selector], "cm_sim": k.get("cm_sim"),
                     "intensity": k.get("player_intensity")})
    return rows


def merge_rows(rows, id_key):
    """One output row per (segment, variation, id), with the cm_sim / player_intensity values it is selected for."""
    acc = {}
    for r in rows:
        k = (r["segment"], r["variation"], r["id"])
        a = acc.setdefault(k, {"cm_sim": set(), "intensity": set()})
        if r["cm_sim"] is not None:
            a["cm_sim"].add(r["cm_sim"])
        if r["intensity"] is not None:
            a["intensity"].add(r["intensity"])
    out = []
    for (s, v, i), a in sorted(acc.items(), key=lambda x: (x[0][2], x[0][0])):
        out.append({"segment": s, "variation": v, id_key: i, "intensity": sorted(a["intensity"]),
                    "cm_sim": sorted(a["cm_sim"])})
    return out


def check_variations(fam, rows):
    """The Variations data set (one row per segment index here) must give the same VariationId as the Selection."""
    v = fam["sets"].get("Variations")
    if v is None:
        return "no Variations data set"
    vid = v.fields.get(djb2x("VariationId"))
    bad = sum(1 for r in rows if not (0 <= r["segment"] < v.rows and vid[r["segment"]] == r["variation"]))
    return "ok" if bad == 0 else "%d rows disagree with Variations" % bad


# -------------------------------------------------------------------------------------------------------- audio
def segment_streams(bank, fam):
    """[(segment, stream bytes)] for every segment of the family, from its chunk(s)."""
    segs = fam["sets"].get("Segments")
    chunks = fam["sets"].get("Chunks")
    if segs is None or H_SEG_OFFSET not in segs.fields:
        raise FormatError("%s: no Segments data set with offsets" % fam["name"])
    guids = bank.chunk_guids(fam["ebx"])
    nchunks = chunks.rows if chunks is not None else 1
    if len(guids) != nchunks:
        raise FormatError("%s: the EBX names %d chunks, the Chunks data set %d" % (fam["name"], len(guids), nchunks))
    sizes = chunks.fields.get(djb2x("ChunkSize")) if chunks is not None else None
    datas = []
    for k, (g, toc) in enumerate(guids):
        if not toc.own(toc.chunks[g][0]):
            raise FormatError("%s: chunk %s lives in another install package (%s)" % (fam["name"], g, toc.chunks[g][0]))
        data = bank.cas.read(toc.path, toc.chunks[g])
        if sizes is not None and sizes[k] != len(data):
            raise FormatError("%s: chunk %s is %d bytes, ChunkSize says %d" % (fam["name"], g, len(data), sizes[k]))
        datas.append(data)
    offs = segs.fields[H_SEG_OFFSET]
    cidx = segs.fields.get(H_SEG_CHUNK, [0] * segs.rows)
    out = []
    for s in range(segs.rows):
        d = datas[cidx[s]]
        o = offs[s] & ~3
        out.append((s, d, o))
    return out


def parse_sns(d, o):
    """EA SNS stream at o: H block (SNR header), D blocks (u32 BE samples + Opus packet), E block. Returns the header
    and the [(samples, packet)] list."""
    if d[o] != 0x48:
        raise FormatError("no SNS header at %#x" % o)
    hs = int.from_bytes(d[o + 1:o + 4], "big")
    w0, w1 = struct.unpack_from(">II", d, o + 4)
    head = {"version": w0 >> 28, "codec": (w0 >> 24) & 0xF, "channels": ((w0 >> 18) & 0x3F) + 1,
            "rate": w0 & 0x3FFFF, "type": w1 >> 30, "samples": w1 & 0x1FFFFFFF}
    p = o + hs
    packets = []
    while True:
        t = d[p]
        bs = int.from_bytes(d[p + 1:p + 4], "big")
        if t == 0x45:
            break
        if t != 0x44 or bs < 8:
            raise FormatError("bad SNS block %#x (size %d) at %#x" % (t, bs, p))
        packets.append((struct.unpack_from(">I", d, p + 4)[0], d[p + 8:p + bs]))
        p += bs
    return head, packets


def opus_packet_samples(pkt):
    """Decoded length at 48 kHz of an Opus packet, from its TOC byte (RFC 6716 3.1)."""
    toc = pkt[0]
    config = toc >> 3
    if config < 12:
        per = (480, 960, 1920, 2880)[config & 3]
    elif config < 16:
        per = (480, 960)[config & 1]
    else:
        per = (120, 240, 480, 960)[config & 3]
    c = toc & 3
    frames = 1 if c == 0 else 2 if c in (1, 2) else (pkt[1] & 0x3F)
    return per * frames


_CRC = []
for _i in range(256):
    _r = _i << 24
    for _ in range(8):
        _r = ((_r << 1) ^ 0x04C11DB7) if _r & 0x80000000 else (_r << 1)
    _CRC.append(_r & 0xFFFFFFFF)


def ogg_crc(data):
    crc = 0
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC[((crc >> 24) ^ b) & 0xFF]
    return crc


def _ogg_page(serial, seq, granule, flags, packets):
    lace = bytearray()
    for p in packets:
        n = len(p)
        lace += b"\xff" * (n // 255) + bytes([n % 255])
    if len(lace) > 255:
        raise ValueError("too many segments for one page")
    head = struct.pack("<4sBBqIII", b"OggS", 0, flags, granule, serial, seq, 0) + bytes([len(lace)]) + bytes(lace)
    page = bytearray(head + b"".join(packets))
    struct.pack_into("<I", page, 22, ogg_crc(page))
    return bytes(page)


def ogg_opus(head, packets, serial=0x54524230):
    """Wrap the stream's Opus packets in an Ogg Opus file (RFC 7845). The first block's sample count is the packet's
    length minus the encoder delay, which becomes the pre-skip; the last page's granule trims the end."""
    if head["channels"] != 1:
        raise FormatError("only mono streams are handled (%d channels)" % head["channels"])
    pre_skip = opus_packet_samples(packets[0][1]) - packets[0][0]
    total = sum(s for s, _ in packets)
    opus_head = struct.pack("<8sBBHIhB", b"OpusHead", 1, 1, pre_skip, head["rate"], 0, 0)
    vendor = b"fc27_commentary"
    tags = struct.pack("<8sI", b"OpusTags", len(vendor)) + vendor + struct.pack("<I", 0)
    pages = [_ogg_page(serial, 0, 0, 0x02, [opus_head]), _ogg_page(serial, 1, 0, 0x00, [tags])]
    seq, gran, batch = 2, 0, []
    for i, (_s, pkt) in enumerate(packets):
        gran += opus_packet_samples(pkt)
        batch.append(pkt)
        last = i == len(packets) - 1
        if last or len(batch) == 50:
            g = pre_skip + total if last else gran
            pages.append(_ogg_page(serial, seq, g, 0x04 if last else 0x00, batch))
            seq, batch = seq + 1, []
    return b"".join(pages), total


def decode_wav(ffmpeg, ogg, path, expected):
    r = subprocess.run([ffmpeg, "-v", "error", "-f", "ogg", "-i", "pipe:0", "-f", "s16le", "-ac", "1", "-ar",
                        str(OPUS_RATE), "pipe:1"], input=ogg, capture_output=True)
    if r.returncode != 0:
        return "ffmpeg failed: %s" % r.stderr.decode("utf-8", "replace").strip()[:200]
    pcm = r.stdout
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OPUS_RATE)
        w.writeframes(pcm)
    got = len(pcm) // 2
    return None if got == expected else "%d samples decoded, the stream says %d" % (got, expected)


def write_wavs(bank, fam, prefix, folder, ffmpeg, segments_wanted, log):
    os.makedirs(folder, exist_ok=True)
    jobs, problems = [], []
    for s, d, o in segment_streams(bank, fam):
        if s not in segments_wanted:
            continue
        head, packets = parse_sns(d, o)
        if head["codec"] not in (0x0C, 0x0E) or head["rate"] != OPUS_RATE:
            problems.append("segment %d: codec %d rate %d not handled" % (s, head["codec"], head["rate"]))
            continue
        ogg, total = ogg_opus(head, packets)
        if total != head["samples"]:
            problems.append("segment %d: blocks hold %d samples, the header says %d" % (s, total, head["samples"]))
        base = os.path.join(folder, "%s_%d_%d" % (prefix, s, s))
        if ffmpeg:
            jobs.append((ogg, base + ".wav", total, s))
        else:
            with open(base + ".opus", "wb") as f:
                f.write(ogg)
    if jobs:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(2, (os.cpu_count() or 4))) as ex:
            futs = {ex.submit(decode_wav, ffmpeg, o, p, n): s for o, p, n, s in jobs}
            for f in concurrent.futures.as_completed(futs):
                err = f.result()
                if err:
                    problems.append("segment %d: %s" % (futs[f], err))
    log("  %s: %d segments written to %s%s" % (prefix, len(jobs) if ffmpeg else len(segments_wanted), folder,
                                                  "" if ffmpeg else " as .opus (no ffmpeg)"))
    return problems


# ---------------------------------------------------------------------------------------------------- the run
def game_build(game):
    """The installer's <gameVersion version="1.0.140.64835" /> (FC27.exe's own version resource says 1.0.0.0)."""
    try:
        with open(os.path.join(game, "__Installer", "installerdata.xml"), "r", encoding="utf-8", errors="replace") as f:
            m = re.search(r'<gameVersion\s+version="([0-9.]+)"', f.read())
        if m:
            return m.group(1)
    except OSError:
        pass
    exe = os.path.join(game, "FC27.exe")
    try:
        ver = ctypes.WinDLL("version.dll")
        n = ver.GetFileVersionInfoSizeW(exe, None)
        if not n:
            return None
        buf = ctypes.create_string_buffer(n)
        if not ver.GetFileVersionInfoW(exe, 0, n, buf):
            return None
        p, ln = ctypes.c_void_p(), ctypes.c_uint()
        if not ver.VerQueryValueW(buf, "\\", ctypes.byref(p), ctypes.byref(ln)):
            return None
        ms, ls = struct.unpack_from("<II", ctypes.string_at(p.value, ln.value), 8)
        return "%d.%d.%d.%d" % (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF)
    except (OSError, AttributeError):
        return None


def source_files(bank):
    out = []
    for toc in bank.tocs:
        paths = [toc.path] + sorted({cas_path(toc.path, loc[0]) for b in toc.bundles for loc in b} |
                                    {cas_path(toc.path, c[0]) for c in toc.chunks.values() if toc.own(c[0])})
        for p in paths:
            st = os.stat(p)
            out.append({"path": p, "size": st.st_size,
                        "modified": datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds")})
    return out


def run(a, log=print):
    bank = Bank(a.game, a.lang)
    try:
        if a.list:
            for f in bank.families():
                log(f)
            return 0
        result = {"turbo_bank": 1, "lang": a.lang, "game_build": game_build(a.game), "source_files": source_files(bank)}
        notes, raw = [], {}
        for key, (family, selector, _prefix) in FAMILIES.items():
            fam = bank.load_family(family)
            rows = selection_rows(fam, selector)
            raw[key] = (fam, rows)
            id_key = "commentaryid" if selector == "surname_ID" else "playerid"
            merged = merge_rows(rows, id_key)
            result[key] = [{"segment": r["segment"], "variation": r["variation"], id_key: r[id_key],
                            "intensity": r["intensity"], "cm_sim": r["cm_sim"]} for r in merged]
            sel = fam["sets"]["Selection"]
            params = [p["name"] for p in max(sel.indexes, key=lambda x: len(x["params"]))["params"]]
            ids = {r[id_key] for r in merged}
            segs = {r["segment"] for r in merged}
            notes.append("%s (%s): %d selection rows keyed by %s -> %d unique (segment, variation, %s) rows, %d ids, "
                         "%d segments of %d; Variations check: %s" % (
                             key, fam["name"].rsplit("/", 1)[1], sel.rows, ", ".join(params), len(merged), selector,
                             len(ids), len(segs), fam["sets"]["Segments"].rows, check_variations(fam, rows)))
            log("%s: %d rows, %d ids" % (key, len(merged), len(ids)))
        result["wav_dir"] = None
        if a.wav:
            ffmpeg = a.ffmpeg or shutil.which("ffmpeg")
            if not ffmpeg:
                notes.append("no ffmpeg found: the audio was written as Ogg Opus (.opus), not wav")
            problems = []
            for key in WAV_FAMILIES:
                fam, rows = raw[key]
                prefix = FAMILIES[key][2]
                folder = os.path.join(a.wav, key)
                problems += write_wavs(bank, fam, prefix, folder, ffmpeg, {r["segment"] for r in rows}, log)
            result["wav_dir"] = os.path.abspath(a.wav)
            notes.append("audio: EA SNS streams, codec %s, decoded with %s; %d problems%s" % (
                "EA Opus (mono, 48 kHz)", ffmpeg or "nothing (Ogg Opus files)", len(problems),
                (": " + "; ".join(problems[:20])) if problems else ""))
        result["notes"] = notes
        if a.out:
            os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
            with open(a.out, "w", encoding="utf-8") as f:
                json.dump(result, f, indent=1)
                f.write("\n")
            log("written: %s" % a.out)
        for n in notes:
            log("note: " + n)
        return 0
    finally:
        bank.close()


# ------------------------------------------------------------------------------------------------------ self-test
def _synthetic_sble():
    """A tiny SBle with one Selection DSET (3 rows, 2 fields, an index over cm_sim x surname_ID)."""
    d = bytearray(0x400)
    d[0:4] = b"SBle"
    struct.pack_into("<H", d, 8, 1)
    struct.pack_into("<I", d, 0x18, 0x50)
    struct.pack_into("<II", d, 0x50, 0x70, 0)
    o = 0x70
    d[o:o + 4] = b"TESD"
    struct.pack_into("<III", d, o + 4, 0x100, djb2x("Selection"), 0)
    struct.pack_into("<I", d, o + 0x38, 3)
    struct.pack_into("<HHHHI", d, o + 0x3C, 2, 1, 0x48, 0x78, 0x98)
    struct.pack_into("<5I", d, o + 0x48, H_SEGMENT, 0x02000202, 0, 0, 0x200)
    struct.pack_into("<5I", d, o + 0x60, djb2x("VariationId"), 0x04000202, 1000, 0, 0x210)
    struct.pack_into("<I", d, o + 0x78, 0x220)
    struct.pack_into("<I", d, o + 0x78 + 0x1C, 0x02000000)
    struct.pack_into("<5I", d, o + 0x98, djb2x("cm_sim"), 2, 0, 1, 0)
    struct.pack_into("<5I", d, o + 0xB0, djb2x("surname_ID"), 0x04000002, 900002, 900762, 0x230)
    struct.pack_into("<3H", d, 0x200, 931, 1419, 7)
    struct.pack_into("<3I", d, 0x210, 5, 6, 9)
    struct.pack_into("<5H", d, 0x220, 0, 0, 2, 2, 3)      # (0,900002): -, (0,900762): rows 0-1, (1,900002): -, (1,900762): 2
    struct.pack_into("<2I", d, 0x230, 0, 760)
    return bytes(d)


def self_test(game, log=print):
    fails = []

    def check(name, cond):
        log(("  ok   " if cond else "  FAIL ") + name)
        if not cond:
            fails.append(name)

    log("synthetic:")
    check("djb2x names match the bank's hashes", djb2x("surname_ID") == 0xDE127DE4 and
          djb2x("player_db_pID") == 0x73D7AD2D and djb2x("VariationId") == 0xF5F914D9 and djb2x("cm_sim") == 0x5BE02103)
    sets = parse_sble(_synthetic_sble())
    sel = sets.get("Selection")
    check("SBle: one Selection data set with 3 rows", sel is not None and sel.rows == 3)
    rows = selection_rows({"name": "synthetic", "sets": sets}, "surname_ID")
    check("SBle: rows decoded through the prefix-sum index",
          [(r["segment"], r["variation"], r["id"], r["cm_sim"]) for r in rows] ==
          [(931, 1005, 900762, 0), (1419, 1006, 900762, 0), (7, 1009, 900762, 1)])
    merged = merge_rows(rows + rows, "commentaryid")
    check("merge: one row per (segment, variation, id)", len(merged) == 3 and merged[0]["commentaryid"] == 900762)
    blocks = struct.pack(">I", 5) + struct.pack("<H", 0x7000) + struct.pack(">H", 5) + b"hello" + \
        struct.pack(">I", 3) + struct.pack("<H", 0x7000) + struct.pack(">H", 3) + b"abc"
    check("blocks: stored blocks are joined", read_blocks(blocks, None) == b"helloabc")
    try:
        read_blocks(struct.pack(">I", 4) + struct.pack("<H", 0x700F) + struct.pack(">H", 4) + b"zstd", None)
        check("blocks: an unknown compression type is refused", False)
    except FormatError:
        check("blocks: an unknown compression type is refused", True)
    check("ogg: CRC of 'OggS' page sample", ogg_crc(b"123456789") == 0x89A1897F)
    check("opus: TOC 0x78 (hybrid FB 20 ms, one frame) = 960 samples", opus_packet_samples(b"\x78\x00") == 960)
    head = {"channels": 1, "rate": 48000}
    ogg, total = ogg_opus(head, [(648, b"\x78" + b"\0" * 10), (960, b"\x78" + b"\0" * 12)])
    check("ogg: pre-skip 312, total 1608, two header pages + one data page",
          total == 1608 and ogg.count(b"OggS") == 3 and struct.unpack_from("<H", ogg, 28 + 10)[0] == 312)

    if game and os.path.isfile(os.path.join(game, "commentary", "commentaryfull_ita_it.toc")):
        log("on disk (ita_it, %s):" % game)
        bank = Bank(game, "ita_it")
        try:
            gen = merge_rows(selection_rows(bank.load_family("psimple_surname"), "surname_ID"), "commentaryid")
            by = {}
            for r in gen:
                by.setdefault(r["commentaryid"], set()).add((r["segment"], r["variation"]))
            check("Bianchi 900762 = segments 931, 1419, 1420 (FC 26 variations 1562356, 1574681, 1574682)",
                  by.get(900762) == {(931, 1562356), (1419, 1574681), (1420, 1574682)})
            check("Pirlo 926385 = segment 1696, variation 4876753", by.get(926385) == {(1696, 4876753)})
            check("Del Piero 922149 = variation 5772903", {v for _, v in by.get(922149, ())} == {5772903})
            fam = bank.load_family("pplayer_names_simple")
            real = merge_rows(selection_rows(fam, "player_db_pID"), "playerid")
            pids = {r["playerid"] for r in real}
            check("pPLAYER_NAMES_SIMPLE has rows keyed by player ids", len(real) > 1000 and 216435 in pids)
            check("Gutierrez 261865 has his own recordings (segments 5041, 5042)",
                  {r["segment"] for r in real if r["playerid"] == 261865} == {5041, 5042})
            check("Lobotka 216435 = variation 1605821 (FC 26's recording)",
                  {r["variation"] for r in real if r["playerid"] == 216435} == {1605821})
            s, d, o = segment_streams(bank, fam)[0]
            head, packets = parse_sns(d, o)
            check("segment 0 is an EA Opus SNS stream, mono 48 kHz, samples = header",
                  head["rate"] == 48000 and head["channels"] == 1 and sum(x for x, _ in packets) == head["samples"])
            ffmpeg = shutil.which("ffmpeg")
            if ffmpeg:
                streams = segment_streams(bank, bank.load_family("psimple_surname"))
                _s, d, o = streams[931]
                head, packets = parse_sns(d, o)
                ogg, total = ogg_opus(head, packets)
                tmp = os.path.join(os.environ.get("TEMP", "."), "fc27_commentary_selftest.wav")
                err = decode_wav(ffmpeg, ogg, tmp, total)
                check("ffmpeg decodes generic segment 931 to 30703 samples (FC 26's wav length)",
                      err is None and total == 30703)
                try:
                    os.remove(tmp)
                except OSError:
                    pass
            else:
                log("  skip ffmpeg decode (no ffmpeg on PATH)")
        finally:
            bank.close()
    else:
        log("on disk: skipped (no ita_it pack under %s)" % game)
    log("self-test: %s" % ("ok" if not fails else "%d failed" % len(fails)))
    return 0 if not fails else 1


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lang", help="commentary language as the game names its pack, e.g. ita_it")
    ap.add_argument("--game", default=DEFAULT_GAME, help="game folder (default: %(default)s)")
    ap.add_argument("--out", help="output json (turbo_bank 1)")
    ap.add_argument("--wav", help="folder for the audio: <wav>\\generic and <wav>\\real")
    ap.add_argument("--ffmpeg", help="ffmpeg.exe to decode the Opus audio (default: the one on PATH)")
    ap.add_argument("--list", action="store_true", help="list the language's soundwave families and stop")
    ap.add_argument("--self-test", action="store_true", help="run the built-in checks (and the ita_it ones on disk)")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test(a.game)
    if not a.lang:
        ap.error("--lang is required")
    try:
        return run(a)
    except FormatError as e:
        print("error: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
