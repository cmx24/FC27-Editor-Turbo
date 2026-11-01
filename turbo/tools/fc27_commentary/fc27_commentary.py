#!/usr/bin/env python3
"""Extract the FC 27 commentary speech bank of one language from disk (track: commentary bank, docs/re/fc27-commentary-bank.md).

What it gives, for every INSTALLED commentary pack of a language. Installed = the pack is live in <game>\\Data\\Win32 or
<game>\\Patch\\Win32 (full toc > 4096 bytes, a non-empty superbundlelayout\\commentaryfull_<lang>\\cas_*.cas, the launch toc
present: the launcher's apply-script test). Only that decides, lists and counts a language. The data source is separate:
the bytes (toc parse, cas reads) come from the backup store <game>\\commentary (commentary{full,launch}_<lang>.toc + the
folder named like the toc) FIRST when it holds the kind, else from the live copy
(<game>\\{Patch,Data}\\Win32\\commentary{full,launch,licensed}_<lang>.toc, cas files found through
<game>\\{Data,Patch}\\layout.toc, e.g. Data\\Win32\\superbundlelayout\\commentaryfull_eng_us\\cas_01.cas). A backup copy
alone never makes a language installed. The source used is printed (data source: ...) and written to the bank json.

  * every row of the pSIMPLE_SURNAME selection data set: SegmentID, VariationId, surname_ID (= commentary id);
  * every row of pPLAYER_NAMES_SIMPLE (SegmentID, VariationId, player_db_pID = player id) and of pPLAYER_NAMES_LINK;
  * every row of pPLAYER_NAMES_HIGH, split by the selector each row is keyed by: player_db_pID -> real_high (player id),
    surname_ID -> generic_high (commentary id); FC 27 1.0.140 ita_it keys all of them by player_db_pID;
  * with --wav, each segment's audio as a 48 kHz 16-bit mono PCM wav, named like FIFA Editor Tool's exports
    (generic\\pSIMPLE_SURNAME_<seg>_<seg>.wav, real\\pPLAYER_NAMES_SIMPLE_<seg>_<seg>.wav,
    real_link\\pPLAYER_NAMES_LINK_<seg>_<seg>.wav, real_high\\ and generic_high\\pPLAYER_NAMES_HIGH_<seg>_<seg>.wav);
  * "family_summary": per family rows, unique (id, segment, variation), ids, segments, segments shared by several ids;
  * "other_player_families": a count (no audio) of the other p* families: selection rows and the ids of each *_pID
    selector they are keyed by.

Sets: a language's name families come in sets, <lang>_<kind> (loccommentary, e.g. ita_it_full, ita_it_demo) or
<lang>_<team>_<kind> (commentary, e.g. eng_us_dr_full, eng_us_gm_demo; team dr = Derek Rae, gm = Guy Mowbray;
kind full | demo | licensed). The rows above (generic, real, real_link, real_high, generic_high, --wav) are those of
the PRIMARY set (<lang>_full, else <lang>_<--team>_full, default team dr): segment numbers index one set's audio only,
so rows of different sets are never mixed. Every set is also read for its ids:
  * "sets": {set: {"team", "kind", "ids": {"generic", "simple", "link", "high", "generic_high"}, "summary", "errors"}};
    a family that cannot be read (FC 27 eng_us gm HIGH: "index without data is not an identity") is recorded in that
    set's "errors" and the run goes on;
  * "teams": {team: the same five id lists, the union of the team's sets} ("" = a language without teams);
  * "union": the same five id lists over every set; "primary_set": the set the rows come from.

Another set's recordings (--set, e.g. ita_it_demo: the players only a demo / licensed set records): its rows become the
output rows and its wavs go to the same folders under encoded segment numbers, set index * 100000 + segment (SET_INDEX:
full 0, licensed 1, demo 2, +3 for team gm; --seg-offset overrides), so they can sit beside the full set's wavs;
the json says "segment_offset" (turbo/tools/callname_coverage.py --bank adds it to the rows' segments).

usage: python fc27_commentary.py --lang ita_it [--game DIR] [--out FILE.json] [--wav DIR] [--ffmpeg EXE] [--list]
                                 [--team dr] [--set ita_it_demo [--seg-offset N]]
       python fc27_commentary.py --languages [--game DIR]
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
             and decodes them with ffmpeg (found on PATH or given with --ffmpeg). Codec 0x0E (ita_it, spa_es, ...) is one
             plain mono packet per block; codec 0x0F (eng_us) is an Opus multistream block of 2 uncoupled mono streams
             (RFC 6716 Appendix B: stream 1 self-delimited = the speech, stream 2 = the 3 silent bytes f8 ff fe): stream 1
             is turned back into a standard packet and decoded, stream 2 is checked to be that silence (split_streams,
             speech_packets).

Pure Python 3.11 standard library + ctypes. Read-only on the game folder.
"""
import argparse
import concurrent.futures
import ctypes
import datetime
import hashlib
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
# The SNR header's codec nibble, named as vgmstream names it (src/meta/ea_eaac.c, EAAC_CODEC_*, about lines 27-30; read online
# 2026-10-24, line numbers approximate): 0x0C EAOPUS, 0x0E EAOPUSM, 0x0F EAOPUSMU. FC 27 ita_it / spa_es / ... are 0x0E mono (one
# plain Opus packet per D block); FC 27 eng_us is 0x0F with 2 channels. vgmstream's EAOPUSMU case sets coupled_count = 0, i.e.
# `channels` UNCOUPLED mono streams per block (src/coding/ffmpeg_decoder_custom_opus.c, make_opus_header: stream count =
# channels - coupled), which are RFC 6716 Appendix B multistream packets: every stream but the last is self-delimited. Measured
# on the 7120 + 11792 + 4874 + 169 dr and 7508 + 1386 + 230 gm segments of FC 27 1.0.141.12554 (turbo_dev\evidence\2026-10-24\
# callnames-eng\codec15_survey.log): every block = stream 1 (TOC code 0, 1-2 byte length, frame; the speech, or a CELT silence
# frame inside a pause) + stream 2 (always the 3 bytes f8 ff fe = digital silence); block sample counts add up to the header's.
CODEC_EAOPUS, CODEC_EAOPUSM, CODEC_EAOPUSMU = 0x0C, 0x0E, 0x0F
SPEECH_CODECS = (CODEC_EAOPUS, CODEC_EAOPUSM, CODEC_EAOPUSMU)
SILENT_STREAM = b"\xf8\xff\xfe"     # the last (standard-framed) stream of every eng_us block

