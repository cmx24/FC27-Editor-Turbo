#!/usr/bin/env python3
"""Minimal vbaProject.bin reader/patcher (stdlib only): OLE compound file + MS-OVBA decompression.

Copied unchanged from turbo_dev\masters\vba_tool.py (2026-10-04); build_callname_master.py uses patch_text to point
the FC 27 workbook's Play macro at the FC 27 audio folder.

python vba_tool.py dump <file.xlsm|vbaProject.bin>          -> prints every module's source
python vba_tool.py find <file> <text>                        -> finds text in raw streams (ascii + utf-16)
"""
import struct
import sys
import zipfile

FREESECT, ENDOFCHAIN = 0xFFFFFFFF, 0xFFFFFFFE


class Ole:
    def __init__(self, data):
        self.data = bytearray(data)
        h = self.data
        assert h[:8] == b'\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1'
        self.ssz = 1 << struct.unpack_from('<H', h, 30)[0]
        self.mssz = 1 << struct.unpack_from('<H', h, 32)[0]
        nfat, self.dir_start, = struct.unpack_from('<II', h, 44)[0], struct.unpack_from('<I', h, 48)[0]
        self.cutoff = struct.unpack_from('<I', h, 56)[0]
        self.minifat_start, nminifat, difat_start, ndifat = struct.unpack_from('<IIII', h, 60)
        difat = list(struct.unpack_from('<109I', h, 76))
        s = difat_start
        for _ in range(ndifat):
            off = self.off(s)
            n = self.ssz // 4
            vals = struct.unpack_from('<%dI' % n, h, off)
            difat += vals[:-1]
            s = vals[-1]
        fat_secs = [x for x in difat if x not in (FREESECT, ENDOFCHAIN)][:nfat]
        self.fat = []
        for fs in fat_secs:
            self.fat += struct.unpack_from('<%dI' % (self.ssz // 4), h, self.off(fs))
        # directory
        self.dir_chain = self.chain(self.dir_start)
        raw = b''.join(bytes(h[self.off(x):self.off(x) + self.ssz]) for x in self.dir_chain)
        self.entries = []
        for i in range(len(raw) // 128):
            e = raw[i * 128:(i + 1) * 128]
            nl = struct.unpack_from('<H', e, 64)[0]
            name = e[:max(nl - 2, 0)].decode('utf-16le', 'replace')
            typ = e[66]
            left, right, child = struct.unpack_from('<III', e, 68)
            start, size = struct.unpack_from('<IQ', e, 116)
            self.entries.append(dict(i=i, name=name, type=typ, left=left, right=right, child=child, start=start,
                                     size=size & 0xFFFFFFFF))
        root = self.entries[0]
        self.ministream_chain = self.chain(root['start'])
        self.minifat = []
        if nminifat:
            for x in self.chain(self.minifat_start):
                self.minifat += struct.unpack_from('<%dI' % (self.ssz // 4), h, self.off(x))
        # paths
        self.paths = {}
        self._walk(root['child'], '')

    def _walk(self, i, prefix):
        if i >= len(self.entries) or i in (FREESECT, ENDOFCHAIN, 0xFFFFFFFF):
            return
        e = self.entries[i]
        if e['type'] == 0:
            return
        p = prefix + e['name']
        self.paths[p] = e
        self._walk(e['left'], prefix)
        self._walk(e['right'], prefix)
        if e['type'] == 1:
            self._walk(e['child'], p + '/')

    def off(self, sec):
        return (sec + 1) * self.ssz

    def chain(self, s, fat=None):
        fat = self.fat if fat is None else fat
        out, seen = [], set()
        while s not in (ENDOFCHAIN, FREESECT) and s < len(fat) and s not in seen:
            seen.add(s)
            out.append(s)
            s = fat[s]
        return out

    def _spans(self, e):
        """(absolute file offset, length) pieces of a stream, in order."""
        size = e['size']
        spans = []
        if size < self.cutoff:
            for ms in self.chain(e['start'], self.minifat):
                pos = ms * self.mssz
                big = self.ministream_chain[pos // self.ssz]
                spans.append((self.off(big) + pos % self.ssz, self.mssz))
        else:
            for s in self.chain(e['start']):
                spans.append((self.off(s), self.ssz))
        out, left = [], size
        for o, n in spans:
            n = min(n, left)
            if n <= 0:
                break
            out.append((o, n))
            left -= n
        return out

    def read(self, path):
        e = self.paths[path]
        return b''.join(bytes(self.data[o:o + n]) for o, n in self._spans(e))

    def write(self, path, new):
        """Overwrite a stream in place; the new content must have exactly the same size."""
        e = self.paths[path]
        assert len(new) == e['size'], (path, len(new), e['size'])
        p = 0
        for o, n in self._spans(e):
            self.data[o:o + n] = new[p:p + n]
            p += n


def decompress(buf):
    """MS-OVBA 2.4.1 decompression of a CompressedContainer."""
    assert buf[0] == 1, 'bad signature'
    out = bytearray()
    pos = 1
    while pos < len(buf):
        hdr = struct.unpack_from('<H', buf, pos)[0]
        size = (hdr & 0x0FFF) + 3
        flag = hdr >> 15
        chunk = buf[pos + 2:pos + size]
        pos += size
        if not flag:
            out += chunk[:4096]
            continue
        start = len(out)
        i = 0
        while i < len(chunk):
            fb = chunk[i]
            i += 1
            for bit in range(8):
                if i >= len(chunk):
                    break
                if not fb & (1 << bit):
                    out.append(chunk[i])
                    i += 1
                else:
                    tok = struct.unpack_from('<H', chunk, i)[0]
                    i += 2
                    d = len(out) - start
                    bc = max((d - 1).bit_length(), 4)
                    lmask = 0xFFFF >> bc
                    length = (tok & lmask) + 3
                    offset = (tok >> (16 - bc)) + 1
                    for _ in range(length):
                        out.append(out[-offset])
    return bytes(out)


def compress(data):
    """MS-OVBA 2.4.1 compression (greedy matcher)."""
    out = bytearray(b'\x01')
    for cs in range(0, max(len(data), 1), 4096):
        chunk = data[cs:cs + 4096]
        body = bytearray()
        i = 0
        while i < len(chunk):
            flag_pos = len(body)
            body.append(0)
            fb = 0
            for bit in range(8):
                if i >= len(chunk):
                    break
                bc = max((i - 1).bit_length(), 4)
                lmask = 0xFFFF >> bc
                maxlen = lmask + 3
                maxoff = 1 << bc
                best_len, best_off = 0, 0
                lo = max(0, i - maxoff)
                for j in range(i - 1, lo - 1, -1):
                    ln = 0
                    while ln < maxlen and i + ln < len(chunk) and chunk[j + ln] == chunk[i + ln]:
                        ln += 1
                    if ln > best_len:
                        best_len, best_off = ln, i - j
                        if ln == maxlen:
                            break
                if best_len >= 3:
                    tok = ((best_off - 1) << (16 - bc)) | (best_len - 3)
                    body += struct.pack('<H', tok)
                    fb |= 1 << bit
                    i += best_len
                else:
                    body.append(chunk[i])
                    i += 1
            body[flag_pos] = fb
        if len(body) < 4096:
            out += struct.pack('<H', 0xB000 | (len(body) + 2 - 3)) + body
        else:  # raw chunk
            out += struct.pack('<H', 0x3000 | (4096 + 2 - 3)) + chunk.ljust(4096, b'\0')
    return bytes(out)


def parse_dir(ole):
    """Return [(module name, stream name, text offset)] and the code page from the dir stream."""
    d = decompress(ole.read('VBA/dir'))
    pos, mods, cur, cp = 0, [], {}, 1252
    while pos + 6 <= len(d):
        rid, size = struct.unpack_from('<HI', d, pos)
        pos += 6
        if rid == 0x0009:  # PROJECTVERSION: size field is 4 but record is 6 bytes
            size = 6
        val = d[pos:pos + size]
        pos += size
        if rid == 0x0003:
            cp = struct.unpack_from('<H', val)[0]
        elif rid == 0x0019:
            cur = {'name': val.decode('latin-1')}
        elif rid == 0x001A:
            cur['stream'] = val.decode('latin-1')
        elif rid == 0x0031:
            cur['offset'] = struct.unpack_from('<I', val)[0]
        elif rid == 0x002B:
            mods.append(cur)
            cur = {}
    return mods, cp


def patch_text(vba_bin, old, new):
    """Same-length text patch of a vbaProject.bin: compressed module source, p-code (performance cache) literals and
    the __SRP_ caches (UTF-16). Stream sizes never change, so the compound file stays valid. Returns (bytes, report)."""
    old_b, new_b = old.encode('cp1252'), new.encode('cp1252')
    assert len(old_b) == len(new_b), 'same byte length required'
    ole = Ole(vba_bin)
    mods, cp = parse_dir(ole)
    report = []
    by_stream = {'VBA/' + m['stream']: m for m in mods}
    for p, e in sorted(ole.paths.items()):
        if e['type'] != 2 or not p.startswith('VBA/'):
            continue
        raw = bytearray(ole.read(p))
        m = by_stream.get(p)
        if m:
            off = m['offset']
            src = decompress(bytes(raw[off:]))
            want = src.replace(old_b, new_b)
            pc = bytes(raw[:off])
            n_pc = pc.count(old_b)
            raw[:off] = pc.replace(old_b, new_b)
            if want != src:
                comp = bytes(raw[off:])
                # 1) the old text normally sits as literal bytes in the compressed container: patch them in place
                # parts of the old text can be back-references, so patch only a literal window around the bytes
                # that differ, widest window first, and accept it only if the whole module decompresses to `want`
                pre = next(i for i in range(len(old_b)) if old_b[i] != new_b[i])
                suf = next(i for i in range(len(old_b)) if old_b[-1 - i] != new_b[-1 - i])
                end = len(old_b) - suf
                patched = None
                windows = sorted({(a, b) for a in range(pre + 1) for b in range(end, len(old_b) + 1)},
                                 key=lambda w: -(w[1] - w[0]))
                for a, b in windows:
                    o, n = old_b[a:b], new_b[a:b]
                    if comp.count(o) != src.count(old_b):
                        continue
                    cand = comp.replace(o, n)
                    try:
                        if decompress(cand) == want:
                            patched = cand
                            break
                    except Exception:
                        pass
                if patched is None:
                    raise RuntimeError('in-place literal patch impossible for %s (needs a recompress)' % p)
                raw[off:] = patched
                assert decompress(bytes(raw[off:])) == want
                report.append('%s: source %d, p-code %d' % (p, src.count(old_b), n_pc))
            elif n_pc:
                report.append('%s: p-code %d' % (p, n_pc))
        else:
            n = raw.count(old_b) + raw.count(old.encode('utf-16le'))
            raw = bytearray(bytes(raw).replace(old_b, new_b).replace(old.encode('utf-16le'), new.encode('utf-16le')))
            if n:
                report.append('%s: %d' % (p, n))
        ole.write(p, bytes(raw))
    out = bytes(ole.data)
    assert len(out) == len(vba_bin)
    return out, report


def replace_zip_member(path, member, data):
    tmp = path + '.ziptmp'
    with zipfile.ZipFile(path) as zin, zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zout:
        for it in zin.infolist():
            zout.writestr(it, data if it.filename == member else zin.read(it.filename))
    import os
    os.replace(tmp, path)


def load(path):
    data = open(path, 'rb').read()
    if data[:2] == b'PK':
        data = zipfile.ZipFile(path).read('xl/vbaProject.bin')
    return Ole(data)


def main():
    cmd, path = sys.argv[1], sys.argv[2]
    ole = load(path)
    if cmd == 'dump':
        mods, cp = parse_dir(ole)
        for m in mods:
            raw = ole.read('VBA/' + m['stream'])
            src = decompress(raw[m['offset']:]).decode('cp%d' % cp, 'replace')
            print('=' * 20, m['name'], 'stream', m['stream'], 'size', len(raw), 'textoffset', m['offset'])
            print(src)
    elif cmd == 'find':
        needles = [sys.argv[3].encode('cp1252'), sys.argv[3].encode('utf-16le')]
        for p, e in sorted(ole.paths.items()):
            if e['type'] != 2:
                continue
            b = ole.read(p)
            for nd in needles:
                k = b.find(nd)
                while k >= 0:
                    print(p, e['size'], 'at', k, 'utf16' if nd[1:2] == b'\0' else 'ansi', repr(b[max(0, k - 8):k + len(nd) + 8]))
                    k = b.find(nd, k + 1)
    elif cmd == 'list':
        for p, e in sorted(ole.paths.items()):
            print(p, e['type'], e['size'])


if __name__ == '__main__':
    main()
