#!/usr/bin/env python3
"""worldgen/kits.py -- World 2026-27's club kits again from Wikipedia, in the game's 50 kit colours
(docs/WORLD_DATAGEN.md; the owner, 2026-11-23).

    python3 -I scripts/worldgen/kits.py --map            # clubs -> their Wikidata item and articles
    python3 -I scripts/worldgen/kits.py --fetch          # the articles and the kit images (cached)
    python3 -I scripts/worldgen/kits.py --apply [--dry-run]   # the kits into teams/*.json
    python3 -I scripts/worldgen/seal.py                  # then: the Amiga files, the fingerprint

Each club is found by name among its country's football clubs on Wikidata (one query a country;
its labels and aliases in the country's language and English, the club name the game's
model.club_name makes of each; a tie goes to the item with the most articles). A club's kits are
read from its article in its COUNTRY'S Wikipedia (German clubs the German one, Spanish the Spanish
...), the English one when that has no kit. The guest clubs (no country in our data) are looked
for in every country of their confederation. bin/data_json/worldgen/2627/kits_titles.json fixes a
club by hand: {"club:<code>.<name>": "<lang>:<article title>"} (or "" for none).

A kit: each part (body, arms, shorts, socks) the infobox colour, or -- where the infobox draws the
part with a pattern image (Kit_body_barcelona2627h.png) -- the image composed over that colour, read
in the part's box; every colour the nearest of the game's 50 (scripts/kit_variants/kit_variants.json,
the game palette's own RGB, CIEDE2000). Two main body colours apart (CIEDE2000 >= the table's
similar_below) make stripes or hoops, by where the colour changes run; arms other than the body,
sleeves. Home is kit 1; away the one of kits 2, 3 (and 4) that differs most from home (the owner:
"pick the one that differs more in coloring from the home jersey"). A kit Wikipedia has not keeps
the team's own. The colour bytes: the base colour in the low nibble, its variant in the high one
(include/kit_variants.h).

Everything comes through fetch.get (cached, 3 s apart a host): bin/data_json/worldgen-cache/.
"""
import argparse
import collections
import csv
import glob
import json
import os
import re
import sys
import urllib.parse
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(REPO, 'scripts'))
import fetch                    # noqa: E402
import wiki                     # noqa: E402
import gen_kit_variants as gkv  # noqa: E402

WORLD = os.path.join(REPO, 'bin', 'data_json', 'worlds', '2627')
WORK = os.path.join(REPO, 'bin', 'data_json', 'worldgen', '2627')
MAP = os.path.join(WORK, 'kits_map.json')
TITLES = os.path.join(WORK, 'kits_titles.json')
REPORT = os.path.join(WORK, 'kits_report.txt')
NAMESPACE = uuid.UUID("af19cc5a-6529-537b-935b-407bd73b75fb")
SPARQL = 'https://query.wikidata.org/sparql?format=json&query='

