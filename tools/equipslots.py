"""List which equip types each character's gear slots accept (from equipslotinfo; research only).

    python equipslots.py [Equip_Kliff ...]
Heuristic: a slot record holds u32 n, n equip-type hashes, a u32, then the u16 slot number.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BIN = os.path.join(HERE, "..", "extracted", "0008", "gamedata", "binarystaticinfo__", "bin")
types_b = open(os.path.join(BIN, "equiptypeinfo.staticinfobody"), "rb").read()
slots_b = open(os.path.join(BIN, "equipslotinfo.staticinfobody"), "rb").read()

names = {}
for m in re.finditer(rb"([\x03-\x40])\x00\x00\x00([A-Za-z_][A-Za-z0-9_]+)", types_b):
    n = m.group(1)[0]
    if len(m.group(2)) >= n:
        names[struct.unpack_from("<I", types_b, m.start() + 4 + n)[0]] = m.group(2)[:n].decode()

blocks = [(m.start(), m.group(1).decode()) for m in re.finditer(rb"[\x05-\x30]\x00\x00\x00(Equip_[A-Za-z_]+)", slots_b)]
want = set(sys.argv[1:])
for bi, (start, name) in enumerate(blocks):
    if want and name not in want:
        continue
    end = blocks[bi + 1][0] if bi + 1 < len(blocks) else len(slots_b)
    print("==", name)
    i = start
    while i < end - 8:
        n = struct.unpack_from("<I", slots_b, i)[0]
        if 1 <= n <= 40 and i + 4 + 4 * n + 6 <= end:
            hs = struct.unpack_from("<%dI" % n, slots_b, i + 4)
            if all(h in names for h in hs):
                after = i + 4 + 4 * n
                tail = slots_b[after:after + 12]
                slot = struct.unpack_from("<H", slots_b, after + 4)[0]
                print("  slot %3d  %s   (tail %s)" % (slot, ", ".join(names[h] for h in hs), tail.hex(" ")))
                i = after
                continue
        i += 1
