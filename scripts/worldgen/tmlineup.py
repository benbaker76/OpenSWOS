#!/usr/bin/env python3
"""worldgen/tmlineup.py -- a national team's last line-up from its Transfermarkt page, saved from the
owner's own browser ("Webpage, Complete" or SingleFile: the page as drawn, its line-up box filled in
by the site's script; never fetched here -- docs/WORLD_DATAGEN.md §4, §15).

    python3 -I scripts/worldgen/tmlineup.py DIR...      # each page found: team id, formation, XI

parse(path) -> {'team': Transfermarkt team id, 'formation': 'Starting Line-up: <this>', 'xi': [{name,
slug, tm_id, number, top, left, captain}], 'subs': [{name, slug, tm_id, number}]} or None. top / left are
the player's box on the pitch drawing in percent: top 80 the goalkeeper, smaller nearer the opponents'
goal; left small on the team's LEFT (the drawing looks up the pitch, as the team attacks).
"""
import html
import os
import re
import sys

PLAYER = re.compile(r'<div class="formation-player-container[^"]*" style="top:\s*([\d.]+)%;\s*left:\s*([\d.]+)%;?">'
                    r'(.*?)(?=<div class="formation-player-container|<div class="large-5|$)', re.S)
LINK = re.compile(r'<a[^>]*href="[^"]*/([^/"]+)/profil/spieler/(\d+)"[^>]*>([^<]*)</a>')


def team_id(text):
    for pat in (r'<link rel="canonical" href="[^"]*/verein/(\d+)', r'<meta property="og:url" content="[^"]*/verein/(\d+)',
                r'saved from url=\(\d+\)[^ ]*/verein/(\d+)', r'url: [^ ]*/verein/(\d+)'):
        m = re.search(pat, text)
        if m:
            return m.group(1)
    return None


SQUAD_ROW = re.compile(r'<div class="rn_nummer">\s*(\d+|-)\s*</div>(.{0,2500}?)href="[^"]*/([^/"]+)/profil/spieler/'
                       r'(\d+)"[^>]*>\s*([^<]+?)\s*</a>', re.S)


def squad_numbers(text):
    """[{number, slug, tm_id, name}] of the page's squad table (Transfermarkt's current national squad:
    each player's shirt number, '-' none)."""
    out, seen = [], set()
    for m in SQUAD_ROW.finditer(text):
        if m.group(4) in seen or 'rn_nummer' in m.group(2):
            continue
        seen.add(m.group(4))
        out.append(dict(number=int(m.group(1)) if m.group(1).isdigit() else None, slug=m.group(3),
                        tm_id=m.group(4), name=html.unescape(m.group(5)).strip()))
    return out


def parse(path):
    text = open(path, encoding='utf-8', errors='replace').read()
    tid = team_id(text)
    if not tid or '/startseite/verein/' not in text:
        return None
    numbers = squad_numbers(text)
    m = re.search(r'Starting Line-up:\s*([^<]+)<', text)
    if not m:                                   # (no line-up box: the squad's numbers still count)
        return dict(team=tid, formation=None, xi=[], subs=[], numbers=numbers, path=path) if numbers else None
    start = m.end()
    sub_at = text.find('Substitutes', start)
    field = text[start:]
    xi = []
    for pm in PLAYER.finditer(field):
        top, left, body = float(pm.group(1)), float(pm.group(2)), pm.group(3)
        lm = LINK.search(body)
        num = re.search(r'tm-shirt-number[^>]*>\s*(\d+)\s*<', body)
        if not lm:
            continue
        xi.append(dict(slug=lm.group(1), tm_id=lm.group(2), name=html.unescape(lm.group(3)).strip(),
                       number=int(num.group(1)) if num else None, top=top, left=left,
                       captain='captain-icon' in body or 'kapitaenicon' in body))
        if len(xi) == 11:
            break
    subs = []
    if sub_at > 0:
        seg = text[sub_at:sub_at + 40000]
        end = re.search(r'<h2|class="content-box-headline|Squad', seg[200:])
        if end:
            seg = seg[:200 + end.start()]
        seen = set()
        for lm in LINK.finditer(seg):
            if lm.group(2) in seen or lm.group(2) in {p['tm_id'] for p in xi}:
                continue
            seen.add(lm.group(2))
            subs.append(dict(slug=lm.group(1), tm_id=lm.group(2), name=html.unescape(lm.group(3)).strip()))
    return dict(team=tid, formation=html.unescape(m.group(1)).strip(), xi=xi, subs=subs, numbers=numbers, path=path)


