#!/usr/bin/env python3
"""world_from_mod.py -- World 2026-27's input for scripts/world_import.py, from an INSTALLED copy of
the SWOS 96/97 Mod (DOS, Davide Lorigliola's), not from the JSON Davide sends (docs/WORLD_2026_27.md,
"Converting the installed Mod").

    python3 -I scripts/world_from_mod.py --mod GAME --base OLD --out DIR [--repo SWOS9697MOD]
    python3 -I scripts/world_import.py --mod DIR ...            # then, as from Davide's JSON

GAME   the installed Mod: its folder (DATA/ inside) or DATA itself -- read only.
OLD    the last JSON delivery of the Mod's data (bin/data_json/worlds/2627/mod/ as Davide sent it):
       what the installed game does not hold in a form we can read -- the packs (pack.json,
       clubs.csv, players.csv, ties.csv, cupd.json, complist.json), conti/, competitions/,
       reference/, teams/NATIONALITIES.json -- is taken from it (the packs' club names brought up
       to the installed ones, below).
DIR    the `mod/` folder written: the same layout and fields as Davide's JSON.
SWOS9697MOD  optional, a checkout of the Mod's public repo (MIT): its tag of the installed version
       is noted in DIR/SOURCE.txt (the tools that describe the formats read here).

What is read from the installed game (DATA/, the files the game itself plays):

    TMW5.nnn    World 2026-27's team files (world 5's own namespace: TEAM.nnn of the 1996-97 game
                and Europe 1984-85 are other worlds), nnn = the Mod's file number = the "country" of
                Davide's teams JSON: [u16 BE count] + count x 684-byte records, the SWOS record
                (76-byte header + 16 x 38-byte players), uncompressed (RNC accepted too)
    XTW5.nnn    each club's players 17..30 (the Mod's tools/xtra.py: [u16 BE clubs][u8 per club]
                + clubs x per x 38 bytes; a player with no name is no player)
    RS5nnn.DAT  a league's off-file pool (gplay.c ri_season; tools/mkseason.py rs_files), team-file
                form, nnn a file of the country -> <CODE>_POOL.json
    POOL05.DAT  the world's foreign market (copied as it is)
    *.EXE       the version ("MOD x.y.z")

Ids (docs/TEAM_IDS.md): a club's uuid is uuid5 of "2627/team/<club id>", so its club id must never
change. Davide's ids are "club:<code>.<press name, lower case, apostrophes dropped, other runs of
non-letters a '-'>" -- every one of 4.0.5's 2,226 follows that rule -- but the name can change (4.1:
TREFELIN B & G -> TREFELIN BGC). So the ids of the clubs we have are a registry,
scripts/world_club_ids_2627.csv (country, team file, ordinal, name, the pack's key, club id: no
squad), and a club is its row: by its name in its country, else (a rename) by its file and ordinal
when that row's name is nobody's now. Only a club in no row gets an id by the rule (reported;
`--registry-add` writes it in). scripts/world_countries_2627.csv: each team file's country code,
name and JSON name (COUNTS.json).

    python3 -I scripts/world_from_mod.py --make-registry OLD   # the registries from a JSON delivery
                                                               # (rows only added, never changed)
    python3 -I scripts/world_from_mod.py --compare OLD DIR     # the teams of two deliveries, by kind

The data stays private: DIR is never inside the repo's tracked tree (bin/data_json/worlds/ is
ignored). Standard library only.
"""
import argparse
import collections
import csv
import glob
import json
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CLUB_IDS = os.path.join(HERE, "world_club_ids_2627.csv")
COUNTRIES = os.path.join(HERE, "world_countries_2627.csv")
WORLD = 5                       # World 2026-27 in the Mod (its TMW5 / XTW5 / RS5 / POOL05 files)
TEAM_SIZE, PLAYER_SIZE = 684, 38
POOL_FIRST = 191                # the pools' pseudo files in Davide's JSON: 191.. in code order
ABOUT = "Mod-only data; a loader that does not know it ignores it (see README)"
TAKEN = ("packs", "conti", "competitions", "reference")    # from the JSON delivery, not the game


# ---- the installed game's files ----------------------------------------------------------------

def unpack(data):
    if data[:3] == b"RNC":
        import swos_team_file           # (scripts/, RNC ProPack)
        return swos_team_file.rnc_unpack(data)
    return data


