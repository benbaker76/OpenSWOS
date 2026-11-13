#!/usr/bin/env python3
"""world_mod_classic.py -- a classic (historic) world of the SWOS 96/97 Mod, for scripts/world_import.py
(docs/WORLD_2026_27.md §6g): Europe 1984-85, the Mod's world 4 -- DaniSwos's database (his permission:
the owner, 2026-11-13).

    python3 -I scripts/world_from_mod.py --world 8485 --mod GAME --repo SWOS9697MOD --out DIR
    python3 -I scripts/world_import.py --tag 8485 --mod DIR

A classic world is not compiled into the Mod's exe the way World 2026-27 is: its packs are PUBLIC, in the Mod's
repo (packs/europe-1984-85/<country>/pack.json, clubs.csv, ties.csv), so they are taken from there, at the tag of
the installed release (`git show v<version>:...`); the squads are the installed game's own files, as World
2026-27's are:

    DATA/TEAM.101-132   the countries' league clubs (the base namespace: the Mod's world 4 numbers its own
                        files, 101.. ; TEAM.000-081 stay 1996-97's), [u16 BE count] + 684-byte records, in
                        clubs.csv's order (its rows of that file -- "numbers": "place")
    DATA/TEAM.133-144   the cup-only clubs ("cuponly": a national cup's clubs of no division)
    DATA/TEAM.180-185   the era's national teams (as TEAM.080-085, the same order: East Germany in Croatia's
                        place, the USSR in Russia's ...; national/pack.json "as")
    DATA/WORLD04.TMD    the first season's European clubs (128 records: cc, cwc, uefa)

(TEAM.145 Italy 1947-48 and TEAM.146 the World Cup 1934 are the classic season and tourney: not this world.)

Written, the layout world_import.py reads (world_from_mod.py's for World 2026-27):

    DIR/packs/<code>/pack.json, clubs.csv, ties.csv   from the repo; clubs.csv's names the game's
    DIR/teams/<CODE>.json                              league + cup-only clubs, x_swos9697: club_id
                                                       "club:<code>.<pack key>", division ("" none)
    DIR/teams/COUNTS.json                              code, country, json, files, clubs
    DIR/national/NAT80.json .. NAT85.json, nations.csv, pack.json
    DIR/WORLD04.TMD, DIR/SOURCE.txt

A club's id is its pack key (DaniSwos's database names it once): never renumbered by the file. Standard
library only; the installed game is read, never written.
"""
import csv
import io
import json
import os
import struct
import subprocess
import sys

TEAM_SIZE, PLAYER_SIZE = 684, 38
CLASSIC = {     # our tag -> (the Mod's world number, its packs folder, its first file, its last club file)
    "8485": (4, "europe-1984-85"),
}


def _git(repo, *args):
    r = subprocess.run(["git", "-C", repo] + list(args), capture_output=True)
    if r.returncode:
        sys.exit("git %s: %s" % (" ".join(args), r.stderr.decode(errors="replace").strip()))
    return r.stdout


def records(path):
    d = open(path, "rb").read()
    if d[:3] == b"RNC":
        import swos_team_file
        d = swos_team_file.rnc_unpack(d)
    n = struct.unpack_from(">H", d)[0]
    if len(d) < 2 + n * TEAM_SIZE:
        sys.exit("%s: %d records do not fit its %d bytes" % (path, n, len(d)))
    return [d[2 + i * TEAM_SIZE:2 + (i + 1) * TEAM_SIZE] for i in range(n)]


