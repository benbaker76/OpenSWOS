#!/usr/bin/env python3
"""worldgen/gen.py -- generate World 2026-27 squads from the original sources (docs/WORLD_DATAGEN.md).

    python3 -I scripts/worldgen/gen.py [--sample scripts/worldgen/sample_2627.csv] [--out DIR]
                                       [--offline] [--no-move] [--no-calibrate]

For each club of the sample (country, division, Transfermarkt club id, its 2025-26 level and
place, in that table's order) it writes the Mod's pack format -- DIR/<country>/clubs.csv and
players.csv, what the Mod's mkseason and our world importer read -- from:

  squad list, shirt numbers, nations  the club's English Wikipedia article ({{Fs player}}; the
                                       article found through Wikidata's Transfermarkt id P7223);
                                       the Transfermarkt dataset's club list when it has none
  market value, sub-position, birth   Transfermarkt (--tm): the open transfermarkt-datasets
                                       (snapshot 2026-07-06; 'dataset', the default), Transfermarkt's
                                       own pages saved from a browser (tmpages.py; 'pages': then the
                                       squad list, numbers, second nationality and coach are the
                                       pages' too, as the Mod's), or 'both' (the pages where a club
                                       has them, the dataset filling the rest)
  attributes, overall                 EA SPORTS FC's public ratings feed
  kits, coach, club name              the Wikipedia infobox

The default output is bin/data_json/worldgen/ (gitignored: it is derived from the sources but
the squads are still real people's data; it is not shipped). compare.py measures it against the
Mod's packs club by club.
"""
import argparse
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ea                                               # noqa: E402
import join                                             # noqa: E402
import model                                            # noqa: E402
import modpack                                          # noqa: E402
import modport                                          # noqa: E402
import tmpages                                          # noqa: E402
import wiki                                             # noqa: E402

COUNTRY = {'eng': ('ENG', 'ENG'), 'ita': ('ITA', 'ITA'), 'sco': ('SCO', 'SCO'), 'mlt': ('MLT', 'MLT'),
           'esp': ('ESP', 'ESP'), 'ger': ('GER', 'GER'), 'fra': ('FRA', 'FRA')}   # pack code -> (clubs.csv
                                                        # country, the game nationality of a home player)
MIN_WIKI_SQUAD = 14


def match_tm(name, club_players, src):
    """The dataset player of a Wikipedia / dataset name: at the club first (the most shared name
    tokens; the surname among them, or the dataset's whole name inside the given one -- TM's
    'Gabriel' for Wikipedia's 'Gabriel Magalhaes'), else the one player of the whole dataset with
    exactly these tokens."""
    want = ea.tokens(name)
    last = ea.fold(name).split()[-1:]
    best, score = None, 0
    for p in club_players:
        have = ea.tokens(p['name'])
        s = len(want & have)
        if s > score and ((last and last[0] in have) or (have and have <= want)):
            best, score = p, s
    if best:
        return best
    cands = src.tm_by_tokens.get(frozenset(want)) or []
    return cands[0] if len(cands) == 1 else None


def squad_of(club, src, texts, arts, pages_squads=True):
    """[{name, number, fifa, wpos, tm, ea, order}] of one club."""
    tid = club['tm_id']
    tm_players = src.tm.by_club.get(tid, [])
    wl = wiki.squad(texts[arts[tid]]) if tid in arts and arts[tid] in texts else []
    out = []
    if pages_squads and tid in src.tm_pages_clubs:     # Transfermarkt's own squad (the Mod's)
        for k, t in enumerate(tm_players):
            out.append(dict(name=t['name'], number=t.get('number'), fifa='', wpos='', tm=t, order=k))
        club['squad_source'] = 'tm-pages'
    elif len(wl) >= MIN_WIKI_SQUAD:
        taken = set()
        for k, w in enumerate(wl):
            t = match_tm(w['name'], [p for p in tm_players if p['player_id'] not in taken], src)
            if t:
                taken.add(t['player_id'])
            out.append(dict(name=w['name'], number=w['no'], fifa=w['nat'], wpos=w['pos'], tm=t, order=k))
        club['squad_source'] = 'wikipedia'
    else:
        for k, t in enumerate(sorted(tm_players, key=lambda p: -float(p['market_value_in_eur'] or 0))):
            if t['last_season'] and int(t['last_season']) < 2025:
                continue
            out.append(dict(name=t['name'], number=None, fifa='', wpos='', tm=t, order=k))
        club['squad_source'] = 'transfermarkt-datasets' if out else 'none'
    for p in out:
        t = p['tm']
        birth = t['date_of_birth'][:10] if t and t['date_of_birth'] else None
        p['ea'] = src.ea.match(p['name'], birth) if birth else src.ea_player(p['name'])
    return out