FAMILIES = {                        # output key -> (family name in the bank, selector parameter, wav prefix)
    "generic": ("psimple_surname", "surname_ID", "pSIMPLE_SURNAME"),
    "real": ("pplayer_names_simple", "player_db_pID", "pPLAYER_NAMES_SIMPLE"),
    "real_link": ("pplayer_names_link", "player_db_pID", "pPLAYER_NAMES_LINK"),
}
# pPLAYER_NAMES_HIGH: each row goes to the output key of the selector its key holds (both keys when it holds both)
HIGH_FAMILY = "pplayer_names_high"
HIGH = {"real_high": ("player_db_pID", "pPLAYER_NAMES_HIGH"), "generic_high": ("surname_ID", "pPLAYER_NAMES_HIGH")}
WAV_FAMILIES = ("generic", "real", "real_link", "real_high", "generic_high")
NAME_FAMILIES = ("psimple_surname", "pplayer_names_simple", "pplayer_names_link", HIGH_FAMILY)


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
    "ChunkSize", "ChunkId", "ChunkIndex", "cm_sim", "surname_ID", "player_db_pID", "player_intensity",
    # the other player-id parameter names of the image (docs/re/inmatch-callnames.json player_param_names) and the
    # group selectors next to them
    "assist_player_pID", "challenger_pID", "keeper_pID", "marker_player_pID", "pass_from_pID", "passer_db_pID",
    "player_db_pID_A", "player_db_pID_B", "player_target_pID", "possessor_pID", "ptw_player_b_db_pID",
    "ptw_player_db_pID", "receiver_db_pID", "replay_defender_db_pID", "replay_keeper_db_pID", "replay_shot_taker_db_pID",
    "replay_starter_player_db_pID", "shot_taker_pID", "storyline_player_db_pID", "sub_off_pID", "sub_on_pID",
    "target_player_pID", "tr_player_db_pID", "ts_def_C_db_pID", "ts_fwd_C_db_pID", "ts_mid_C_db_pID",
    "ut_ms_player_db_pID", "player_db_gID", "team_gID", "opposite_team_gID")}
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


# ------------------------------------------------------------------------------------------------ layout.toc
def _varint(d, p):
    r = s = 0
    while True:
        b = d[p]
        p += 1
        r |= (b & 0x7F) << s
        s += 7
        if not b & 0x80:
            return r, p


def read_dbobject(d, p=0):
    """Frostbite DbObject (binary object) value at p: (name, value, end). Types: 1 list, 2 object, 6 bool, 7 string,
    8 i32, 9 i64, 0xB f32, 0xC f64, 0xF guid, 0x10 sha1, 0x13 blob; bit 7 = no name."""
    t = d[p]
    p += 1
    if t == 0:
        return None, None, p
    name = None
    if not t & 0x80:
        e = d.index(b"\0", p)
        name = d[p:e].decode("utf-8", "replace")
        p = e + 1
    tt = t & 0x1F
    if tt in (1, 2):
        size, p = _varint(d, p)
        end = p + size
        out = [] if tt == 1 else {}
        while p < end:
            n, v, p2 = read_dbobject(d, p)
            if n is None and v is None:
                break
            if tt == 1:
                out.append(v)
            else:
                out[n] = v
            p = p2
        return name, out, end
    if tt == 6:
        return name, bool(d[p]), p + 1
    if tt == 7:
        n, p = _varint(d, p)
        return name, d[p:p + n - 1].decode("utf-8", "replace"), p + n
    fixed = {8: ("<i", 4), 9: ("<q", 8), 0xB: ("<f", 4), 0xC: ("<d", 8)}
    if tt in fixed:
        fmt, n = fixed[tt]
        return name, struct.unpack_from(fmt, d, p)[0], p + n
    if tt in (0xF, 0x10):
        n = 16 if tt == 0xF else 20
        return name, d[p:p + n].hex(), p + n
    if tt == 0x13:
        n, p = _varint(d, p)
        return name, d[p:p + n].hex(), p + n
    raise FormatError("DbObject type %#x at %d not supported" % (t, p - 1))


class Layout:
    """The install chunks of <game>\\Patch\\layout.toc and <game>\\Data\\layout.toc: a cas id (8 bytes, hex) names
    its layer (bytes 0..1, 0 = Data, else Patch), its install chunk (bytes 2..5 = the chunk's persistentIndex) and its
    cas file (bytes 6..7): <game>\\<Data|Patch>\\Win32\\<chunk name>\\cas_NN.cas.
    datas: {"Data": bytes, "Patch": bytes} instead of the files (tests)."""

    def __init__(self, game, datas=None):
        self.game = game
        self.chunks = {}                # persistentIndex (8 hex digits) -> install chunk name
        for root in ("Patch", "Data"):
            if datas is not None:
                d = datas.get(root)
            else:
                p = os.path.join(game, root, "layout.toc")
                d = open(p, "rb").read() if os.path.isfile(p) else None
            if d is None:
                continue
            off = TOC_DATA if d[:4] == TOC_MAGIC else 0
            _n, v, _e = read_dbobject(d, off)
            for c in ((v or {}).get("installManifest") or {}).get("installChunks") or []:
                if isinstance(c, dict) and "persistentIndex" in c and c.get("name"):
                    self.chunks.setdefault("%08x" % (c["persistentIndex"] & 0xFFFFFFFF), c["name"])

    def cas_path(self, cas_id):
        name = self.chunks.get(cas_id[4:12])
        if name is None:
            return None
        root = "Data" if cas_id[:4] == "0000" else "Patch"
        return os.path.join(self.game, root, "Win32", name.replace("/", os.sep), "cas_%02d.cas" % int(cas_id[-4:], 16))


