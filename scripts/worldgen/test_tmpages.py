#!/usr/bin/env python3
"""worldgen/test_tmpages.py -- the saved-page parser's tests (docs/WORLD_DATAGEN.md).

Two sets:
  tests/worldgen/tm_*.html   our own small pages in Transfermarkt's markup with INVENTED players
                             (committed): every field of a detailed squad (English and German, a
                             browser "saved from url" mark, absolute and relative links), a staff
                             page with an assistant and a youth coach before the manager, a table.
  --wayback DIR              real pages: Wayback Machine copies of transfermarkt.com (fetched by
                             fetch.py from web.archive.org, never from Transfermarkt; kept in the
                             ignored cache, not in git: they are Transfermarkt's pages). Checks that
                             every page is recognised and how many players have each field.

    python3 -I scripts/worldgen/test_tmpages.py [--wayback bin/data_json/worldgen-cache/tm-pages-wayback]
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tmpages                                          # noqa: E402

FIX = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tests', 'worldgen')
FAILS = []


def check(what, got, want):
    if got != want:
        FAILS.append(f'{what}: {got!r} != {want!r}')


def synthetic():
    for lang in ('en', 'de'):
        pg = tmpages.parse(os.path.join(FIX, f'tm_squad_{lang}.html'))
        check(f'{lang} kind', (pg['kind'], pg['id'], pg['season'], pg['host']),
              ('squad', '900000', '2026', 'com' if lang == 'en' else 'de'))
        ps = pg['players']
        check(f'{lang} players', len(ps), 3)
        k, b, c = ps
        check(f'{lang} keeper', (k['player_id'], k['name'], k['position'], k['number'], k['birth'], k['age'],
                                 k['height'], k['foot'], k['joined'], k['contract'], k['value'], k['signed_from']),
              ('900001', 'Aldo Keeperson', 'Goalkeeper', 1, '1995-09-15', 31, 191, 'right', '2024-07-01',
               '2028-06-30', 30000000, 'Example Town'))
        check(f'{lang} back', (b['position'], b['value'], b['foot'], b['captain'], [i for _, i in b['nat']]),
              ('Centre-Back', 800000, 'left', True, ['50', '31']))
        check(f'{lang} striker', (c['position'], c['number'], c['value'], c['height'], c['contract'],
                                  bool(c['injury'])), ('Centre-Forward', None, None, None, None, True))
    en = tmpages.parse(os.path.join(FIX, 'tm_squad_en.html'))
    check('en nationality names', en['players'][1]['nat'], [('France', '50'), ('Cameroon', '31')])
    st = tmpages.parse(os.path.join(FIX, 'tm_staff_en.html'))
    check('staff', (st['kind'], st['id'], st['coach'], len(st['staff'])), ('staff', '900000', 'Manu Manager', 3))
    tb = tmpages.parse(os.path.join(FIX, 'tm_table_en.html'))
    check('table', (tb['kind'], tb['id'], tb['season'], [(r['place'], r['club_id'], r['points']) for r in tb['table']]),
          ('table', 'XX1', '2025', [(1, '900000', 10), (2, '900100', 6), (3, '900101', 1)]))
    check('table row', {k: tb['table'][0][k] for k in ('played', 'won', 'drawn', 'lost', 'goals_for', 'goals_against')},
          dict(played=4, won=3, drawn=1, lost=0, goals_for=9, goals_against=2))
    # the folder view: the German page's countries named through the English page's flag ids
    pages = tmpages.Pages(FIX, 2026)
    check('folder squads', sorted(pages.squads), ['900000'])
    check('folder coach', pages.coaches.get('900000'), 'Manu Manager')
    pages.country['50'] = 'France'
    de = tmpages.parse(os.path.join(FIX, 'tm_squad_de.html'))
    check('de names via ids', pages.nat_names(de['players'][1])[0], 'France')
    rec = pages.records('900000')
    check('record', {k: rec[1][k] for k in ('player_id', 'sub_position', 'position', 'country_of_citizenship',
                                             'second_citizenship', 'height_in_cm', 'market_value_in_eur')},
          dict(player_id='900002', sub_position='Centre-Back', position='Defender', country_of_citizenship='France',
               second_citizenship='Cameroon', height_in_cm='193', market_value_in_eur='800000'))
    check('keeper record', (rec[0]['sub_position'], rec[0]['position']), ('Goalkeeper', 'Goalkeeper'))
    for s, want in (('€1.34bn', 1340000000), ('€500k', 500000), ('€30.00m', 30000000), ('30,00 Mio. €', 30000000),
                    ('800 Tsd. €', 800000), ('-', None), ('£2.5m', 2500000)):
        check(f'value {s}', tmpages.read_value(s), want)
    for s, want in (('15/09/1995 (30)', '1995-09-15'), ('15.09.1995', '1995-09-15'), ('Sep 15, 1995', '1995-09-15'),
                    ('15. Sep. 1995', '1995-09-15'), ('Last update: Oct 2, 2026', '2026-10-02')):
        check(f'date {s}', tmpages.read_date(s), want)


def wayback(folder):
    pages = tmpages.scan(folder)
    fields = ('name', 'position', 'number', 'birth', 'age', 'nat', 'height', 'foot', 'joined', 'contract', 'value')
    count, total = {f: 0 for f in fields}, 0
    second = 0
    for (k, i, s), pg in sorted(pages.items(), key=lambda x: str(x[0])):
        n = len(pg.get('players') or pg.get('table') or pg.get('clubs') or pg.get('staff') or ())
        print(f"  {k:8} {str(i):8} {str(s):5} {tmpages.stamp(pg)}  {n:3} rows  {os.path.basename(pg['file'])}")
        if k == 'unknown':
            FAILS.append(f"not recognised: {pg['file']}")
        for p in pg.get('players', ()):
            total += 1
            second += len(p['nat']) > 1
            for f in fields:
                count[f] += p[f] not in (None, '', [])
        if k in ('squad', 'club') and n < 18:
            FAILS.append(f"{pg['file']}: only {n} players")
    if total:
        print(f'  {total} players; ' + ', '.join(f'{f} {100 * count[f] / total:.0f}%' for f in fields)
              + f', second nationality {100 * second / total:.0f}%')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--wayback', help='a folder of Wayback copies of Transfermarkt pages')
    a = ap.parse_args()
    synthetic()
    if a.wayback:
        wayback(a.wayback)
    for f in FAILS:
        print('FAIL', f)
    print('tmpages: ' + ('PASS' if not FAILS else f'{len(FAILS)} FAILED'))
    sys.exit(1 if FAILS else 0)


if __name__ == '__main__':
    main()
