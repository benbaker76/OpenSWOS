#!/usr/bin/env bash
#
# league_playoffs_check.sh -- does our port of the Mod's season-end play-offs decide as the
# Mod's own code does? (src/league_playoffs.c; docs/WORLD_2026_27.md, phase 4)
#
#   scripts/league_playoffs_check.sh [SEASONS]      # default 20000
#
# Builds, natively, the Mod's tools/dplay.c and tools/gplay.c as they are (MOD=<its checkout>,
# default D:\Projects\GitHub\swos-9697-mod) and our port, gives both the same random final
# tables for Italy 2026-27's 14 divisions, season after season, and compares every move.
# The Mod's side walks the pack's whole knockout spec (its compile_spec, every kind); ours
# the part world_import.py compiles (promotion, play-out) -- the rest must not change them.
# Then the B-team rules (its sec_post): built cases, and random ones against the Mod's
# (Italy's league, and each other country with B teams).
# Then the mid-season splits' tables, fixture stages too (scripts/world_split.py), against the
# Mod's split.entry.
# Then the cups' clubs from the second season (its 4.0.1 cf_build, CF_SEED 1 as released):
# random moves, last season's places and career clubs, both cups' lists compared.
# Then the Coppa Italia Serie D table against its cupd.py compile_table, and its later
# seasons' entry order (cd_rank, cd_season, cd_make) over random season ends.
# Needs the world's data (bin/data_json/worlds/2627/mod/, not in git): skipped without it.
set -euo pipefail
cd "$(dirname "$0")/.."
N=${1:-20000}
MOD=${MOD:-/mnt/d/Projects/GitHub/swos-9697-mod}
DATA=bin/data_json/worlds/2627/mod
T=tests/league_playoffs
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
if [ ! -d "$DATA" ] || [ ! -f "$MOD/tools/gplay.c" ]; then
    echo "SKIP: needs $DATA and the Mod's tools ($MOD)"; exit 0
fi
python3 -I - "$MOD" "$OUT" <<'PY'
import importlib.util, sys
mod, out = sys.argv[1], sys.argv[2]
def load(name, path):
    s = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(s); s.loader.exec_module(m); return m
sys.path.insert(0, mod + "/tools")
g = load("gplay", mod + "/tools/gplay.py")
w = load("world_import", "scripts/world_import.py")
lg = w.mod_league()
nd = len(lg["divisions"])
open(out + "/mod.spec", "wb").write(bytes(g.compile_spec(lg["ko_playoffs"], nd)))
open(out + "/ours.spec", "wb").write(w.italian_playoffs(20, None)["spec"])
PY
CC="gcc -O1 -w"
$CC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -c -o "$OUT/mod_gp.o" $T/mod_gp.c
$CC -DDPLAY_C="\"$MOD/tools/dplay.c\"" -c -o "$OUT/mod_dp.o" $T/mod_dp.c
$CC -c -o "$OUT/mod_main.o" $T/mod_main.c
$CC -o "$OUT/mod" "$OUT/mod_main.o" "$OUT/mod_dp.o" "$OUT/mod_gp.o"
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/ours" $T/ours.c
"$OUT/mod" "$OUT/mod.spec" "$N" > "$OUT/mod.txt"
"$OUT/ours" "$OUT/ours.spec" "$N" > "$OUT/ours.txt" 2>/dev/null   # (its [playoffs] log)
if cmp -s "$OUT/mod.txt" "$OUT/ours.txt"; then
    moves=$(awk '{ m += NF - 1 } END { print m }' "$OUT/ours.txt")
    echo "PASS: $N seasons, $moves moves, every one the Mod's"
else
    echo "FAIL: the first season that differs (Mod, then ours):"
    diff "$OUT/mod.txt" "$OUT/ours.txt" | head -4
    exit 1
fi

# Every pack's knockouts (not only Italy's): our compile_spec's bytes against the Mod's, then
# its gp_season against ours over random tables of that league's shape.
python3 -I - "$MOD" "$OUT" <<'PY'
import glob, json, os, sys
sys.path.insert(0, 'scripts'); sys.path.insert(1, sys.argv[1] + '/tools')
import world_import as w
import gplay
out = open(os.path.join(sys.argv[2], 'packs.txt'), 'w')
for f in sorted(glob.glob(os.path.join(w.MOD, 'packs', '*', 'pack.json'))):
    for c in json.load(open(f, encoding='utf-8'))['competitions']:
        if c.get('key') != 'league' or not c.get('ko_playoffs'):
            continue
        nd = len(c.get('divisions') or [1])
        sizes = [d['teams'] for d in c.get('divisions') or []] or [len(c.get('clubs') or []) or 16]
        code = os.path.basename(os.path.dirname(f))
        mod = bytes(gplay.compile_spec(c['ko_playoffs'], nd, c.get('names')))
        if w.compile_spec(c['ko_playoffs'], nd) != mod:
            sys.exit('FAIL: %s: compile_spec differs from the Mod\'s' % code)
        open(os.path.join(sys.argv[2], code + '.spec'), 'wb').write(mod)
        out.write('%s %s\n' % (code, ','.join(map(str, sizes))))
PY
PN=$(( N / 10 ))
npacks=0; bad=""
while read -r code shape; do
    "$OUT/mod" "$OUT/$code.spec" "$PN" "$shape" > "$OUT/p_mod.txt"
    "$OUT/ours" "$OUT/$code.spec" "$PN" "$shape" > "$OUT/p_ours.txt" 2>/dev/null
    if cmp -s "$OUT/p_mod.txt" "$OUT/p_ours.txt"; then
        npacks=$((npacks + 1))
    else
        bad="$bad $code"
        [ -n "${VERBOSE:-}" ] && { echo "  $code ($shape) differs:"; diff "$OUT/p_mod.txt" "$OUT/p_ours.txt" | head -3; }
    fi