# our country code -> (Wikidata country, its Wikipedia's language)
COUNTRY = {
    'alb': ('Q222', 'sq'), 'alg': ('Q262', 'ar'), 'and': ('Q228', 'ca'), 'arg': ('Q414', 'es'),
    'arm': ('Q399', 'hy'), 'aus': ('Q408', 'en'), 'aut': ('Q40', 'de'), 'aze': ('Q227', 'az'),
    'bel': ('Q31', 'nl'), 'bih': ('Q225', 'bs'), 'blr': ('Q184', 'be'), 'bol': ('Q750', 'es'),
    'bra': ('Q155', 'pt'), 'bul': ('Q219', 'bg'), 'can': ('Q16', 'en'), 'chi': ('Q298', 'es'),
    'chn': ('Q148', 'zh'), 'col': ('Q739', 'es'), 'crc': ('Q800', 'es'), 'cro': ('Q224', 'hr'),
    'cyp': ('Q229', 'el'), 'cze': ('Q213', 'cs'), 'den': ('Q35', 'da'), 'ecu': ('Q736', 'es'),
    'egy': ('Q79', 'ar'), 'eng': ('Q145', 'en'), 'esp': ('Q29', 'es'), 'est': ('Q191', 'et'),
    'fin': ('Q33', 'fi'), 'fra': ('Q142', 'fr'), 'fro': ('Q4628', 'fo'), 'geo': ('Q230', 'ka'),
    'ger': ('Q183', 'de'), 'gha': ('Q117', 'en'), 'gib': ('Q1410', 'en'), 'gre': ('Q41', 'el'),
    'gua': ('Q774', 'es'), 'hon': ('Q783', 'es'), 'hun': ('Q28', 'hu'), 'ind': ('Q668', 'en'),
    'irl': ('Q27', 'en'), 'irn': ('Q794', 'fa'), 'isl': ('Q189', 'is'), 'isr': ('Q801', 'he'),
    'ita': ('Q38', 'it'), 'jpn': ('Q17', 'ja'), 'kaz': ('Q232', 'kk'), 'kor': ('Q884', 'ko'),
    'kos': ('Q1246', 'sq'), 'ksa': ('Q851', 'ar'), 'lie': ('Q347', 'de'), 'ltu': ('Q37', 'lt'),
    'lux': ('Q32', 'lb'), 'lva': ('Q211', 'lv'), 'mar': ('Q1028', 'ar'), 'mda': ('Q217', 'ro'),
    'mex': ('Q96', 'es'), 'mkd': ('Q221', 'mk'), 'mlt': ('Q233', 'mt'), 'mne': ('Q236', 'sr'),
    'ned': ('Q55', 'nl'), 'nir': ('Q145', 'en'), 'nor': ('Q20', 'no'), 'nzl': ('Q664', 'en'),
    'pan': ('Q804', 'es'), 'par': ('Q733', 'es'), 'per': ('Q419', 'es'), 'pol': ('Q36', 'pl'),
    'por': ('Q45', 'pt'), 'qat': ('Q846', 'ar'), 'rou': ('Q218', 'ro'), 'rsa': ('Q258', 'en'),
    'rus': ('Q159', 'ru'), 'sco': ('Q145', 'en'), 'slv': ('Q792', 'es'), 'smr': ('Q238', 'it'),
    'srb': ('Q403', 'sr'), 'sui': ('Q39', 'de'), 'sur': ('Q730', 'nl'), 'svk': ('Q214', 'sk'),
    'svn': ('Q215', 'sl'), 'swe': ('Q34', 'sv'), 'tpe': ('Q865', 'zh'), 'tun': ('Q948', 'ar'),
    'tur': ('Q43', 'tr'), 'uae': ('Q878', 'ar'), 'ukr': ('Q212', 'uk'), 'uru': ('Q77', 'es'),
    'usa': ('Q30', 'en'), 'ven': ('Q717', 'es'), 'wal': ('Q145', 'en'),
}
# the guest clubs' files -> their confederation (Wikidata), looked for in all its countries
GUESTS = {'GUESTS_AFC': 'Q48', 'GUESTS_CAF': 'Q15', 'GUESTS_CONCACAF': 'Q49', 'GUESTS_OFC': 'Q538'}  # (continents)
ALSO = {'usa': ['Q16']}                     # (the MLS's Canadian clubs are in our USA)
NATIONS = {'EUROPE', 'AFRICA', 'ASIA', 'SOUTH_AMERICA', 'NORTH_AMERICA', 'OCEANIA'}

