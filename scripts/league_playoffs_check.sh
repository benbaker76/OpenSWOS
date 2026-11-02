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
# Then the UEFA qualifying rounds (src/uefa_access.c) against the Mod's uefaq.c, and the league
# phase's draw (src/league_phase.c) against its rules.
# Then the cups' clubs from the second season (its 4.0.1 cf_build, CF_SEED 1 as released):
# random moves, last season's places and career clubs, both cups' lists compared.
# Then the Coppa Italia Serie D table against its cupd.py compile_table, and its later
# seasons' entry order (cd_rank, cd_season, cd_make) over random season ends.
# Then the parallel groups (src/league_groups.c): each league's table against the Mod's, and its
# season ends against its pg_season (4.0.5's two choices of group).
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
# The Mod's own C with its locals pattern-filled where a compiler can (the rules library's
# check.sh does the same): the Mod 4.0.5's per-play-off seed (gplay.c gp_season, rc19) reads club[i]
# of an entrant that failed its check and never set it -- a random address, a crash once in a few
# runs; pattern-filled it is negative, the library's "no club". Fixed in the Mod 4.0.6.
MC="$CC"
for c in gcc clang /usr/lib/llvm-14/bin/clang; do
    if echo 'int main(void){return 0;}' | $c -ftrivial-auto-var-init=pattern -x c -o /dev/null - 2>/dev/null; then
        MC="$c -O1 -w -ftrivial-auto-var-init=pattern"
        break
    fi
done
$MC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -c -o "$OUT/mod_gp.o" $T/mod_gp.c
$MC -DDPLAY_C="\"$MOD/tools/dplay.c\"" -c -o "$OUT/mod_dp.o" $T/mod_dp.c
$CC -c -o "$OUT/mod_main.o" $T/mod_main.c
$CC -o "$OUT/mod" "$OUT/mod_main.o" "$OUT/mod_dp.o" "$OUT/mod_gp.o"
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/ours" $T/ours.c src/swos_rules_dplay.c src/swos_rules_gplay.c src/swos_rules_post.c src/swos_rules_check.c
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

# Every pack's knockouts (not only Italy's): the bytes src/world_data.c bakes for its league against
# the Mod's own (mkseason's Pack.gplay: its compile_spec, a {"club": key} entrant resolved to its
# file and ordinal -- New Zealand's reserves, 4.0.5; the file our number for it, as the Mod's is its
# own), then its gp_season against ours over random tables of that league's shape (the Mod's bytes).
python3 -I - "$MOD" "$OUT" "$DATA" <<'PY'
import glob, json, os, re, sys
sys.path.insert(1, sys.argv[1] + '/tools')
import mkseason
wd = open('src/world_data.c', encoding='utf-8').read()
specs = {m.group(1): bytes(int(x, 0) for x in m.group(2).split(',') if x.strip())
         for m in re.finditer(r'static const uint8_t (kWorld\w+Spec)\[\d+\] = \{(.*?)\};', wd, re.S)}
baked = {int(m.group(1)): specs[m.group(2)] for m in re.finditer(r'^    \{ (\d+), \d+, \w+, \w+, [^}]*?(kWorld\w+Spec),', wd, re.M)}
sys.path.insert(0, 'scripts')
import world_import as w
ours_file = {c['code'].lower(): c['number'] for c in w.world_countries()}
ours_file['ita'] = w.italy()[0]['teams'][0]['team']['country_number']


def as_ours(spec, mod_file, our_file):
    """The Mod's spec with a club entrant's file (type 8: file, ordinal -- the rules library's
    team_file_high) as our number for the league's file, the Mod's own numbers being its own."""
    b, at = bytearray(spec), 0
    while b[at]:
        n, nr = b[at + 1], b[at + 2]
        if b[at] == 7:
            at += 6
            continue
        for i in range(n):
            e = at + 6 + 4 * i
            if b[e] == 8 and b[e + 2] == mod_file:
                b[e + 2] = our_file
        at += 6 + 4 * n
        for _ in range(nr):
            at += 6 + 2 * b[at]
    return bytes(b)


