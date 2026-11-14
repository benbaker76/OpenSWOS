#!/usr/bin/env python3
"""worldgen/tmpages.py -- Transfermarkt pages SAVED FROM A BROWSER, parsed (docs/WORLD_DATAGEN.md §4).

transfermarkt.com answers a script with a CAPTCHA, so worldgen never fetches it: the owner opens
the pages in his own browser and saves them (Ctrl+S, "Webpage, HTML only" or "complete") into one
folder; this module reads that folder. (So does the SWOS 96/97 Mod's own tools/mls_tm.py: it
parses saved pages, it does not fetch them.)

A page is recognised by what it SAYS it is -- its canonical / og:url link, a browser's
"saved from url" mark, a Wayback Machine capture's original URL -- never by its file name, so
the browser's default file names work too. Tables are read by stable anchors (profile links
/profil/spieler/<id>, /profil/trainer/<id>, club links /verein/<id>, the flag images'
class="flaggenrahmen", the market-value link /marktwertverlauf/) and each cell by what it holds
(a date with an age, a height, a foot, a value), not by column position.

Pages (the URL forms are in urls()):
  squad       <club>/kader/verein/<id>/saison_id/<season>/plus/1     the detailed squad: number,
              name, player id, position, birth date + age, nationalities (first, second, with
              Transfermarkt's own country ids), height, foot, joined, contract, market value
              <club>/startseite/verein/<id>[/saison_id/<season>]     the club page: the same table
              without height / foot / contract (and the club's total value, its date)
  staff       <club>/mitarbeiter/verein/<id>                        the coach (Manager / Head Coach)
  table       <league>/tabelle/wettbewerb/<code>/saison_id/<season>  the final table: place, club,
              played, won, drawn, lost, goals, points
  clubs       <league>/startseite/wettbewerb/<code>/saison_id/<season>  the league's clubs

Languages: transfermarkt.com (English) first; transfermarkt.co.uk / .us the same. The German
site (.de, .at, .ch) parses too -- positions are translated to the English names, values ("Mio.
€", "Tsd. €") and dates (15.09.1995) read -- but its country names are German: nationalities
are kept with Transfermarkt's country ID (the flag image), which tm_country_names() maps to the
English name from any English page the folder holds. Other languages: positions untranslated.

    python3 -I scripts/worldgen/tmpages.py parse FILE...            # what a page holds (JSON)
    python3 -I scripts/worldgen/tmpages.py urls [--sample CSV] [--season 2026] [--prev 2025]
    python3 -I scripts/worldgen/tmpages.py check DIR [--sample CSV] [--since YYYY-MM-DD]
    python3 -I scripts/worldgen/tmpages.py sample DIR --country eng --division 0 --comp GB1
"""
import argparse
import csv
import datetime
import html
import html.parser
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
# the saved pages are raw source data: gitignored with the rest of bin/data_json
DEFAULT_DIR = os.environ.get('WORLDGEN_TM_PAGES') or os.path.join(REPO, 'bin', 'data_json', 'worldgen-cache',
                                                                     'tm-pages')

# ---- a small DOM ------------------------------------------------------------------------------

VOID = {'area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source',
        'track', 'wbr'}
# tags whose open element an opening one of these closes (the browser's implied ends)
IMPLIED = {'tr': {'tr', 'td', 'th'}, 'td': {'td', 'th'}, 'th': {'td', 'th'}, 'li': {'li'}, 'p': {'p'},
           'option': {'option'}, 'thead': {'tbody', 'thead'}, 'tbody': {'tbody', 'thead'}}
SCOPE = {'table', 'ul', 'ol', 'select', 'div'}    # an implied end never reaches past one of these


class Node:
    __slots__ = ('tag', 'attrs', 'children', 'parent')

    def __init__(self, tag, attrs, parent):
        self.tag, self.attrs, self.children, self.parent = tag, attrs, [], parent

    def get(self, k, default=''):
        return self.attrs.get(k) or default

    def classes(self):
        return self.get('class').split()

    def iter(self, tag=None):
        for c in self.children:
            if isinstance(c, Node):
                if tag is None or c.tag == tag:
                    yield c
                yield from c.iter(tag)

    def find(self, tag=None, cls=None, pred=None):
        for n in self.iter(tag):
            if (cls is None or cls in n.classes()) and (pred is None or pred(n)):
                return n
        return None

    def findall(self, tag=None, cls=None, pred=None):
        return [n for n in self.iter(tag) if (cls is None or cls in n.classes()) and (pred is None or pred(n))]

    def kids(self, tag):
        """Direct children with this tag (tbody looked through, for a table's rows)."""
        out = []
        for c in self.children:
            if isinstance(c, Node):
                if c.tag == tag:
                    out.append(c)
                elif c.tag in ('tbody', 'thead') and tag == 'tr':
                    out.extend(c.kids('tr'))
        return out

    def text(self):
        parts = []

        def walk(n):
            for c in n.children:
                if isinstance(c, str):
                    parts.append(c)
                elif c.tag not in ('script', 'style'):
                    if c.tag == 'br':
                        parts.append(' ')
                    walk(c)
        walk(self)
        return re.sub(r'\s+', ' ', html.unescape(''.join(parts)).replace('\xa0', ' ')).strip()