def make_resolver(layout):
    """cas file of (toc path, cas id): a toc in a <Data|Patch>\\Win32 folder (base-game pack) through layout.toc when
    that file exists, else <that Win32>\\superbundlelayout\\<toc stem>\\cas_NN.cas; any other toc (downloaded pack) the
    folder named like it (cas_path)."""
    def resolve(toc_path, cas_id):
        d = os.path.dirname(os.path.abspath(toc_path))
        if os.path.normcase(os.path.basename(d)) == "win32":
            p = layout.cas_path(cas_id) if layout is not None else None
            if p and os.path.isfile(p):
                return p
            stem = os.path.splitext(os.path.basename(toc_path))[0]
            return os.path.join(d, "superbundlelayout", stem, "cas_%02d.cas" % int(cas_id[-4:], 16))
        return cas_path(toc_path, cas_id)
    return resolve


TOC_KINDS = ("commentaryfull", "commentarylaunch", "commentarylicensed")
LANG_RE = re.compile(r"^[a-z]{3}_[a-z]{2}$")
PACK_TOC_RE = re.compile(r"^commentary(?:full|launch)_([a-z]{3}_[a-z]{2})\.toc$", re.I)
LIVE_ROOTS = ("Patch", "Data")          # <game>\<root>\Win32: the folders the game loads packs from
BACKUP_DIR = "commentary"               # <game>\commentary: Cassio's backup store (flat: tocs at the root, cas in a folder
                                        # named like the toc); a data source only, it never decides what is installed
FULL_TOC_FLOOR = 4096                   # a full toc of 4096 bytes or less is an EA-app stub, not a pack


def _nonempty(p):
    try:
        return os.path.getsize(p) > 0
    except OSError:
        return False


def _has_cas(folder):
    """True when the folder holds at least one non-empty cas_*.cas."""
    try:
        names = os.listdir(folder)
    except OSError:
        return False
    return any(n.lower().startswith("cas_") and n.lower().endswith(".cas") and _nonempty(os.path.join(folder, n))
               for n in names)


def pack_live(game, lang, root):
    """The language's pack is live in <game>\\<root>\\Win32: the launcher's apply-script test, step (a): the full toc is
    bigger than 4096 bytes AND superbundlelayout\\commentaryfull_<lang> holds a non-empty cas_*.cas AND the launch toc is
    present (no size floor on that side: Dutch's 923-byte toc and 4096-byte cas stub ARE its real launch pack)."""
    w = os.path.join(game, root, "Win32")
    try:
        big = os.path.getsize(os.path.join(w, "commentaryfull_%s.toc" % lang)) > FULL_TOC_FLOOR
    except OSError:
        return False
    return (big and _has_cas(os.path.join(w, "superbundlelayout", "commentaryfull_%s" % lang))
            and os.path.isfile(os.path.join(w, "commentarylaunch_%s.toc" % lang)))


def installed_languages(game):
    """The commentary languages that are INSTALLED: the pack is live in Data\\Win32 or Patch\\Win32 (pack_live). Only those
    two folders are looked at: the backup store <game>\\commentary never adds a language to this list."""
    cands = set()
    for root in LIVE_ROOTS:
        try:
            names = os.listdir(os.path.join(game, root, "Win32"))
        except OSError:
            continue
        for n in names:
            m = PACK_TOC_RE.match(n)
            if m:
                cands.add(m.group(1).lower())
    return sorted(l for l in cands if any(pack_live(game, l, r) for r in LIVE_ROOTS))


def is_installed(game, lang):
    return bool(LANG_RE.match(lang or "")) and any(pack_live(game, lang, r) for r in LIVE_ROOTS)


def backup_holds(game, lang, kind):
    """The backup store's toc of that kind for the language when it can be read (toc not empty AND its cas folder, named
    like the toc, holds a cas file), else None."""
    toc = os.path.join(game, BACKUP_DIR, "%s_%s.toc" % (kind, lang))
    if _nonempty(toc) and _has_cas(os.path.splitext(toc)[0]):
        return toc
    return None


def pack_data_source(game, lang, use_backup=True):
    """Where the BYTES of an installed language are read from, one toc per kind (full, launch, licensed):
    the backup store <game>\\commentary first when it holds that kind (safe from EA-app cleanup), else the live copy
    (<game>\\Patch\\Win32, then <game>\\Data\\Win32). use_backup=False reads the live copy only (to compare the two sources). A language that is not installed (installed_languages) has no
    data source at all: a backup copy alone gives nothing ("tocs" empty, "backup_only" True). Result:
      {"lang", "installed", "backup_only", "source": "backup" | "live" | "backup+live" | None,
       "kinds": {kind: "backup" | "live"}, "tocs": [paths], "notes": [str]}"""
    out = {"lang": lang, "installed": is_installed(game, lang), "backup_only": False, "source": None, "kinds": {},
           "tocs": [], "notes": []}
    if not out["installed"]:
        out["backup_only"] = any(backup_holds(game, lang, k) for k in TOC_KINDS) if LANG_RE.match(lang or "") else False
        return out
    for kind in TOC_KINDS:
        live = None
        for root in LIVE_ROOTS:
            p = os.path.join(game, root, "Win32", "%s_%s.toc" % (kind, lang))
            if os.path.isfile(p):
                live = p
                break
        bak = backup_holds(game, lang, kind) if use_backup else None
        if bak:
            out["tocs"].append(bak)
            out["kinds"][kind] = "backup"
            if live and os.path.getsize(live) != os.path.getsize(bak):
                out["notes"].append("%s: backup toc is %d bytes, the live one %d (backup read first, as asked)" % (
                    kind, os.path.getsize(bak), os.path.getsize(live)))
        elif live:
            out["tocs"].append(live)
            out["kinds"][kind] = "live"
    used = set(out["kinds"].values())
    out["source"] = "+".join(s for s in ("backup", "live") if s in used) or None
    return out


def find_tocs(game, lang):
    """The language's commentary tocs to read, one per kind: [] unless the language is installed, then pack_data_source's."""
    return pack_data_source(game, lang)["tocs"]