def convert(game, repo, out, tag, version, wfm):
    """wfm: world_from_mod (its record decoders)."""
    if tag not in CLASSIC:
        sys.exit("no classic world %s (scripts/world_mod_classic.py CLASSIC)" % tag)
    if not repo:
        sys.exit("a classic world's packs are the Mod's repo's: --repo <a checkout of swos-9697-mod>")
    number, folder = CLASSIC[tag]
    rev = "v%s" % version
    data = next((d for d in (os.path.join(game, "DATA"), game) if os.path.exists(os.path.join(d, "TEAM.101"))), None)
    if not data:
        sys.exit("%s: no DATA/TEAM.101 (Europe 1984-85's team files): not an installed SWOS 96/97 Mod 3.x/4.x" % game)
    names = [n.decode().rsplit("/", 1)[-1] for n in
             _git(repo, "ls-tree", "--name-only", "%s:packs/%s" % (rev, folder)).split()]
    counts, summary = {}, []
    os.makedirs(os.path.join(out, "teams"), exist_ok=True)
    os.makedirs(os.path.join(out, "national"), exist_ok=True)

    def show(code, f):
        try:
            return _git(repo, "show", "%s:packs/%s/%s/%s" % (rev, folder, code, f)).decode("utf-8")
        except SystemExit:
            return None

    for code in sorted(names):
        pk_text = show(code, "pack.json")
        if pk_text is None:
            continue
        pk = json.loads(pk_text)
        if pk.get("world") != number:
            sys.exit("packs/%s/%s: world %s, not %d" % (folder, code, pk.get("world"), number))
        clubs_text = show(code, "clubs.csv")
        rows = list(csv.DictReader(io.StringIO(clubs_text)))
        fields = next(csv.reader(io.StringIO(clubs_text)))
        if pk.get("kind") == "national":
            nat = []
            for key, f in sorted(pk["files"].items()):
                recs = records(os.path.join(data, "TEAM.%03d" % f["file"]))
                mine = [r for r in rows if r["file"] == key]
                if len(mine) != len(recs):
                    sys.exit("national %s: %d rows, TEAM.%03d %d teams" % (key, len(mine), f["file"], len(recs)))
                entries = []
                for i, (rec, row) in enumerate(zip(recs, mine)):
                    h = wfm.header(rec)
                    entries.append({"team": h, "players": [wfm.player(rec[76 + j * PLAYER_SIZE:76 + (j + 1) * PLAYER_SIZE])
                                                           for j in range(16)],
                                    "x_swos9697": {"v": 1, "club_id": "nation:%s" % row["key"], "as": f["as"]}})
                doc = {"type": "team", "country": f["as"], "count": len(entries), "teams": entries,
                       "x_swos9697": {"v": 1, "world": number, "mod_file": f["file"]}}
                _write_json(os.path.join(out, "national", "NAT%d.json" % f["as"]), doc)
                nat.append(len(entries))
            for f in ("pack.json", "nations.csv", "clubs.csv"):
                _write_text(os.path.join(out, "national", f), show(code, f))
            counts["national teams"] = sum(nat)
            continue
        CODE = code.upper()
        entries = []
        files = 0
        for key, f in sorted(pk["files"].items(), key=lambda kv: kv[1]["file"]):
            recs = records(os.path.join(data, "TEAM.%03d" % f["file"]))
            mine = [r for r in rows if r["file"] == key]
            if len(mine) != len(recs):
                sys.exit("%s %s: clubs.csv has %d rows, TEAM.%03d %d clubs" % (CODE, key, len(mine), f["file"], len(recs)))
            files += 1
            for rec, row in zip(recs, mine):
                h = wfm.header(rec)
                if row["name"] != h["team_name"]:       # (the game's name: the cup's ties find clubs by it)
                    counts["pack club names brought up to the game's"] = counts.get(
                        "pack club names brought up to the game's", 0) + 1
                    row["name"] = h["team_name"]
                entries.append({"team": h, "players": [wfm.player(rec[76 + j * PLAYER_SIZE:76 + (j + 1) * PLAYER_SIZE])
                                                       for j in range(16)],
                                "x_swos9697": {"v": 1, "club_id": "club:%s.%s" % (code, row["key"]),
                                               "division": row["division"] if row["division"] != "" else None,
                                               "mod_file": f["file"]}})
        doc = {"type": "team", "country": pk["files"]["league"]["file"], "country_name": pk["button"],
               "count": len(entries), "teams": entries,
               "x_swos9697": {"v": 1, "country_code": CODE, "world": number}}
        _write_json(os.path.join(out, "teams", CODE + ".json"), doc)
        pdir = os.path.join(out, "packs", code)
        os.makedirs(pdir, exist_ok=True)
        _write_text(os.path.join(pdir, "pack.json"), pk_text)
        buf = io.StringIO()
        w = csv.DictWriter(buf, fieldnames=fields, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
        _write_text(os.path.join(pdir, "clubs.csv"), buf.getvalue())
        ties = show(code, "ties.csv")
        if ties:
            _write_text(os.path.join(pdir, "ties.csv"), ties)
        summary.append({"code": CODE, "country": pk["button"], "files": files, "clubs": len(entries), "json": CODE})
        counts["clubs"] = counts.get("clubs", 0) + len(entries)
        counts["countries"] = counts.get("countries", 0) + 1
    _write_json(os.path.join(out, "teams", "COUNTS.json"), summary)
    tmd = os.path.join(data, "WORLD%02d.TMD" % number)
    if os.path.exists(tmd):
        with open(tmd, "rb") as f, open(os.path.join(out, os.path.basename(tmd).upper()), "wb") as g:
            g.write(f.read())
    commit = _git(repo, "rev-parse", "--verify", "-q", "%s^{commit}" % rev).decode().strip()
    _write_text(os.path.join(out, "SOURCE.txt"),
                "Written by scripts/world_from_mod.py --world %s from an installed SWOS 96/97 Mod %s\n"
                "teams/, national/, WORLD%02d.TMD: the game's DATA (TEAM.nnn of the Mod's world %d)\n"
                "packs/: the Mod's repo, packs/%s at %s = %s\n"
                "Europe 1984-85's squads: DaniSwos (CM01/02 Retro Update Team), 1984-85 database\n"
                % (tag, version, number, number, folder, rev, commit))
    print("the installed Mod %s, world %d (%s) -> %s" % (version, number, folder, out))
    for what, n in sorted(counts.items()):
        print("  %-52s %d" % (what, n))


def _write_json(path, doc):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=1)
        f.write("\n")


def _write_text(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
