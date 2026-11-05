#!/usr/bin/env python3
"""make_classics_index.py -- each HISTORIC TOURNAMENTS group's index.json (bin/mods/classics).

The classics mod lists a group's competitions by name. Reading the name out of every .DIY means
reading every file, about 0.1-0.3 MB of JSON each, which on the Amiga takes seconds per list.
So each group folder carries an index.json written here, the competitions' files and names in
it; the mod reads that (src/mod_classics.c) and still scans folders that have none (a player's
own classic/ in the assets folder).

    scripts/make_classics_index.py [--check]     (--check: fail when an index is stale)
"""
import json, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'bin', 'mods', 'classics', 'classic')

def index_of(group_dir):
    rows = []
    for f in sorted(os.listdir(group_dir)):
        if not f.upper().endswith('.DIY'):
            continue
        with open(os.path.join(group_dir, f), encoding='utf-8') as fh:
            name = json.load(fh)['comp_scalars']['diy_preset_name_of_competition']
        rows.append({'file': f, 'name': name})
    return {'format': 'openswos-classics-index', 'version': 1, 'competitions': rows}

def main():
    check = '--check' in sys.argv[1:]
    stale = 0
    for g in sorted(os.listdir(ROOT)):
        d = os.path.join(ROOT, g)
        if not os.path.isdir(d):
            continue
        text = json.dumps(index_of(d), indent=1, ensure_ascii=False) + '\n'
        path = os.path.join(d, 'index.json')
        old = open(path, encoding='utf-8').read() if os.path.exists(path) else None
        if old == text:
            continue
        stale += 1
        if check:
            print(f'{path}: stale (run scripts/make_classics_index.py)')
        else:
            open(path, 'w', encoding='utf-8').write(text)
            print(f'{path}: written ({len(index_of(d)["competitions"])} competitions)')
    return 1 if check and stale else 0

if __name__ == '__main__':
    sys.exit(main())
