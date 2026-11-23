#!/usr/bin/env python3
"""worldgen/seal.py -- make a world's teams/ as they are now (after nations.py and rebalance.py) its
master data: what the package carries, the Amiga plays and the game checks (docs/WORLD_DATAGEN.md §17).

    python3 -I scripts/worldgen/seal.py [--world DIR] [--data-c FILE] [--dry-run]

0. each team's checksum (what the server's team list checks it by: assign_team_uuids.py checksum, its
   16's positions, skills and prices) made again where an edit after the import left it stale.
1. data/TEAM.nnn: each club team file's Amiga binary written again from its JSON (world_import.py's own
   writer), so the Amiga plays the same skills. A club file with no binary in data/ gets none; the
   national files (80..85) always get theirs.
1c. the national files' sizes and national_teams.json's records into world_data.c (a block between
   "seal.py: the national files" markers, called first in the world's InstallBaked): the Amiga
   numbers the teams as SDL does and plays the world's nations (docs/WORLD_DATAGEN.md §15).
1b. data/ and teams/POOLPLYR.DAT: the foreign market's pool, from those files (world_import.py
   write_pool).
2. the fingerprint of teams/ competitions/ data/ (world_import.py world_fingerprint) into WORLD.ini and
   into the baked tables (src/world_data.c World_DataFingerprint), so a build and the package agree.
Then rebuild the game and run scripts/make_world_package.sh (which refreshes the server's team list).
A later world_import.py run writes teams/ from the Mod again: run nations.py, rebalance.py --apply and
this again after one.
"""
import argparse
import glob
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, 'scripts'))

import assign_team_uuids as au  # noqa: E402
import world_import as wi       # noqa: E402


def fresh_checksums(teams_dir, dry):
    """[team names] whose checksum was stale, now made again (files kept in their layout, indent 1)."""
    fixed = []
    for f in sorted(glob.glob(os.path.join(teams_dir, '*.json'))):
        d = json.load(open(f, encoding='utf-8'))
        if d.get('type') != 'team':
            continue
        here = []
        for t in d['teams']:
            want = au.checksum(t['players'])
            if 'uuid' in t and t.get('checksum') != want:
                t['checksum'] = want
                here.append(t['team']['team_name'])
        if here and not dry:
            open(f, 'w', encoding='utf-8', newline='\n').write(json.dumps(d, indent=1, ensure_ascii=False) + '\n')
        fixed += here
    return fixed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--world', default=os.path.join(REPO, 'bin', 'data_json', 'worlds', '2627'))
    ap.add_argument('--data-c', default=os.path.join(REPO, 'src', 'world_data.c'))
    ap.add_argument('--dry-run', action='store_true', help='say what would change, write nothing')
    a = ap.parse_args()
    wi.WORLD = os.path.abspath(a.world)
    data = os.path.join(wi.WORLD, 'data')
    fixed = fresh_checksums(os.path.join(wi.WORLD, 'teams'), a.dry_run)
    print('checksums: %d made again%s' % (len(fixed), (' (' + ', '.join(fixed[:12]) + ')') if fixed else ''))

    have = set(os.listdir(data))
    rewritten = same = skipped = 0
    for f in sorted(glob.glob(os.path.join(wi.WORLD, 'teams', '*.json'))):
        teams = json.load(open(f, encoding='utf-8'))
        name, packed = wi.team_file_binary(teams)
        name = name.upper()
        if name not in have and name not in NATIONAL_FILES:
            skipped += 1
            continue
        path = os.path.join(data, name)
        if os.path.exists(path) and open(path, 'rb').read() == packed:
            same += 1
            continue
        rewritten += 1
        if not a.dry_run:
            open(path, 'wb').write(packed)
    print('data/: %d Amiga team files written again, %d already the same, %d team files with none (kept so)'
          % (rewritten, same, skipped))

    if not a.dry_run:
        wi.write_pool()     # the foreign market's players, from those team files (GitHub issue 25)

    ini = os.path.join(wi.WORLD, 'WORLD.ini')
    text = open(ini, encoding='utf-8').read()
    old = re.search(r'^fingerprint *= *(\S+)', text, re.M).group(1)
    new = wi.world_fingerprint()
    if a.dry_run:
        print('fingerprint %s -> %s (dry run: nothing written)' % (old, new))
        return
    c = open(a.data_c, encoding='utf-8').read()
    if c.count('"%s"' % old) != 1:
        sys.exit('seal: %s does not hold the fingerprint %s once -- rerun world_import.py first'
                 % (os.path.relpath(a.data_c, REPO), old))
    c2 = national_block(c, wi.WORLD)
    if new != old:
        open(ini, 'w', newline='\n', encoding='utf-8').write(
            re.sub(r'^(fingerprint *= *)\S+', r'\g<1>' + new, text, flags=re.M))
        c2 = c2.replace('"%s"' % old, '"%s"' % new)
    if c2 != c:
        open(a.data_c, 'w', newline='\n', encoding='utf-8').write(c2)
    print('fingerprint %s -> %s (WORLD.ini, %s)' % (old, new, os.path.relpath(a.data_c, REPO)))