def records(path):
    d = unpack(open(path, "rb").read())
    n = struct.unpack_from(">H", d)[0]
    if len(d) < 2 + n * TEAM_SIZE:
        sys.exit("%s: %d records do not fit its %d bytes" % (path, n, len(d)))
    return [d[2 + i * TEAM_SIZE:2 + (i + 1) * TEAM_SIZE] for i in range(n)]


def extras(path, clubs):
    """{ordinal: [38-byte players]} of an XTW file (the Mod's xtra.read)."""
    if not os.path.exists(path):
        return {}
    d = unpack(open(path, "rb").read())
    k, e = struct.unpack_from(">HB", d)
    if k != clubs:
        sys.exit("%s: %d clubs, its team file %d" % (path, k, clubs))
    out = {}
    for i in range(k):
        b = d[3 + i * e * PLAYER_SIZE:3 + (i + 1) * e * PLAYER_SIZE]
        out[i] = [p for p in (b[j * PLAYER_SIZE:(j + 1) * PLAYER_SIZE] for j in range(e)) if p[3]]
    return out


def text(b):
    return b.split(b"\0")[0].decode("latin1")


def player(b):
    """A 38-byte PlayerFile as Davide's JSON has it (every byte unsigned)."""
    return {"nationality": b[0], "field_1": b[1], "shirt_number": b[2], "player_name": text(b[3:25]),
            "field_19": b[25], "position_and_face": b[26], "cards_injuries": b[27], "passing": b[28],
            "velocity_heading": b[29], "tackling_control": b[30], "speed_finishing": b[31], "price": b[32],
            "goal_stats": list(b[33:37]), "some_flag": b[37]}


def header(r):
    return {"country_number": r[0], "team_ordinal": r[1], "global_team_number": struct.unpack_from(">H", r, 2)[0],
            "team_controls": r[4], "team_name": text(r[5:22]), "writes_zero_here": r[22], "and_with_0xfe": r[23],
            "tactics": r[24], "league": r[25], "kit_colors": list(r[26:36]), "coach_name": text(r[36:59]),
            "some_flag": r[59], "player_numbers": list(r[60:76])}


def mod_version(game):
    for exe in sorted(glob.glob(os.path.join(game, "*.EXE"))):
        m = re.search(rb"MOD (\d+\.\d+(?:\.\d+)?)", open(exe, "rb").read())
        if m:
            return m.group(1).decode()
    return None


def data_dir(game):
    for d in (os.path.join(game, "DATA"), game):
        if glob.glob(os.path.join(d, "TMW%d.*" % WORLD)):
            return d
    sys.exit("%s: no TMW%d.nnn (World 2026-27's team files): not an installed SWOS 96/97 Mod 4.x" % (game, WORLD))


# ---- the registries ----------------------------------------------------------------------------

def slug(name):
    """Davide's club id rule (every 4.0.5 id): lower case, apostrophes dropped, other runs of
    non-letters/digits one '-'."""
    return re.sub(r"[^a-z0-9]+", "-", name.lower().replace("'", "")).strip("-")


def read_csv(path):
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def write_csv(path, rows, fields, comment=None):
    with open(path, "w", encoding="utf-8", newline="") as f:
        if comment:
            f.write(comment)
        w = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        w.writeheader()
        w.writerows(rows)


def read_registry(path):
    with open(path, encoding="utf-8", newline="") as f:
        return list(csv.DictReader(line for line in f if not line.startswith("#")))


ID_FIELDS = ["code", "json", "file", "ordinal", "name", "key", "club_id"]
COUNTRY_FIELDS = ["code", "country_name", "file", "json"]
ID_COMMENT = ("# World 2026-27's club ids (scripts/world_from_mod.py): a club's uuid is uuid5 of\n"
              "# 2627/team/<club_id> (docs/TEAM_IDS.md) -- never change a club_id; add rows only.\n"
              "# json/file/ordinal: where the club was when its row was made (file 'pool': <code>_POOL);\n"
              "# key: the Mod's pack key (clubs.csv).\n")
COUNTRY_COMMENT = ("# World 2026-27's team files (scripts/world_from_mod.py): the Mod's file number, its\n"
                   "# country and Davide's JSON name of it (COUNTS.json). Pools are not here: RS5<file>.DAT.\n")


