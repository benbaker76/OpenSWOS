"""Map the original saves' competition-name pointers to SWOS2.s strings.

Walks the data lines around aPremierLeague, giving each label its byte offset,
anchors 0x1ced66 = aPremierLeague (the Newcastle saves' league pointer), and
resolves every pointer: it must land on a label that starts a string.
"""
import re
import sys



def item_size(tok):
    tok = tok.strip()
    if not tok:
        return 0
    if tok[0] in "'\"":
        q = tok[0]
        body = tok[1:tok.rindex(q)]
        return len(body.replace(q * 2, q))
    return 1


def split_args(s):
    out, cur, q = [], '', None
    for ch in s:
        if q:
            cur += ch
            if ch == q:
                q = None
        elif ch in "'\"":
            q = ch
            cur += ch
        elif ch == ',':
            out.append(cur)
            cur = ''
        elif ch == ';':
            break
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return out


def walk(lines):
    """[(label, offset, text)] from lines; stops at anything it cannot size."""
    off, res, last = 0, [], None
    for l in lines:
        m = re.match(r'^([A-Za-z_]\w*):', l)
        if m:
            last = [m.group(1), off, '']
            res.append(last)
            continue
        m = re.match(r'\s+dc\.([bwl])\s+(.*)', l)
        if m:
            w = {'b': 1, 'w': 2, 'l': 4}[m.group(1)]
            for a in split_args(m.group(2)):
                a = a.strip()
                if w == 1 and a and a[0] in "'\"":
                    n = item_size(a)
                    if last is not None and last[1] + len(last[2]) == off:
                        last[2] += a[1:a.rindex(a[0])]
                    off += n
                else:
                    off += w
            continue
        m = re.match(r'\s+ds\.([bwl])\s+\$?([0-9A-Fa-f]+)', l)
        if m:
            off += int(m.group(2), 16 if '$' in l else 10) * {'b': 1, 'w': 2, 'l': 4}[m.group(1)]
            continue
        if re.match(r'\s+(even|align|cnop)', l):
            return res, off, 'stop: ' + l.strip()
        if l.strip() and not l.strip().startswith(';'):
            return res, off, 'stop: ' + l.strip()
    return res, off, 'end'


if __name__ == '__main__':
    src = open(sys.argv[1], encoding='latin-1').read().splitlines()
    anchor_label, anchor_addr = 'aPremierLeague', 0x1ced66
    start = next(i for i, l in enumerate(src) if l.startswith(anchor_label + ':'))
    labels, size, why = walk(src[start:start + 60000])
    by_off = {o: (n, t) for n, o, t in labels}
    print('walked', len(labels), 'labels,', hex(size), 'bytes;', why)
    ptrs = [int(x, 16) for x in sys.argv[2:]]
    for p in ptrs:
        rel = p - anchor_addr
        hit = by_off.get(rel)
        print(hex(p), '->', (hit[0] + ' ' + repr(hit[1][:30])) if hit else f'NO LABEL at +{rel:#x}')