def use_tm_source(src, clubs, mode, folder, season):
    """Point src.tm's per-club players (and the name index) at the chosen Transfermarkt source.
    'pages': a club's players are its saved squad page's (none when it has no page); 'both': the
    page's where there is one, a field the page lacks filled from the dataset's record of the same
    player id, the dataset's players for a club without a page."""
    src.tm_pages_clubs, src.tm_pages_coach = set(), {}
    if mode == 'dataset':
        return
    pages = tmpages.Pages(folder, season)
    by_id = {p['player_id']: p for p in src.tm.players}
    by_club = {}
    for c in clubs:
        tid = c['tm_id']
        recs = pages.records(tid)
        if recs is not None:
            if mode == 'both':
                for r in recs:
                    d = by_id.get(r['player_id'])
                    for k, v in (d or {}).items():
                        if not r.get(k) and v:
                            r[k] = v
            by_club[tid] = recs
            src.tm_pages_clubs.add(tid)
        elif mode == 'both':
            by_club[tid] = src.tm.by_club.get(tid, [])
        else:
            by_club[tid] = []
        if pages.coaches.get(tid):
            src.tm_pages_coach[tid] = pages.coaches[tid]
    src.tm.by_club = by_club
    players = [p for ps in by_club.values() for p in ps]
    if mode == 'both':
        seen = {p['player_id'] for p in players}
        players += [p for p in src.tm.players if p['player_id'] not in seen]
    src.tm_by_tokens = {}
    for p in players:
        src.tm_by_tokens.setdefault(frozenset(ea.tokens(p['name'])), []).append(p)
    dates = sorted(pages.dates[t] for t in src.tm_pages_clubs if pages.dates.get(t))
    print(f'[tm] {mode}: saved squads for {len(src.tm_pages_clubs)} of {len(clubs)} clubs, coaches for '
          f'{len(src.tm_pages_coach)} (pages dated {dates[0] if dates else "-"} .. {dates[-1] if dates else "-"})')


def build_club(club, squad, curves, home_nat):
    """The club's players (16 + extras) with positions, values and S before calibration."""
    lv = curves.get('level', {})
    known = [float(p['tm']['market_value_in_eur']) for p in squad
             if p['tm'] and p['tm']['market_value_in_eur']]
    for p in squad:
        t, e = p['tm'], p['ea']
        sub = (t and t['sub_position']) or (e and model.EA_SUB.get(e['pos'])) or model.WIKI_SUB.get(p['wpos'], '')
        pos = (t and model.TM_POS.get(t['sub_position'] or t['position'])) or (e and model.EA_POS.get(e['pos'])) \
            or model.WIKI_POS.get(p['wpos'], 'M')
        p['sub'], p['pos'] = sub, pos
        p['rated'] = bool(e or (t and t['market_value_in_eur']))
        if t and t['market_value_in_eur']:
            p['value'], p['value_known'] = float(t['market_value_in_eur']), True
        else:                                   # the Mod: 70% of the club's lowest known value
            p['value'], p['value_known'] = (0.7 * min(known) if known else club['division_median']), False
        birth = (t and t['date_of_birth']) or (e and e['birth'])
        years = model.age(birth) if birth else ((t and t.get('age')) or 25)   # (a page with ages only)
        x = model.adjusted_value(p['value'], years)
        c = lv.get('keeper' if pos == 'G' else 'outfield')
        p['S'] = max(0, min(49, round(model.level(c['value'], c['overall'], x, e['overall'] if e else None)))) \
            if c else 10
        nat = model.nationality(p['fifa'], t and t['country_of_citizenship'], curves, home_nat)
        p['nat'], p['skin'] = nat, model.face(nat, curves)
        if t and t.get('second_citizenship'):           # (the pages': the Mod's rule, any of them)
            nat2 = model.nationality('', t['second_citizenship'], curves, None)
            if nat2 is not None and model.face(nat2, curves) == 'dark':
                p['skin'] = 'dark'
        p['out_name'] = modport.swos_name(t['name'] if t else p['name'])
        p['ident'] = t['player_id'] if t else p['name']
    return model.pick_sixteen(squad)


def finish_player(p, step, curves, move):
    S = max(0, min(49, p['S'] + step))
    if p['position'] == 'G':
        return dict(p, skills=[0] * 7, keys=[], price=S)
    if p['ea']:
        v, keys = model.ea_skills(S, p['ea']['stats'])
    else:
        v, keys = model.profile_skills(S, p['sub'], curves, p['ident'], move)
    return dict(p, skills=v, keys=keys, price=sum(v))