def make_registry(old, add_only_from=None):
    """The registries from a JSON delivery (OLD/teams + OLD/packs/*/clubs.csv); rows already in them
    are kept as they are (a club's id is for ever), new ones appended."""
    ids = read_registry(CLUB_IDS) if os.path.exists(CLUB_IDS) else []
    countries = read_registry(COUNTRIES) if os.path.exists(COUNTRIES) else []
    have = {r["club_id"] for r in ids}
    have_files = {r["file"] for r in countries}
    added = 0
    for path in sorted(glob.glob(os.path.join(old, "teams", "*.json"))):
        name = os.path.splitext(os.path.basename(path))[0]
        if name in ("COUNTS", "NATIONALITIES"):
            continue
        d = json.load(open(path, encoding="utf-8"))
        code = d["x_swos9697"]["country_code"]
        pool = "pool_file" in d["x_swos9697"]
        if not pool and str(d["country"]) not in have_files:
            countries.append({"code": code, "country_name": d["country_name"], "file": str(d["country"]),
                              "json": name})
            have_files.add(str(d["country"]))
        key_of = {r["name"]: r["key"] for r in read_csv(os.path.join(old, "packs", code.lower(), "clubs.csv"))}
        for i, e in enumerate(d["teams"]):
            cid = e["x_swos9697"]["club_id"]
            if cid in have:
                continue
            nm = e["team"]["team_name"]
            ids.append({"code": code, "json": name, "file": "pool" if pool else str(d["country"]),
                        "ordinal": str(i), "name": nm, "key": key_of.get(nm, ""), "club_id": cid})
            have.add(cid)
            added += 1
    countries.sort(key=lambda r: int(r["file"]))
    write_csv(CLUB_IDS, ids, ID_FIELDS, ID_COMMENT)
    write_csv(COUNTRIES, countries, COUNTRY_FIELDS, COUNTRY_COMMENT)
    print("%s: %d clubs (%d added); %s: %d team files" % (os.path.relpath(CLUB_IDS), len(ids), added,
                                                            os.path.relpath(COUNTRIES), len(countries)))


class Ids:
    """A club's id: its registry row (by name in its country, else a renamed club by its slot)."""

    def __init__(self):
        self.rows = read_registry(CLUB_IDS)
        self.by_name = {(r["code"], r["file"] == "pool", r["name"]): r for r in self.rows}
        self.by_slot = {(r["code"], r["json"], r["ordinal"]): r for r in self.rows}
        self.taken = {r["club_id"] for r in self.rows}
        self.used = {}                  # club_id -> (json, ordinal, name) this run
        self.renamed, self.new, self.added = [], [], []
        self.file_of = {}               # json name -> team file number (new clubs' rows)

    def resolve(self, clubs):
        """clubs: [(code, json, ordinal, name, pool)] of one country, every one -> its club id."""
        out, left = {}, []
        names_now = {(c, p, n) for c, _, _, n, p in clubs}
        for c in clubs:
            code, js, o, nm, pool = c
            r = self.by_name.get((code, pool, nm))
            if r:
                out[c] = r["club_id"]
            else:
                left.append(c)
        for c in left:
            code, js, o, nm, pool = c
            r = self.by_slot.get((code, js, str(o)))
            if r and (code, pool, r["name"]) not in names_now and r["club_id"] not in out.values():
                out[c] = r["club_id"]
                self.renamed.append((r["club_id"], r["name"], nm))
                continue
            cid = "club:%s.%s" % (code.lower(), slug(nm))
            if cid in self.taken or cid in out.values():
                n = 2
                while "%s-%d" % (cid, n) in self.taken or "%s-%d" % (cid, n) in out.values():
                    n += 1
                cid = "%s-%d" % (cid, n)
            out[c] = cid
            self.new.append((cid, js, o, nm))
            self.added.append({"code": code, "json": js, "file": "pool" if pool else self.file_of[js],
                               "ordinal": str(o),
                               "name": nm, "key": "", "club_id": cid})
        for c, cid in out.items():
            if cid in self.used:
                sys.exit("club id %s twice: %s and %s" % (cid, self.used[cid], c))
            self.used[cid] = c
        return out

    def missing(self):
        return [r for r in self.rows if r["club_id"] not in self.used]


# ---- the conversion ----------------------------------------------------------------------------

