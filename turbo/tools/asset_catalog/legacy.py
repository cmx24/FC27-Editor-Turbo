"""Legacy files (data/ui/imgAssets/...): FNV-1 64 name hashes, the game's ChunkFileCollector manifests, and the
candidate names (Live Editor's csv lists + generated patterns per family, the brute-force ranges of the research).
A candidate is a game file when its hash is a manifest entry. FNV-1 is vectorised with numpy when it is available."""
import re
import struct

try:
    import numpy as np
except ImportError:  # pragma: no cover - the builder still works, the brute force is just slower
    np = None

FNV_BASIS = 0xCBF29CE484222325
FNV_PRIME = 0x100000001B3
MASK = (1 << 64) - 1

# Collector EBX names (Patch over Data, in globals.toc); their manifest chunks hold the entry hashes
COLLECTORS = ("core/chunkfiles/chunkfilecollector", "core/chunkfiles/cfc_gamemodes", "core/chunkfiles/launch/cfc_gm_launch",
              "core/chunkfiles/cfc_additional_data", "core/chunkfiles/cfc_telstar")


def fnv1(s, h=FNV_BASIS):
    """FNV-1 64 (multiply, then xor) of the UTF-8 bytes of s; continue from state h."""
    for c in s.encode("utf-8"):
        h = (h * FNV_PRIME) & MASK
        h ^= c
    return h


def path_hash(path):
    return fnv1(path.lower())


def parse_manifest_chunk(d):
    """A CFC manifest chunk: u32 version (2 or 3), count at +0x0C, u64 entry offset at +0x10, 28-byte entries with the
    u64 name hash first. Returns the list of hashes, or None when the chunk is not a manifest."""
    if len(d) < 0x18:
        return None
    ver, _a, _b, n = struct.unpack_from("<4I", d, 0)
    eo = struct.unpack_from("<Q", d, 0x10)[0]
    if ver not in (2, 3) or eo + 28 * n > len(d):
        return None
    return [struct.unpack_from("<Q", d, eo + 28 * k)[0] for k in range(n)]


def guids_in(ebx, known):
    """16-byte windows of an EBX blob that are chunk guids of the toc chunk tables (`known`: guid hex -> location)."""
    found = []
    for o in range(len(ebx) - 15):
        g = ebx[o:o + 16].hex()
        if g in known and g not in found:
            found.append(g)
    return found


# ---------------------------------------------------------------- families of legacy files
I = "data/ui/imgassets/"
# family -> regex on the LOWER-CASE path; group "id" (or group 1) is the id; group "v" (optional) the variant number
LEGACY_FAMILIES = [
    ("miniface", I + r"heads/p(\d+)\.dds$"),
    ("youthface", I + r"youthheads/p(\d+)\.dds$"),
    ("staffface", I + r"heads_staff/heads_staff_(\d+)\.dds$"),
    ("legendface", I + r"cmlegendaryplayers/(?:small/)?legend_(\d+)\.dds$"),
    ("hair", I + r"craniumhair/hair_(\d+)_(\d+)\.dds$"),
    ("hair_old", I + r"hairstyle/item_(\d+)_(\d+)\.dds$"),
    ("facialhair", I + r"craniumfacialhair/facial_hair_(\d+)_(\d+)\.dds$"),
    ("facialhair_old", I + r"facialhairstyle/item_(\d+)_(\d+)\.dds$"),
    ("boots", I + r"shoe/shoe_(\d+)_(\d+)\.dds$"),
    ("boots_old", I + r"boots/item_(\d+)_(\d+)(?:_\d+_\d+)?_?\.dds$"),
    ("gkglove", I + r"gkglove/gkglove_(\d+)(?:_(\d+))?\.dds$"),
    ("accessory", I + r"accessories/item_(\d+)_(\d+)\.dds$"),
    ("tattoo", I + r"tattoo/item_(\d+)_(\d+)\.dds$"),
    ("ball", I + r"balls/ball_(\d+)\.dds$"),
    ("crest", I + r"crest(16x16|32x32|50x50|512x512|1024x1024)?/(light|dark|custom)/l(\d+)\.dds$"),
]
LEGACY_RX = [(f, re.compile(r)) for f, r in LEGACY_FAMILIES]

# Preview preference (lower is better) between the new and the older picture folders of the same field
PREVIEW_RANK = {"hair": 0, "hair_old": 1, "facialhair": 0, "facialhair_old": 1, "boots": 0, "boots_old": 1}
CREST_SIZE_RANK = {"": 0, "512x512": 1, "1024x1024": 2, "50x50": 3, "32x32": 4, "16x16": 5}
CREST_VARIANT_RANK = {"light": 0, "dark": 1, "custom": 2}

