"""Crimson Desert PAZ/PAMT reader: list and extract files from the game's archives.

Format knowledge (PAMT layout, ChaCha20 key derivation, LZ4) comes from
lazorr410/crimson-desert-unpacker (MIT). Re-implemented here for this project.

Usage:
    python paz.py list  <pattern>            # glob over all archives, e.g. "*dye*"
    python paz.py get   <pattern> <outdir>   # extract matching files
"""
import fnmatch
import os
import struct
import sys

import lz4.block
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms

GAME = r"G:\SteamLibrary\steamapps\common\Crimson Desert"


def _rot(v, k):
    return ((v << k) | (v >> (32 - k))) & 0xFFFFFFFF


def hashlittle(data, initval=0):
    M = 0xFFFFFFFF
    length = len(data)
    a = b = c = (0xDEADBEEF + length + initval) & M
    off = 0
    while length > 12:
        a = (a + struct.unpack_from('<I', data, off)[0]) & M
        b = (b + struct.unpack_from('<I', data, off + 4)[0]) & M
        c = (c + struct.unpack_from('<I', data, off + 8)[0]) & M
        a = (a - c) & M; a ^= _rot(c, 4); c = (c + b) & M
        b = (b - a) & M; b ^= _rot(a, 6); a = (a + c) & M
        c = (c - b) & M; c ^= _rot(b, 8); b = (b + a) & M
        a = (a - c) & M; a ^= _rot(c, 16); c = (c + b) & M
        b = (b - a) & M; b ^= _rot(a, 19); a = (a + c) & M
        c = (c - b) & M; c ^= _rot(b, 4); b = (b + a) & M
        off += 12
        length -= 12
    tail = data[off:] + b'\x00' * 12
    if length == 0:
        return c
    if length >= 12:
        c = (c + struct.unpack_from('<I', tail, 8)[0]) & M
    elif length >= 9:
        c = (c + (struct.unpack_from('<I', tail, 8)[0] & (M >> (8 * (12 - length))))) & M
    if length >= 8:
        b = (b + struct.unpack_from('<I', tail, 4)[0]) & M
    elif length >= 5:
        b = (b + (struct.unpack_from('<I', tail, 4)[0] & (M >> (8 * (8 - length))))) & M
    if length >= 4:
        a = (a + struct.unpack_from('<I', tail, 0)[0]) & M
    else:
        a = (a + (struct.unpack_from('<I', tail, 0)[0] & (M >> (8 * (4 - length))))) & M
    c ^= b; c = (c - _rot(b, 14)) & M
    a ^= c; a = (a - _rot(c, 11)) & M
    b ^= a; b = (b - _rot(a, 25)) & M
    c ^= b; c = (c - _rot(b, 16)) & M
    a ^= c; a = (a - _rot(c, 4)) & M
    b ^= a; b = (b - _rot(a, 14)) & M
    c ^= b; c = (c - _rot(b, 24)) & M
    return c


def decrypt(data, path):
    seed = hashlittle(os.path.basename(path).lower().encode(), 0xC5EDE)
    iv = struct.pack('<I', seed) * 4
    kb = seed ^ 0x60616263
    key = b''.join(struct.pack('<I', kb ^ d) for d in
                   (0, 0x0A0A0A0A, 0x0C0C0C0C, 0x06060606, 0x0E0E0E0E, 0x0A0A0A0A, 0x06060606, 0x02020202))
    return Cipher(algorithms.ChaCha20(key, iv), mode=None).encryptor().update(data)


def _varint(d, off):
    v = s = 0
    while True:
        b = d[off]; off += 1
        v |= (b & 0x7F) << s; s += 7
        if b < 0x80:
            return v, off


def _section(d, off):
    """Name tree: records of [varint parent-delta][u8 len][name]; delta 0 = root."""
    size = struct.unpack_from('<I', d, off)[0]; off += 4
    st = off
    recs = {}
    while off < st + size:
        rel = off - st
        delta, off = _varint(d, off)
        n = d[off]
        recs[rel] = (rel - delta if delta else None, d[off + 1:off + 1 + n].decode('utf-8', 'replace'))
        off += 1 + n
    return off, recs


def _join(recs, ref):
    parts = []
    while ref is not None and ref in recs and len(parts) < 256:
        ref, nm = recs[ref][0], recs[ref][1]
        parts.append(nm)
    return ''.join(reversed(parts))


def parse_pamt(pamt_path):
    """PAMT layout as of game 1.0.0.3663 (2.05). Returns (path, paz, offset, csize, osize, flags)."""
    d = open(pamt_path, 'rb').read()
    paz_dir = os.path.dirname(pamt_path)
    off = 4
    n = struct.unpack_from('<I', d, off)[0]; off += 12
    off += n * 8 + (n - 1) * 4
    off, folders = _section(d, off)
    off, nodes = _section(d, off)
    fc = struct.unpack_from('<I', d, off)[0]; off += 4
    frecs = [struct.unpack_from('<IIII', d, off + 16 * i) for i in range(fc)]; off += 16 * fc
    nf = struct.unpack_from('<I', d, off)[0]; off += 4
    recs = [struct.unpack_from('<IIIII', d, off + 20 * i) for i in range(nf)]; off += 20 * nf
    ext_len = struct.unpack_from('<I', d, off)[0]; off += 4
    exts = d[off:off + ext_len].split(b'\x00')
    exts = [e.decode() for e in exts]
    out = []
    for _h, fref, first, cnt in frecs:
        folder = _join(folders, fref)
        for ref, poff, cs, os_, fl in recs[first:first + cnt]:
            ei = fl >> 24
            name = _join(nodes, ref) + (exts[ei] if ei < len(exts) else '')
            out.append((f"{folder}/{name}", os.path.join(paz_dir, f"{fl & 0xFF}.paz"), poff, cs, os_, fl))
    return out


def all_entries():
    for d in sorted(os.listdir(GAME)):
        pamt = os.path.join(GAME, d, "0.pamt")
        if d.isdigit() and os.path.exists(pamt):
            for e in parse_pamt(pamt):
                yield d, e


def read_entry(e):
    path, paz, poff, csize, osize, flags = e
    with open(paz, 'rb') as f:
        f.seek(poff)
        raw = f.read(csize)
    if ((flags >> 20) & 0x0F) == 3:  # ChaCha20 encrypted
        raw = decrypt(raw, path)
    ctype = (flags >> 16) & 0x0F
    if csize != osize and ctype == 2:
        raw = lz4.block.decompress(raw, uncompressed_size=osize)
    return raw


def main():
    cmd, pat = sys.argv[1], sys.argv[2].lower()
    for d, e in all_entries():
        if not fnmatch.fnmatch(e[0].lower(), pat):
            continue
        if cmd == 'list':
            print(f"{d} {e[4]:>10} {e[0]}")
        else:
            out = os.path.join(sys.argv[3], d, e[0].replace('/', os.sep))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            try:
                open(out, 'wb').write(read_entry(e))
                print("ok", d, e[0])
            except Exception as ex:
                print("FAIL", d, e[0], ex)


if __name__ == '__main__':
    main()
