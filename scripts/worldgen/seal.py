#!/usr/bin/env python3
"""worldgen/seal.py -- make a world's teams/ as they are now (after nations.py and rebalance.py) its
master data: what the package carries, the Amiga plays and the game checks (docs/WORLD_DATAGEN.md §17).

    python3 -I scripts/worldgen/seal.py [--world DIR] [--data-c FILE] [--dry-run]

0. each team's checksum (what the server's team list checks it by: assign_team_uuids.py checksum, its
   16's positions, skills and prices) made again where an edit after the import left it stale.
1. data/TEAM.nnn: each club team file's Amiga binary written again from its JSON (world_import.py's own
   writer), so the Amiga plays the same skills. A team file with no binary in data/ gets none (the
   national files: the Amiga keeps 96/97's 143 nations, docs/WORLD_DATAGEN.md §15).
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
        if name not in have:
            skipped += 1
            continue
        path = os.path.join(data, name)
        if open(path, 'rb').read() == packed:
            same += 1
            continue
        rewritten += 1
        if not a.dry_run:
            open(path, 'wb').write(packed)
    print('data/: %d Amiga team files written again, %d already the same, %d team files with none (kept so)'
          % (rewritten, same, skipped))

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
    if new != old:
        open(ini, 'w', newline='\n', encoding='utf-8').write(
            re.sub(r'^(fingerprint *= *)\S+', r'\g<1>' + new, text, flags=re.M))
        open(a.data_c, 'w', newline='\n', encoding='utf-8').write(c.replace('"%s"' % old, '"%s"' % new))
    print('fingerprint %s -> %s (WORLD.ini, %s)' % (old, new, os.path.relpath(a.data_c, REPO)))


if __name__ == '__main__':
    main()
