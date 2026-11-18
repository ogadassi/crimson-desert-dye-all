"""Static analysis helpers for CrimsonDesert.exe (research only; nothing here touches the game).

The exe's section names are scrambled by its protector: ".reloc" is the real code section,
".00cfg" holds read-only data and RTTI, ".sxdata" is writable data.
"""
import os
import re
import struct

import numpy as np
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

EXE = r"G:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe"
CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "research", "cache")
BASE = 0x140000000

_pe = pefile.PE(EXE, fast_load=True)
_raw = open(EXE, 'rb').read()
SECTIONS = [(s.Name.rstrip(b'\0').decode(), s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData)
            for s in _pe.sections]
TEXT = next(s for s in SECTIONS if s[0] == '.reloc')
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False


def rva2off(rva):
    for _n, va, vs, po, ps in SECTIONS:
        if va <= rva < va + max(vs, ps):
            return po + (rva - va) if rva - va < ps else None
    return None


def off2rva(off):
    for _n, va, vs, po, ps in SECTIONS:
        if po <= off < po + ps:
            return va + off - po
    return None


def read(rva, n):
    o = rva2off(rva)
    return _raw[o:o + n] if o is not None else b''


def u32(rva): return struct.unpack('<I', read(rva, 4))[0]
def u64(rva): return struct.unpack('<Q', read(rva, 8))[0]


def find_bytes(pat):
    return [off2rva(m.start()) for m in re.finditer(re.escape(pat), _raw)]


def cstr(rva, n=200):
    b = read(rva, n)
    return b.split(b'\0')[0].decode('latin1')


# ---- rip-relative reference index over the code section (cached) ----------------------
def _build_riprefs():
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, "riprefs.npz")
    if os.path.exists(path):
        z = np.load(path)
        return z['src'], z['dst']
    _n, va, vs, po, ps = TEXT
    code = _raw[po:po + ps]
    srcs, dsts = [], []
    # lea/mov/cmp reg, [rip+disp32]: REX + opcode + modrm(00 reg 101) + disp32
    pat = re.compile(rb'[\x48\x4c](?:\x8d|\x8b|\x89|\x39|\x3b|\x63)[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', re.S)
    for m in pat.finditer(code):
        i = m.start()
        disp = struct.unpack_from('<i', code, i + 3)[0]
        srcs.append(va + i)
        dsts.append(va + i + 7 + disp)
    # call/jmp rel32
    pat2 = re.compile(rb'[\xe8\xe9]', re.S)
    for m in pat2.finditer(code):
        i = m.start()
        if i + 5 > len(code):
            continue
        disp = struct.unpack_from('<i', code, i + 1)[0]
        t = va + i + 5 + disp
        if va <= t < va + ps:
            srcs.append(va + i)
            dsts.append(t)
    src = np.array(srcs, dtype=np.int64)
    dst = np.array(dsts, dtype=np.int64)
    order = np.argsort(dst)
    src, dst = src[order], dst[order]
    np.savez(path, src=src, dst=dst)
    return src, dst


_SRC = _DST = None


def xrefs(target_rva):
    global _SRC, _DST
    if _SRC is None:
        _SRC, _DST = _build_riprefs()
    lo = np.searchsorted(_DST, target_rva)
    hi = np.searchsorted(_DST, target_rva, side='right')
    return sorted(int(x) for x in _SRC[lo:hi])


def data_refs(value, size=8):
    """Find where a pointer/dword value is stored in the image (non-code)."""
    pat = struct.pack('<Q' if size == 8 else '<I', value)
    return find_bytes(pat)


# ---- RTTI -------------------------------------------------------------------------------
def vtables_for(mangled):
    """mangled like '.?AVUIGamePlayControlRootDyeing@uiCommonScript@pa@@' -> list of vtable RVAs."""
    out = []
    for srva in find_bytes(mangled.encode() + b'\0'):
        td = srva - 0x10
        for col_off in find_bytes(struct.pack('<I', td)):
            col = col_off - 0xC
            if read(col, 4) != b'\x01\x00\x00\x00':
                continue
            for p in data_refs(BASE + col):
                out.append((p + 8, u32(col + 4)))  # (vtable rva, offset of this subobject)
    return out


def func_start(rva, back=0x4000):
    """Walk back to the nearest int3/padding boundary (rough function start)."""
    b = read(rva - back, back)
    i = len(b) - 1
    while i > 0:
        if b[i] == 0xCC and b[i - 1] == 0xCC:
            j = i + 1
            while b[j] == 0xCC:
                j += 1
            return rva - back + j
        i -= 1
    return None


def disasm(rva, n=60, until_ret=True):
    lines = []
    code = read(rva, n * 15)
    for ins in md.disasm(code, BASE + rva):
        r = ins.address - BASE
        extra = ''
        if 'rip' in ins.op_str:
            m = re.search(r'rip ([+-]) (0x[0-9a-f]+)', ins.op_str)
            if m:
                t = r + ins.size + (int(m.group(2), 16) * (1 if m.group(1) == '+' else -1))
                s = cstr(t, 80)
                extra = f'  ; -> {t:#x}' + (f' "{s}"' if len(s) >= 3 and s.isprintable() else '')
        elif ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x'):
            extra = f'  ; -> {int(ins.op_str, 16) - BASE:#x}'
        lines.append(f'{r:#010x}: {ins.mnemonic:6} {ins.op_str}{extra}')
        if len(lines) >= n or (until_ret and ins.mnemonic in ('ret', 'int3')):
            break
    return '\n'.join(lines)


_CODE_I32 = None


def riprefs_any(target_rva, extra=(0, 1, 4)):
    """Brute-force: every disp32 in code that resolves to target (any opcode). extra = imm bytes after disp."""
    global _CODE_I32
    _n, va, vs, po, ps = TEXT
    if _CODE_I32 is None:
        b = np.frombuffer(_raw, dtype=np.uint8, count=ps - 4, offset=po).astype(np.int64)
        b1 = np.frombuffer(_raw, dtype=np.uint8, count=ps - 4, offset=po + 1).astype(np.int64)
        b2 = np.frombuffer(_raw, dtype=np.uint8, count=ps - 4, offset=po + 2).astype(np.int64)
        b3 = np.frombuffer(_raw, dtype=np.uint8, count=ps - 4, offset=po + 3).astype(np.int64)
        v = b | (b1 << 8) | (b2 << 16) | (b3 << 24)
        v = np.where(v >= 1 << 31, v - (1 << 32), v)
        _CODE_I32 = v + np.arange(ps - 4, dtype=np.int64) + va + 4  # target if no imm follows
    res = []
    for e in extra:
        idx = np.nonzero(_CODE_I32 + e == target_rva)[0]
        res += [int(va + i) for i in idx]
    return sorted(res)
