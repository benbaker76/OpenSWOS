#!/usr/bin/env bash
# make_world_package.sh [TAG] -- dist/OpenSWOS-World-<TAG>.zip: a world's data, for OpenSWOS
# Setup to import (docs/WORLD_2026_27.md, phase 3): 2627 (World 2026-27, the default) or 8485
# (Europe 1984-85, DaniSwos's database: §6g).
#
# World 2026-27's squads are their authors' (the SWOS 96/97 Mod's data), so OpenSWOS does not
# ship them: this package is hosted apart, and the player's OpenSWOS Setup installs it like
# the original game's data (its "World 2026-27" source: tools/swosdata SdSources_OpenWorld,
# step_world). It is scripts/world_import.py's output, unchanged:
#
#   OpenSWOS-World-2627/
#     mod.json         the world as a mod (docs/MODS.md §17): OpenSWOS Setup installs it into the
#                      player's mods/world<tag>/ (an older setup ignores this file)
#     WORLD.ini        tag, name, the fingerprint of the files below
#     teams/           the PC game's team files (JSON)
#     competitions/    leagues.json
#     data/            the Amiga's team files (TEAM.nnn)
#
# The fingerprint is baked into the game too (src/world_data.c World_DataFingerprint): a
# package belongs with the builds made from the same import run. Make a new one, and upload
# it with its .txt (scripts/upload_dlc.sh), whenever world_import.py's output changes (the fingerprint in WORLD.ini says so). The
# matchmaking server's team list is refreshed here too (assign_team_uuids.py --worlds).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TAG="${1:-2627}"
SRC="$ROOT/bin/data_json/worlds/$TAG"
OUT="$ROOT/dist/OpenSWOS-World-$TAG.zip"

for f in WORLD.ini competitions/leagues.json; do
    [ -f "$SRC/$f" ] || { echo "make_world_package: no $SRC/$f -- run scripts/world_import.py first" >&2; exit 1; }
done
FP=$(sed -n 's/^fingerprint *= *//p' "$SRC/WORLD.ini")
NAME=$(sed -n 's/^name *= *//p' "$SRC/WORLD.ini")
# (each world's baked tables: World 2026-27's src/world_data.c, another's src/world_data_<tag>.c)
DATA_C="$ROOT/src/world_data.c"
[ "$TAG" = 2627 ] || DATA_C="$ROOT/src/world_data_$TAG.c"
grep -q "\"$FP\"" "$DATA_C" ||
    { echo "make_world_package: WORLD.ini's data ($FP) is not ${DATA_C#$ROOT/}'s -- rerun world_import.py" >&2; exit 1; }

mkdir -p "$ROOT/dist"
python3 -I - "$SRC" "$OUT.tmp" "OpenSWOS-World-$TAG" "$TAG" "$FP" "$NAME" <<'PY'
import json, os, sys, zipfile
src, out, top, tag, fp, name = sys.argv[1:]
# The mod's uuid is the world's for ever (a DLC's own: docs/MODS.md §14); the version, its data.
UUIDS = {"2627": "15ea9117-6aa0-4968-a293-6cf33092ff33",
         "8485": "48496e1d-27d7-49d8-acff-c428814a1bc6"}
AUTHORS = {"8485": "DaniSwos (the 1984-85 database) and the SWOS 96/97 Mod (Davide Lorigliola and its contributors)"}
if tag not in UUIDS:
    sys.exit("make_world_package: world %s has no mod uuid yet -- give it one (uuid4) in UUIDS" % tag)
name = name or ("World %s-%s" % ("20" + tag[:2], tag[2:]) if len(tag) == 4 else "World " + tag)
manifest = {
    "uuid": UUIDS[tag], "id": "world" + tag, "name": name, "version": fp,
    "author": AUTHORS.get(tag, "the SWOS 96/97 Mod (Davide Lorigliola and its contributors)"),
    "about": "The football world of %s: its countries, leagues, cups and squads." % name.replace("World ", ""),
    "targets": ["sdl", "amiga"], "changes": "data", "default": False,
    # The world's switches (docs/WORLD_2026_27.md, the Mod 4.1's features): each the Mod's own
    # default; read by the game through ModApi_SettingInt("world<tag>", key).
    "settings": [
        # -- career (the CAREER agent's block: what a NEW career starts with; each is also a
        #    switch of its own in the career, saved with it) --
        {"key": "uefa_prizes", "label": "UEFA prize money (new careers)",
         "choices": ["on", "off"], "default": "on"},
        {"key": "financial_fair_play", "label": "Financial fair play (new careers)",
         "choices": ["on", "off"], "default": "on"},
        {"key": "euro", "label": "Money in euros",     # (the pound sign shows the euro)
         "choices": ["on", "off"], "default": "on"},
        {"key": "cpu_replacements", "label": "CPU replacements (new careers)",
         "choices": ["invented", "from_squad"], "default": "invented"},
    ],
}
if tag != "2627":       # (World 2026-27's career switches: a classic world has none of them)
    manifest["settings"] = []
# The Mod 4.1's match and bench rules (include/world_rules.h), each ON as the Mod plays it: its
# settings, at the END of the list (the career's rules come before them).
MATCH_SETTINGS = [
    ("stoppage_time", "Stoppage time"),             # (online: the host's)
    ("cpu_red_cards", "CPU red cards"),
    ("cpu_injuries", "CPU injuries"),
    ("goalkeeper_injuries", "Goalkeeper injuries"),
    ("bench_info", "Bench info strip"),
    ("opponent_squad", "Opponent's squad"),         # (on the bench: UP on the coach; offline)
]
manifest.setdefault("settings", []).extend(
    {"key": k, "label": label, "choices": ["on", "off"], "default": "on"} for k, label in MATCH_SETTINGS)