def row(club_key, p):
    out = dict(club=club_key, name=p['out_name'], role=modpack.ROLE[p['position']], position=p['position'],
               number=p['number'], nat=p['nat'], skin=p['skin'], price=p['price'])
    for i, s in enumerate(modpack.SKILLS):
        out[s] = f"{p['skills'][i]}{'*' if i in p['keys'] else ''}"
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sample', default=os.path.join(model.HERE, 'sample_2627.csv'))
    ap.add_argument('--out', default=os.path.join(modpack.REPO, 'bin', 'data_json', 'worldgen', '2627'))
    ap.add_argument('--offline', action='store_true', help='read the cache only')
    ap.add_argument('--no-move', action='store_true', help="no hashed point move on profile players")
    ap.add_argument('--no-calibrate', action='store_true', help='no division calibration step')
    ap.add_argument('--no-images', action='store_true', help="kits from the infobox's colours only (no "
                    'pattern images from Commons)')
    ap.add_argument('--tm', choices=('dataset', 'pages', 'both'), default='dataset',
                    help="Transfermarkt's data from the open dataset, from saved pages (--tm-pages), or "
                    'both (the pages win where a club has them)')
    ap.add_argument('--tm-pages', default=tmpages.DEFAULT_DIR, help='the folder of saved Transfermarkt pages')
    ap.add_argument('--season', type=int, default=2026, help="the pages' season (2026 = 2026-27)")
    ap.add_argument('--wiki-squads', action='store_true', help="squad lists from Wikipedia even where a "
                    'club has a saved Transfermarkt squad (its values still from the pages)')
    a = ap.parse_args()
    curves = model.load_curves()
    src = join.Sources(offline=a.offline)
    clubs = model.sample(a.sample)
    use_tm_source(src, clubs, a.tm, a.tm_pages, a.season)
    arts = wiki.articles([c['tm_id'] for c in clubs], offline=a.offline)
    texts = wiki.wikitext(arts.values(), offline=a.offline)
    divisions = defaultdict(list)
    for c in clubs:
        divisions[(c['country'], c['division'])].append(c)
    out = defaultdict(lambda: ([], []))
    for (cc, d), cl in divisions.items():
        country, home = COUNTRY.get(cc, (cc.upper(), cc.upper()))
        home_nat = model.GAME_NAT.get(home, 0)
        squads = [squad_of(c, src, texts, arts, not a.wiki_squads) for c in cl]
        vals = sorted(float(p['tm']['market_value_in_eur']) for s in squads for p in s
                      if p['tm'] and p['tm']['market_value_in_eur'])
        median = vals[len(vals) // 2] if vals else 50000.0
        built = []
        for c, s in zip(cl, squads):
            c['division_median'] = 0.6 * median
            built.append(build_club(c, s, curves, home_nat))
        calib = [dict(rows=r, prev_level_same=c['prev_division'] == c['division'], prev_rank=int(c['prev_position']))
                 for c, (r, _) in zip(cl, built)]
        steps = [0] * len(cl) if a.no_calibrate else model.calibrate(calib)
        for c, (rows, extras), step in zip(cl, built, steps):
            rows = [finish_player(p, step, curves, not a.no_move) for p in rows]
            extras = [modport.xtra_cap(finish_player(p, step, curves, not a.no_move), rows) for p in extras]
            model.free_numbers(rows + extras)
            title = arts.get(c['tm_id'], '')
            text = texts.get(title, '')
            tmclub = src.tm.clubs.get(c['tm_id'], {})
            name = model.club_name(title or tmclub.get('name', c['tm_id']))
            key = re.sub(r'[^a-z0-9]', '', name.lower()) or 'club' + c['tm_id']
            coach = re.sub(r'\(.*?\)', '', wiki.infobox_field(text, 'manager', 'head_coach', 'coach')).strip()
            if src.tm_pages_coach.get(c['tm_id']):         # the staff page's, when saved
                coach = src.tm_pages_coach[c['tm_id']]
            if not coach or coach.lower() in ('vacant', 'tba', 'tbd'):
                coach = tmclub.get('coach_name', '') or ''
            kits = []
            for n, default in ((1, '0 1 1 1 1'), (2, '0 2 2 2 2')):
                k = wiki.infobox_kit(text, n)
                px = wiki.pattern_image('body', k['pattern_body'], a.offline) \
                    if k.get('pattern_body') and not a.no_images else None
                kits.append(model.kit(k, px) or default)
            k1, k2 = kits
            out[cc][0].append(dict(key=key, name=name, coach=modport.swos_name(coach, 24), country=country,
                                   file='league', division=c['division'], tactic=0, kit1=k1, kit2=k2,
                                   lineup=' '.join(str(i) for i in range(16)),
                                   source=f"{title or tmclub.get('name', '')} (TM {c['tm_id']}; "
                                          f"squad {c['squad_source']}; step {step:+d})"))
            out[cc][1].extend(row(key, p) for p in rows + extras)
    for cc, (cl, pl) in out.items():
        modpack.write(os.path.join(a.out, cc), cl, pl)
        print(f'{cc}: {len(cl)} clubs, {len(pl)} players -> {os.path.join(a.out, cc)}')


if __name__ == '__main__':
    main()
