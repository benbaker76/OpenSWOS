"""Generate car_rebuild_names.inc: the original game's competition-name strings
with their byte offsets from aPremierLeague, for resolving an original save's
season-history pointers (src/car_rebuild.c).

Two data areas of the original binary: the one starting at aPremierLeague, and
the one ending at aEuropeanChampi_1, which lies 0x804b bytes after
aPremierLeague (both measured from the user's saves: 0x1ced66 / 0x1d6db1).
"""
import re
import sys

sys.path.insert(0, sys.argv[1])
from swos2s_data import walk  # noqa: E402

src = open(sys.argv[2], encoding='latin-1').read().splitlines()
out = sys.argv[3]

# area 1: from aPremierLeague
a1 = next(i for i, l in enumerate(src) if l.startswith('aPremierLeague:'))
r1, _, _ = walk(src[a1:a1 + 60000])

# area 2: the longest data run ending at aEuropeanChampi_1, then on past it
anchor = 'aEuropeanChampi_1'
a2 = next(i for i, l in enumerate(src) if l.startswith(anchor + ':'))
start = a2
for s in range(a2 - 1, max(0, a2 - 20000), -1):
    res, off, why = walk(src[s:a2 + 1])
    if why != 'end' or not res or res[-1][0] != anchor:
        break
    start = s
r2, _, _ = walk(src[start:a2 + 2000])
anc = next(o for n, o, t in r2 if n == anchor)

rows = {}
for n, o, t in r1:
    rows[o] = (n, t)
for n, o, t in r2:
    rows[0x804b + (o - anc)] = (n, t)


def ok(t):
    return 2 <= len(t) <= 40 and all(32 <= ord(c) < 127 for c in t) and any(c.isalpha() for c in t)


with open(out, 'w', newline='\n') as f:
    f.write('/* Generated from data/SWOS2.s (scripts/car_rebuild/gen_names.py): the original game\'s\n'
            ' * name strings, byte offset from aPremierLeague -> text. src/car_rebuild.c. */\n')
    n = 0
    for o in sorted(rows):
        name, t = rows[o]
        if ok(t):
            esc = t.replace('\\', '\\\\').replace('"', '\\"')
            f.write('    { %d, "%s" },   /* %s */\n' % (o, esc, name))
            n += 1
print('wrote', n, 'names; area 2 walked from line', start + 1)
