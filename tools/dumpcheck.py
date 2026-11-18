"""Which module crashed in a minidump, and which modules have return addresses on the crashing thread's stack.

    python dumpcheck.py <file.dmp>
Read-only; research only.
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
sig, ver, nstreams, dir_rva = struct.unpack_from("<IIII", d, 0)
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from("<III", d, dir_rva + 12 * i)
    streams[t] = (size, rva)


def mstr(rva):
    n = struct.unpack_from("<I", d, rva)[0]
    return d[rva + 4:rva + 4 + n].decode("utf-16le")


mods = []
size, rva = streams[4]
n = struct.unpack_from("<I", d, rva)[0]
for i in range(n):
    base, msize = struct.unpack_from("<QI", d, rva + 4 + 108 * i)
    name_rva = struct.unpack_from("<I", d, rva + 4 + 108 * i + 20)[0]
    mods.append((base, msize, mstr(name_rva).split("\\")[-1]))


def where(a):
    for b, s, nm in mods:
        if b <= a < b + s:
            return "%s+0x%X" % (nm, a - b)
    return None


size, rva = streams[6]
tid = struct.unpack_from("<I", d, rva)[0]
code, flags, rec, addr = struct.unpack_from("<IIQQ", d, rva + 8)
print("thread %d exception 0x%08X at %s" % (tid, code, where(addr) or hex(addr)))

# Memory list (5) or Memory64 (9) to read the stack
mem = []
if 5 in streams:
    size, rva = streams[5]
    n = struct.unpack_from("<I", d, rva)[0]
    for i in range(n):
        start, dsize, drva = struct.unpack_from("<QII", d, rva + 4 + 16 * i)
        mem.append((start, dsize, drva))
size, rva = streams[3]
n = struct.unpack_from("<I", d, rva)[0]
for i in range(n):
    off = rva + 4 + 48 * i
    t_id = struct.unpack_from("<I", d, off)[0]
    if t_id != tid:
        continue
    stack_start, ssize, srva = struct.unpack_from("<QII", d, off + 24)
    stack = d[srva:srva + ssize]
    hits = {}
    for k in range(0, len(stack) - 8, 8):
        w = where(struct.unpack_from("<Q", stack, k)[0])
        if w:
            m = w.split("+")[0]
            hits.setdefault(m, []).append(w)
    print("modules with addresses on the crashing thread's stack (%d bytes):" % ssize)
    for m, ws in hits.items():
        print("  %-28s %3d  e.g. %s" % (m, len(ws), ", ".join(ws[:3])))
print("DyeAll loaded:", any(nm.lower().startswith("dyeall") for _b, _s, nm in mods))
