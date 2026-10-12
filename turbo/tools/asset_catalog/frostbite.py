"""Read-only Frostbite readers for FC 27: DbObject (layout.toc), toc bundle and chunk tables, bundle manifests, cas
files and the Oodle-compressed block stream. Ported from the asset research scripts (dbobject.py, toc_bundles.py,
legacy_manifest.py). Nothing here writes to the game folder."""
import ctypes
import os
import struct

TOC_DATA = 0x22C              # start of the toc payload after the signed header
MANIFEST_MAGIC = 0x9D798ED6   # bundle manifest (location 0 of an inline bundle)


# ---------------------------------------------------------------- DbObject
def _varint(d, p):
    r = 0
    s = 0
    while True:
        b = d[p]
        p += 1
        r |= (b & 0x7F) << s
        s += 7
        if not b & 0x80:
            return r, p


def _read_value(d, p):
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
    if tt == 1:  # list
        size, p = _varint(d, p)
        end = p + size
        out = []
        while p < end:
            n, v, p = _read_value(d, p)
            if n is None and v is None:
                break
            out.append(v)
        return name, out, end
    if tt == 2:  # object
        size, p = _varint(d, p)
        end = p + size
        out = {}
        while p < end:
            n, v, p = _read_value(d, p)
            if n is None and v is None:
                break
            out[n] = v
        return name, out, end
    if tt == 6:
        return name, bool(d[p]), p + 1
    if tt == 7:
        n, p = _varint(d, p)
        return name, d[p:p + n - 1].decode("utf-8", "replace"), p + n
    if tt == 8:
        return name, struct.unpack_from("<i", d, p)[0], p + 4
    if tt == 9:
        return name, struct.unpack_from("<q", d, p)[0], p + 8
    if tt == 0xB:
        return name, struct.unpack_from("<f", d, p)[0], p + 4
    if tt == 0xC:
        return name, struct.unpack_from("<d", d, p)[0], p + 8
    if tt == 0xF:
        return name, d[p:p + 16].hex(), p + 16
    if tt == 0x10:
        return name, d[p:p + 20].hex(), p + 20
    if tt == 0x13:
        n, p = _varint(d, p)
        return name, d[p:p + n].hex(), p + n
    raise ValueError("DbObject type %#x at %d" % (t, p - 1))


def dbobject(data):
    """Parse a DbObject blob (layout.toc). A signed file starts with 00 D1 CE 01 and has its object at 0x22C."""
    off = TOC_DATA if data[:4] == b"\x00\xd1\xce\x01" else 0
    return _read_value(data, off)[1]


def layout_packages(game):
    """persistentIndex (8 hex digits) -> install chunk name, from Data\\layout.toc (cas id bytes 2..5)."""
    with open(os.path.join(game, "Data", "layout.toc"), "rb") as f:
        lay = dbobject(f.read())
    return {"%08x" % (c["persistentIndex"] & 0xFFFFFFFF): c["name"] for c in lay["installManifest"]["installChunks"]}


# ---------------------------------------------------------------- Oodle blocks
class Oodle:
    def __init__(self, game):
        self.dll = ctypes.WinDLL(os.path.join(game, "oo2core_9_win64.dll"))
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
            raise ValueError("oodle %d != %d" % (n, rawlen))
        return out.raw


def read_blocks(data, oodle=None):
    """Frostbite block stream: 8-byte headers (BE size, LE code, BE csize); type 0 = stored, 0x11/0x15/0x19 = Oodle."""
    out, p, n = [], 0, len(data)
    while p < n:
        dsize = struct.unpack_from(">I", data, p)[0]
        code = struct.unpack_from("<H", data, p + 4)[0]
        csize = struct.unpack_from(">H", data, p + 6)[0] + (((code >> 8) & 0x0F) << 16)
        ctype = code & 0x7F
        p += 8
        if ctype == 0:
            out.append(data[p:p + dsize])
        elif ctype in (0x11, 0x15, 0x19):
            if oodle is None:
                raise ValueError("Oodle block without a decompressor")
            out.append(oodle.decompress(data[p:p + csize], dsize & 0xFFFFFF))
        else:
            raise ValueError("block type %#x" % ctype)
        p += csize
    return b"".join(out)