class _Builder(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=False)
        self.root = Node('#root', {}, None)
        self.cur = self.root
        self.comments = []

    def handle_starttag(self, tag, attrs):
        ends = IMPLIED.get(tag)
        if ends:
            n = self.cur
            while n is not self.root and n.tag not in SCOPE:
                if n.tag in ends:
                    self.cur = n.parent
                    break
                n = n.parent
        node = Node(tag, {k: (v if v is not None else '') for k, v in attrs}, self.cur)
        self.cur.children.append(node)
        if tag not in VOID:
            self.cur = node

    def handle_startendtag(self, tag, attrs):
        node = Node(tag, {k: (v if v is not None else '') for k, v in attrs}, self.cur)
        self.cur.children.append(node)

    def handle_endtag(self, tag):
        n = self.cur
        while n is not self.root:
            if n.tag == tag:
                self.cur = n.parent
                return
            n = n.parent                                # a stray end tag: ignored

    def handle_data(self, data):
        self.cur.children.append(data)

    def handle_entityref(self, name):
        self.cur.children.append(f'&{name};')

    def handle_charref(self, name):
        self.cur.children.append(f'&#{name};')

    def handle_comment(self, data):
        self.comments.append(data)


def decode(raw):
    """A saved page's bytes -> text (its meta charset, else UTF-8, else cp1252)."""
    if raw[:3] == b'\xef\xbb\xbf':
        raw = raw[3:]
    m = re.search(rb'<meta[^>]+charset=["\']?([\w-]+)', raw[:4096], re.I)
    for enc in ([m.group(1).decode('ascii', 'ignore')] if m else []) + ['utf-8', 'cp1252']:
        try:
            return raw.decode(enc)
        except (LookupError, UnicodeDecodeError):
            pass
    return raw.decode('utf-8', 'replace')


def dom(text):
    b = _Builder()
    b.feed(text)
    b.close()
    return b.root, b.comments


# ---- what a page is ---------------------------------------------------------------------------

TM_HOST = re.compile(r'https?://(?:www\.)?transfermarkt\.([a-z.]+)(/[^"\'\s<>]*)', re.I)
WAYBACK = re.compile(r'/web/(\d{8,14})[a-z_]*/')


def page_url(root, comments, text):
    """(the Transfermarkt URL the page says it is, its host suffix, a Wayback timestamp or None)."""
    cands = []
    for c in comments:                                  # Chrome / Edge "saved from url=(0071)..."
        m = re.search(r'saved from url=\(\d+\)(\S+)', c)
        if m:
            cands.append(m.group(1))
    for n in root.iter('meta'):
        if n.get('property') == 'og:url':
            cands.append(n.get('content'))
    for n in root.iter('link'):
        if 'canonical' in n.get('rel').split():
            cands.append(n.get('href'))
    wb = WAYBACK.search(text[:20000])
    for u in cands:
        m = TM_HOST.search(u)
        if m:
            return 'https://www.transfermarkt.' + m.group(1) + m.group(2), m.group(1), wb.group(1) if wb else None
    return None, None, wb.group(1) if wb else None


KINDS = [('squad', re.compile(r'/kader/verein/(\d+)')),
         ('staff', re.compile(r'/mitarbeiter/verein/(\d+)')),
         ('stats', re.compile(r'/leistungsdaten/verein/(\d+)')),
         ('club', re.compile(r'/startseite/verein/(\d+)')),
         ('table', re.compile(r'/tabelle/wettbewerb/([A-Z0-9]+)')),
         ('clubs', re.compile(r'/startseite/wettbewerb/([A-Z0-9]+)'))]


def kind_of(url):
    for k, rx in KINDS:
        m = rx.search(url or '')
        if m:
            s = re.search(r'/saison_id/(\d{4})', url)
            return k, m.group(1), s.group(1) if s else None
    return None, None, None


# ---- cell readers (English and German) ---------------------------------------------------------

MONTHS = {m: i + 1 for i, m in enumerate(['jan', 'feb', 'mar', 'apr', 'may', 'jun', 'jul', 'aug', 'sep', 'oct',
                                          'nov', 'dec'])}
MONTHS.update({'mär': 3, 'mrz': 3, 'mai': 5, 'okt': 10, 'dez': 12})