out = open(os.path.join(sys.argv[2], 'packs.txt'), 'w')
for f in sorted(glob.glob(os.path.join(sys.argv[3], 'packs', '*', 'pack.json'))):
    for c in json.load(open(f, encoding='utf-8'))['competitions']:
        if c.get('key') != 'league' or not c.get('ko_playoffs'):
            continue
        sizes = [d['teams'] for d in c.get('divisions') or []] or [len(c.get('clubs') or []) or 16]
        code = os.path.basename(os.path.dirname(f))
        built = mkseason.Pack(os.path.dirname(f)).gplay()
        if not built:
            sys.exit('FAIL: %s: the Mod builds no knockouts for it' % code)
        mod = bytes(built[2])
        if baked.get(ours_file.get(code)) != as_ours(mod, built[1], ours_file.get(code, 0)):
            sys.exit("FAIL: %s: world_data.c's spec (file %s) differs from the Mod's" % (code, ours_file.get(code)))
        open(os.path.join(sys.argv[2], code + '.spec'), 'wb').write(mod)
        out.write('%s %s\n' % (code, ','.join(map(str, sizes))))
PY
PN=$(( N / 10 ))
npacks=0; bad=""
while read -r code shape; do
    "$OUT/mod" "$OUT/$code.spec" "$PN" "$shape" ko > "$OUT/p_mod.txt"
    "$OUT/ours" "$OUT/$code.spec" "$PN" "$shape" ko > "$OUT/p_ours.txt" 2>/dev/null
    if cmp -s "$OUT/p_mod.txt" "$OUT/p_ours.txt"; then
        npacks=$((npacks + 1))
        echo $(( $(cat "$OUT/p_ours_ko.txt" 2>/dev/null || echo 0) + $(grep -o 'K[0-9]*:[0-9A-F,]*' "$OUT/p_ours.txt" | tr -cd , | wc -c) )) > "$OUT/p_ours_ko.txt"
    else
        bad="$bad $code"
        [ -n "${VERBOSE:-}" ] && { echo "  $code ($shape) differs:"; diff "$OUT/p_mod.txt" "$OUT/p_ours.txt" | head -"${VLINES:-3}"; }
    fi
done < "$OUT/packs.txt"
if [ -z "$bad" ]; then     # (Argentina's promedio too, its history kept season to season)
    echo "PASS: every pack's knockouts ($npacks leagues): compiled as the Mod compiles them, $PN seasons each (stage tables given)," \
         "every move, champion and stage title the Mod's; the career club's contest built as its ko_build, every club's play-off chosen as its gp_need" \
         "($(cat "$OUT"/p_ours_ko.txt 2>/dev/null || echo 0) round-1 ties simulated at the build)"
else
    echo "FAIL: the knockouts of$bad differ from the Mod's ($npacks leagues equal; VERBOSE=1 for the first seasons)"
    exit 1
fi

# The SEASON WINNERS page's titles (the Mod's fmtgame.win_lines, English) against ours
# (world_import.winners_lines), every pack's league: each division's titles and sources.
if python3 -I - "$MOD" > "$OUT/winners.txt" <<'PY'
import csv, glob, json, os, sys
sys.path.insert(0, 'scripts'); sys.path.insert(1, sys.argv[1] + '/tools')
import world_import as w
import complist, fmtgame
bad = n = titles = 0
for f in sorted(glob.glob(os.path.join(w.MOD, 'packs', '*', 'pack.json'))):
    d = os.path.dirname(f)
    pk = json.load(open(f, encoding='utf-8'))
    lg = next((c for c in pk['competitions'] if c['type'] == 'league'), None)
    if not lg:
        continue
    cp = os.path.join(d, 'clubs.csv')
    clubs = [dict(r, file=r.get('file') or 'league', division=r.get('division') or '0')
             for r in csv.DictReader(open(cp, encoding='utf-8'))] if os.path.exists(cp) else []
    nt = sum(1 for r in clubs if r['file'] == lg['clubs'] and r['division'] not in ('reserve', ''))
    ours = w.winners_lines(pk, lg, nt)
    m = complist.load_pack(d)
    fmtgame._WN_RAW.clear()
    fmtgame._raw_names(pk, m)
    comp = next(c for c in m['competitions'] if c['type'] == 'league')
    theirs, k = [], 0
    for lv in complist._league(comp, clubs)['levels']:
        for div in lv:
            theirs += [(k, t, s) for t, s in fmtgame.win_lines(comp, div, k, (m.get('id') or '').rsplit('-', 1)[-1]).get('en', [])]
            k += 1
    if any(c['type'] == 'state' for c in m['competitions']):
        theirs.append((0xFE, None, 'C'))
    n += 1
    titles += len(ours)
    if ours != theirs:
        bad += 1
        print('DIFF', os.path.basename(d), ours, theirs)