SET_RE = re.compile(r"^sound/speech/(?:loc)?commentary/([a-z]+_[a-z]+)/soundwaves/"
                    r"(\1(?:_([a-z0-9]+))?_(full|demo|licensed))/([^/]+)$")


def parse_set_name(asset_name, lang):
    """sound/speech/{loc,}commentary/<lang>/soundwaves/<set>/<family> -> (set, team, kind, family), else None.
    set = <lang>_<kind> (team "") or <lang>_<team>_<kind>."""
    m = SET_RE.match(asset_name)
    if not m or m.group(1) != lang:
        return None
    return m.group(2), m.group(3) or "", m.group(4), m.group(5)


class CasReader:
    def __init__(self, oodle, resolve=None):
        self.oodle = oodle
        self.resolve = resolve or cas_path
        self.files = {}

    def raw(self, toc_path, loc):
        cas, off, size = loc
        p = self.resolve(toc_path, cas)
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

    def __init__(self, d, o, with_fields=True):
        if d[o:o + 4] != b"TESD":
            raise FormatError("no DSET at %#x" % o)
        self.offset = o
        self.size, self.name, self.type = _u32(d, o + 4), _u32(d, o + 8), _u32(d, o + 12)
        self.rows = _u32(d, o + 0x38)
        nf, ni = _u16(d, o + 0x3C), _u16(d, o + 0x3E)
        fo, io, po = _u16(d, o + 0x40), _u16(d, o + 0x42), _u32(d, o + 0x44)
        self.fields = {}
        self.field_flags = {}
        for k in range(nf if with_fields else 0):     # without fields: the keys only (the count of other families)
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
            # No prefix sum stored: the index is an identity, one row per key combination in the same order as the
            # prefix-sum case (last parameter fastest). FC 27 spa_es psimple_surname (cm_sim x surname_ID x
            # player_intensity = 2 x 2841 x 1 = 5682 rows) and eng_us gm pPLAYER_NAMES_HIGH use it with several parameters.
            if total != self.rows:
                raise FormatError("DSET %s: index without data is not an identity" % hname(self.name))
            pref = list(range(total + 1))
        else:
            width = 2 if self.rows <= 0xFFFF else 4
            pref = _ints(self._d, index["ptr"], total + 1, width)
            if self.rows <= 0xFF and (pref[0] != 0 or pref[-1] != self.rows):
                # a data set of up to 255 rows (none of the name families): the prefix sum is stored in bytes
                pref = _ints(self._d, index["ptr"], total + 1, 1)
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


def parse_sble(d, with_fields=True, only=None):
    """SBle resource: 'SBle', u32 size, u16 data set count, ..., +0x18 pointer to the data set pointer array.
    only: the name of the one data set to parse (the others are skipped)."""
    if d[:4] != b"SBle":
        raise FormatError("not an SBle resource (%s)" % d[:4].hex())
    n, arr = _u16(d, 8), _u32(d, 0x18)
    offs = [_u32(d, arr + 8 * k) for k in range(n)]
    if only is not None:
        offs = [o for o in offs if hname(_u32(d, o + 8)) == only]
    sets = [DataSet(d, o, with_fields) for o in offs]
    return {hname(s.name): s for s in sets}


# ---------------------------------------------------------------------------------------------------- the bank
DEFAULT_TEAM = "dr"     # eng_us: Derek Rae, the team proved in game on 2026-10-08 (docs/callnames.md)
# set index of the encoded segment numbers (index * SET_STRIDE + segment), as turbo/tools/callname_coverage.py SET_INDEX:
# dr / no team: full 0, licensed 1, demo 2; gm: full 3, licensed 4, demo 5
SET_KIND_INDEX = {"full": 0, "licensed": 1, "demo": 2}
SET_TEAM_BASE = {"": 0, "dr": 0, "gm": 3}
SET_STRIDE = 100000


def set_index(team, kind):
    """The encoded-segment index of a set (team "" = a language without teams), None when not in the table"""
    if team not in SET_TEAM_BASE or kind not in SET_KIND_INDEX:
        return None
    return SET_TEAM_BASE[team] + SET_KIND_INDEX[kind]


class Bank:
    data_source = None      # pack_data_source's answer when the tocs were found here (None: tocs passed in)

    def __init__(self, game, lang, team=None, tocs=None, primary=None):
        self.game, self.lang = game, lang
        if tocs is None:
            self.data_source = pack_data_source(game, lang)
            if not self.data_source["installed"]:
                why = ("; the backup store %s\\commentary holds it, but a backup never makes a language installed" % game
                       if self.data_source["backup_only"] else "")
                raise FormatError("no commentary pack for %s: not installed (its pack is not live in %s\\Data\\Win32 or "
                                  "%s\\Patch\\Win32)%s" % (lang, game, game, why))
            tocs = self.data_source["tocs"]
        self.oodle = Oodle(game)
        self.layout = Layout(game)
        self.cas = CasReader(self.oodle, make_resolver(self.layout))
        self.tocs = [Toc(p) for p in tocs]
        if not self.tocs:
            raise FormatError("no commentary pack for %s: no commentary{full,launch,licensed}_%s.toc to read" % (lang, lang))
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
        self._index_sets(team, primary)

    def _index_sets(self, team=None, primary=None):
        """self.sets {set: {"team", "kind", "families": {family: asset name}}} and the primary set (module doc).
        `primary` names the set directly (e.g. eng_us_dr_demo, to read its recordings like a primary set's)."""
        self.sets = {}
        for n, a in self.assets.items():
            p = parse_set_name(n, self.lang)
            if p is None or "res" not in a:
                continue
            s, t, kind, fam = p
            self.sets.setdefault(s, {"team": t, "kind": kind, "families": {}})["families"][fam] = n
        if primary is not None:
            if primary not in self.sets:
                raise FormatError("no set %s in the %s bank (sets: %s)" % (primary, self.lang, ", ".join(sorted(self.sets)) or "none"))
            self.primary = primary
            return
        team = team or DEFAULT_TEAM
        for cand in ("%s_full" % self.lang, "%s_%s_full" % (self.lang, team)):
            if cand in self.sets:
                self.primary = cand
                break
        else:
            full = sorted(s for s, v in self.sets.items() if v["kind"] == "full")
            if team != DEFAULT_TEAM or not full:
                raise FormatError("no %s_full or %s_%s_full set in the %s bank (sets: %s)" % (
                    self.lang, self.lang, team, self.lang, ", ".join(sorted(self.sets)) or "none"))
            self.primary = full[0]

    def family_name(self, family, set_name=None):
        fams = self.sets.get(set_name or self.primary, {}).get("families", {})
        return fams.get(family, "<%s/%s: not in the bank>" % (set_name or self.primary, family))

    def families(self, set_name=None):
        return sorted(self.sets.get(set_name or self.primary, {}).get("families", {}))

    def load_res(self, name):
        """The SBle data sets of one family asset (res only, no EBX): for the ids of every set."""
        a = self.assets.get(name)
        if not a or "res" not in a:
            raise FormatError("%s not found in the %s bank" % (name, self.lang))
        toc_path, loc, r = a["res"]
        res = self.cas.read(toc_path, loc)
        if len(res) != r["size"]:
            raise FormatError("%s: res is %d bytes, the manifest says %d" % (name, len(res), r["size"]))
        return {"name": name, "res": res, "sets": parse_sble(res)}

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

    def load_selection_keys(self, family):
        """The family's Selection data set, keys only (no fields, no EBX): for the count of the other families."""
        a = self.assets.get(self.family_name(family))
        if not a or "res" not in a:
            raise FormatError("family %s not found in the %s bank" % (family, self.lang))
        toc_path, loc, _r = a["res"]
        sel = parse_sble(self.cas.read(toc_path, loc), with_fields=False, only="Selection").get("Selection")
        if sel is None:
            raise FormatError("%s: no Selection data set" % family)
        return sel

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