n = 0
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    z.writestr(top + "/mod.json", json.dumps(manifest, indent=1) + "\n")
    z.write(os.path.join(src, "WORLD.ini"), top + "/WORLD.ini")
    for sub in ("teams", "competitions", "data"):
        for f in sorted(os.listdir(os.path.join(src, sub))):
            path = os.path.join(src, sub, f)
            if os.path.isfile(path):
                z.write(path, "%s/%s/%s" % (top, sub, f))
                n += 1
print("%d files" % n)
PY
mv "$OUT.tmp" "$OUT"
echo "$OUT: $(du -h "$OUT" | cut -f1), data $FP"

# Its credits, hosted beside it (OpenSWOS Setup's "credits" link, kWorldCredits): the data's
# sources, the Mod release it was converted from (world_from_mod.py's SOURCE.txt, or
# MOD_RELEASE=x.y.z) and the fingerprint -- written with the zip, so the two always agree.
REL=${MOD_RELEASE:-$(sed -n 's/.*installed SWOS 96\/97 Mod \([0-9.]*\).*/\1/p' "$SRC/mod/SOURCE.txt" 2>/dev/null | head -1)}
[ -n "$REL" ] || { echo "make_world_package: which Mod release is this? (MOD_RELEASE=x.y.z)" >&2; exit 1; }
NCOUNTRY=$(ls "$SRC/teams"/*.json | grep -vc NATIONALITIES || true)
if [ "$TAG" = 8485 ]; then
cat > "${OUT%.zip}.txt" <<EOF
OpenSWOS-World-$TAG.zip -- $NAME for OpenSWOS
==========================================================

What it is: the classic career world Europe 1984-85 of the SWOS 96/97 Mod (github.com/aminta/swos-9697-mod),
release $REL, converted for OpenSWOS (github.com/benbaker76/OpenSWOS) by its scripts/world_from_mod.py
--world 8485 and scripts/world_import.py --tag 8485: 32 countries' clubs of the 1984-85 season, their leagues
and national cups, the three European cups as they were, the era's national teams. OpenSWOS Setup downloads
and installs it as a mod; the game checks its fingerprint (WORLD.ini) against the one baked into the build.
Contents: $NCOUNTRY team files (JSON, and the Amiga's TEAM.nnn), leagues.json, WORLD.ini, mod.json.
Fingerprint: $FP.

Data sources and credits
- DaniSwos (CM01/02 Retro Update Team): the 1984-85 database -- the squads, players and national teams.
  Used with his permission.
- The SWOS 96/97 Mod's packs (packs/europe-1984-85): club order from the 1983-84 final tables, European
  entrants 1984-85, coaches and national cups from en/it/es/nl/pt/fr.wikipedia.org and Wikidata.
- The original SWOS 96/97 game data is not included.

Europe 1984-85 by DaniSwos and the SWOS 96/97 Mod (Davide Lorigliola and its contributors); conversion by
Ben Baker (OpenSWOS).
EOF
else
cat > "${OUT%.zip}.txt" <<EOF
OpenSWOS-World-$TAG.zip -- the World $(sed -n 's/^name *= *World *//p' "$SRC/WORLD.ini") for OpenSWOS
==========================================================

What it is: the World 2026-27 of the SWOS 96/97 Mod (github.com/aminta/swos-9697-mod), release $REL,
converted for OpenSWOS (github.com/benbaker76/OpenSWOS) by its scripts/world_from_mod.py and
scripts/world_import.py. OpenSWOS Setup downloads and installs it as a mod; the game checks its
fingerprint (WORLD.ini) against the one baked into the build.
Contents: $NCOUNTRY team files (JSON, and the Amiga's TEAM.nnn), leagues.json, WORLD.ini, mod.json.
Fingerprint: $FP.

Data sources and credits
- Transfermarkt (transfermarkt.com): squads, market values (turned into SWOS prices), coaches, league
  members 2026-27.
- EA SPORTS FC ratings feed: player attributes (skill profiles).
- Wikipedia contributors (CC BY-SA 4.0): club kits; Wikidata (CC0): club <-> article links.
- Derived data only: no market values or ratings are included as such. The original SWOS 96/97 game data
  is not included.

World 2026-27 by Davide Lorigliola and the SWOS 96/97 Mod's contributors; conversion by Ben Baker (OpenSWOS).
EOF
fi
echo "${OUT%.zip}.txt: its credits (Mod $REL)"

# The matchmaking server's team list carries the world's teams (their ids and checksums: an
# online match's teams are checked against it): it goes with the same data.
# Its 96/97 half comes from bin/data_json/teams (the original game's, never in git): a tree
# without it would write a list with no 96/97 team in it, which refuses every 96/97 match.
if ! ls "$ROOT"/bin/data_json/teams/*.json >/dev/null 2>&1; then
    echo "make_world_package: no bin/data_json/teams here (96/97's teams) -- the server's team list" >&2
    echo "left as it is; refresh it from a tree that has them (assign_team_uuids.py --registry --worlds)" >&2
    exit 0
fi
python3 -I "$ROOT/scripts/assign_team_uuids.py" --registry --worlds | grep "^registry:"
if ! git -C "$ROOT" diff --quiet -- third_party/matchmaker/Server/include/TeamRegistry.inc; then
    echo "make_world_package: the server's team list changed -- commit TeamRegistry.inc, then rebuild"
    echo "and deploy the server (its admin page) when the package goes up"
fi