# The national files, 80..85 (step 1c): the Amiga reads no JSON, so the world's national teams reach it
# as binaries in data/ (written above) and as a block baked into world_data.c -- each file's size (its
# range of global team numbers grows to it, as fit_world_national_files does on SDL, so both targets
# number every team alike) and the national_teams.json records (_nationalTeams: jobs, a random
# national side), which SDL reads from the JSON itself.
NATIONAL_FILES = ['TEAM.%03d' % n for n in range(80, 86)]
NAT_BEGIN = '/* seal.py: the national files -- BEGIN (scripts/worldgen/seal.py writes this block) */'
NAT_END = '/* seal.py: the national files -- END */'


def national_block(c, world):
    sizes = {}
    for f in glob.glob(os.path.join(world, 'teams', '*.json')):
        d = json.load(open(f, encoding='utf-8'))
        if d.get('type') == 'team':
            name = wi.team_file_binary(d)[0].upper()
            if name in NATIONAL_FILES:
                sizes[int(name[-3:])] = len(d['teams'])
    rec_path = os.path.join(world, 'competitions', 'national_teams.json')
    if not os.path.exists(rec_path):
        rec_path = os.path.join(world, 'national_teams.json')
    if len(sizes) != 6 or not os.path.exists(rec_path):
        print('national files: %d of 6 and %s -- no block (the Amiga keeps 96/97\'s)'
              % (len(sizes), 'records' if os.path.exists(rec_path) else 'no records'))
        return c
    recs = json.load(open(rec_path, encoding='utf-8'))
    rows = recs['records']
    if len(rows) > 256:
        sys.exit('seal: %d national team records, NATIONAL_TEAMS_MAX is 256' % len(rows))
    m = re.search(r'^void (\w+)_InstallBaked\(void\)\n\{\n    switch \(World_Current\(\)\) \{\n#if SWOS_WORLDS\n'
                  r'    case (WORLD_\w+):\n', c, re.M)
    if not m:
        sys.exit('seal: no %s InstallBaked of the expected shape' % 'world_data.c')
    P = m.group(1)
    data = []
    for r in rows:
        sk = int(r['skill'])
        data += [int(r['country']), int(r['ordinal']), int(r['nationality']), int(r['team_file']),
                 sk >> 8, sk & 255]
    lines = []
    for i in range(0, len(data), 18):
        lines.append('    ' + ', '.join('%d' % b for b in data[i:i + 18]) + ',')
    block = '\n'.join([
        NAT_BEGIN,
        '/* The world\'s national files (80..85): their sizes and national_teams.json\'s %d records, each' % len(rows),
        ' * country, ordinal, nationality, home team file, skill (big-endian) -- _nationalTeams\' layout. */',
        '#if SWOS_WORLDS',
        'static const uint8_t k%sNationalSizes[6] = { %s };' % (P, ', '.join(str(sizes[n]) for n in range(80, 86))),
        '#ifndef SWOS_SDL_BUILD',
        'static const uint8_t k%sNationalTeams[%d * 6] = {' % (P, len(rows)),
        '\n'.join(lines),
        '};',
        '#endif',
        '',
        '/* Each national file\'s range of team numbers grown to it (never shrunk; SDL\'s',
        ' * fit_world_national_files then finds them so); the Amiga\'s records (SDL reads the JSON). */',
        'static void install_nationals(void)',
        '{',
        '    for (uint16_t c = 80; c <= 85; c++)',
        '        if (k%sNationalSizes[c - 80] > _teamsCountryNumbers[c + 1] - _teamsCountryNumbers[c])' % P,
        '            resize_country(c, k%sNationalSizes[c - 80]);' % P,
        '#ifndef SWOS_SDL_BUILD',
        '    {',
        '        extern uint8_t  _nationalTeams[];',
        '        extern uint16_t _numNationalTeams, _natTeamsRandLimit2, _nat_teams_rand_limit1;',
        '        memset(_nationalTeams, 0, NATIONAL_TEAMS_MAX * 6);',
        '        memcpy(_nationalTeams, k%sNationalTeams, sizeof k%sNationalTeams);' % (P, P),
        '        _numNationalTeams = %d;' % len(rows),
        '        _natTeamsRandLimit2 = %d;     /* (a random national team -- friendlies, tours -- from all) */' % len(rows),
        '        _nat_teams_rand_limit1 = %d;  /* (the first records a national job is drawn from) */'
        % int(recs.get('job_offer_pick', len(rows))),
        '    }',
        '#endif',
        '}',
        '#endif',
        NAT_END,
        ''])
    if NAT_BEGIN in c:
        i = c.index(NAT_BEGIN)
        j = c.index(NAT_END) + len(NAT_END) + 1
        c = c[:i] + block + c[j:]
    else:
        at = c.index(m.group(0))
        c = c[:at] + block + '\n' + c[at:]
    head = m.group(0)
    if head + '        install_nationals();\n' not in c:
        c = c.replace(head, head + '        install_nationals();\n', 1)
    for inc in ('#include "national_teams.h"\n', '#include <string.h>\n'):
        if inc not in c:
            c = c.replace('#include "game_types.h"\n', '#include "game_types.h"\n' + inc, 1)
    print('national files: %s teams, %d records baked (%s)'
          % ('/'.join(str(sizes[n]) for n in range(80, 86)), len(rows), P))
    return c


if __name__ == '__main__':
    main()