def high_rows(fam):
    """pPLAYER_NAMES_HIGH: {output key: selection rows} by the selector each row's key holds (HIGH), and the number of
    rows whose key holds both selectors."""
    sel = fam["sets"].get("Selection")
    if sel is None:
        raise FormatError("%s: no Selection data set" % fam["name"])
    if H_SEGMENT not in sel.fields or djb2x("VariationId") not in sel.fields:
        raise FormatError("%s: Selection has no SegmentID / VariationId column" % fam["name"])
    keys = sel.row_keys()
    seg, var = sel.fields[H_SEGMENT], sel.fields[djb2x("VariationId")]
    out = {k: [] for k in HIGH}
    both = 0
    for r in range(sel.rows):
        k = keys[r]
        hit = [key for key, (selector, _p) in HIGH.items() if selector in k]
        if not hit:
            raise FormatError("%s: Selection row %d is keyed by none of %s (%s)" % (
                fam["name"], r, ", ".join(s for s, _p in HIGH.values()), list(k)))
        both += len(hit) > 1
        for key in hit:
            out[key].append({"segment": seg[r], "variation": var[r], "id": k[HIGH[key][0]], "cm_sim": k.get("cm_sim"),
                             "intensity": k.get("player_intensity")})
    return out, both


def family_summary(selection_rows_n, merged, id_key):
    """rows, unique (id, segment, variation), ids, segments, segments shared by several ids."""
    by_seg = {}
    for r in merged:
        by_seg.setdefault(r["segment"], set()).add(r[id_key])
    return {"selection_rows": selection_rows_n, "unique_rows": len(merged),
            "ids": len({r[id_key] for r in merged}), "segments": len(by_seg),
            "shared_segments": sum(1 for v in by_seg.values() if len(v) > 1)}


def other_player_families(bank, log):
    """Count (no audio) of the language's p* families other than the name ones: selection rows and, per *_pID
    selector, the ids. Families whose Selection cannot be read are listed with the reason."""
    out = {}
    for f in bank.families():
        if not f.startswith("p") or f in NAME_FAMILIES:
            continue
        try:
            sel = bank.load_selection_keys(f)
            index = max(sel.indexes, key=lambda x: len(x["params"]))
            params = [p["name"] for p in index["params"]]
            pid = [p for p in params if "_pID" in p]
            e = {"selection_rows": sel.rows, "selectors": params}
            if pid:
                keys, err = None, None
                # the widest index first; when it cannot be read, a narrower one that still holds the player ids
                for ix in sorted(sel.indexes, key=lambda x: -len(x["params"])):
                    if not any(p["name"] in pid for p in ix["params"]):
                        continue
                    try:
                        keys = sel.row_keys(ix)
                        break
                    except FormatError as ex:
                        err = err or ex
                if keys is None:
                    raise err
                ids = {p: {k[p] for k in keys.values() if p in k} for p in pid}
                e["player_ids"] = len(set().union(*ids.values()))
                e["ids_per_selector"] = {p: len(v) for p, v in ids.items()}
        except FormatError as ex:
            e = {"error": str(ex)}
        out[f] = e
    keyed = [e for e in out.values() if "player_ids" in e]
    log("other p* families: %d, %d keyed by a player id (%d selection rows), %d not keyed by one, %d unreadable" % (
        len(out), len(keyed), sum(e["selection_rows"] for e in keyed),
        sum(1 for e in out.values() if "selectors" in e and "player_ids" not in e),
        sum(1 for e in out.values() if "error" in e)))
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


def _sd_length(pkt, p):
    """A self-delimited frame length (RFC 6716 3.2.1): one byte 0..251, else two bytes (first 252..255): 4 * second + first."""
    if p >= len(pkt):
        raise FormatError("self-delimited length past the packet end")
    b = pkt[p]
    if b < 252:
        return b, p + 1
    if p + 1 >= len(pkt):
        raise FormatError("self-delimited length cut after its first byte")
    return 4 * pkt[p + 1] + b, p + 2


