#!/usr/bin/env python3
"""worldgen/wiki.py -- clubs' English Wikipedia articles and their kit colours (the Mod's sources:
"Kits: Wikipedia contributors (CC BY-SA 4.0); the club -> article links from Wikidata (CC0)").

- Wikidata (CC0): one SPARQL query maps Transfermarkt club ids (property P7223) to items and
  their en.wikipedia article titles.
- Wikipedia (MediaWiki API): the articles' wikitext, 50 titles a request; the football club
  infobox's kit parameters (body1 / leftarm1 / rightarm1 / shorts1 / socks1 colours as hex,
  pattern_b1 / pattern_la1 ... the overlay images' names) for the home kit, ...2 the away kit.

Everything is fetched through fetch.get (cached once, 3 s apart).
"""
import json
import os
import re
import sys
import urllib.parse

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fetch                                            # noqa: E402

SPARQL = 'https://query.wikidata.org/sparql?format=json&query='
API = 'https://en.wikipedia.org/w/api.php?'


def articles(tm_ids, offline=False):
    """{Transfermarkt club id: en.wikipedia title} (ids without an item or an article are absent)."""
    out = {}
    ids = sorted(set(tm_ids), key=int)
    for k in range(0, len(ids), 80):
        vals = ' '.join(f'"{i}"' for i in ids[k:k + 80])
        q = ('SELECT ?tm ?title WHERE { VALUES ?tm { ' + vals + ' } ?item wdt:P7223 ?tm . '
             '?article schema:about ?item ; schema:isPartOf <https://en.wikipedia.org/> ; '
             'schema:name ?title . }')
        d = json.loads(fetch.get(SPARQL + urllib.parse.quote(q), json_api=True, offline=offline))
        for b in d['results']['bindings']:
            out.setdefault(b['tm']['value'], b['title']['value'])
    return out


def wikitext(titles, offline=False):
    """{title: wikitext} of the articles (redirects followed; the requested title is the key)."""
    out = {}
    titles = sorted(set(titles))
    for k in range(0, len(titles), 50):
        chunk = titles[k:k + 50]
        q = urllib.parse.urlencode({'action': 'query', 'prop': 'revisions', 'rvprop': 'content',
                                    'rvslots': 'main', 'format': 'json', 'formatversion': '2',
                                    'redirects': '1', 'titles': '|'.join(chunk)})
        d = json.loads(fetch.get(API + q, json_api=True, offline=offline))
        back = {t: t for t in chunk}
        for r in d['query'].get('normalized', []) + d['query'].get('redirects', []):
            back[r['to']] = back.get(r['from'], r['from'])
        for p in d['query']['pages']:
            if 'revisions' in p:
                out[back.get(p['title'], p['title'])] = p['revisions'][0]['slots']['main']['content']
    return out


def infobox_kit(text, n=1):
    """The infobox's kit n (1 home, 2 away): {part: '#rrggbb' or None, 'pattern_<part>': name}."""
    kit = {}
    for part in ('leftarm', 'body', 'rightarm', 'shorts', 'socks'):
        m = re.search(r'\|\s*' + part + str(n) + r'\s*=\s*#?([0-9A-Fa-f]{6}|[0-9A-Fa-f]{3})\b', text)
        if m:
            h = m.group(1)
            if len(h) == 3:
                h = ''.join(c * 2 for c in h)
            kit[part] = '#' + h.lower()
        short = {'leftarm': 'la', 'rightarm': 'ra', 'body': 'b', 'shorts': 'sh', 'socks': 'so'}[part]
        m = re.search(r'\|\s*pattern_' + short + str(n) + r'\s*=\s*([^|\n}]*)', text)
        if m and m.group(1).strip():
            kit['pattern_' + part] = m.group(1).strip()
    return kit