def pages(dirs):
    """{team id: the newest page's parse} of every saved page in the folders (subfolders too)."""
    out = {}
    for d in dirs:
        for root, sub, files in os.walk(d):
            sub[:] = [s for s in sub if not s.endswith('_files')]
            for f in files:
                if not f.lower().endswith(('.html', '.htm')):
                    continue
                p = os.path.join(root, f)
                try:
                    r = parse(p)
                except OSError:
                    continue
                if not r:
                    continue
                old = out.get(r['team'])        # (a page with a line-up over one without, then the newest)
                if not old or (len(r['xi']), os.path.getmtime(p)) > (len(old['xi']), os.path.getmtime(old['path'])):
                    out[r['team']] = r
    return out




# ---- Wikipedia: a tournament match article's line-ups -----------------------------------------------

# the tournaments whose match articles give line-ups with positions, newest first (a nation's line-up
# is its newest match's): each a list of articles in the tournament's order (groups, then knockouts)
WIKI_TOURNAMENTS = [
    ['2026 FIFA World Cup Group %s' % g for g in 'ABCDEFGHIJKL'] + ['2026 FIFA World Cup knockout stage'],
    ['2026 FIFA World Cup qualification (inter-confederation play-offs)'],
    ['2026 FIFA World Cup qualification – UEFA second round'],
    ['2025 Africa Cup of Nations Group %s' % g for g in 'ABCDEF'] + ['2025 Africa Cup of Nations knockout stage'],
    ['2025 FIFA Arab Cup Group %s' % g for g in 'ABCD'] + ['2025 FIFA Arab Cup knockout stage'],
    ['2025 CONCACAF Gold Cup Group %s' % g for g in 'ABCD'] + ['2025 CONCACAF Gold Cup knockout stage'],
    ['UEFA Euro 2024 Group %s' % g for g in 'ABCDEF'] + ['UEFA Euro 2024 knockout stage'],
    ['2024 Copa América Group %s' % g for g in 'ABCD'] + ['2024 Copa América knockout stage'],
    ['2024 OFC Men\'s Nations Cup'],
    ['2023 AFC Asian Cup Group %s' % g for g in 'ABCDEF'] + ['2023 AFC Asian Cup knockout stage'],
]
WIKI_POS = {'GK', 'RB', 'LB', 'CB', 'SW', 'RWB', 'LWB', 'DM', 'CM', 'AM', 'RM', 'LM', 'RW', 'LW', 'CF', 'ST', 'SS',
            'RF', 'LF', 'FW', 'MF', 'DF', 'RCB', 'LCB', 'RCM', 'LCM', 'RDM', 'LDM'}


def wiki_lineups(text):
    """[(team name, [{pos, number, name, link, captain}] the XI, [{pos, name, link}] the substitutes)] of
    an article, in its order: each match's two kits name its teams, its two line-up tables follow."""
    titles = []
    for km in re.finditer(r'\{\{\s*football kit', text, flags=re.I):
        seg = text[km.end():km.end() + 1500]
        m = re.search(r'\|\s*title\s*=\s*((?:\{\{nowrap\|[^}]*\}\})|[^|\n<}]+)', seg)
        if m:
            titles.append(re.sub(r'\{\{nowrap\|([^}]*)\}\}', r'\1', m.group(1)).strip())
    tables = []
    for tm in re.finditer(r'\{\|[^\n]*\n(.*?)\n\|\}', text, flags=re.S):
        body = tm.group(1)
        if not re.search(r'\n\|\s*GK\s*\|\|', '\n' + body):
            continue
        xi, subs, part = [], [], 'xi'
        for line in body.split('\n'):
            if 'Substitut' in line:
                part = 'subs'
                continue
            if 'Manager' in line or 'Coach' in line:
                break
            m = re.match(r'\|\s*([A-Z]{2,3})\s*\|\|\s*(?:\'\'\'\s*(\d+)\s*\'\'\')?\s*\|\|\s*(.*)', line)
            if not m or m.group(1) not in WIKI_POS:
                continue
            rest = m.group(3)
            lm = re.search(r'\[\[([^|\]]+)(?:\|([^\]]+))?\]\]', rest)
            if not lm:
                continue
            p = dict(pos=m.group(1), number=int(m.group(2)) if m.group(2) else None,
                     name=(lm.group(2) or lm.group(1)).strip(), link=lm.group(1).strip(),
                     captain='Captain (association football)' in rest or "'''c'''" in rest)
            (xi if part == 'xi' else subs).append(p)
        if len(xi) == 11:
            tables.append((xi, subs))
    return [(titles[k], xi, subs) for k, (xi, subs) in enumerate(tables) if k < len(titles)]


if __name__ == '__main__':
    for tid, r in sorted(pages(sys.argv[1:]).items(), key=lambda kv: kv[1]['path']):
        cap = [p['name'] for p in r['xi'] if p['captain']]
        print('%-6s %-24s %2d XI %2d subs %2d numbers  captain %-18s %s' % (tid, r['formation'] or '-', len(r['xi']),
                                                                         len(r['subs']), len(r['numbers']),
                                                           cap[0] if cap else '-', os.path.basename(r['path'])[:40]))