done < "$OUT/packs.txt"
KNOWN=" arg"     # (Argentina: relegation by average, promedio -- not ported yet)
if [ "$bad" = "$KNOWN" ] || [ -z "$bad" ]; then
    echo "PASS: every pack's knockouts ($npacks leagues): compiled as the Mod compiles them, $PN seasons each," \
         "every move the Mod's${bad:+ (known, not ported yet:$bad)}"
else
    echo "FAIL: the knockouts of$bad differ from the Mod's ($npacks leagues equal; VERBOSE=1 for the first seasons)"
    exit 1
fi

# The B-team rules (the Mod's sec_post): built cases, then random ones against the Mod's.
$CC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/mod_sec" $T/mod_sec.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/seconds" $T/seconds.c
"$OUT/seconds" 2>/dev/null
"$OUT/mod_sec" "$N" > "$OUT/mod_sec.txt"
"$OUT/seconds" random "$N" > "$OUT/sec.txt" 2> "$OUT/sec.log"
if cmp -s "$OUT/mod_sec.txt" "$OUT/sec.txt"; then
    echo "PASS: B teams, $N cases in each of Italy, Spain, Germany, the Netherlands, Portugal" \
         "($(grep -o '^[0-9]* of' "$OUT/sec.log" | cut -d' ' -f1) changed by the rules), every move the Mod's"
else
    echo "FAIL: B teams, the first case that differs (Mod, then ours):"
    diff "$OUT/mod_sec.txt" "$OUT/sec.txt" | head -4
    exit 1
fi

# The cups' clubs from the second season (the Mod's cf_build; src/world_cups.c build).
$CC -DCF_SEED=1 -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/cups_mod" $T/cups_mod.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/cups_ours" $T/cups_ours.c
"$OUT/cups_mod" "$N" > "$OUT/cups_mod.txt"
"$OUT/cups_ours" "$N" > "$OUT/cups_ours.txt" 2>/dev/null
if cmp -s "$OUT/cups_mod.txt" "$OUT/cups_ours.txt"; then
    echo "PASS: cups, $N seasons, both cups' clubs the Mod's"
else
    echo "FAIL: cups, the first season that differs (Mod, then ours):"
    diff "$OUT/cups_mod.txt" "$OUT/cups_ours.txt" | head -4
    exit 1
fi

# The Coppa Italia Serie D's later seasons (the Mod's cd_rank, cd_season, cd_make; ours).
$CC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/cupd_mod" $T/cupd_mod.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/cupd_ours" $T/cupd_ours.c
"$OUT/cupd_mod" "$N" > "$OUT/cupd_mod.txt"
"$OUT/cupd_ours" "$N" > "$OUT/cupd_ours.txt" 2>/dev/null
if cmp -s "$OUT/cupd_mod.txt" "$OUT/cupd_ours.txt"; then
    echo "PASS: the Coppa Italia Serie D, $N later seasons, every first two rounds the Mod's"
else
    echo "FAIL: the Coppa Italia Serie D, the first season that differs (Mod, then ours):"
    diff "$OUT/cupd_mod.txt" "$OUT/cupd_ours.txt" | head -4 | cut -c1-200
    exit 1
fi

# The Coppa Italia Serie D's table (world_import.py italian_cupd) against the Mod's cupd.py
# compile_table on the same cupd.json and club mapping.
python3 -I - "$MOD" <<'PY'
import csv, json, os, struct, sys
sys.path.insert(0, 'scripts'); sys.path.insert(1, sys.argv[1] + '/tools')
import world_import as w
import cupd
picked = w.italy()[1]
ours = w.italian_cupd(picked, 20, None)
pk = os.path.join(w.MOD, 'packs', 'ita')
name_of = {r['key']: r['name'] for r in csv.DictReader(open(os.path.join(pk, 'clubs.csv'), encoding='utf-8'))}
ordinal = {e['team']['team_name']: o for o, (e, _) in enumerate(picked)}
def word(key):
    if not key or key not in name_of or name_of[key] not in ordinal:
        return None
    return 20 | ordinal[name_of[key]] << 8
tab, nr, nt = cupd.compile_table(json.load(open(os.path.join(pk, 'cupd.json'), encoding='utf-8')), word)
mine = b''.join(bytes(r[:6]) + struct.pack('<H', r[6]) for r in ours['rounds'])
mine += b''.join(struct.pack('<HHBB', a, b, win, 0) for a, b, win in ours['ties'])
if mine != tab:
    sys.exit('FAIL: the Coppa Italia Serie D table differs from the Mod\'s compile_table')
print('PASS: the Coppa Italia Serie D table, %d rounds and %d ties, byte for byte the Mod\'s' % (nr, nt))
PY

# The mid-season splits (scripts/world_split.py): every split league's table, its spec as the Mod's
# mkseason.splits makes it from the same pack and its bytes as its split.entry makes them; the odd
# pair tables as its oddtables'.
if python3 -I $T/split_check.py "$MOD/tools" bin/data_json/worlds/2627/mod/packs > "$OUT/split.txt"; then
    echo "PASS: the splits, $(grep -c '^PASS' "$OUT/split.txt") divisions' tables byte for byte the Mod's" \
         "($(grep -c '^SKIP' "$OUT/split.txt") of more than 20 clubs: Argentina, the USA)"
else
    echo "FAIL: the splits:"
    grep '^FAIL' "$OUT/split.txt" | head -5
    exit 1
fi