# Generated candidate names: (family label, prefix as the csv spells it, id range, suffixes after the id)
P = "data/ui/imgAssets/"
BRUTE = [
    ("miniface", P + "heads/p", (0, 1500000), [".dds"]),
    ("youthface", P + "youthheads/p", (0, 1500000), [".dds"]),
    ("staffface", P + "heads_staff/heads_staff_", (0, 1500000), [".dds"]),
    ("legendface", P + "cmLegendaryPlayers/legend_", (0, 1500000), [".dds"]),
    ("legendface", P + "cmLegendaryPlayers/small/legend_", (0, 1500000), [".dds"]),
    ("hair", P + "craniumhair/hair_", (0, 30000), ["_%d.dds" % n for n in range(12)]),
    ("hair_old", P + "hairstyle/item_", (0, 30000), ["_%d.dds" % n for n in range(6)]),
    ("facialhair", P + "craniumfacialhair/facial_hair_", (0, 30000), ["_%d.dds" % n for n in range(6)]),
    ("facialhair_old", P + "facialhairstyle/item_", (0, 30000), ["_%d.dds" % n for n in range(6)]),
    ("boots", P + "shoe/shoe_", (0, 30000), ["_%d.dds" % n for n in range(6)]),
    ("boots_old", P + "boots/item_", (0, 30000), ["_%d.dds" % n for n in range(6)]),
    ("gkglove", P + "gkglove/gkglove_", (0, 30000), [".dds"] + ["_%d.dds" % n for n in range(4)]),
    ("accessory", P + "accessories/item_", (0, 3000), ["_%d.dds" % n for n in range(256)]),
    ("tattoo", P + "tattoo/item_", (0, 30000), ["_%d.dds" % n for n in range(8)]),
    ("ball", P + "balls/ball_", (0, 30000), [".dds"]),
] + [("crest", P + "%s/%s/l" % (d, v), (0, 1000000), [".dds"])
     for d in ("crest", "crest16x16", "crest32x32", "crest50x50", "crest512x512", "crest1024x1024")
     for v in ("light", "dark", "custom")]


def classify_path(path):
    """(family, id, sort key for the preview choice) of a legacy path, or None."""
    lp = path.lower()
    for fam, rx in LEGACY_RX:
        m = rx.match(lp)
        if not m:
            continue
        if fam == "crest":
            size, variant, i = m.group(1) or "", m.group(2), int(m.group(3))
            return fam, i, (CREST_VARIANT_RANK[variant], CREST_SIZE_RANK[size], lp)
        i = int(m.group(1))
        var = int(m.group(2)) if m.lastindex and m.lastindex >= 2 and m.group(2) else 0
        return fam, i, (PREVIEW_RANK.get(fam, 0), var, lp)
    return None


# ---------------------------------------------------------------- vectorised FNV-1 over numbers
def _np_fnv_bytes(h, data):
    """h: uint64 array of states; data: bytes (the same for every row)."""
    P64 = np.uint64(FNV_PRIME)
    for c in data:
        h = (h * P64) ^ np.uint64(c)
    return h


def brute_hits(prefix, lo, hi, suffixes, game_set, game_sorted=None):
    """Candidate names prefix + <id> + suffix for id in [lo, hi): return [(id, path)] whose lower-case hash is a game
    hash. Uses numpy when available (a 1.5M range takes well under a second)."""
    base = fnv1(prefix.lower())
    hits = []
    if np is None or game_sorted is None:
        for i in range(lo, hi):
            h = fnv1(str(i), base)
            for s in suffixes:
                if fnv1(s.lower(), h) in game_set:
                    hits.append((i, prefix + str(i) + s))
        return hits
    with np.errstate(over="ignore"):
        ndig = len(str(hi - 1)) if hi > 1 else 1
        for L in range(1, ndig + 1):
            a = max(lo, 0 if L == 1 else 10 ** (L - 1))
            b = min(hi, 10 ** L)
            if a >= b:
                continue
            ids = np.arange(a, b, dtype=np.uint64)
            h = np.full(ids.shape, np.uint64(base), dtype=np.uint64)
            P64 = np.uint64(FNV_PRIME)
            for k in range(L - 1, -1, -1):
                digit = (ids // np.uint64(10 ** k)) % np.uint64(10) + np.uint64(48)
                h = (h * P64) ^ digit
            for s in suffixes:
                hs = _np_fnv_bytes(h, s.lower().encode("utf-8"))
                pos = np.searchsorted(game_sorted, hs)
                pos[pos >= len(game_sorted)] = 0
                found = np.nonzero(game_sorted[pos] == hs)[0]
                for j in found.tolist():
                    i = int(ids[j])
                    hits.append((i, prefix + str(i) + s))
    return hits


def hash_many(paths):
    """FNV-1 of the lower-case paths (vectorised over a padded byte matrix when numpy is there)."""
    if np is None or not paths:
        return [path_hash(p) for p in paths]
    enc = [p.lower().encode("utf-8") for p in paths]
    width = max(len(e) for e in enc)
    buf = np.zeros((len(enc), width), dtype=np.uint8)
    lens = np.fromiter((len(e) for e in enc), dtype=np.int64, count=len(enc))
    flat = b"".join(e.ljust(width, b"\0") for e in enc)
    buf[:] = np.frombuffer(flat, dtype=np.uint8).reshape(len(enc), width)
    h = np.full(len(enc), np.uint64(FNV_BASIS), dtype=np.uint64)
    P64 = np.uint64(FNV_PRIME)
    with np.errstate(over="ignore"):
        for c in range(width):
            live = lens > c
            nh = (h * P64) ^ buf[:, c].astype(np.uint64)
            h = np.where(live, nh, h)
    return [int(x) for x in h]


def read_le_csv(path):
    """Live Editor's legacy_filename_hash_list.csv: 'hash;path' rows after a header. Returns the paths."""
    out = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f):
            if i == 0:
                continue
            _h, sep, p = line.rstrip("\r\n").partition(";")
            if sep and p:
                out.append(p)
    return out