def read_date(s):
    """'15/09/1995', '15.09.1995', 'Sep 15, 1995', '1995-09-15' -> '1995-09-15' (else None)."""
    s = s.strip()
    m = re.search(r'(\d{4})-(\d\d)-(\d\d)', s)
    if m:
        return m.group(0)
    m = re.search(r'\b(\d{1,2})[/.](\d{1,2})[/.](\d{4})\b', s)
    if m:
        d, mo, y = int(m.group(1)), int(m.group(2)), int(m.group(3))
        if mo > 12:                                     # (a US-style date)
            d, mo = mo, d
        return f'{y:04d}-{mo:02d}-{d:02d}'
    m = re.search(r'\b([A-Za-zä]{3})[a-zä]*\.? (\d{1,2}), (\d{4})', s)
    if m and m.group(1).lower() in MONTHS:
        return f'{int(m.group(3)):04d}-{MONTHS[m.group(1).lower()]:02d}-{int(m.group(2)):02d}'
    m = re.search(r'\b(\d{1,2})\.? ([A-Za-zä]{3})[a-zä]*\.? (\d{4})', s)
    if m and m.group(2).lower() in MONTHS:
        return f'{int(m.group(3)):04d}-{MONTHS[m.group(2).lower()]:02d}-{int(m.group(1)):02d}'
    return None


def read_value(s):
    """'€30.00m', '€500k', '€1.34bn', '30,00 Mio. €', '500 Tsd. €' -> euros (int), '-' -> None."""
    s = s.replace('\xa0', ' ').strip()
    m = re.search(r'([\d.,]+)\s*(bn|m|k|Mrd\.?|Mio\.?|Tsd\.?|mln|mila|mil\.?|M|K)?', s.replace('€', '').replace('£', '')
                  .replace('$', '').strip())
    if not m or not re.search(r'\d', m.group(1)) or not re.search(r'[€£$]', s):
        return None
    num, unit = m.group(1), (m.group(2) or '').rstrip('.').lower()
    if ',' in num and '.' in num:
        num = num.replace('.', '').replace(',', '.') if num.rfind(',') > num.rfind('.') else num.replace(',', '')
    elif ',' in num:
        num = num.replace(',', '.')
    mult = {'bn': 1e9, 'mrd': 1e9, 'm': 1e6, 'mio': 1e6, 'mln': 1e6, 'mil': 1e6, 'k': 1e3, 'tsd': 1e3,
            'mila': 1e3}.get(unit, 1)
    try:
        return int(round(float(num) * mult))
    except ValueError:
        return None


def read_height(s):
    m = re.fullmatch(r'(\d)[,.](\d\d)\s?m', s.strip())
    return int(m.group(1)) * 100 + int(m.group(2)) if m else None


FOOT = {'right': 'right', 'left': 'left', 'both': 'both', 'rechts': 'right', 'links': 'left',
        'beidfüßig': 'both', 'beidfuessig': 'both', 'destro': 'right', 'sinistro': 'left', 'ambidestro': 'both',
        'derecho': 'right', 'izquierdo': 'left', 'ambidiestro': 'both', 'droit': 'right', 'gauche': 'left'}

POSITION_DE = {'Torwart': 'Goalkeeper', 'Innenverteidiger': 'Centre-Back', 'Linker Verteidiger': 'Left-Back',
               'Rechter Verteidiger': 'Right-Back', 'Defensives Mittelfeld': 'Defensive Midfield',
               'Zentrales Mittelfeld': 'Central Midfield', 'Offensives Mittelfeld': 'Attacking Midfield',
               'Linkes Mittelfeld': 'Left Midfield', 'Rechtes Mittelfeld': 'Right Midfield',
               'Linksaußen': 'Left Winger', 'Rechtsaußen': 'Right Winger', 'Mittelstürmer': 'Centre-Forward',
               'Hängende Spitze': 'Second Striker', 'Abwehr': 'Defender', 'Mittelfeld': 'Midfield',
               'Sturm': 'Attack'}
POSITIONS_EN = {'Goalkeeper', 'Centre-Back', 'Left-Back', 'Right-Back', 'Defensive Midfield', 'Central Midfield',
                'Attacking Midfield', 'Left Midfield', 'Right Midfield', 'Left Winger', 'Right Winger',
                'Centre-Forward', 'Second Striker', 'Defender', 'Midfield', 'Attack', 'Sweeper'}
# the number cell's colour class: the position GROUP on every language
GROUP_CLASS = {'bg_Torwart': 'Goalkeeper', 'bg_Abwehr': 'Defender', 'bg_Mittelfeld': 'Midfield',
               'bg_Sturm': 'Attack'}
COACH_ROLES = ('manager', 'head coach', 'coach', 'trainer', 'cheftrainer', 'interim', 'caretaker',
               'allenatore', 'entrenador', 'entraîneur', 'treinador')
NOT_COACH = ('assistant', 'goalkeeping', 'goalkeeper', 'fitness', 'co-trainer', 'torwart', 'athletic',
             'analyst', 'youth', 'u21', 'u23', 'u19', 'academy', 'scout', 'director', 'video', 'set-piece',
             'individual', 'technical', 'rehab', 'conditioning', 'physio', 'kit', 'team manager', 'sporting')


def position(s):
    s = s.strip()
    return POSITION_DE.get(s, s)


def ids(node, kind):
    """Transfermarkt ids of the /profil/<kind>/<id> links under node, in order (no repeats)."""
    out = []
    for a in node.iter('a'):
        m = re.search(r'/profil/' + kind + r'/(\d+)', a.get('href'))
        if m and m.group(1) not in out:
            out.append(m.group(1))
    return out