print('RESULT %d %d %d' % (n, bad, titles))
PY
then
    read -r _ wn wbad wtitles <<< "$(grep '^RESULT' "$OUT/winners.txt")"
fi
if [ "${wbad:-1}" = 0 ]; then
    echo "PASS: the SEASON WINNERS page, $wtitles titles of $wn leagues: each division's titles and sources the Mod's" \
         "(its last season's record checked with every gp_season above)"
else
    echo "FAIL: the SEASON WINNERS page's titles differ from the Mod's:"
    grep -v '^RESULT' "$OUT/winners.txt" | head -3 | cut -c1-300
    exit 1
fi

# The B-team rules (the Mod's sec_post): built cases, then random ones against the Mod's.
$MC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/mod_sec" $T/mod_sec.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/seconds" $T/seconds.c src/swos_rules_dplay.c src/swos_rules_gplay.c src/swos_rules_post.c src/swos_rules_check.c
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
$MC -DCF_SEED=1 -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/cups_mod" $T/cups_mod.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/cups_ours" $T/cups_ours.c src/swos_rules_cups.c src/swos_rules_check.c
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
$MC -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/cupd_mod" $T/cupd_mod.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/cupd_ours" $T/cupd_ours.c src/swos_rules_cupd.c src/swos_rules_check.c
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
picked = w.italy()[2]
ours = w.italian_cupd(picked, 20, None)
pk = os.path.join(w.MOD, 'packs', 'ita')
name_of = {r['key']: r['name'] for r in csv.DictReader(open(os.path.join(pk, 'clubs.csv'), encoding='utf-8'))}
ordinal = {e['team']['team_name']: o for o, (e, _) in enumerate(picked)}
def word(key):
    if not key or key not in name_of or name_of[key] not in ordinal:
        return None
    f, o = w.slot_team(20, ordinal[name_of[key]])       # (a second file's slot: its file, include/world.h)
    return f | o << 8
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
         "($(grep -c '^SKIP' "$OUT/split.txt") skipped)"
else
    echo "FAIL: the splits:"
    grep '^FAIL' "$OUT/split.txt" | head -5
    exit 1
fi