# words a club name carries that ours leave out (legal forms, "football club" in many languages)
DROP = {'FC', 'F.C.', 'AFC', 'CF', 'SC', 'AC', 'SSC', 'SS', 'US', 'CFC', 'FK', 'SK', 'BK', 'IF', 'CD',
        'SD', 'UD', 'CA', 'SV', 'VFB', 'VFL', 'TSV', 'TSG', 'FSV', 'CALCIO', 'FOOTBALL', 'CLUB', 'ASD',
        'SSD', 'SRL', 'SPA', 'ACF', 'UC', 'BC', 'AS', 'KV', 'KRC', 'RSC', 'KAA', 'KSC', 'RC', 'NK', 'HNK',
        'GNK', 'FUTBOL', 'FUTEBOL', 'FUSSBALL', 'FUSSBALLCLUB', 'SPORT', 'DE', 'DEL', 'DA', 'DO', 'THE',
        'KS', 'MKS', 'GKS', 'PFC', 'PFK', 'FCI', 'IFK', 'AIK', 'CLUBE', 'ESPORTE', 'SPORTE', 'REGATAS',
        'SAD', 'SA', 'SAS', 'S.A.D.', 'EC', 'AA', 'SE', 'CR', 'FBC', 'AFK', 'TJ', 'SFC', 'JK', 'FF',
        'ATLETICO', 'DEPORTIVO', 'CLUBUL', 'SPORTIV', 'ASOCIATIA', 'ACS', 'CSM', 'CS'}
WOMEN = re.compile(r'women|frauen|femen|femin|ladies|donne|damen|kvinn|dames|vrouwen', re.I)
YOUTH = re.compile(r'\b(u-?\d\d|under-?\d\d|youth|academy|reserves?|juvenil|primavera|amateure)\b', re.I)


def norm(s):
    import modport
    s = modport.swos_name(s.replace('-', ' ').replace('.', ' ').replace("'", '').replace('&', ' '), 99)
    s = re.sub(r'[^A-Z0-9 ]', ' ', s)
    return ' '.join(s.split())


def core(s):
    """A name without its legal forms and years: the words that tell clubs apart."""
    return ' '.join(w for w in norm(s).split() if w not in DROP and not (w.isdigit() and len(w) == 4))


def sparql(q):
    return json.loads(fetch.get(SPARQL + urllib.parse.quote(q), json_api=True))['results']['bindings']


def clubs_of(where, langs):
    """{item: {'labels': set, 'sitelinks': n, 'titles': {lang: title}}} of the football clubs
    `where` selects (a SPARQL pattern on ?item)."""
    ll = ', '.join('"%s"' % l for l in sorted(set(langs) | {'en'}))
    site = ' '.join('<https://%s.wikipedia.org/>' % l for l in sorted(set(langs) | {'en'}))
    q = ('SELECT ?item ?sl ?name ?site ?title WHERE { %s { ?item wdt:P31/wdt:P279* wd:Q476028 . } UNION '
         '{ ?item wdt:P641 wd:Q2736 ; wdt:P31/wdt:P279* wd:Q847017 . } '
         '?item wikibase:sitelinks ?sl . '
         '{ ?item rdfs:label ?name FILTER(lang(?name) IN (%s)) } UNION '
         '{ ?item skos:altLabel ?name FILTER(lang(?name) IN (%s)) } UNION '
         '{ VALUES ?site { %s } ?a schema:about ?item ; schema:isPartOf ?site ; schema:name ?title . } }'
         % (where, ll, ll, site))
    out = {}
    for b in sparql(q):
        it = out.setdefault(b['item']['value'].rsplit('/', 1)[1],
                            {'labels': set(), 'sitelinks': int(b['sl']['value']), 'titles': {}})
        if 'name' in b:
            it['labels'].add(b['name']['value'])
        if 'title' in b:
            lang = b['site']['value'].split('//')[1].split('.')[0]
            it['titles'][lang] = b['title']['value']
            it['labels'].add(b['title']['value'].split(' (')[0])
    return {k: v for k, v in out.items() if v['titles'] and not any(WOMEN.search(l) for l in v['labels'])}