def flags(node):
    """[(country name as written, Transfermarkt country id)] of the flag images under node."""
    out = []
    for img in node.iter('img'):
        if 'flaggenrahmen' in img.classes():
            src = img.get('data-src') or img.get('src')
            m = re.search(r'/flagge/[a-z]+/(\d+)\.png', src)
            out.append((html.unescape(img.get('title') or img.get('alt')), m.group(1) if m else ''))
    return out


def last_update(root):
    """The page's own date stamp: 'Last update: <date>' (market values), else None."""
    for n in root.findall(cls='data-header__last-update'):
        d = read_date(n.text())
        if d:
            return d
    for n in root.iter():
        if n.tag in ('div', 'span', 'p') and not n.findall('div'):
            t = n.text()
            if len(t) < 80 and re.search(r'(Last update|Letzte Aktualisierung|Ultimo aggiornamento)', t):
                d = read_date(t)
                if d:
                    return d
    return None


def items_tables(root):
    return [t for t in root.iter('table') if 'items' in t.classes()]


# ---- the pages ---------------------------------------------------------------------------------

def squad_rows(root):
    """The players of a squad / club page (kader, startseite: the table with player profile links)."""
    out = []
    for table in items_tables(root):
        heads = [th.text().lower() for th in table.findall('th')]
        for tr in table.kids('tr'):
            tds = tr.kids('td')
            pid = None
            for td in tds:
                p = ids(td, 'spieler')
                if p and td.find('a', pred=lambda a: '/profil/spieler/' in a.get('href')
                                 and 'marktwert' not in a.get('href')):
                    pid = p[0]
                    name_cell = td
                    break
            if not pid:
                continue
            link = name_cell.find('a', pred=lambda a: '/profil/spieler/' in a.get('href') and a.text())
            name = link.text() if link else ''
            if not name and link is not None:
                name = html.unescape(link.get('title'))
            # the position: the inline table's second row (the line under the name)
            pos = ''
            inner = name_cell.find('table', cls='inline-table')
            if inner is not None:
                rows = inner.kids('tr')
                if len(rows) > 1:
                    pos = position(rows[1].text())
            p = dict(player_id=pid, name=name, position=pos, number=None, birth=None, age=None, nat=[],
                     height=None, foot=None, joined=None, contract=None, value=None, signed_from=None,
                     captain=False, injury=None, group=None)
            for a in name_cell.iter('a'):                # 'Joined from Leeds United; date: 09/07/2026; ...'
                m = re.match(r'(?:Joined from|Internal transfer:|Zugang von|Interner Wechsel:)\s*([^;]+);'
                             r'\s*(?:date|Datum):\s*([^;]+)', html.unescape(a.get('title')))
                if m:
                    p['signed_from'], p['joined'] = m.group(1).strip(), read_date(m.group(2))
                    break
            for n in name_cell.iter('span'):
                t = n.get('title')
                if 'kapitaenicon' in n.classes() or t.lower() in ('team captain', 'captain', 'mannschaftskapitän'):
                    p['captain'] = True
                elif 'verletzt' in ' '.join(n.classes()):
                    p['injury'] = t
            plain_dates = []
            for td in tds:
                if td is name_cell:
                    continue
                cls = td.classes()
                t = td.text()
                for c in cls:
                    if c in GROUP_CLASS:
                        p['group'] = GROUP_CLASS[c]
                num = td.find(cls='rn_nummer')
                if num is not None or 'rueckennummer' in cls:
                    s = (num or td).text()
                    p['number'] = int(s) if s.isdigit() else None
                    continue
                fl = flags(td)
                if fl and not td.find('a', pred=lambda a: '/verein/' in a.get('href')):
                    p['nat'] = fl
                    continue
                if td.find('a', pred=lambda a: '/marktwertverlauf/' in a.get('href')) is not None or \
                        ('rechts' in cls and read_value(t) is not None):
                    p['value'] = read_value(t)
                    continue
                club = td.find('a', pred=lambda a: re.search(r'/verein/\d+', a.get('href')))
                if club is not None:
                    img = club.find('img')
                    p['signed_from'] = html.unescape((img.get('title') if img is not None else '')
                                                     or club.get('title').split(':')[0])
                    continue
                col = tds.index(td)
                h = heads[col] if len(heads) == len(tds) else ''
                if re.fullmatch(r'\d{1,2}', t) and any(w in h for w in ('age', 'alter', 'età', 'edad')):
                    p['age'] = int(t)
                elif read_height(t) is not None:
                    p['height'] = read_height(t)
                elif t.lower() in FOOT:
                    p['foot'] = FOOT[t.lower()]
                elif re.search(r'\(\d{1,2}\)\s*$', t) and read_date(t):
                    p['birth'] = read_date(t)
                    p['age'] = int(re.search(r'\((\d{1,2})\)\s*$', t).group(1))
                elif read_date(t):
                    plain_dates.append((len(plain_dates), td, read_date(t)))
            # joined / contract: by the header when it names them, else in their order on the page
            if plain_dates:
                for k, td, d in plain_dates:
                    col = tds.index(td)
                    h = heads[col] if len(heads) == len(tds) else ''
                    if any(w in h for w in ('contract', 'vertrag', 'scadenza')):
                        p['contract'] = d
                    elif any(w in h for w in ('joined', 'seit', 'in rosa', 'in team')):
                        p['joined'] = d
                    elif len(plain_dates) >= 2:
                        p['joined' if k == 0 else 'contract'] = d
                    else:
                        p['contract'] = d
            if not p['position'] and p['group']:
                p['position'] = p['group']
            out.append(p)
    seen, uniq = set(), []
    for p in out:                                       # (a mobile duplicate of a row)
        if p['player_id'] not in seen:
            seen.add(p['player_id'])
            uniq.append(p)
    return uniq