# The state championships (Brazil's estaduais): the season end's simulation (src/state_sim.c, the
# Mod's statesim.c) and the state pools (src/state_pool.c, the Mod's gplay.c ri_season, its 'state'
# pools) -- their tables as scripts/world_states.py compiles them, byte for byte the Mod's
# (statespec.sim_table, ripool.table_bytes); both C files over the same seasons (the pools' over
# the Mod's own rankings): every block, champion, queue, override and identity the Mod's.
# (the packs the Mod's own mkseason can load: not one whose league is over two team files --
# Italy's, v4, "files" "league2": its file holds 256; it has no states, splits or other
# confederation's cups)
mod_packs() {
    for d in "$PWD"/bin/data_json/worlds/2627/mod/packs/*/; do
        [ -f "$d/pack.json" ] && ! grep -q '"league2"' "$d/pack.json" && printf '%s,' "${d%/}"
    done
}
SPACKS=$(mod_packs)
SEASON_PACKS="${SPACKS%,}" python3 -I - "$MOD" "$OUT" "$PWD/bin/data_json/worlds/2627/mod/packs" <<'PY' > /dev/null
import json, os, sys
mod, out, packs = sys.argv[1:4]
sys.path.insert(0, os.path.abspath("scripts")); sys.path.insert(1, mod + "/tools")
import mkseason, ripool, statespec, world_states as ws
sim = statespec.sim_table()
pk = next((p for p in mkseason.packs() if p.world is not None and any(c["type"] == "state" for c in p.comps)), None)
if not sim or pk is None:
    sys.exit(0)
raw = json.load(open(os.path.join(packs, os.path.basename(pk.path.rstrip(os.sep)), "pack.json"), encoding="utf-8"))
states = [c for c in raw["competitions"] if c.get("type") == "state"]
mine = ws.sim_table(states, lambda k: pk.where[k][0] | pk.where[k][1] << 8,
                    ws.strengths(os.path.join(packs, os.path.basename(pk.path.rstrip(os.sep)))))
recs = dict(pk.team_files())
price = lambda r: sum(r[76 + j * 38 + 32] for j in range(16))
def price_of(k):
    f, o = pk.where[k]
    if k in pk.pool_clubs:
        return price(pk.pool_data[2 + 684 * pk.pool_clubs.index(k):][:684])
    return price(recs[f][2 + 684 * o:][:684])
base = next(b for f, b in pk.files.values() if f == pk.pool_file)
pools = ws.state_pools(states, lambda k: pk.where[k], pk.pool_file, base, pk.pool_clubs.index, price_of)
spares = [pk.where[k][1] for k in raw.get("state_spares") or []]
ri = ws.pool_table([(pk.world, pk.pool_file, pools, base, spares, pk._nrec(pk.pool_file), len(pk.pool_clubs))])
if mine != sim or ri != ripool.table_bytes([pk.off_pool()]):
    sys.exit(0)
open(os.path.join(out, "ss_tab.bin"), "wb").write(sim)
open(os.path.join(out, "ri_tab.bin"), "wb").write(ri)
PY
if [ ! -s "$OUT/ss_tab.bin" ] || [ ! -s "$OUT/ri_tab.bin" ]; then
    echo "FAIL: the state championships: scripts/world_states.py's tables differ from the Mod's (statespec / ripool)"
    exit 1
fi
SN=$((N / 10))
$CC -w -DMOD -o "$OUT/ss_mod" $T/states_drv.c "$MOD/tools/statesim.c"
$CC -w -Iinclude -include include/swos_platform.h -DSWOS_WORLDS=1 -o "$OUT/ss_ours" $T/states_drv.c
"$OUT/ss_mod" "$OUT/ss_tab.bin" "$SN" > "$OUT/ss_mod.txt"
"$OUT/ss_ours" "$OUT/ss_tab.bin" "$SN" > "$OUT/ss_ours.txt"
python3 -I - "$OUT" <<'PY'
import re, sys
out = sys.argv[1]
with open(out + "/sr.txt", "w") as o:
    for line in open(out + "/ss_mod.txt"):
        words = []
        for blk in re.findall(r"\[([0-9a-f ]+)\]", line.split(" champ")[0]):
            w = blk.split()
            words += [w[0], "%x" % (len(w) - 1)] + w[1:]
        o.write(" ".join(words) + "\n")
PY
$CC -m32 -w -fno-builtin -DMOD -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/ri_mod" $T/pools_drv.c
$CC -w -Iinclude -include include/swos_platform.h -DSWOS_WORLDS=1 -o "$OUT/ri_ours" $T/pools_drv.c
"$OUT/ri_mod" "$OUT/ri_tab.bin" "$OUT/sr.txt" > "$OUT/ri_mod.txt"
"$OUT/ri_ours" "$OUT/ri_tab.bin" "$OUT/sr.txt" > "$OUT/ri_ours.txt"
if cmp -s "$OUT/ss_mod.txt" "$OUT/ss_ours.txt" && cmp -s "$OUT/ri_mod.txt" "$OUT/ri_ours.txt"; then
    echo "PASS: the state championships, $SN seasons of $(head -c 1 "$OUT/ss_tab.bin" | od -An -tu1 | tr -d ' ') states:" \
         "every ranking, champion and pool swap the Mod's; their tables compiled as the Mod's"
else
    echo "FAIL: the state championships, the first season that differs (Mod, then ours):"
    cmp -s "$OUT/ss_mod.txt" "$OUT/ss_ours.txt" || diff "$OUT/ss_mod.txt" "$OUT/ss_ours.txt" | head -4 | cut -c1-300
    cmp -s "$OUT/ri_mod.txt" "$OUT/ri_ours.txt" || diff "$OUT/ri_mod.txt" "$OUT/ri_ours.txt" | head -4 | cut -c1-300
    exit 1
fi

# The UEFA qualifying rounds (src/uefa_access.c, the Mod's uefaq.c): random seasons -- the
# associations, their clubs, holders, a career club, league cup winners, the packs' own European
# play-offs -- the three league phases, the bracket, the career club's ties, the coefficient
# points, against the Mod's own C.
python3 -I - "$OUT/uefa_specs.h" <<'PY'
import glob, json, os, sys
sys.path.insert(0, "scripts")
import world_import as w
specs = []
for f in sorted(glob.glob(os.path.join(w.MOD, "packs", "*", "pack.json"))):
    lg = next((c for c in json.load(open(f, encoding="utf-8"))["competitions"] if c["key"] == "league"), None)
    kos = (lg or {}).get("ko_playoffs") or []
    if any(p.get("kind") == "europe" for p in kos):
        specs.append(w.compile_spec(kos, len(lg.get("divisions") or [0])))
with open(sys.argv[1], "w") as o:
    for i, b in enumerate(specs):
        o.write("static const unsigned char SPEC%d[] = { %s };\n" % (i, ", ".join(map(str, b))))
    o.write("static const unsigned char *const SPECS[] = { %s };\n" % ", ".join("SPEC%d" % i for i in range(len(specs))))
    o.write("#define NSPECS %d\n" % len(specs))
PY
UN=$((N / 10))
$CC -no-pie -DUQ_RO= -DUEFAQ_C="\"$MOD/tools/uefaq.c\"" -I"$OUT" -o "$OUT/uefa_mod" $T/uefa_mod.c
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -I"$OUT" -o "$OUT/uefa_ours" $T/uefa_ours.c
"$OUT/uefa_mod" "$UN" > "$OUT/uefa_mod.txt"
"$OUT/uefa_ours" "$UN" > "$OUT/uefa_ours.txt"
if cmp -s "$OUT/uefa_mod.txt" "$OUT/uefa_ours.txt"; then
    echo "PASS: the UEFA qualifying rounds, $UN seasons ($(grep -c '|tie' "$OUT/uefa_ours.txt") with the career" \
         "club's ties), every list, bracket and point the Mod's"
else
    echo "FAIL: the UEFA qualifying rounds, the first season that differs (Mod, then ours):"
    diff "$OUT/uefa_mod.txt" "$OUT/uefa_ours.txt" | head -4 | cut -c1-300
    exit 1
fi

# The league phase's draw (src/league_phase.c, the Mod's lp36 model): random fields, the pots as
# the Mod makes them, every draw's rules (lp36.check / check6).
$CC -Iinclude -include include/swos_platform.h -DSWOS_SDL_BUILD -o "$OUT/lp_check" $T/lp_check.c
"$OUT/lp_check" "$((N / 10))"

# The other confederations' cups (src/world_conti.c, the Mod's conti.c as it is): the world's
# cups packed by the Mod's own conti.load over the world's data, both C files over random
# seasons of a synthetic world -- every round's entrants and draw, every winner the Mod's.
PACKS="$PWD/bin/data_json/worlds/2627/mod/packs"
SPACKS=$(mod_packs)
SEASON_PACKS="${SPACKS%,}" \
    python3 -I - "$MOD" "$OUT" "$PWD/bin/data_json/worlds/2627/mod/conti" <<'PY' > /dev/null
import sys
mod, out, data = sys.argv[1:4]
sys.path.insert(0, mod + "/tools")
import conti
conti.DATA = data
spec, assocs, cups = conti.load(conti.CONFS)
open(out + "/conti.spec", "wb").write(spec)
sys.path.insert(0, "scripts")
import world_conti                      # (ours: the same bytes, with and without the team files)
files = {k: 100 + i for i, k in enumerate(assocs)}
if world_conti.load(world_conti.CONFS) != (spec, assocs, cups) or \
        world_conti.load(world_conti.CONFS, files=files) != conti.load(conti.CONFS, files=files):
    open(out + "/conti.spec", "wb").write(b"")
PY
if [ ! -s "$OUT/conti.spec" ]; then
    echo "FAIL: the other confederations' cups: scripts/world_conti.py's spec differs from the Mod's conti.load"
    exit 1
fi
$CC -m32 -w -fno-builtin -DCONTI_C="\"$MOD/tools/conti.c\"" -o "$OUT/conti_mod" $T/conti_mod.c
$CC -w -fno-builtin -Iinclude -include include/swos_platform.h -o "$OUT/conti_ours" $T/conti_ours.c
CN=$((N / 400))
"$OUT/conti_mod" "$OUT/conti.spec" "$CN" > "$OUT/conti_mod.txt"
"$OUT/conti_ours" "$OUT/conti.spec" "$CN" > "$OUT/conti_ours.txt"
if cmp -s "$OUT/conti_mod.txt" "$OUT/conti_ours.txt"; then
    echo "PASS: the other confederations' cups, $CN seasons of $(head -c 4 "$OUT/conti.spec" | od -An -tu1 | awk '{print $1}') cups," \
         "every round's draw and every winner the Mod's; their spec packed as the Mod packs it"
else
    echo "FAIL: the other confederations' cups, the first season that differs (Mod, then ours):"
    diff "$OUT/conti_mod.txt" "$OUT/conti_ours.txt" | head -4 | cut -c1-300
    exit 1
fi

# The parallel groups (src/league_groups.c) against the Mod's gplay.c pg_season (4.0.5: two choices
# of group per level, a full group making room): each league with groups -- first its tables as
# world_data.c bakes them against the Mod's Pack.parallel (levels, sizes, every club's division and
# area bytes), then random season ends of both C over the Mod's own table (groups_cases.h: the
# clubs coming back from below on odd seasons, every fourth season all of one area).
python3 -I - "$MOD" "$OUT" "$DATA" <<'PY'
import json, os, re, sys
mod, out, data = sys.argv[1], sys.argv[2], sys.argv[3]
sys.path.insert(0, mod + '/tools')
import mkseason, pgroups
NAMES = {'ita': 'italy', 'arg': 'argentina', 'aus': 'australia', 'slv': 'elsalvador'}
wd = open('src/world_data.c', encoding='utf-8').read()
bad = 0
codes = []
for code, name in NAMES.items():
    pk = mkseason.Pack(os.path.join(data, 'packs', code))
    world, levels, clubs = pk.parallel()
    open(os.path.join(out, code + '.pg'), 'wb').write(pgroups.table_bytes([(world, levels, clubs)]))
    divs = pk.divisions()
    open(os.path.join(out, code + '.pr'), 'w').write(' '.join(str(d.get('promoted', 0)) for d in divs) + '\n'
                                                     + ' '.join(str(d.get('relegated', 0)) for d in divs) + '\n')
    # ours, as world_data.c bakes it: the levels, then every club (ordinal, base division, areas)
    lv = re.search(r'kWorld2627_%sLevels\[\] = \{\n(.*?)\n\};' % name, wd, re.S).group(1)
    ours_lv = [(int(a), [int(x) for x in s.split(',')]) for a, _, s in re.findall(r'\{ (\d+), (\d+), \{ ([\d, ]+) \}', lv)]
    cl = re.search(r'kWorld2627_%sClubs\[\] = \{\n(.*?)\n\};' % name, wd, re.S).group(1)
    ours = [(int(o), int(b), [255 if x.strip() == 'LEAGUE_GROUPS_NO_AREA' else int(x) for x in a.split(',')])
            for o, b, a in re.findall(r'\{ (\d+), (\d+), \{ ([^}]*) \} \}', cl)]
    npar = sum(1 for _, s in levels if len(s) > 1)
    gmin = min(g for g, _, _ in clubs)
    theirs = sorted((g - gmin, b, (list(a) if isinstance(a, (list, tuple)) else [a] * npar)) for g, b, a in clubs)
    ours = sorted((o, b, a[:npar]) for o, b, a in ours)
    if [(f, list(s)) for f, s in levels] != ours_lv:
        print('FAIL: %s levels: the Mod %s, ours %s' % (code, levels, ours_lv)); bad += 1
    if theirs != ours:
        diff = [(t, o) for t, o in zip(theirs, ours) if t != o][:3]
        print('FAIL: %s clubs (%d / %d): first differences (the Mod, ours) %s' % (code, len(theirs), len(ours), diff)); bad += 1
    codes.append(code)
open(os.path.join(out, 'groups.txt'), 'w').write('\n'.join(codes) + '\n')
sys.exit(1 if bad else 0)
PY
$MC -I$T -DGPLAY_C="\"$MOD/tools/gplay.c\"" -o "$OUT/groups_mod" $T/groups_mod.c
$CC -Iinclude -I$T -include include/swos_platform.h -o "$OUT/groups_ours" $T/groups_ours.c src/league_groups.c src/swos_rules_groups.c
GN=$((N / 10)); bad=""
while read -r code; do
    "$OUT/groups_mod" "$OUT/$code.pg" "$OUT/$code.pr" "$GN" > "$OUT/g_mod.txt"
    "$OUT/groups_ours" "$OUT/$code.pg" "$OUT/$code.pr" "$GN" > "$OUT/g_ours.txt"
    cmp -s "$OUT/g_mod.txt" "$OUT/g_ours.txt" || bad="$bad $code"
done < "$OUT/groups.txt"
if [ -z "$bad" ]; then
    echo "PASS: the parallel groups ($(tr '\n' ' ' < "$OUT/groups.txt")), their tables the Mod's and $GN season ends each:" \
         "every club placed as the Mod's pg_season places it"
else
    echo "FAIL: the parallel groups of$bad differ from the Mod's pg_season"
    exit 1
fi