def convert(game, old, out, repo=None, registry_add=False):
    data = data_dir(game)
    version = mod_version(game) or mod_version(os.path.dirname(data)) or "?"
    countries = read_registry(COUNTRIES)
    by_file = {int(r["file"]): r for r in countries}
    ids = Ids()
    packs = {}

    def pack(code):
        if code not in packs:
            p = os.path.join(old, "packs", code.lower())
            packs[code] = (json.load(open(os.path.join(p, "pack.json"), encoding="utf-8")),
                           read_csv(os.path.join(p, "clubs.csv")))
        return packs[code]

    files = sorted(int(m.group(1)) for f in os.listdir(data)
                   for m in [re.fullmatch(r"TMW%d\.(\d{3})" % WORLD, f, re.I)] if m)
    unknown = [f for f in files if f not in by_file and not (80 <= f <= 85 or 189 <= f <= 199)]
    if unknown:
        print("team files of no known country (left out; scripts/world_countries_2627.csv): %s"
              % " ".join("TMW%d.%03d" % (WORLD, f) for f in unknown))
    absent = sorted(f for f in by_file if f not in files)
    if absent:
        sys.exit("the installed Mod has no TMW%d.%s (in scripts/world_countries_2627.csv)"
                 % (WORLD, ", ".join("%03d" % f for f in absent)))
    pools = {}                          # code -> RS file number
    for f in os.listdir(data):
        m = re.fullmatch(r"RS%d(\d{3})\.DAT" % WORLD, f, re.I)
        if m:
            n = int(m.group(1))
            if n not in by_file:
                sys.exit("%s: a pool of file %d, no country's" % (f, n))
            pools[by_file[n]["code"]] = os.path.join(data, f)
    pool_file = {code: POOL_FIRST + i for i, code in enumerate(sorted(pools))}

    # every club of a country at once (a renamed club is found by its slot when its old name is free)
    by_code = collections.defaultdict(list)
    for r in countries:
        by_code[r["code"]].append(r)
    teams = {}                          # json name -> (country row, [(record, extras)], pool)
    for code, rows in sorted(by_code.items()):
        clubs = []
        for r in sorted(rows, key=lambda r: int(r["file"])):
            f = int(r["file"])
            recs = records(os.path.join(data, "TMW%d.%03d" % (WORLD, f)))
            xt = extras(os.path.join(data, "XTW%d.%03d" % (WORLD, f)), len(recs))
            ids.file_of[r["json"]] = r["file"]
            teams[r["json"]] = (r, [(rec, xt.get(i, [])) for i, rec in enumerate(recs)], False)
            clubs += [(code, r["json"], i, text(rec[5:22]), False) for i, rec in enumerate(recs)]
        if code in pools:
            recs = records(pools[code])
            js = code + "_POOL"
            teams[js] = (dict(rows[0], json=js), [(rec, None) for rec in recs], True)
            clubs += [(code, js, i, text(rec[5:22]), True) for i, rec in enumerate(recs)]
        ids.resolve(clubs)

    club_id = {(c[1], c[2]): cid for cid, c in ids.used.items()}
    key_id = collections.defaultdict(dict)          # code -> pack key -> club id (the registry's keys)
    for r in ids.rows:
        if r["key"]:
            key_id[r["code"]][r["key"]] = r["club_id"]

    # the pools' global numbers (Davide's JSON: after the world's last file, pool after pool)
    top = 0
    for code in by_code:
        for fk, fv in pack(code)[0].get("files", {}).items():
            if fk != "pool":
                n = len(teams[by_file[fv["file"]]["json"]][1]) if fv["file"] in by_file else 0
                top = max(top, fv["base"] + n)
    pool_global = {}
    for code in sorted(pools):
        pool_global[code] = top
        top += len(teams[code + "_POOL"][1])

    os.makedirs(os.path.join(out, "teams"), exist_ok=True)
    counts = collections.Counter()
    summary = collections.OrderedDict()
    for js, (crow, clubs, pool) in sorted(teams.items()):
        code = crow["code"]
        pk, rows = pack(code)
        csv_of = {key_id[code].get(r["key"]): r for r in rows if key_id[code].get(r["key"])}
        states = {fv["file"] for fk, fv in pk.get("files", {}).items() if fk == "states"}
        entries = []
        for i, (rec, xt) in enumerate(clubs):
            cid = club_id[(js, i)]
            row = csv_of.get(cid)
            if row is None:
                counts["clubs in no pack row (no area, no B team)"] += 1
            h = header(rec)
            x = {"v": 1, "club_id": cid}
            if pool:
                x["division"] = None
            else:
                x["division"] = None if int(crow["file"]) in states else str(h["league"])
            x["area"] = (row or {}).get("area") or None
            parent = key_id[code].get((row or {}).get("second_of") or "")     # (a first team of this
            x["second_of"] = parent if parent in ids.used else None              # country's: as Davide's)
            x["purple_kit"] = 10 in h["kit_colors"]
            if pool:
                x.update(pool=True, mod_file=h["country_number"], mod_global=h["global_team_number"])
                h["country_number"] = pool_file[code]
                h["global_team_number"] = pool_global[code] + i
            else:
                x["extra_players"] = [player(p) for p in xt]
                counts["extra players"] += len(xt)
            entries.append({"team": h, "players": [player(rec[76 + j * PLAYER_SIZE:76 + (j + 1) * PLAYER_SIZE])
                                                   for j in range(16)], "x_swos9697": x})
        xh = {"v": 1, "country_code": code, "world": WORLD, "about": ABOUT}
        if pool:
            xh["pool_file"] = pool_file[code]
        doc = {"type": "team", "country": pool_file[code] if pool else int(crow["file"]),
               "country_name": crow["country_name"] + (" (POOL)" if pool else ""), "count": len(entries),
               "teams": entries, "x_swos9697": xh}
        with open(os.path.join(out, "teams", js + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
        s = summary.setdefault(code, {"code": code, "country": crow["country_name"], "files": 0, "clubs": 0,
                                      "players": 0, "extra_players": 0, "json": []})
        s["files"] += 1
        s["clubs"] += len(entries)
        nx = sum(len(e["x_swos9697"].get("extra_players", [])) for e in entries)
        s["players"] += 16 * len(entries) + nx
        s["extra_players"] += nx
        s["json"].append(js)
        counts["clubs"] += len(entries)
        counts["pool clubs" if pool else "team files"] += len(entries) if pool else 1
    for s in summary.values():
        s["json"] = ", ".join(sorted(s["json"], key=lambda j: (j.endswith("_POOL"), j)))
    with open(os.path.join(out, "teams", "COUNTS.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(list(summary.values()), f, indent=1)
        f.write("\n")
    nat = os.path.join(old, "teams", "NATIONALITIES.json")
    if os.path.exists(nat):
        shutil.copyfile(nat, os.path.join(out, "teams", "NATIONALITIES.json"))
    shutil.copyfile(os.path.join(data, "POOL0%d.DAT" % WORLD), os.path.join(out, "POOL0%d.DAT" % WORLD))

    # what the game does not carry readably: the delivery's, the packs' club names the game's now
    for sub in TAKEN:
        src, dst = os.path.join(old, sub), os.path.join(out, sub)
        if os.path.isdir(src):
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
    name_of = {cid: text(teams[c[1]][1][c[2]][0][5:22]) for cid, c in ids.used.items()}
    for code in by_code:
        path = os.path.join(out, "packs", code.lower(), "clubs.csv")
        with open(path, encoding="utf-8", newline="") as f:
            head = f.readline()
        fields = next(csv.reader([head]))
        rows = read_csv(path)
        changed = 0
        for r in rows:
            cid = key_id[code].get(r["key"])
            if cid in name_of and r["name"] != name_of[cid]:
                r["name"] = name_of[cid]
                changed += 1
        if changed:
            write_csv(path, rows, fields)
            counts["pack club names brought up to the game's"] += changed

    tag = None
    if repo:
        import subprocess
        try:
            tag = subprocess.run(["git", "-C", repo, "rev-parse", "--verify", "-q", "v%s^{commit}" % version],
                                 capture_output=True, text=True).stdout.strip() or None
        except OSError:
            tag = None
    with open(os.path.join(out, "SOURCE.txt"), "w", newline="\n") as f:
        f.write("Written by scripts/world_from_mod.py from an installed SWOS 96/97 Mod %s\n" % version)
        f.write("teams/, POOL0%d.DAT: the game's DATA (TMW%d, XTW%d, RS%d files)\n" % (WORLD, WORLD, WORLD, WORLD))
        f.write("%s, teams/NATIONALITIES.json: the JSON delivery %s\n" % ("/, ".join(TAKEN) + "/", os.path.abspath(old)))
        if repo:
            f.write("the Mod's repo: tag v%s = %s\n" % (version, tag or "(none)"))

    print("the installed Mod %s -> %s" % (version, out))
    for what, n in sorted(counts.items()):
        print("  %-52s %d" % (what, n))
    for cid, was, now in ids.renamed:
        print("  renamed: %s  %s -> %s" % (cid, was, now))
    for cid, js, o, nm in ids.new:
        print("  NEW club (id by the rule): %s  %s #%d %s" % (cid, js, o, nm))
    for r in ids.missing():
        print("  GONE: %s  (%s #%s %s)" % (r["club_id"], r["json"], r["ordinal"], r["name"]))
    if ids.added and registry_add:
        rows = read_registry(CLUB_IDS) + ids.added
        write_csv(CLUB_IDS, rows, ID_FIELDS, ID_COMMENT)
        print("  %d new clubs added to %s" % (len(ids.added), os.path.relpath(CLUB_IDS)))
    if repo and not tag:
        print("  (the Mod's repo has no tag v%s)" % version)


# ---- comparing two deliveries ------------------------------------------------------------------

def compare(a, b, examples=3):
    """The teams of two mod/ folders, field by field, by kind (clubs paired by club id)."""
    def load(d):
        out = {}
        for path in glob.glob(os.path.join(d, "teams", "*.json")):
            js = os.path.splitext(os.path.basename(path))[0]
            if js in ("COUNTS", "NATIONALITIES"):
                continue
            doc = json.load(open(path, encoding="utf-8"))
            for i, e in enumerate(doc["teams"]):
                out[e["x_swos9697"]["club_id"]] = (js, i, e)
        return out
    A, B = load(a), load(b)
    kinds, ex = collections.Counter(), collections.defaultdict(list)

    def note(kind, what):
        kinds[kind] += 1
        if len(ex[kind]) < examples:
            ex[kind].append(what)
    for cid in sorted(set(A) | set(B)):
        if cid not in B:
            note("club only in A", (cid, A[cid][0]))
            continue
        if cid not in A:
            note("club only in B", (cid, B[cid][0]))
            continue
        (ja, ia, ea), (jb, ib, eb) = A[cid], B[cid]
        if (ja, ia) != (jb, ib):
            note("club moved", (cid, ja, ia, jb, ib))
        for k in sorted(set(ea["team"]) | set(eb["team"])):
            va, vb = ea["team"].get(k), eb["team"].get(k)
            if va != vb:
                note("team." + k, (cid, va, vb))
        for k in sorted(set(ea["x_swos9697"]) | set(eb["x_swos9697"])):
            if k == "extra_players":
                continue
            va, vb = ea["x_swos9697"].get(k), eb["x_swos9697"].get(k)
            if va != vb:
                note("x." + k, (cid, va, vb))
        for part, pa, pb in (("player", ea["players"], eb["players"]),
                             ("extra", ea["x_swos9697"].get("extra_players", []),
                              eb["x_swos9697"].get("extra_players", []))):
            na = {p["player_name"]: p for p in pa}
            nb = {p["player_name"]: p for p in pb}
            if [p["player_name"] for p in pa] != [p["player_name"] for p in pb]:
                if set(na) == set(nb):
                    note(part + "s reordered", (cid,))
                else:
                    for n in sorted(set(na) - set(nb)):
                        note(part + " only in A", (cid, n))
                    for n in sorted(set(nb) - set(na)):
                        note(part + " only in B", (cid, n))
            for n in sorted(set(na) & set(nb)):
                for k in na[n]:
                    if na[n][k] != nb[n].get(k):
                        note("%s.%s" % (part, k), (cid, n, na[n][k], nb[n].get(k)))
    print("A %s: %d clubs; B %s: %d clubs" % (a, len(A), b, len(B)))
    for k, n in sorted(kinds.items()):
        print("  %-36s %5d   %s" % (k, n, "; ".join(map(str, ex[k]))))
    return kinds


def main():
    ap = argparse.ArgumentParser(description="World 2026-27's input for world_import.py from an installed SWOS 96/97 Mod")
    ap.add_argument("--mod", help="the installed Mod's folder (or its DATA)")
    ap.add_argument("--base", help="the last JSON delivery of the Mod's data (a mod/ folder): what the game lacks")
    ap.add_argument("--out", help="the mod/ folder to write")
    ap.add_argument("--repo", help="a checkout of the Mod's public repo (its tag of the version, noted)")
    ap.add_argument("--registry-add", action="store_true", help="write new clubs' ids into the registry")
    ap.add_argument("--make-registry", metavar="OLD", help="the registries from a JSON delivery (rows only added)")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"), help="the teams of two mod/ folders, by kind")
    a = ap.parse_args()
    if a.make_registry:
        make_registry(a.make_registry)
    elif a.compare:
        compare(*a.compare)
    elif a.mod and a.base and a.out:
        if os.path.abspath(a.out) == os.path.abspath(a.base):
            sys.exit("--out is the delivery itself")
        convert(a.mod, a.base, a.out, a.repo, a.registry_add)
    else:
        ap.error("--mod, --base and --out (or --make-registry / --compare)")


if __name__ == "__main__":
    main()
