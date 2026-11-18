"""Read-only look at the open dye screen's color tables (research only).

    python peek_dye.py <dyeScreenHex>
Prints the picked ([this+0x398]) and saved ([this+0x3B8]) tables the way the mod reads them.
"""
import ctypes
import ctypes.wintypes as wt
import struct
import subprocess
import sys

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq CrimsonDesert.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
h = k32.OpenProcess(0x410, False, int(out.split(",")[1].strip('"')))


def rd(a, n):
    b = ctypes.create_string_buffer(n)
    g = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(g)):
        raise OSError("read %X failed" % a)
    return b.raw


self_ = int(sys.argv[1], 16)
print("state 0x41C =", rd(self_ + 0x41C, 1)[0], " tab 0x443 =", rd(self_ + 0x443, 1)[0], " built 0x445 =", rd(self_ + 0x445, 1)[0])
for name, cnt, arr in (("picked", 0x39C, 0x3B0), ("saved", 0x3BC, 0x3D0)):
    n = struct.unpack("<I", rd(self_ + cnt, 4))[0]
    print(name, "entries:", n)
    if not n or n > 64:
        continue
    base = struct.unpack("<Q", rd(self_ + arr, 8))[0]
    for i in range(n):
        e = struct.unpack("<Q", rd(base + 8 * i, 8))[0]
        slot = struct.unpack("<h", rd(e + 4, 2))[0]
        recs, rn = struct.unpack("<QI", rd(e + 8, 12))
        print("  gear slot %d: %d records" % (slot, rn))
        for j in range(min(rn, 12)):
            print("     ", rd(recs + 16 * j, 16).hex(" "))