def staff_rows(root):
    """[{coach_id, name, role}] of a staff page, and of a club page's staff box."""
    out = []
    for a in root.iter('a'):
        m = re.search(r'/profil/trainer/(\d+)', a.get('href'))
        if not m or not a.text():
            continue
        # the role: the inline table's next row, else the nearest following text in the row
        role = ''
        n = a
        while n is not None and n.tag != 'tr':
            n = n.parent
        if n is not None:
            table = n.parent if n.parent.tag == 'table' else n.parent.parent
            rows = table.kids('tr') if table is not None else []
            if n in rows and rows.index(n) + 1 < len(rows) and 'inline-table' in table.classes():
                role = rows[rows.index(n) + 1].text()
        if any(r['coach_id'] == m.group(1) and r['role'] == role for r in out):
            continue
        out.append(dict(coach_id=m.group(1), name=a.text(), role=role))
    return out


def coach(staff):
    """The head coach of staff_rows(): the first whose role is Manager / Head Coach / Trainer (not
    an assistant's or a youth side's)."""
    for r in staff:
        role = r['role'].lower()
        if any(w in role for w in COACH_ROLES) and not any(w in role for w in NOT_COACH):
            return r
    return None


def table_rows(root):
    """A league table: [{place, club_id, name, played, won, drawn, lost, goals_for, goals_against,
    points}] -- the first table.items whose rows link clubs."""
    for table in items_tables(root):
        out = []
        for tr in table.kids('tr'):
            tds = tr.kids('td')
            club, name = None, ''
            for td in tds:
                for a in td.iter('a'):
                    m = re.search(r'/verein/(\d+)', a.get('href'))
                    if m:
                        club = club or m.group(1)
                        if a.text() and not name:
                            name = a.text()
                        if not name and a.get('title'):
                            name = html.unescape(a.get('title'))
            if not club:
                continue
            nums, goals = [], None
            for td in tds:
                t = td.text()
                g = re.fullmatch(r'(\d+):(\d+)', t)
                if g:
                    goals = int(g.group(1)), int(g.group(2))
                elif re.fullmatch(r'[+-]?\d+', t):
                    nums.append((tds.index(td), int(t), t))
            if len(nums) < 5:
                continue
            # place first; then played won drawn lost [goal difference] points (the last number)
            place = nums[0][1]
            rest = [n for n in nums[1:]]
            pts = rest[-1][1]
            body = [n for n in rest[:-1] if not n[2].startswith(('+', '-'))]
            pl, w, d, l = (body + [(0, None, '')] * 4)[0][1], body[1][1] if len(body) > 1 else None, \
                body[2][1] if len(body) > 2 else None, body[3][1] if len(body) > 3 else None
            out.append(dict(place=place, club_id=club, name=name, played=pl, won=w, drawn=d, lost=l,
                            goals_for=goals[0] if goals else None, goals_against=goals[1] if goals else None,
                            points=pts))
        if out:
            return out
    return []


def club_list(root):
    """A league's clubs page: [{club_id, name, squad, value}] in its order."""
    for table in items_tables(root):
        out = []
        for tr in table.kids('tr'):
            club, name = None, ''
            for a in tr.iter('a'):
                m = re.search(r'/startseite/verein/(\d+)', a.get('href'))
                if m:
                    club = club or m.group(1)
                    name = name or a.text() or html.unescape(a.get('title'))
            if not club or any(c['club_id'] == club for c in out):
                continue
            vals = [read_value(td.text()) for td in tr.kids('td')]
            vals = [v for v in vals if v is not None]
            out.append(dict(club_id=club, name=name, total_value=vals[-1] if vals else None))
        if len(out) >= 4:
            return out
    return []


def header_club(root):
    """(club id, club name) of a club-side page's header."""
    h = root.find('h1')
    name = h.text() if h is not None else ''
    return name


