#!/usr/bin/env python3
"""The C++ half of the setup's languages: tr_table.inc (app/tr.cpp) from the .po files here.

    make_tr.py           write ../tr_table.inc
    make_tr.py --check   say, per language, which texts of openswos-setup.pot have no translation
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
LANGS = ['de', 'fr', 'es', 'it', 'nl']

def read_po(path):
    entries, cur, field = {}, {}, None
    def flush():
        if 'msgid' in cur and cur['msgid'] and 'fuzzy' not in cur.get('flags', ''):
            entries[cur['msgid']] = cur.get('msgstr', '')
    for line in open(path, encoding='utf-8'):
        line = line.rstrip('\n')
        if line.startswith('#,'):
            cur['flags'] = line
        elif line.startswith('msgid '):
            if 'msgid' in cur:           # (no blank line between two entries)
                flush(); cur = {}
            field = 'msgid'; cur[field] = unq(line[6:])
        elif line.startswith('msgstr '):
            field = 'msgstr'; cur[field] = unq(line[7:])
        elif line.startswith('"') and field:
            cur[field] += unq(line)
        elif not line.strip():
            flush(); cur, field = {}, None
    flush()
    return entries

def unq(s):
    s = s.strip()[1:-1]
    return s.replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')

def c_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

pot = read_po(os.path.join(HERE, 'openswos-setup.pot'))
check = '--check' in sys.argv
rows, bad = [], 0
for lang in LANGS:
    path = os.path.join(HERE, lang, 'LC_MESSAGES', 'openswos-setup.po')
    po = read_po(path) if os.path.exists(path) else {}
    missing = [m for m in pot if not po.get(m)]
    holes = [m for m, t in po.items() if t and sorted(re.findall(r'\{\d\}', m)) != sorted(re.findall(r'\{\d\}', t))]
    print('%s: %d of %d translated%s' % (lang, len(pot) - len(missing), len(pot),
                                          ', %d with placeholders that differ' % len(holes) if holes else ''))
    if check:
        for m in missing[:10]: print('   missing: ' + m[:90])
        for m in holes: print('   placeholders: ' + m[:90])
        bad += len(missing) + len(holes)
    for msgid in pot:
        if po.get(msgid): rows.append('{"%s", %s, %s},' % (lang, c_str(msgid), c_str(po[msgid])))
if check:
    sys.exit(1 if bad else 0)
open(os.path.join(os.path.dirname(HERE), 'tr_table.inc'), 'w', encoding='utf-8').write(
    '// Made by lang/make_tr.py from lang/<code>/LC_MESSAGES/openswos-setup.po: do not edit.\n' + '\n'.join(rows) + '\n')
print('tr_table.inc: %d rows' % len(rows))