def split_streams(pkt, streams):
    """The `streams` Opus packets of one multistream block (RFC 6716 Appendix B): every stream but the last is
    self-delimited (TOC, explicit frame length, frame) and is returned as a STANDARD packet (TOC + frame, the length
    byte(s) dropped); the last one is standard already and is the rest of the block. Only TOC code 0 (one frame) is
    handled: it is the only code in the FC 27 eng_us bank (7120 + 11792 + 4874 + 169 dr segments, 0 others); any other
    code is refused instead of guessed."""
    out, p = [], 0
    for _k in range(streams - 1):
        if p >= len(pkt):
            raise FormatError("multistream block ends before stream %d of %d" % (len(out) + 1, streams))
        toc = pkt[p]
        if toc & 3:
            raise FormatError("multistream stream %d: TOC code %d (only code 0, one frame, is handled)" % (len(out) + 1, toc & 3))
        n, q = _sd_length(pkt, p + 1)
        if q + n > len(pkt):
            raise FormatError("multistream stream %d: frame of %d bytes runs past the block (%d bytes)" % (len(out) + 1, n, len(pkt)))
        out.append(bytes([toc]) + pkt[q:q + n])
        p = q + n
    out.append(pkt[p:])
    return out


def speech_packets(head, packets):
    """The mono Opus packets that carry the speech of an SNS stream, [(samples, packet)]: codec 0x0C / 0x0E (mono) the
    packets as they are; codec 0x0F (EAOPUSMU, multistream) the first stream of every block, after checking that every
    other stream is the known silent packet (SILENT_STREAM) - audio in another stream would be dropped, so it is refused."""
    codec, ch = head["codec"], head["channels"]
    if codec in (CODEC_EAOPUS, CODEC_EAOPUSM):
        if ch != 1:
            raise FormatError("codec %#x with %d channels: only mono is handled" % (codec, ch))
        return packets
    if codec != CODEC_EAOPUSMU:
        raise FormatError("codec %d is not an EA Opus codec" % codec)
    out = []
    for i, (n, pkt) in enumerate(packets):
        streams = split_streams(pkt, ch)
        for k, extra in enumerate(streams[1:], 2):
            if extra != SILENT_STREAM:
                raise FormatError("block %d: stream %d is %s, not the silent packet %s" % (i, k, extra.hex(), SILENT_STREAM.hex()))
        out.append((n, streams[0]))
    return out


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
    length minus the encoder delay, which becomes the pre-skip; the last page's granule trims the end. A multistream
    codec (0x0F) is reduced to its speech stream first (speech_packets)."""
    packets = speech_packets(head, packets)
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


def write_wavs(bank, fam, prefix, folder, ffmpeg, segments_wanted, log, seg_offset=0):
    """Decode each wanted segment to <folder>\\<prefix>_<n>_<n>.wav, n = segment + seg_offset (an offset lets the segments
    of several sets, whose numbers collide, share one folder)."""
    os.makedirs(folder, exist_ok=True)
    jobs, problems = [], []
    for s, d, o in segment_streams(bank, fam):
        if s not in segments_wanted:
            continue
        head, packets = parse_sns(d, o)
        if head["codec"] not in SPEECH_CODECS or head["rate"] != OPUS_RATE:
            problems.append("segment %d: codec %d rate %d not handled" % (s, head["codec"], head["rate"]))
            continue
        try:
            ogg, total = ogg_opus(head, packets)
        except FormatError as ex:
            problems.append("segment %d: %s" % (s, ex))
            continue
        if total != head["samples"]:
            problems.append("segment %d: blocks hold %d samples, the header says %d" % (s, total, head["samples"]))
        base = os.path.join(folder, "%s_%d_%d" % (prefix, s + seg_offset, s + seg_offset))
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
    res = bank.cas.resolve
    for toc in bank.tocs:
        paths = [toc.path] + sorted({res(toc.path, loc[0]) for b in toc.bundles for loc in b} |
                                    {res(toc.path, c[0]) for c in toc.chunks.values() if toc.own(c[0])})
        for p in paths:
            try:
                st = os.stat(p)
            except OSError:
                out.append({"path": p, "size": None, "modified": None})
                continue
            out.append({"path": p, "size": st.st_size,
                        "modified": datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds")})
    return out


ID_KEYS = ("generic", "simple", "link", "high", "generic_high")
# family -> [(id list key, selector)]; pPLAYER_NAMES_HIGH is split by selector (high_rows)
SET_FAMILIES = (("psimple_surname", "generic", "surname_ID"), ("pplayer_names_simple", "simple", "player_db_pID"),
                ("pplayer_names_link", "link", "player_db_pID"), (HIGH_FAMILY, None, None))


def collect_sets(bank, log=print):
    """The ids of every set that has a name family (module doc: "sets", "teams", "union"). A family that cannot be read
    is recorded in its set's "errors" (the run goes on); a family a set does not have is simply absent."""
    sets = {}
    for s in sorted(bank.sets):
        info = bank.sets[s]
        if not any(f in info["families"] for f, _k, _s in SET_FAMILIES):
            continue            # e.g. eng_us_gmsr_full, eng_us_common_full: other families only
        ids = {k: set() for k in ID_KEYS}
        summary, errors = {}, {}
        for family, key, selector in SET_FAMILIES:
            name = info["families"].get(family)
            if name is None:
                continue
            try:
                fam = bank.load_res(name)
                if family == HIGH_FAMILY:
                    split, _both = high_rows(fam)
                    ids["high"] |= {r["id"] for r in split["real_high"]}
                    ids["generic_high"] |= {r["id"] for r in split["generic_high"]}
                    summary["high"] = len(split["real_high"]) + len(split["generic_high"])
                else:
                    rows = selection_rows(fam, selector)
                    ids[key] |= {r["id"] for r in rows}
                    summary[key] = len(rows)
            except FormatError as ex:
                errors[family] = str(ex)
        sets[s] = {"team": info["team"], "kind": info["kind"], "ids": {k: sorted(v) for k, v in ids.items()},
                   "selection_rows": summary, "errors": errors}
        log("set %-22s %s%s" % (s, ", ".join("%s %d" % (k, len(v)) for k, v in ids.items() if v),
                                ("; errors: " + "; ".join("%s: %s" % e for e in errors.items())) if errors else ""))
    teams, union = {}, {k: set() for k in ID_KEYS}
    for s, v in sets.items():
        t = teams.setdefault(v["team"], {k: set() for k in ID_KEYS})
        for k in ID_KEYS:
            t[k] |= set(v["ids"][k])
            union[k] |= set(v["ids"][k])
    teams = {t: {k: sorted(x) for k, x in v.items()} for t, v in sorted(teams.items())}
    for t, v in teams.items():
        log("team %-4s generic %d, simple|link %d, any own %d" % (
            t or '""', len(v["generic"]), len(set(v["simple"]) | set(v["link"])),
            len(set(v["simple"]) | set(v["link"]) | set(v["high"]))))
    return sets, teams, {k: sorted(v) for k, v in union.items()}