def parse(path_or_bytes, name=None):
    """A saved page -> dict(kind, id, season, url, host, wayback, date, file, ...the kind's data)."""
    if isinstance(path_or_bytes, (bytes, bytearray)):
        raw, name = path_or_bytes, name or '<bytes>'
    else:
        with open(path_or_bytes, 'rb') as f:
            raw = f.read()
        name = path_or_bytes
    text = decode(raw)
    root, comments = dom(text)
    url, host, wb = page_url(root, comments, text)
    kind, ident, season = kind_of(url)
    if url and '/saison_id/' not in url:
        # a canonical link drops the season; og:url and the page's own links keep it
        for a in root.iter('a'):
            m = re.search(r'/(?:kader|startseite|tabelle|mitarbeiter)/(?:verein|wettbewerb)/' + re.escape(ident or '#')
                          + r'/saison_id/(\d{4})', a.get('href'))
            if m:
                season = m.group(1)
                break
    title = root.find('title').text() if root.find('title') else ''
    if kind and not season:                             # the page's own season: '25/26' (or '2026')
        h1 = root.find('h1')
        for t in ((h1.text() if h1 is not None else ''), title):
            m = re.search(r'\b(\d\d)/(\d\d)\b', t)
            if m and (int(m.group(1)) + 1) % 100 == int(m.group(2)):
                season = str(2000 + int(m.group(1)))
                break
    out = dict(kind=kind, id=ident, season=season, url=url, host=host, wayback=wb, file=name,
               date=last_update(root), title=title)
    if 'Human Verification' in out['title'] or (not kind and 'captcha' in text[:5000].lower()):
        out['kind'] = 'captcha'
        return out
    if kind in ('squad', 'club', 'stats'):
        out['club'] = header_club(root)
        out['players'] = squad_rows(root)
        st = staff_rows(root)
        if st:
            out['staff'] = st
    elif kind == 'staff':
        out['club'] = header_club(root)
        out['staff'] = staff_rows(root)
        c = coach(out['staff'])
        out['coach'] = c['name'] if c else None
    elif kind == 'table':
        out['league'] = header_club(root)
        out['table'] = table_rows(root)
    elif kind == 'clubs':
        out['league'] = header_club(root)
        out['clubs'] = club_list(root)
    return out


# ---- a folder of pages -------------------------------------------------------------------------

def scan(folder):
    """Every page of a folder (and its subfolders, but not a "complete" save's _files folders):
    {(kind, id, season): parsed page}, the newest copy of each."""
    found = {}
    for dirpath, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if not d.endswith(('_files', '-Dateien', '_file'))]
        for fn in sorted(files):
            if not fn.lower().endswith(('.html', '.htm', '.mhtml', '.xhtml')):
                continue
            p = os.path.join(dirpath, fn)
            try:
                pg = parse(p)
            except Exception as e:                      # a broken file is reported, not fatal
                sys.stderr.write(f'[tmpages] {p}: {e}\n')
                continue
            pg['mtime'] = datetime.datetime.fromtimestamp(os.path.getmtime(p)).strftime('%Y-%m-%d')
            if not pg['kind'] or pg['kind'] == 'captcha':
                found.setdefault(('unknown', p, None), pg)
                continue
            key = (pg['kind'], pg['id'], pg['season'])
            old = found.get(key)
            if old is None or (stamp(pg) or '') >= (stamp(old) or ''):
                found[key] = pg
    return found


def stamp(pg):
    """The date a page shows the world as of: its own 'Last update', else the Wayback capture's
    date, else the file's date."""
    if pg.get('date'):
        return pg['date']
    if pg.get('wayback'):
        w = pg['wayback']
        return f'{w[:4]}-{w[4:6]}-{w[6:8]}'
    return pg.get('mtime')


def tm_country_names(pages):
    """Transfermarkt country id -> English name, from every English page of a folder."""
    names = {}
    for pg in pages.values():
        if pg.get('host') in ('com', 'co.uk', 'us', 'com/en') or (pg.get('host') or '').startswith('com'):
            for p in pg.get('players', ()):
                for n, i in p['nat']:
                    if i:
                        names.setdefault(i, n)
    return names


def _days(a, b):
    try:
        return (datetime.date.fromisoformat(a) - datetime.date.fromisoformat(b)).days
    except (TypeError, ValueError):
        return 0


def _better(new, old, rank):
    d = _days(stamp(new), stamp(old))
    if rank[new['kind']] < rank[old['kind']]:
        return d >= -7
    if rank[new['kind']] > rank[old['kind']]:
        return d > 7
    return d > 0


