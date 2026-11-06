"""Generate car_rebuild_names_dos.inc: the PC (DOS) game's name strings, by their address in
its data segment, for resolving a PC / SWOS 2020 save's season-history pointers
(src/car_rebuild.c).

    python3 scripts/car_rebuild/gen_names_dos.py <swos-port>/swos/swos.asm src/car_rebuild_names_dos.inc

Walks swos.asm's data (db/dw/dd, dup, the JobOffer/MarketPlayer/... struct arrays, align),
giving each label its address. IDA names unnamed data dseg_<address>, so every such label
checks the walk; where one disagrees the walk takes the label's address (and says so).
"""
import re
import sys

src = open(sys.argv[1], encoding='latin-1').read().splitlines()
out = sys.argv[2]

# struct sizes, from the "(sizeof=0x..)" IDA writes on each struc line
struct_size = {}
for l in src:
    m = re.match(r'^(\w+) struc ; \(sizeof=0x([0-9A-Fa-f]+)', l)
    if m:
        struct_size[m.group(1)] = int(m.group(2), 16)

SIZE = {'db': 1, 'dw': 2, 'dd': 4, 'df': 6, 'dq': 8, 'dt': 10}


def num(t):
    t = t.strip()
    return int(t[:-1], 16) if t.lower().endswith('h') else int(t)


def split_args(s):
    out, cur, q, depth = [], '', None, 0
    for ch in s:
        if q:
            cur += ch
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
            cur += ch
        elif ch == '(':
            depth += 1
            cur += ch
        elif ch == ')':
            depth -= 1
            cur += ch
        elif ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        elif ch == ';':
            break
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def item(kind, a):
    """(bytes, text) of one operand"""
    a = a.strip()
    m = re.match(r'^(\w+)\s+dup\s*\((.*)\)$', a)
    if m:
        n = num(m.group(1))
        inner = sum(item(kind, x)[0] for x in split_args(m.group(2)))
        return n * inner, ''
    if a and a[0] in "'\"" and kind == 'db':
        body = a[1:a.rindex(a[0])]
        return len(body), body
    return SIZE[kind], ''


start = next(i for i, l in enumerate(src) if re.match(r'^dseg_[0-9A-F]+ ', l))
base = None
off = 0
labels = []            # [label, address, text]
cur = None
resyncs = 0
for l in src[start:]:
    if re.match(r'^\w+\s+ends\b', l) or l.startswith('end '):
        break
    code = l.split(';')[0].rstrip()
    if not code.strip():
        continue
    m = re.match(r'^\s*align\s+(\d+)', code)
    if m:
        a = int(m.group(1))
        addr = base + off
        off += (-addr) % a
        continue
    m = re.match(r'^(\w+)?\s+(db|dw|dd|df|dq|dt)\s+(.*)$', code) or re.match(r'^(\w+)?\s*(db|dw|dd|df|dq|dt)\s+(.*)$', code)
    ms = re.match(r'^(\w+)\s+(\w+)\s+(.*)$', code)
    if m:
        lab, kind, rest = m.groups()
    elif ms and ms.group(2) in struct_size:
        lab, kind, rest = ms.group(1), ms.group(2), ms.group(3)
    else:
        continue
    if lab:
        dm = re.match(r'^dseg_([0-9A-F]+)$', lab)
        if dm:
            addr = int(dm.group(1), 16)
            if base is None:
                base = addr - off
            elif base + off != addr:
                resyncs += 1
                off = addr - base
        cur = [lab, base + off if base is not None else off, '']
        labels.append(cur)
    if kind in struct_size:
        mm = re.match(r'^(\w+)\s+dup', rest.strip())
        off += (num(mm.group(1)) if mm else 1) * struct_size[kind]
        continue
    for a in split_args(rest):
        n, t = item(kind, a)
        if t and cur is not None:
            if cur[1] + len(cur[2]) == (base or 0) + off:
                cur[2] += t
            else:       # a string inside a label, after other bytes: one of its own, at its address
                cur = [cur[0] + '+%d' % ((base or 0) + off - cur[1]), (base or 0) + off, t]
                labels.append(cur)
        elif not t and cur is not None and cur[2]:
            cur = [cur[0], cur[1], cur[2]]    # (text ends at a non-string operand)
            labels[-1] = cur
            cur = [cur[0] + '~', (base or 0) + off + n, '']   # later strings start afresh
            labels.append(cur)
        off += n

def ok(t):
    return 2 <= len(t) <= 40 and all(32 <= ord(c) < 127 for c in t) and any(c.isalpha() for c in t)

n = 0
with open(out, 'w', newline='\n') as f:
    f.write('/* Generated from the PC game\'s swos.asm by scripts/car_rebuild/gen_names_dos.py: its name\n'
            ' * strings, by address in its data segment -> text (a translation\'s "*" padding cut).\n'
            ' * src/car_rebuild.c. */\n')
    for lab, addr, t in sorted(labels, key=lambda x: x[1]):
        t = t.rstrip('*')
        if lab.startswith('a') and ok(t):
            f.write('    { 0x%X, "%s" },   /* %s */\n' % (addr, t.replace('\\', '\\\\').replace('"', '\\"'), lab))
            n += 1
print('wrote', n, 'names;', len(labels), 'labels walked,', resyncs, 'resyncs at dseg_ labels')
