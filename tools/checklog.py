"""Grades a DyeAll.log against the release test battery (TESTS.md). Read-only.

    python tools/checklog.py [path to DyeAll.log]

Each test gets PASS (the log shows it worked), FAIL (the log shows it went wrong) or ---- (not seen:
either not done, or only checkable by eye). Problems and the items swapped, by who wore them, follow.
"""
import re
import sys

LOG = sys.argv[1] if len(sys.argv) > 1 else r"G:\SteamLibrary\steamapps\common\Crimson Desert\bin64\DyeAll.log"
LINE = re.compile(r"^\[(\d\d:\d\d:\d\d\.\d{3})\] \[(\w+)\s*\] (.*)$")

rows = []
with open(LOG, encoding="utf-8", errors="replace") as f:
    for raw in f:
        m = LINE.match(raw.rstrip("\n"))
        if m:
            rows.append(m.groups())


def seen(pat):
    r = re.compile(pat)
    return [(t, msg) for t, _lvl, msg in rows if r.search(msg)]


def n(pat):
    return len(seen(pat))


errors = [(t, msg) for t, lvl, msg in rows if lvl == "error"]
stops = seen(r"^\[swap\] stopped: ")

# (id, name, pass pattern(s) - all must appear, fail pattern or None)
TESTS = [
    ("1", "Mod loads (1.0.0, every hook in place)", [r"^Dye All 1\.0\.0 ", r"dye screen show hooked", r"dye screen update hooked"],
     r"not where exe|could not stack|not hooked|vtable not found"),
    ("2", "Panel follows the dye screen", [r"^\[dye\] dye screen opened", r"^\[dye\] dye screen closed"], None),
    ("3", "Armor from the wardrobe", [r"^\[swap\] move .* from Wardrobe", r"^\[swap\] You are wearing "], None),
    ("4", "Armor from the private chest", [r"^\[swap\] (Taking .* out of the Private chest|Your bag is full; putting .* on straight from the Private chest)", r"^\[swap\] You are wearing "], None),
    ("5", "Item from the warehouse", [r"^\[swap\] (Taking .* out of the Warehouse|Your bag is full; putting .* on straight from the Warehouse)"], r"refused to take .* out"),
    ("6", "Dyer list refreshes in place", [r"the dyer's list was rebuilt"], r"list was not rebuilt within"),
    ("7", "Colors applied before the next swap, dyer stays open", [r"game confirmed the colors", r"dyer's Close held back"],
     r"did not confirm the colors|could not apply them|cannot tell whether"),
    ("8", "Three of the same type in a row", [r"game confirmed the colors"], None),
    ("9", "Everything goes back after closing", [r"^\[swap\] .* is back in the "], r"could not go back to the|stays on\.$|no longer in your"),
    ("10", "Weapon or shield", [r"^\[swap\] equip .* into gear slot (0|1|2|12|13)$"], None),
    ("11", "Horse gear", [r"^\[swap\] Your horse wears "], r"dyer shows no horse"),
    ("12", "A.T.A.G. tab and gear", [r"the dyer has an A\.T\.A\.G\. tab", r"^\[swap\] The A\.T\.A\.G\. wears "],
     r"dyer shows no A\.T\.A\.G\.|has no A\.T\.A\.G\. tab"),
    ("13", "Full bag", [r"^\[swap\] bag full", r"equipped straight from the"], r"Your bag is full .*free a bag slot"),
    ("14", "The previous piece again", [r" is on again\.$"], r"has moved since the list was read"),
    ("15", "Reopen within 10 s keeps the item on", [r"dye screen opened again; keeping the item on"], None),
]

print("DyeAll.log: %d lines, %s to %s\n" % (len(rows), rows[0][0] if rows else "-", rows[-1][0] if rows else "-"))
for tid, name, need, bad in TESTS:
    hits = [n(p) for p in need]
    fails = seen(bad) if bad else []
    if tid == "8":
        ok = hits[0] >= 2
    else:
        ok = all(h > 0 for h in hits)
    mark = "FAIL" if fails else "PASS" if ok else "----"
    extra = ""
    if fails:
        extra = "  <- " + fails[0][1][:110]
    elif tid == "7":
        extra = "  (%d applies confirmed)" % n(r"game confirmed the colors")
    elif tid == "6":
        extra = "  (%d refreshes)" % hits[0]
    print("%-4s T%-3s %s%s" % (mark, tid, name, extra))

print("\nItems worn through the mod:")
for who, pat in (("you", r"^\[swap\] You are wearing (.*?)(; pick| \. Open|\. Open)"),
                 ("horse", r"^\[swap\] Your horse wears (.*?);"), ("A.T.A.G.", r"^\[swap\] The A\.T\.A\.G\. wears (.*?);")):
    names = []
    for _t, msg in seen(pat):
        nm = re.search(pat, msg).group(1)
        if nm not in names:
            names.append(nm)
    print("  %-9s %s" % (who + ":", ", ".join(names) if names else "-"))

print("\nStops (%d):" % len(stops))
for t, msg in stops[:20]:
    print("  %s %s" % (t, msg[len("[swap] stopped: "):][:150]))
uniq = []
for t, msg in errors:
    key = re.sub(r"\d+", "#", msg)
    if key not in [k for k, _ in uniq]:
        uniq.append((key, (t, msg)))
print("\nError lines (%d, %d different):" % (len(errors), len(uniq)))
for _k, (t, msg) in uniq[:25]:
    print("  %s %s" % (t, msg[:150]))