class Pages:
    """A folder of saved pages, as the generator reads it: squads, coaches, tables per club id."""

    def __init__(self, folder, season=None):
        self.folder = folder
        self.pages = scan(folder) if folder and os.path.isdir(folder) else {}
        self.country = tm_country_names(self.pages)
        self.squads, self.coaches, self.dates = {}, {}, {}
        rank = {'squad': 0, 'club': 1}                  # the detailed squad wins over the club page
        best = {}
        for (k, i, s), pg in self.pages.items():
            if k in rank and (season is None or s in (None, str(season))):
                # the detailed squad wins, unless the club page is more than a week newer
                if i not in best or _better(pg, best[i], rank):
                    best[i] = pg
            if k == 'staff':
                self.coaches[i] = pg.get('coach')
        for i, pg in best.items():
            self.squads[i] = pg['players']
            self.dates[i] = stamp(pg)
            if i not in self.coaches and pg.get('staff'):
                c = coach(pg['staff'])
                if c:
                    self.coaches[i] = c['name']

    def nat_names(self, p):
        """A player's nationalities as English names (a German page's through the country ids)."""
        return [self.country.get(i, n) for n, i in p['nat']]

    def records(self, club_id):
        """The club's players as the open dataset's records (tmdata.TM's columns), or None."""
        rows = self.squads.get(club_id)
        if rows is None:
            return None
        out = []
        for p in rows:
            pos = p['position']
            nats = self.nat_names(p)
            out.append({
                'player_id': p['player_id'], 'name': p['name'], 'current_club_id': club_id,
                'date_of_birth': p['birth'] or '', 'age': p['age'],
                'sub_position': pos if pos not in GROUPS or pos == 'Goalkeeper' else '',
                'position': GROUPS.get(pos) or GROUP_OF.get(pos, ''),
                'country_of_citizenship': nats[0] if nats else '',
                'second_citizenship': nats[1] if len(nats) > 1 else '',
                'height_in_cm': str(p['height'] or ''), 'foot': p['foot'] or '',
                'market_value_in_eur': str(p['value']) if p['value'] else '',
                'contract_expiration_date': p['contract'] or '', 'number': p['number'],
                'last_season': '', 'source': 'tm-pages'})
        return out


GROUPS = {'Goalkeeper': 'Goalkeeper', 'Defender': 'Defender', 'Midfield': 'Midfield', 'Attack': 'Attack'}
GROUP_OF = {'Centre-Back': 'Defender', 'Left-Back': 'Defender', 'Right-Back': 'Defender', 'Sweeper': 'Defender',
            'Defensive Midfield': 'Midfield', 'Central Midfield': 'Midfield', 'Attacking Midfield': 'Midfield',
            'Left Midfield': 'Midfield', 'Right Midfield': 'Midfield', 'Left Winger': 'Attack',
            'Right Winger': 'Attack', 'Centre-Forward': 'Attack', 'Second Striker': 'Attack'}


# ---- the owner's list: which pages to save -----------------------------------------------------

# the sample's divisions -> Transfermarkt's competition code and URL name
COMPETITION = {('eng', '0'): ('GB1', 'premier-league'), ('eng', '1'): ('GB2', 'championship'),
               ('eng', '2'): ('GB3', 'league-one'), ('eng', '3'): ('GB4', 'league-two'),
               ('ita', '0'): ('IT1', 'serie-a'), ('ita', '1'): ('IT2', 'serie-b'),
               ('sco', '0'): ('SC1', 'scottish-premiership'), ('mlt', '0'): ('MAL1', 'premier-league'),
               ('esp', '0'): ('ES1', 'laliga'), ('ger', '0'): ('L1', 'bundesliga'), ('fra', '0'): ('FR1', 'ligue-1')}
BASE = 'https://www.transfermarkt.com'


def slugify(name):
    import unicodedata
    s = unicodedata.normalize('NFKD', name).encode('ascii', 'ignore').decode().lower()
    return re.sub(r'[^a-z0-9]+', '-', s).strip('-') or 'club'


def wanted(clubs, season=2026, prev=None, slugs=None):
    """[(kind, id, season, url, file name)] of the pages a run needs: per division its clubs page
    and its previous season's table, per club its detailed squad and its staff page."""
    prev = prev or season - 1
    slugs = slugs or {}
    out, seen = [], set()
    for c in clubs:
        comp = COMPETITION.get((c['country'], c['division']))
        if comp and comp not in seen:
            seen.add(comp)
            code, slug = comp
            out.append(('clubs', code, str(season), f'{BASE}/{slug}/startseite/wettbewerb/{code}/saison_id/{season}',
                        f'clubs-{code}-{season}.html'))
            out.append(('table', code, str(prev), f'{BASE}/{slug}/tabelle/wettbewerb/{code}/saison_id/{prev}',
                        f'table-{code}-{prev}.html'))
        i = c['tm_id']
        slug = slugs.get(i) or 'verein'
        out.append(('squad', i, str(season), f'{BASE}/{slug}/kader/verein/{i}/saison_id/{season}/plus/1',
                    f'squad-{i}.html'))
        out.append(('staff', i, None, f'{BASE}/{slug}/mitarbeiter/verein/{i}', f'staff-{i}.html'))
    return out