def squad(text):
    """The first-team squad of a club article: [{no, nat (FIFA code), pos (GK/DF/MF/FW), name}] from
    the {{Fs player}} templates between the first {{Fs start}} and the {{Fs end}} after it (the
    loan / youth lists that follow are left out)."""
    s = re.search(r'\{\{\s*[Ff]s start', text)
    if not s:
        return []
    e = re.search(r'\{\{\s*[Ff]s end\s*\}\}', text[s.end():])
    block = text[s.end():s.end() + e.start()] if e else text[s.end():]
    out = []
    for m in re.finditer(r'\{\{\s*[Ff]s player\s*\|', block):
        i, depth = m.end(), 2
        while i < len(block) and depth:          # to the template's own closing braces
            depth += {'{': 1, '}': -1}.get(block[i], 0)
            i += 1
        body = block[m.end():i - 2]
        fields = {}
        depth, cur = 0, ''
        for ch in body:                         # split on | outside [[ ]] / {{ }}
            if ch in '[{':
                depth += 1
            elif ch in ']}':
                depth -= 1
            if ch == '|' and depth == 0:
                fields_add(fields, cur)
                cur = ''
            else:
                cur += ch
        fields_add(fields, cur)
        name = fields.get('name', '')
        name = re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]*)\]\]', r'\1', name)
        name = re.sub(r'\{\{[^}]*\}\}|<[^>]*>|\(.*?\)', '', name).strip(" '")
        no = fields.get('no', '').strip()
        out.append(dict(no=int(no) if no.isdigit() else None, nat=fields.get('nat', '').strip().upper(),
                        pos=fields.get('pos', '').strip().upper(), name=name))
    return out


def fields_add(fields, cur):
    if '=' in cur:
        k, v = cur.split('=', 1)
        fields[k.strip().lower()] = v.strip()


def infobox_field(text, *names):
    """A football club infobox field's plain text (links resolved, refs and templates dropped)."""
    for n in names:
        m = re.search(r'\|\s*' + n + r'\s*=\s*([^\n]*)', text)
        if m and m.group(1).strip():
            v = re.sub(r'<ref[^>]*/>|<ref.*?</ref>|<!--.*?-->', '', m.group(1))
            v = re.sub(r'\[\[(?:[^|\]]*\|)?([^\]]*)\]\]', r'\1', v)
            v = re.sub(r'\{\{[^}]*\}\}|<[^>]*>', '', v).strip(" '|")
            if v:
                return v
    return ''


# ---- kit pattern images (Commons) ----------------------------------------------------------------

FILEPATH = 'https://commons.wikimedia.org/wiki/Special:FilePath/'


def png_rgba(data):
    """A PNG's pixels as rows of (r, g, b, a) -- 8-bit grey / RGB / palette / RGBA, not interlaced
    (what Wikipedia's kit pattern images are)."""
    import struct
    import zlib as z
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, idat, plte, trns = 8, b'', None, None
    while pos < len(data):
        n, typ = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if typ == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
        elif typ == b'PLTE':
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif typ == b'tRNS':
            trns = body
        elif typ == b'IDAT':
            idat += body
    if depth != 8 or inter:
        return None
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw, stride = z.decompress(idat), w * ch
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - ch] if x >= ch else 0
            b, c = prev[x], prev[x - ch] if x >= ch else 0
            if f == 1:
                line[x] = (line[x] + a) & 255
            elif f == 2:
                line[x] = (line[x] + b) & 255
            elif f == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        px = []
        for x in range(w):
            v = line[x * ch:(x + 1) * ch]
            if ctype == 6:
                px.append(tuple(v))
            elif ctype == 2:
                px.append((v[0], v[1], v[2], 255))
            elif ctype == 0:
                px.append((v[0], v[0], v[0], 255))
            elif ctype == 4:
                px.append((v[0], v[0], v[0], v[1]))
            else:
                r, g, b_ = plte[v[0]]
                px.append((r, g, b_, trns[v[0]] if trns and v[0] < len(trns) else 255))
        rows.append(px)
    return rows


def pattern_image(part, name, offline=False):
    """The pixels of a kit pattern ('Kit_body_milan2627h.png'), or None (no such file)."""
    prefix = {'body': 'Kit_body', 'leftarm': 'Kit_left_arm', 'rightarm': 'Kit_right_arm', 'shorts': 'Kit_shorts',
              'socks': 'Kit_socks'}[part]
    name = name.strip().replace(' ', '_')
    data = fetch.get(FILEPATH + prefix + name + '.png', offline=offline)
    if not data or data[:4] != b'\x89PNG':
        return None
    try:
        return png_rgba(data)
    except Exception:                           # an image we cannot read: as if there were none
        return None
