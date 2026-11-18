"""Find the client code that sends TrocTrDyeItemListByEquipSlotReq (descriptor id 0x09B7 on 3663).

Research only: reads CrimsonDesert.exe from disk.
"""
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import exe  # noqa: E402

ALLOC, ENQ = 0x165D9C0, 0x2A0F460

for name, pat in (("mov edx,9B7", b"\xBA\xB7\x09\x00\x00"), ("mov r8d,9B7", b"\x41\xB8\xB7\x09\x00\x00"),
                  ("mov ecx,9B7", b"\xB9\xB7\x09\x00\x00"), ("mov word 9B7", b"\xB7\x09\xFF")):
    hits = exe.find_bytes(pat)
    print(name, [hex(h) for h in hits[:40]])

lo, n = 0x1150000, 0x30000
code = exe.read(lo, n)
for i in range(len(code) - 5):
    if code[i] == 0xE8:
        t = lo + i + 5 + struct.unpack_from("<i", code, i + 1)[0]
        if t in (ALLOC, ENQ):
            at = lo + i
            print("call", hex(t), "at", hex(at), "in function", hex(exe.func_start(at) or 0))