def club_slugs(offline=True):
    """Transfermarkt's URL names of the clubs, from the open dataset when it is in the cache (a
    URL works with any name: Transfermarkt routes by the id; the name only makes it readable)."""
    try:
        import tmdata
        t = tmdata.TM(offline=offline, valuations=False)
        return {i: c.get('club_code') for i, c in t.clubs.items() if c.get('club_code')}
    except BaseException:                               # (no dataset in the cache: SystemExit)
        return {}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    a1 = sub.add_parser('parse', help='print what saved pages hold (JSON)')
    a1.add_argument('files', nargs='+')
    a1.add_argument('--brief', action='store_true', help='counts and the first rows only')
    for name in ('urls', 'check'):
        a = sub.add_parser(name, help={'urls': 'the pages to save for a run, with a file name each',
                                       'check': 'which of them a folder holds: present / missing / stale'}[name])
        if name == 'check':
            a.add_argument('folder')
            a.add_argument('--since', help='a page dated before this (YYYY-MM-DD) is stale')
        a.add_argument('--sample', default=os.path.join(HERE, 'sample_2627.csv'))
        a.add_argument('--season', type=int, default=2026, help="the season's start year (2026 = 2026-27)")
        a.add_argument('--prev', type=int, help="the previous table's season (default: season - 1)")
        a.add_argument('--country', help='only this country (pack code: eng, ita ...)')
        a.add_argument('--division', help='only this division')
        a.add_argument('--format', choices=('text', 'tsv', 'html'), default='text')
    a3 = sub.add_parser('sample', help="a sample CSV's rows from a division's saved clubs + table pages")
    a3.add_argument('folder')
    a3.add_argument('--country', required=True)
    a3.add_argument('--division', required=True)
    a3.add_argument('--comp', help='Transfermarkt competition code (default: from the country / division)')
    a3.add_argument('--season', type=int, default=2026)
    args = ap.parse_args()

    if args.cmd == 'parse':
        for f in args.files:
            pg = parse(f)
            if args.brief:
                for k in ('players', 'staff', 'table', 'clubs'):
                    if k in pg:
                        pg[k] = {'count': len(pg[k]), 'first': pg[k][:3]}
            print(json.dumps(pg, ensure_ascii=False, indent=1))
        return

    if args.cmd == 'sample':
        pages = scan(args.folder)
        code = args.comp or COMPETITION[(args.country, args.division)][0]
        members = pages.get(('clubs', code, str(args.season)))
        table = pages.get(('table', code, str(args.season - 1)))
        if not members:
            raise SystemExit(f'no clubs page of {code} {args.season} in {args.folder}')
        place = {r['club_id']: r['place'] for r in (table or {}).get('table', ())}
        rows = sorted(members['clubs'], key=lambda c: (place.get(c['club_id'], 99), -(c['total_value'] or 0)))
        w = csv.writer(sys.stdout, lineterminator='\n')
        for c in rows:
            p = place.get(c['club_id'])
            # a club not in the previous table came from another level (promoted / relegated):
            # its previous division is left for the sample's editor ('?')
            w.writerow([args.country, args.division, c['club_id'], args.division if p else '?', p or ''])
        return

    import model
    clubs = model.sample(args.sample)
    if args.country:
        clubs = [c for c in clubs if c['country'] == args.country]
    if args.division is not None:
        clubs = [c for c in clubs if c['division'] == args.division]
    want = wanted(clubs, args.season, args.prev, club_slugs())
    if args.cmd == 'urls':
        if args.format == 'tsv':
            for k, i, s, u, fn in want:
                print(f'{fn}\t{u}')
        elif args.format == 'html':
            print('<!doctype html><meta charset="utf-8"><title>Transfermarkt pages to save</title><ol>')
            for k, i, s, u, fn in want:
                print(f'<li><a href="{html.escape(u)}" target="_blank">{html.escape(u)}</a> &rarr; '
                      f'<code>{html.escape(fn)}</code></li>')
            print('</ol>')
        else:
            for k, i, s, u, fn in want:
                print(f'{fn:24} {u}')
            print(f'# {len(want)} pages; save each (Ctrl+S, "Webpage, HTML only" is enough) into one folder. '
                  'The names are a suggestion: a page is recognised by its content.', file=sys.stderr)
        return
    pages = scan(args.folder)
    present = missing = stale = 0
    for k, i, s, u, fn in want:
        pg = pages.get((k, i, s))
        club = pages.get(('club', i, s)) if k == 'squad' else None
        if club is not None and (pg is None or _better(club, pg, {'squad': 0, 'club': 1})):
            pg = club                                   # a club page is a squad, without the details
        if pg is None:
            missing += 1
            print(f'MISSING  {fn:24} {u}')
            continue
        st = stamp(pg)
        n = len(pg.get('players') or pg.get('table') or pg.get('clubs') or pg.get('staff') or ())
        what = f"{n} rows" + (f", coach {pg['coach']}" if pg.get('coach') else '')
        if k == 'squad' and pg['kind'] == 'club':
            what += ' (club page: no height / foot / contract; save the detailed squad)'
        if args.since and st and st < args.since and k != 'table':     # (a final table does not age)
            stale += 1
            print(f'STALE    {fn:24} {st} {what}  <- {os.path.basename(pg["file"])}')
        else:
            present += 1
            print(f'present  {fn:24} {st} {what}  <- {os.path.basename(pg["file"])}')
    for (k, i, s), pg in pages.items():
        if k == 'unknown':
            print(f"UNKNOWN  {os.path.basename(pg['file'])}: {pg['kind'] or 'not a Transfermarkt page'}"
                  f" ({pg.get('title', '')[:60]})")
    print(f'{present} present, {stale} stale, {missing} missing of {len(want)}')


if __name__ == '__main__':
    sys.path.insert(0, HERE)
    main()
