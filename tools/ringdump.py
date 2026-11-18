"""Read test 16's call recording straight out of the running game (read-only, research only).

    python ringdump.py > research/trace_dump.txt

Finds the recorder's 16 MB ring buffer (64-byte records: u32 tick, u32 thread, u32 site id, pad,
u64 caller, u64 rcx, rdx, r8, r9, 5th arg), names the sites the way test 16 numbered them, and prints
every call around the tab-bar activity, minus calls that run every frame.
"""
import collections
import ctypes
import ctypes.wintypes as wt
import struct
import subprocess
import sys

PROCESS_VM_READ, PROCESS_QUERY_INFORMATION = 0x10, 0x400
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
k32.OpenProcess.restype = wt.HANDLE
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_uint64), ("AllocationBase", ctypes.c_uint64), ("AllocationProtect", wt.DWORD),
                ("__a", wt.DWORD), ("RegionSize", ctypes.c_uint64), ("State", wt.DWORD), ("Protect", wt.DWORD),
                ("Type", wt.DWORD), ("__b", wt.DWORD)]


k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
k32.VirtualQueryEx.restype = ctypes.c_size_t


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
        return None
    return buf.raw


mods = (ctypes.c_void_p * 1)()
need = wt.DWORD()
psapi.EnumProcessModules(h, mods, ctypes.sizeof(mods), ctypes.byref(need))
BASE = mods[0]

# Site names, numbered as test 16 installed them.
names = ["(unused)", "TabBar.SelectTab", "TabBar.ConfirmPendingMove", "TabBar.NextIndex", "Dye.SetDisplayMode",
         "Dye.ReadEquippedGear", "Dye.WidgetReset", "Dye.AskDiscard", "Dye.Fn1160B60", "List.Fn_E2BCB0", "List.Fn_E2C260"]
for cls, n in [("Dyeing", 172), ("TabBar", 167), ("CommonDefault@1F8", 167), ("EquipmentSlot@238", 167),
               ("GridList@248", 182), ("CheckBox@268", 167)]:
    names += ["%s::v%d" % (cls, i) for i in range(n)]

RING = 1 << 18
SIZE = RING * 64
addr = 0
ring = None
mbi = MBI()
while k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)):
    if mbi.State == 0x1000 and mbi.Protect == 0x04 and mbi.RegionSize == SIZE and mbi.Type == 0x20000:
        data = rd(mbi.BaseAddress, SIZE)
        if data:
            ids = [struct.unpack_from("<I", data, i * 64 + 8)[0] for i in range(0, RING, 997)]
            if all(x < len(names) for x in ids) and any(ids):
                ring = data
                print("# ring at %X" % mbi.BaseAddress)
                break
    addr = mbi.BaseAddress + mbi.RegionSize
    if addr >= 1 << 47:
        break
if ring is None:
    sys.exit("ring buffer not found")

recs = []
for i in range(RING):
    t, tid, sid, _p, ret, a1, a2, a3, a4, a5 = struct.unpack_from("<IIIIQQQQQQ", ring, i * 64)
    if t == 0:
        continue
    recs.append((t, i, tid, sid, ret, a1, a2, a3, a4, a5))
recs.sort()
print("# %d records, ticks %d..%d (%.1f s)" % (len(recs), recs[0][0], recs[-1][0], (recs[-1][0] - recs[0][0]) / 1000))


def rva(p):
    return "+0x%07X" % (p - BASE) if BASE <= p < BASE + 0x10000000 else "%X" % p


# Calls that happen every frame: seen in more than 40% of the 100 ms buckets.
buckets = collections.defaultdict(set)
for r in recs:
    buckets[(r[3], r[4])].add(r[0] // 100)
nb = len({r[0] // 100 for r in recs})
noisy = {k for k, v in buckets.items() if len(v) > 0.4 * nb}

marks = [r for r in recs if r[3] in (1, 2, 3, 4)]
print("# tab-bar / display-mode calls:", len(marks))
for r in marks:
    print("#   %d %s from %s rcx=%X rdx=%X r8=%X r9=%X" % (r[0], names[r[3]], rva(r[4]), r[5], r[6], r[7], r[8]))
if not marks:
    sys.exit()

shown = set()
for m in marks:
    lo, hi = m[0] - 300, m[0] + 900
    if any(lo <= s <= hi for s in shown):
        continue
    shown.add(m[0])
    print("\n==== window around %d ====" % m[0])
    last = None
    rep = 0
    for r in recs:
        if not lo <= r[0] <= hi or (r[3], r[4]) in noisy:
            continue
        key = (r[3], r[4])
        if key == last:
            rep += 1
            continue
        if rep:
            print("        (x%d more)" % rep)
        rep = 0
        last = key
        print("%+5d t%-6d %-26s from %s  rcx=%X rdx=%X r8=%X r9=%X a5=%X" % (
            r[0] - m[0], r[2], names[r[3]] if r[3] < len(names) else r[3], rva(r[4]), r[5], r[6], r[7], r[8], r[9]))
    if rep:
        print("        (x%d more)" % rep)
