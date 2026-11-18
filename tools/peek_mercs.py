"""Read-only list of the player's mercenaries (horses, A.T.A.G., ...) in the running game (research only).

player actor = [[[exe+0x72451C0]+0x30]+0x50]; mercenary manager = [[actor+0x68]+0x110];
entries: pointer array at +0x18, count +0x20. Entry: +0x24 u16 character key, +0x28 u64 mercenary key.
"""
import ctypes
import ctypes.wintypes as wt
import struct
import subprocess

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq CrimsonDesert.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
h = k32.OpenProcess(0x410, False, int(out.split(",")[1].strip('"')))
mods = (ctypes.c_void_p * 1)()
need = wt.DWORD()
psapi.EnumProcessModules(h, mods, ctypes.sizeof(mods), ctypes.byref(need))
BASE = mods[0]


def rd(a, n):
    b = ctypes.create_string_buffer(n)
    g = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(g)):
        raise OSError("read %X failed" % a)
    return b.raw


def q(a):
    return struct.unpack("<Q", rd(a, 8))[0]


g = q(BASE + 0x72451C0)
actor = q(q(g + 0x30) + 0x50)
comps = q(actor + 0x68)
mgr = q(comps + 0x110)
arr, n = struct.unpack("<QI", rd(mgr + 0x18, 12))
print("actor %X comps %X mercenary manager %X: %d entries" % (actor, comps, mgr, n))
for i in range(min(n, 200)):
    e = q(arr + 8 * i)
    raw = rd(e, 0x100)
    ck = struct.unpack_from("<H", raw, 0x24)[0]
    key = struct.unpack_from("<Q", raw, 0x28)[0]
    print("#%d entry %X  char %5d  key %6d  +0xEE=%d  +0x50=%08X" % (i, e, ck, key, raw[0xEE], struct.unpack_from("<I", raw, 0x50)[0]))
    if key == 7164: print("     20:", raw[0x20:0x100].hex(" "))


# The dye screen's horse lookup inputs (SetDisplayMode +0x115B8BE..): G = [exe+0x7245558] or [exe+0x7245560]
for gl in (0x7245558, 0x7245560):
    try:
        G = q(q(BASE + gl))
        o = q(G + 8)
        cat = struct.unpack("<H", rd(q(o + 0x3F0) + 0x9A, 2))[0]
        t = q(o + 0x178)
        horse, robot = struct.unpack("<HH", rd(t + 0x78, 4))
        print("global +0x%X: category %d, horse type %d, robot type %d" % (gl, cat, horse, robot))
    except OSError as ex:
        print("global +0x%X: %s" % (gl, ex))
try:
    a2 = q(q(g + 0x30) + 0x58)
    print("controlled actor? %X" % a2)
except OSError as ex:
    print(ex)
pk = struct.unpack("<H", rd(q(comps + 0x20) + 0x30, 2))[0]
print("player character key (main actor) %d" % pk)
