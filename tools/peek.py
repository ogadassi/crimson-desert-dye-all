"""Read-only look at the running game's inventory slots (research only).

    python peek.py <holderHex> <bucket> <slot> [<slot> ...]
Dumps the 0xC8-byte slot records so fields can be matched against request payloads.
"""
import ctypes
import ctypes.wintypes as wt
import struct
import subprocess
import sys

PROCESS_VM_READ, PROCESS_QUERY_INFORMATION = 0x10, 0x400
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


def pid_of(name):
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"], capture_output=True, text=True).stdout
    return int(out.split(",")[1].strip('"'))


h = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, pid_of("CrimsonDesert.exe"))
if not h:
    sys.exit("OpenProcess failed %d" % ctypes.get_last_error())


def rd(addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t()
    if not k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
        raise OSError("read %x failed %d" % (addr, ctypes.get_last_error()))
    return buf.raw


holder = int(sys.argv[1], 16)
bucket = int(sys.argv[2])
barr = struct.unpack("<Q", rd(holder + 0x18, 8))[0]
bk = struct.unpack("<Q", rd(barr + 8 * bucket, 8))[0]
slots = struct.unpack("<Q", rd(bk, 8))[0]
for s in sys.argv[3:]:
    s = int(s)
    rec = rd(slots + s * 0xC8, 0xC8)
    print("slot", s)
    for o in range(0, 0xC8, 16):
        chunk = rec[o:o + 16]
        print("  %02X: %s" % (o, " ".join("%02X" % b for b in chunk)))
