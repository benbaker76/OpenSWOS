#!/usr/bin/env python3
"""Every English text OpenSWOS Setup shows, into openswos-setup.pot (README.md here).

From: the .slint files' @tr("..."); main.cpp's T("..."), Tf("...", ...) and N_("...") (C++'s
adjacent literals joined); the C library's step names and needs (lib/swosdata.c); the GAME
OPTIONS (include/game_options.h). Run it after changing any of them, then make_tr.py.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
SD = os.path.dirname(APP)
ROOT = os.path.dirname(os.path.dirname(SD))
LIT = r'"(?:[^"\\]|\\.)*"'

def unescape(body):            # a C/Slint literal's inside -> the text
    out, i = '', 0
    while i < len(body):
        c = body[i]
        if c == '\\' and i + 1 < len(body):
            n = body[i + 1]
            out += {'n': '\n', 't': '\t', '"': '"', '\\': '\\'}.get(n, '\\' + n)
            i += 2
        else:
            out += c
            i += 1
    return out

def literals_at(text, pos):    # adjacent literals from pos (after whitespace): (joined, end)
    m = re.compile(r'\s*(' + LIT + r')').match(text, pos)
    if not m: return None, pos
    out, end = '', pos
    while m:
        out += unescape(m.group(1)[1:-1]); end = m.end()
        m = re.compile(r'\s*(' + LIT + r')').match(text, end)
    return out, end

def balanced(text, pos):       # text[pos] is just after '(': up to its ')'
    depth, i, instr = 1, pos, False
    while i < len(text) and depth:
        c = text[i]
        if instr:
            if c == '\\': i += 1
            elif c == '"': instr = False
        elif c == '"': instr = True
        elif c == '(': depth += 1
        elif c == ')': depth -= 1
        i += 1
    return text[pos:i - 1]

entries = {}                   # msgid -> [refs]
def add(msgid, ref):
    if msgid.strip(): entries.setdefault(msgid, []).append(ref)

def lineno(text, pos): return text.count('\n', 0, pos) + 1

for name in ('app.slint', 'wizard.slint', 'theme.slint'):
    path = os.path.join(APP, 'ui', name)
    text = open(path, encoding='utf-8').read()
    for m in re.finditer(r'@tr\(\s*(' + LIT + r')', text):
        add(unescape(m.group(1)[1:-1]), 'app/ui/%s:%d' % (name, lineno(text, m.start())))

text = open(os.path.join(APP, 'main.cpp'), encoding='utf-8').read()
for m in re.finditer(r'\b(Tf|T|N_)\(', text):
    ref = 'app/main.cpp:%d' % lineno(text, m.start())
    if m.group(1) == 'T':        # T("..."), or T(cond ? "a" : "b"): every literal group in it
        inner = balanced(text, m.end())
        pos = 0
        while True:
            lm = re.compile(LIT).search(inner, pos)
            if not lm: break
            s, end = literals_at(inner, lm.start())
            add(s, ref); pos = end
    else:                        # Tf("...", args) / N_("..."): its first argument
        s, _ = literals_at(text, m.end())
        if s is not None: add(s, ref)

text = open(os.path.join(SD, 'lib', 'swosdata.c'), encoding='utf-8').read()
for m in re.finditer(r'\.(?:name|needs) = (' + LIT + r');', text):
    add(unescape(m.group(1)[1:-1]), 'lib/swosdata.c:%d' % lineno(text, m.start()))

text = open(os.path.join(ROOT, 'include', 'game_options.h'), encoding='utf-8').read()
for m in re.finditer(r'\{\s*(' + LIT + r'),\s*(' + LIT + r'),\s*(' + LIT + r'),\s*(' + LIT + r')', text):
    ref = 'include/game_options.h:%d' % lineno(text, m.start())
    add(unescape(m.group(3)[1:-1]), ref); add(unescape(m.group(4)[1:-1]), ref)
for m in re.finditer(r'\{\s*(' + LIT + r'),\s*(' + LIT + r'),\s*(?:0|GAME_OPTION_VALUE_\w+)\s*\}', text):
    add(unescape(m.group(2)[1:-1]), 'include/game_options.h:%d' % lineno(text, m.start()))

def po_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

out = ['# OpenSWOS Setup: every English text it shows (lang/extract.py). Do not edit: rerun it.',
       'msgid ""', 'msgstr ""', '"Content-Type: text/plain; charset=UTF-8\\n"', '']
for msgid, refs in entries.items():
    out.append('#: ' + ' '.join(refs[:4]))
    out.append('msgid ' + po_str(msgid))
    out.append('msgstr ""')
    out.append('')
open(os.path.join(HERE, 'openswos-setup.pot'), 'w', encoding='utf-8').write('\n'.join(out))
print('openswos-setup.pot: %d texts' % len(entries))