def score(ours, item):
    """How well the game's club name fits an item (0: not at all)."""
    import model
    ours = re.sub(r'\s*\([^)]*\)', '', ours)        # (TOMBENSE (MG): the state)
    o, oc = norm(ours), core(ours)
    best = 0
    for l in item['labels']:
        if YOUTH.search(l) and not YOUTH.search(ours):
            continue
        n, c = norm(l), core(l)
        if o == n or o == model.club_name(l):
            best = max(best, 100)
        elif oc and oc == c:
            best = max(best, 95)
        elif oc and c and (c.startswith(oc + ' ') or c.endswith(' ' + oc) or oc.startswith(c + ' ')):
            best = max(best, 80)
        elif oc and len(oc) >= 4 and (oc.replace(' ', '') == c.replace(' ', '') or n.startswith(o)):
            best = max(best, 75)
        elif oc and set(oc.split()) <= set(c.split()):
            best = max(best, 70 - 3 * (len(c.split()) - len(oc.split())))
    return best


def our_clubs():
    """[(club id, team uuid, json file, team name, country code)] of every club in teams/."""
    rows = [r for r in csv.DictReader(l for l in open(os.path.join(REPO, 'scripts', 'world_club_ids_2627.csv'),
                                                      encoding='utf-8') if not l.startswith('#'))]
    by_uuid = {str(uuid.uuid5(NAMESPACE, '2627/team/%s' % r['club_id'])): r['club_id'] for r in rows}
    out = []
    for f in sorted(glob.glob(os.path.join(WORLD, 'teams', '*.json'))):
        stem = os.path.basename(f)[:-5]
        if stem in NATIONS:
            continue
        for t in json.load(open(f, encoding='utf-8'))['teams']:
            club = by_uuid.get(t['uuid'])
            code = club.split(':')[1].split('.')[0] if club else None
            out.append((club or t['uuid'], t['uuid'], stem, t['team']['team_name'], code))
    return out


