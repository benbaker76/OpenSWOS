#!/usr/bin/env python3
"""world_mod_patcher.py -- install a SWOS 96/97 Mod release headless, from its patcher page, for testing the World
2026-27 decoders on any release (docs/WORLD_2026_27.md 6d):

    git -C <swos-9697-mod> show v4.0.5:release/swos-9697-mod-patcher.html > page.html
    python3 -I scripts/world_mod_patcher.py page.html <original GOG game folder> <out> [ENGLISH.EXE ...]

The page carries its files as deltas ('SWD1' copy/insert ops, raw-deflated, base64) against the original game's
files (by md5); this applies the ones for the originals given (+ the new files), every step checked by md5, the
way the page's own patchFile/applyDelta do. Out: the patched exe(s) + DATA. Optional pages (joypad, mapper) are
left out. Standard library only. Proved: the 4.1.1 page on the GOG originals gives the owner's install byte for
byte.
"""
import json, hashlib, zlib, base64, os, sys, collections
if len(sys.argv) < 4:
    sys.exit(__doc__)
page, orig, out = sys.argv[1:4]
exes = sys.argv[4:] or ['ENGLISH.EXE']
h = open(page, encoding='utf-8').read()
js = h[h.rfind('<script>') + 8:h.rfind('</script>')]
i = js.index('const PATCHES = ') + len('const PATCHES = ')
P, _ = json.JSONDecoder().raw_decode(js[i:])
md5 = lambda b: hashlib.md5(b).hexdigest()

def ops_of(p):
    raw = base64.b64decode(p['delta'])
    return zlib.decompress(raw, -15) if p.get('z') else raw

def apply_delta(src, ops, size):
    assert ops[:4] == b'SWD1'
    o = bytearray(size); p = 4; q = 0; last = 0
    def rd():
        nonlocal p
        n = 0; mul = 1
        while True:
            b = ops[p]; p += 1; n += (b & 127) * mul; mul *= 128
            if not b & 128: return n
    while p < len(ops):
        v = rd(); ln = v // 2
        if v & 1:
            o[q:q + ln] = ops[p:p + ln]; p += ln
        else:
            z = rd(); at = last + (-(z + 1) // 2 if z & 1 else z // 2)
            o[q:q + ln] = src[at:at + ln]; last = at + ln
        q += ln
    assert q == size
    return bytes(o)

def patch_file(src, p):
    frm = src
    if p.get('mid_md5'):
        mid = apply_delta(src, ops_of(p), p['mid_size']); assert md5(mid) == p['mid_md5']
        p = next(b for b in P if b['id'] == p['id'] and b['md5'] == p['mid_md5'] and b['size'] == p['mid_size'] and not b.get('from') and not b.get('via'))
        frm = mid
    o = apply_delta(b'' if p.get('with') else frm, ops_of(p), p['out_size'])
    assert md5(o) == p['out_md5'], p['target']
    return o

by_target = collections.defaultdict(list)
for p in P:
    by_target[p['target']].append(p)
os.makedirs(os.path.join(out, 'DATA'), exist_ok=True)
done = 0
for t, ps in sorted(by_target.items()):
    if t.endswith('.EXE') and t not in exes:
        continue
    srcp = os.path.join(orig, t)
    src = open(srcp, 'rb').read() if os.path.exists(srcp) else None
    cand = [p for p in ps if not p.get('opt') and not p.get('from') and src is not None and p['md5'] == md5(src) and p['size'] == len(src)]
    if not cand:
        cand = [p for p in ps if not p.get('opt') and not p.get('from') and p.get('with')]
    if not cand:
        print('no patch for', t, [{k: v for k, v in p.items() if k != 'delta'} for p in ps][:2]); continue
    o = patch_file(src or b'', cand[0])
    os.makedirs(os.path.dirname(os.path.join(out, t)), exist_ok=True)
    open(os.path.join(out, t), 'wb').write(o); done += 1
print(done, 'files written')