def run(a, log=print):
    bank = Bank(a.game, a.lang, team=getattr(a, "team", None), primary=getattr(a, "set", None))
    try:
        if a.list:
            if bank.data_source:                # stderr, so the family list on stdout is unchanged
                print("data source: %s" % bank.data_source["source"], file=sys.stderr)
            for f in bank.families():
                log(f)
            return 0
        result = {"turbo_bank": 1, "lang": a.lang, "game_build": game_build(a.game), "source_files": source_files(bank)}
        ds = bank.data_source
        if ds:
            result["data_source"] = {"source": ds["source"], "kinds": ds["kinds"]}
            log("data source: %s (%s)" % (ds["source"], ", ".join("%s %s" % (k, v) for k, v in ds["kinds"].items())))
            for n in ds["notes"]:
                log("note: " + n)
        log("sets: %s; primary: %s" % (", ".join(sorted(bank.sets)), bank.primary))
        offset = getattr(a, "seg_offset", None)
        if offset is None:
            offset = 0
            if getattr(a, "set", None):
                info = bank.sets[bank.primary]
                idx = set_index(info["team"], info["kind"])
                if idx is None:
                    raise FormatError("set %s (team %r, kind %r): no set index, give --seg-offset" % (
                        bank.primary, info["team"], info["kind"]))
                offset = idx * SET_STRIDE
        result["segment_offset"] = offset
        if offset:
            log("segment offset: %d (the wavs are named <prefix>_<segment + %d>_<segment + %d>.wav; the rows keep the "
                "set's own segment numbers)" % (offset, offset, offset))
        notes, raw = [], {}
        for key, (family, selector, _prefix) in FAMILIES.items():
            if family not in bank.families():      # e.g. eng_us gm: no surname family
                raw[key] = (None, [])
                result[key] = []
                notes.append("%s (%s): not in the primary set %s" % (key, family, bank.primary))
                log("%s: not in %s" % (key, bank.primary))
                continue
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
        # pPLAYER_NAMES_HIGH, split by selector (new keys after the existing ones, which stay as they were)
        try:
            hfam = bank.load_family(HIGH_FAMILY)
            split, both = high_rows(hfam)
        except FormatError as ex:          # eng_us gm: "index without data is not an identity"
            hfam, split, both = None, {k: [] for k in HIGH}, 0
            notes.append("pplayer_names_high of %s not read: %s" % (bank.primary, ex))
            log("pplayer_names_high: not read (%s)" % ex)
        hsel = hfam["sets"]["Selection"] if hfam else None
        hparams = [p["name"] for p in max(hsel.indexes, key=lambda x: len(x["params"]))["params"]] if hsel else []
        for key, (selector, _prefix) in HIGH.items():
            rows = split[key]
            raw[key] = (hfam, rows)
            id_key = "commentaryid" if selector == "surname_ID" else "playerid"
            merged = merge_rows(rows, id_key)
            result[key] = [{"segment": r["segment"], "variation": r["variation"], id_key: r[id_key],
                            "intensity": r["intensity"], "cm_sim": r["cm_sim"]} for r in merged]
            log("%s: %d rows, %d ids" % (key, len(merged), len({r[id_key] for r in merged})))
        if hfam:
            notes.append("pplayer_names_high: %d selection rows keyed by %s: %d by player_db_pID (real_high), %d by "
                         "surname_ID (generic_high), %d by both; %d segments of %d; Variations check: %s" % (
                             hsel.rows, ", ".join(hparams), len(split["real_high"]), len(split["generic_high"]), both,
                             len({r["segment"] for v in split.values() for r in v}), hfam["sets"]["Segments"].rows,
                             check_variations(hfam, split["real_high"] + split["generic_high"])))
        summary = {}
        for key in WAV_FAMILIES:
            fam, rows = raw[key]
            if fam is None:
                continue
            id_key = "commentaryid" if key in ("generic", "generic_high") else "playerid"
            summary[key] = dict(family=fam["name"].rsplit("/", 1)[1], **family_summary(len(rows), result[key], id_key))
            log("summary %-12s %s" % (key, ", ".join("%s %s" % (k, v) for k, v in summary[key].items())))
        result["wav_dir"] = None
        if a.wav:
            ffmpeg = a.ffmpeg or shutil.which("ffmpeg")
            if not ffmpeg:
                notes.append("no ffmpeg found: the audio was written as Ogg Opus (.opus), not wav")
            problems = []
            for key in WAV_FAMILIES:
                fam, rows = raw[key]
                if not rows:        # e.g. generic_high: no HIGH row is keyed by surname_ID in FC 27 ita_it
                    log("  %s: no rows, no folder" % key)
                    continue
                prefix = FAMILIES[key][2] if key in FAMILIES else HIGH[key][1]
                folder = os.path.join(a.wav, key)
                problems += write_wavs(bank, fam, prefix, folder, ffmpeg, {r["segment"] for r in rows}, log, seg_offset=offset)
            result["wav_dir"] = os.path.abspath(a.wav)
            notes.append("audio: EA SNS streams, codec %s, decoded with %s; %d problems%s" % (
                "EA Opus (0x0C / 0x0E mono; 0x0F multistream, speech stream decoded; 48 kHz)", ffmpeg or "nothing (Ogg Opus files)", len(problems),
                (": " + "; ".join(problems[:20])) if problems else ""))
        result["family_summary"] = summary
        result["other_player_families"] = other_player_families(bank, log)
        # every set's ids (new keys; the ones above are the primary set's rows, as before)
        result["primary_set"] = bank.primary
        result["sets"], result["teams"], result["union"] = collect_sets(bank, log)
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
    split, both = high_rows({"name": "synthetic", "sets": sets})
    check("HIGH: rows keyed by surname_ID go to generic_high, none to real_high",
          len(split["generic_high"]) == 3 and split["real_high"] == [] and both == 0)
    shared = [{"segment": 5, "variation": 1, "playerid": 51}, {"segment": 5, "variation": 1, "playerid": 239598},
              {"segment": 6, "variation": 2, "playerid": 51}]
    check("summary: rows, unique rows, ids, segments, segments shared by several ids",
          family_summary(6, shared, "playerid") == {"selection_rows": 6, "unique_rows": 3, "ids": 2, "segments": 2,
                                                    "shared_segments": 1})
    keys_only = parse_sble(_synthetic_sble(), with_fields=False, only="Selection")["Selection"]
    check("keys only: no fields, the same keys", keys_only.fields == {} and
          [k["surname_ID"] for _r, k in sorted(keys_only.row_keys().items())] == [900762, 900762, 900762])
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
    head = {"codec": CODEC_EAOPUSM, "channels": 1, "rate": 48000}
    ogg, total = ogg_opus(head, [(648, b"\x78" + b"\0" * 10), (960, b"\x78" + b"\0" * 12)])
    check("ogg: pre-skip 312, total 1608, two header pages + one data page",
          total == 1608 and ogg.count(b"OggS") == 3 and struct.unpack_from("<H", ogg, 28 + 10)[0] == 312)
    # codec 0x0F (eng_us): a block = stream 1 self-delimited (TOC, length, frame) + the 3 silent bytes
    def blk(frame, toc=b"\x78"):
        n = len(frame)
        ln = bytes([n]) if n < 252 else bytes([252 + (n & 3), (n - 252 - (n & 3)) // 4])
        return toc + ln + frame + SILENT_STREAM
    check("multistream: stream 1 returns as a standard packet, stream 2 as the silent packet",
          split_streams(blk(b"abcde"), 2) == [b"\x78abcde", SILENT_STREAM])
    check("multistream: a two-byte self-delimited length (4 * second + first = 261)",
          split_streams(blk(b"x" * 261), 2)[0] == b"\x78" + b"x" * 261)
    check("multistream: a CELT silence frame in stream 1 (TOC 0xf8)",
          split_streams(blk(b"\xff\xfe", b"\xf8"), 2)[0] == b"\xf8\xff\xfe")
    for name, bad in (("TOC code 1", b"\x79\x02ab" + SILENT_STREAM), ("a frame past the block end", b"\x78\x40ab"),
                      ("no stream 2", b"")):
        try:
            split_streams(bad, 2)
            check("multistream: refused: " + name, False)
        except FormatError:
            check("multistream: refused: " + name, True)
    head15 = {"codec": CODEC_EAOPUSMU, "channels": 2, "rate": 48000}
    ogg, total = ogg_opus(head15, [(648, blk(b"\0" * 10)), (960, blk(b"\0" * 12))])
    check("codec 15: pre-skip 312, total 1608, only the speech stream is wrapped (no f8 ff fe tail in the Ogg)",
          total == 1608 and ogg.count(b"OggS") == 3 and struct.unpack_from("<H", ogg, 28 + 10)[0] == 312
          and SILENT_STREAM not in ogg)
    try:
        ogg_opus(head15, [(648, b"\x78\x02ab\xf8\xff\xff")])
        check("codec 15: audio in stream 2 is refused, not dropped", False)
    except FormatError:
        check("codec 15: audio in stream 2 is refused, not dropped", True)

    if game and is_installed(game, "ita_it"):
        log("on disk (ita_it, %s):" % game)
        bank = Bank(game, "ita_it")
        try:
            log("  data source: %s" % bank.data_source["source"])
            if bank.data_source["source"] != "live":        # both copies on disk: the source must not change a list
                live = Bank(game, "ita_it", tocs=pack_data_source(game, "ita_it", use_backup=False)["tocs"])
                try:
                    def lists(b):
                        gen = merge_rows(selection_rows(b.load_family("psimple_surname"), "surname_ID"), "commentaryid")
                        text = "\n".join(b.families()) + "\n" + json.dumps(gen, sort_keys=True)
                        return hashlib.md5(text.encode()).hexdigest()
                    check("backup copy and live copy give the same family list and generic rows (md5)",
                          lists(bank) == lists(live))
                finally:
                    live.close()
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
            hsplit, _both = high_rows(bank.load_family(HIGH_FAMILY))
            check("pPLAYER_NAMES_HIGH rows are keyed by player_db_pID (Shearer 51 among them)",
                  len(hsplit["real_high"]) > 1000 and 51 in {r["id"] for r in hsplit["real_high"]})
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
    ap.add_argument("--wav", help="folder for the audio: <wav>\\generic, real, real_link, real_high "
                                         "(and generic_high when a HIGH row is keyed by surname_ID)")
    ap.add_argument("--ffmpeg", help="ffmpeg.exe to decode the Opus audio (default: the one on PATH)")
    ap.add_argument("--list", action="store_true", help="list the language's soundwave families and stop")
    ap.add_argument("--languages", action="store_true",
                    help="list the INSTALLED languages (pack live in Data/Win32 or Patch/Win32) with the data source "
                         "the bytes would be read from (backup store first), and stop")
    ap.add_argument("--team", help="the commentary team of the primary set when the language has teams "
                                   "(eng_us: dr = Derek Rae, gm = Guy Mowbray; default %s)" % DEFAULT_TEAM)
    ap.add_argument("--set", help="read this set's rows and recordings instead of the full set's, e.g. ita_it_demo "
                                  "(wavs under encoded segment numbers: set index * 100000 + segment)")
    ap.add_argument("--seg-offset", type=int, help="the number added to the segment in the wav names (default: the set's "
                                                   "index * 100000 with --set, else 0)")
    ap.add_argument("--self-test", action="store_true", help="run the built-in checks (and the ita_it ones on disk)")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test(a.game)
    if a.languages:
        for lang in installed_languages(a.game):
            ds = pack_data_source(a.game, lang)
            print("%s  %s" % (lang, ds["source"]))
        return 0
    if not a.lang:
        ap.error("--lang is required")
    try:
        return run(a)
    except FormatError as e:
        print("error: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