def do_map():
    hand = json.load(open(TITLES, encoding='utf-8')) if os.path.exists(TITLES) else {}
    clubs = our_clubs()
    pools = {}
    for code in sorted({c[4] for c in clubs if c[4]}):
        qid, lang = COUNTRY[code]
        if qid not in pools:
            pools[qid] = clubs_of('?item wdt:P17 wd:%s .' % qid, [lang])
            print('%s %s: %d clubs on Wikidata' % (code, qid, len(pools[qid])), flush=True)
    for g, conf in GUESTS.items():
        pools[g] = clubs_of('?item wdt:P17 ?c . ?c wdt:P30 wd:%s .' % conf, ['fr', 'pt', 'es'])
        print('%s: %d clubs on Wikidata' % (g, len(pools[g])), flush=True)
    out, lines = {}, []
    for club, tid, stem, name, code in clubs:
        if club in hand:
            out[tid] = {'club': club, 'name': name, 'titles': dict([hand[club].split(':', 1)]) if hand[club] else {},
                        'lang': hand[club].split(':', 1)[0] if hand[club] else None, 'how': 'by hand'}
            continue
        pool = pools.get(COUNTRY[code][0]) if code else pools.get(stem)
        for extra in ALSO.get(code, []):
            if extra not in pools:
                pools[extra] = clubs_of('?item wdt:P17 wd:%s .' % extra, ['en'])
            pool = dict(pools[extra], **pool)
        lang = COUNTRY[code][1] if code else 'en'
        if pool is None:
            lines.append('NO POOL  %-16s %s %s' % (name, stem, club))
            continue
        ranked = sorted(((score(name, it), it['sitelinks'], q) for q, it in pool.items()), reverse=True)
        if not ranked or ranked[0][0] < 70:
            lines.append('NOT FOUND %-16s %-14s %s' % (name, stem, club))
            continue
        s, _, q = ranked[0]
        close = [r for r in ranked[1:4] if r[0] == s]
        it = pool[q]
        out[tid] = {'club': club, 'name': name, 'item': q, 'score': s, 'titles': it['titles'], 'lang': lang}
        if s < 95 or close:
            lines.append('CHECK    %-16s %-14s %s -> %s (%d%s)' % (name, stem, club, it['titles'].get(lang) or
                         it['titles'].get('en') or next(iter(it['titles'].values())), s,
                         ', ties: ' + ', '.join(pool[r[2]]['titles'].get('en', r[2]) for r in close) if close else ''))
    os.makedirs(WORK, exist_ok=True)
    json.dump(out, open(MAP, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    open(REPORT, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('map: %d of %d clubs found (%s); %d lines to look at in %s' % (len(out), len(clubs), MAP, len(lines), REPORT))


# ---- the articles and the kits ------------------------------------------------------------------

def wikitext(lang, titles):
    """{title: wikitext} from lang's Wikipedia (redirects followed; the asked title the key)."""
    out = {}
    titles = sorted(set(titles))
    for k in range(0, len(titles), 50):
        chunk = titles[k:k + 50]
        q = urllib.parse.urlencode({'action': 'query', 'prop': 'revisions', 'rvprop': 'content',
                                    'rvslots': 'main', 'format': 'json', 'formatversion': '2',
                                    'redirects': '1', 'titles': '|'.join(chunk)})
        d = json.loads(fetch.get('https://%s.wikipedia.org/w/api.php?%s' % (lang, q), json_api=True))
        back = {t: t for t in chunk}
        for r in d['query'].get('normalized', []) + d['query'].get('redirects', []):
            back[r['to']] = back.get(r['from'], r['from'])
        for pg in d['query'].get('pages', []):
            if 'revisions' in pg:
                out[back.get(pg['title'], pg['title'])] = pg['revisions'][0]['slots']['main']['content']
    return out


def infobox_kits(text):
    """{n: kit} of the infobox's kits 1-4 that have anything (wiki.infobox_kit's form)."""
    if not text:
        return {}
    return {n: k for n in (1, 2, 3, 4) for k in [wiki.infobox_kit(text, n)]
            if k.get('body') or k.get('pattern_body')}


def articles(m):
    """{team uuid: (lang, text)}: the club's country's article when its infobox has a kit, else the
    English one's."""
    want = collections.defaultdict(set)
    for e in m.values():
        for lang, t in e['titles'].items():
            if lang in (e['lang'], 'en'):
                want[lang].add(t)
    texts = {}
    for lang in sorted(want):
        got = wikitext(lang, want[lang])
        texts.update({(lang, t): x for t, x in got.items()})
        print('%s: %d articles' % (lang, len(got)), flush=True)
    out = {}
    for tid, e in m.items():
        for lang in (e['lang'], 'en'):
            t = e['titles'].get(lang)
            if t and infobox_kits(texts.get((lang, t))):
                out[tid] = (lang, texts[(lang, t)])
                break
    return out


PARTS = ('body', 'leftarm', 'shorts', 'socks')
PART_BOX = {'body': (16, 58, 4, 34), 'leftarm': (18, 31, 22, 31), 'shorts': (2, 30, 34, 66),
            'socks': (12, 38, 32, 68)}      # (rows, columns of a part in its pattern image: nations.py's)


class Palette:
    def __init__(self):
        d, colours, _ = gkv.load()
        self.below = d['similar_below']
        self.colours = colours
        self.rgb = [gkv.rgb12_to_8(c['rgb']) for c in colours]
        self.memo = {}

    def nearest(self, rgb):
        rgb = tuple(int(round(v)) for v in rgb)
        if rgb not in self.memo:
            self.memo[rgb] = min(range(len(self.rgb)), key=lambda i: gkv.ciede2000(rgb, self.rgb[i]))
        return self.memo[rgb]

    def dist(self, i, j):
        return gkv.ciede2000(self.rgb[i], self.rgb[j])

    def byte(self, i):
        c = self.colours[i]
        return c['base'] | (c['var'] << 4)

    def name(self, i):
        return self.colours[i]['name_en']


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def part_colours(pal, k, part, base):
    """{palette index: pixels} of a kit part: its pattern image over its colour, else the colour."""
    pat = k.get('pattern_' + part)
    px = wiki.pattern_image(part, pat) if pat else None
    y0, y1, x0, x1 = PART_BOX[part]
    if px and len(px) >= y1 and len(px[0]) >= x1:
        br, bg, bb = hexrgb(base)
        c = collections.Counter()
        for row in px[y0:y1]:
            for r, g, b, al in row[x0:x1]:
                f = al / 255
                c[pal.nearest((r * f + br * (1 - f), g * f + bg * (1 - f), b * f + bb * (1 - f)))] += 1
        if c:
            return c, px
    return collections.Counter({pal.nearest(hexrgb(base)): 1}), None


def lum(rgb):
    return 0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]


def shades(pal, counts):
    """[(colour, pixels)] of a part, most first: its colours grouped as the game groups them (closer
    than its similar_below: one colour to a player's eye), each group shown by its darkest shade that
    holds at least 3/8 of it -- the images light their colours (Barcelona's garnet: carmine 30%,
    dark red 19% -> dark red; Dortmund's yellow, 36% of it gold, stays yellow)."""
    groups = []
    for c, n in counts.most_common():
        g = next((g for g in groups if pal.dist(g[0][0], c) < pal.below), None)
        if g:
            g.append((c, n))
        else:
            groups.append([(c, n)])
    out = []
    for g in groups:
        total = sum(n for _, n in g)
        big = [c for c, n in g if n * 8 >= total * 3]
        rep = min(big, key=lambda c: lum(pal.rgb[c])) if big else g[0][0]  # (none that big: the commonest)
        out.append((rep, total))
    return sorted(out, key=lambda x: -x[1])


def read_kit(pal, k):
    """(type, stripes, basic, shorts, socks) as palette indexes, from an infobox kit."""
    k = dict(k)
    body_base = k.get('body') or '#ffffff'
    body, px = part_colours(pal, k, 'body', body_base)
    top = lambda part, base: shades(pal, part_colours(pal, k, part, base)[0])[0][0]
    arm = top('leftarm', k.get('leftarm') or body_base)
    shorts = top('shorts', k.get('shorts') or body_base)
    socks = top('socks', k.get('socks') or k.get('shorts') or body_base)
    order = shades(pal, body)
    main = order[0][0]
    total = sum(body.values())
    second = order[1] if len(order) > 1 else None
    if second and second[1] >= 0.2 * total and px:
        group = {c: rep for rep, _ in order for c in body if pal.dist(c, rep) < pal.below}
        two = {main, second[0]}
        y0, y1, x0, x1 = PART_BOX['body']
        br, bg, bb = hexrgb(body_base)
        grid = [[group.get(c, c) for c in (pal.nearest((r * a / 255 + br * (1 - a / 255), g * a / 255 + bg * (1 - a / 255),
                 b * a / 255 + bb * (1 - a / 255))) for r, g, b, a in row[x0:x1])] for row in px[y0:y1]]
        across = sum(1 for line in grid for x in range(1, len(line))
                     if line[x] != line[x - 1] and line[x] in two and line[x - 1] in two)
        down = sum(1 for y in range(1, len(grid)) for x in range(len(grid[0]))
                   if grid[y][x] != grid[y - 1][x] and grid[y][x] in two and grid[y - 1][x] in two)
        return (2 if across >= down else 3, second[0], main, shorts, socks)
    if pal.dist(arm, main) >= pal.below:
        return (1, main, arm, shorts, socks)
    return (0, main, main, shorts, socks)


def kit_distance(pal, a, b):
    """How differently two kits are coloured: the shirt's two colours, the shorts, the socks."""
    return (2 * pal.dist(a[2], b[2]) + pal.dist(a[1], b[1]) + pal.dist(a[3], b[3]) + 0.5 * pal.dist(a[4], b[4]))


def team_kits(pal, text):
    """(home, away, how): palette-index kits, away the alternate kit most unlike home."""
    kits = infobox_kits(text)
    home = read_kit(pal, kits[1]) if 1 in kits else None
    alts = [(n, read_kit(pal, kits[n])) for n in (2, 3, 4) if n in kits]
    away, how = None, ''
    if alts:
        if home and len(alts) > 1:
            n, away = max(alts, key=lambda a: kit_distance(pal, home, a[1]))
            how = 'kit %d of %s' % (n, '/'.join(str(a[0]) for a in alts))
        else:
            away = alts[0][1]
    return home, away, how


def describe(pal, kit):
    names = ('plain', 'sleeves', 'stripes', 'hoops')
    t, st, ba, sh, so = kit
    shirt = pal.name(ba) if t == 0 else '%s/%s' % (pal.name(ba), pal.name(st))
    return '%s %s, %s shorts, %s socks' % (names[t], shirt, pal.name(sh), pal.name(so))


def do_fetch():
    m = json.load(open(MAP, encoding='utf-8'))
    arts = articles(m)
    print('%d of %d clubs have an article with a kit' % (len(arts), len(m)), flush=True)
    pats = set()
    for lang, text in arts.values():
        for n, k in infobox_kits(text).items():
            for part in PARTS:
                if k.get('pattern_' + part):
                    pats.add((part, k['pattern_' + part].strip().replace(' ', '_')))
    todo = [p for p in sorted(pats) if fetch.cached(wiki.FILEPATH + {'body': 'Kit_body', 'leftarm': 'Kit_left_arm',
            'shorts': 'Kit_shorts', 'socks': 'Kit_socks'}[p[0]] + p[1] + '.png') is None]
    print('%d pattern images, %d not cached yet (~%d min)' % (len(pats), len(todo), len(todo) * 3.3 / 60), flush=True)
    for i, (part, name) in enumerate(todo):
        wiki.pattern_image(part, name)
        if i % 100 == 99:
            print('  %d / %d images' % (i + 1, len(todo)), flush=True)


def do_apply(dry):
    pal = Palette()
    m = json.load(open(MAP, encoding='utf-8'))
    arts = articles(m)
    lines, changed, counts = [], 0, collections.Counter()
    names = ('shirt_type', 'stripes_color', 'basic_color', 'shorts_color', 'socks_color')
    for f in sorted(glob.glob(os.path.join(WORLD, 'teams', '*.json'))):
        d = json.load(open(f, encoding='utf-8'))
        dirty = False
        for t in d['teams']:
            a = arts.get(t['uuid'])
            if not a:
                continue
            home, away, how = team_kits(pal, a[1])
            counts['lang ' + a[0]] += 1
            h = t['team']
            for side, kit in (('primary_', home), ('secondary_', away)):
                if not kit:
                    counts['no %skit' % side] += 1
                    continue
                vals = [kit[0]] + [pal.byte(c) for c in kit[1:]]
                if kit[0] == 0:
                    vals[1] = vals[2]
                old = [h[side + names[0]]] + [(h[side + n] & 15) | (h.get(side + n.replace('_color', '_variant'), 0) << 4)
                                              for n in names[1:]]
                if old != vals:
                    h[side + names[0]] = vals[0]
                    for n, v in zip(names[1:], vals[1:]):      # the base, and its variant apart (as the game writes)
                        h[side + n] = v & 15
                        var = n.replace('_color', '_variant')
                        if v >> 4:
                            h[side + var] = v >> 4
                        else:
                            h.pop(side + var, None)
                    dirty = True
                    counts['%skits changed' % side] += 1
            lines.append('%-16s %-14s %s:%s | home %s | away %s %s' % (
                h['team_name'], os.path.basename(f)[:-5], a[0], m[t['uuid']]['titles'].get(a[0]),
                describe(pal, home) if home else '-', describe(pal, away) if away else '-', how))
        if dirty:
            changed += 1
            if not dry:
                json.dump(d, open(f, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    open(os.path.join(WORK, 'kits_applied.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('%s: %d files changed; %s' % ('dry run' if dry else 'applied', changed, dict(sorted(counts.items()))))


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', action='store_true')
    ap.add_argument('--fetch', action='store_true')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    a = ap.parse_args()
    if a.map:
        do_map()
    if a.fetch:
        do_fetch()
    if a.apply:
        do_apply(a.dry_run)