# ---------------------------------------------------------------- toc
def parse_toc_bundles(d):
    """Bundles of a toc payload: list of None (not inline) or [(cas_id_hex, offset, size, flag), ...] per bundle."""
    h = struct.unpack_from(">15I", d, TOC_DATA)
    bundles = []
    for i in range(h[2]):
        _nm, size, off = struct.unpack_from(">IIQ", d, TOC_DATA + h[1] + i * 16)
        if not size & 0x40000000:
            bundles.append(None)
            continue
        o = TOC_DATA + off
        bd = d[o:o + (size & 0x3FFFFFFF)]
        hdr = struct.unpack_from(">9I", bd, 0)
        flags_off, count, p = hdr[2], hdr[3], hdr[4]
        flags = bd[flags_off:flags_off + count]
        locs, cas = [], None
        for k in range(count):
            fl = flags[k]
            if fl & 0x80:
                cas = bd[p:p + 8].hex()
                p += 8
            off2, sz = struct.unpack_from(">II", bd, p)
            p += 8
            locs.append((cas, off2, sz, fl))
        bundles.append(locs)
    return bundles


def parse_toc_chunks(d):
    """Chunk table of a toc payload: guid hex (as stored in EBX) -> (cas_id_hex, offset, size)."""
    h = struct.unpack_from(">15I", d, TOC_DATA)
    nch, gofs, eofs = h[5], TOC_DATA + h[4], TOC_DATA + h[6]
    out = {}
    for k in range(nch):
        o = gofs + k * 20
        guid = d[o:o + 16][::-1].hex()
        v = struct.unpack_from(">I", d, o + 16)[0]
        e = eofs + ((v & 0x00FFFFFF) // 4) * 16
        out[guid] = (d[e:e + 8].hex(), *struct.unpack_from(">II", d, e + 8))
    return out


def _cstr(d, o):
    e = d.index(b"\0", o)
    return d[o:e].decode("utf-8", "replace")


def manifest_names(blob):
    """EBX names and res names of a bundle manifest (magic 0x9D798ED6 at +4)."""
    magic = struct.unpack_from("<I", blob, 4)[0]
    if magic != MANIFEST_MAGIC:
        raise ValueError("manifest magic %08x" % magic)
    total, ne, nr, _nc, so, _mo, _ms = struct.unpack_from("<7I", blob, 8)
    p = 0x24 + 20 * total
    strings = 4 + so
    ebx = []
    for _ in range(ne):
        no = struct.unpack_from("<I", blob, p)[0]
        ebx.append(_cstr(blob, strings + no))
        p += 8
    return ebx


class Game:
    """Read-only access to one FC 27 install: cas files through layout.toc, Oodle through the game's own dll."""

    def __init__(self, root):
        self.root = root
        self._pkg = None
        self._oodle = None
        self._files = {}

    @property
    def pkg(self):
        if self._pkg is None:
            self._pkg = layout_packages(self.root)
        return self._pkg

    @property
    def oodle(self):
        if self._oodle is None:
            self._oodle = Oodle(self.root)
        return self._oodle

    def cas_path(self, cas):
        name = self.pkg.get(cas[4:12])
        if name is None:
            return None
        layer = "Patch" if cas[:4] != "0000" else "Data"
        return os.path.join(self.root, layer, "Win32", name.replace("/", os.sep), "cas_%02d.cas" % int(cas[12:16], 16))

    def raw(self, loc):
        p = self.cas_path(loc[0])
        f = self._files.get(p)
        if f is None:
            if p is None or not os.path.isfile(p):
                raise FileNotFoundError("cas %s -> %s" % (loc[0], p))
            f = self._files[p] = open(p, "rb")
        f.seek(loc[1])
        return f.read(loc[2])

    def manifest(self, locs):
        blob = self.raw(locs[0])
        if struct.unpack_from("<I", blob, 4)[0] != MANIFEST_MAGIC:
            blob = read_blocks(blob, self.oodle)
        return manifest_names(blob)

    def close(self):
        for f in self._files.values():
            f.close()
        self._files.clear()


def build_key(exe_bytes):
    """PE TimeDateStamp-SizeOfImage in upper-case hex (the key Turbo's signature scan uses)."""
    if exe_bytes[:2] != b"MZ":
        raise ValueError("not a PE file")
    pe = struct.unpack_from("<I", exe_bytes, 0x3C)[0]
    if exe_bytes[pe:pe + 4] != b"PE\0\0":
        raise ValueError("no PE signature")
    stamp = struct.unpack_from("<I", exe_bytes, pe + 8)[0]
    size_of_image = struct.unpack_from("<I", exe_bytes, pe + 24 + 56)[0]
    return "%08X-%08X" % (stamp, size_of_image)


def exe_build_key(path):
    with open(path, "rb") as f:
        head = f.read(4096)
    return build_key(head)
