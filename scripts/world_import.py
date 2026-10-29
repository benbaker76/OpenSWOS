#!/usr/bin/env python3
"""world_import.py -- build a world's game data from the SWOS 96/97 Mod's
(docs/WORLD_2026_27.md, phase 3).

    python3 -I scripts/world_import.py              # World 2026-27 -> bin/data_json/worlds/2627/

Reads the Mod's data as Davide Lorigliola sent it (bin/data_json/worlds/2627/mod/: his
teams in OpenSWOS team JSON, his packs) and writes what the game reads for that world:

    bin/data_json/worlds/2627/teams/ITALY.json        the country's team file, our format
    bin/data_json/worlds/2627/competitions/leagues.json  the league records that differ
                                                        from 96/97, by id (an overlay:
                                                        src/save_format_json.c)
    bin/data_json/worlds/2627/data/TEAM.020           the same teams as the binary file the
                                                        Amiga reads (RNC, as 96/97's)
    src/world_data.c                                  the same records baked, for the Amiga
                                                        (and both targets at start-up:
                                                        World_InstallBaked) -- committed, it
                                                        holds only league shapes and the
                                                        cups' brackets (team-file ordinals)

The world so far is ONE country, Italy, whole: 256 clubs, its league Serie A, Serie B,
Serie C's three groups side by side and Serie D's nine (phase 4: levels of groups,
include/league_groups.h; Serie D's odd groups play real rounds, include/fixture_rounds.h), its
Coppa Italia and Coppa Italia Serie C from the real draw (include/world_cups.h). Every other
country stays 96/97's (the overlay falls back to it).

What our engine cannot show yet is mapped, and counted on the way:
  - 16 players a team (the Mod's extra 14 are left out);
  - kit colour 10 (purple) -> 6 (maroon: the palette entry the Mod borrows for it);
  - face 3 (black skin, blond hair) -> 2;
  - nationalities the game has no name for (153 on) -> the club's country's;
  - shirt numbers above 16 (the game draws 1..16) -> the free ones of 1..16.

Ids: every team and player gets its uuid here, CANONICAL for netplay (docs/TEAM_IDS.md):
uuid5 in the database's namespace of "2627/team/<club id>" and ".../player/<index>" --
the Mod's club id (club:ita.<press name>), so a club keeps its id whatever its file or
place -- and the checksum of what a match plays it by, as scripts/assign_team_uuids.py
computes it. The data is not to be published yet: bin/data_json/worlds/ is ignored.
"""
import collections
import copy
import csv
import hashlib
import json
import os
import re
import sys
import uuid

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, "scripts"))
import emit_competitions                     # noqa: E402  (emit_league)
import swos_team_file as tf                  # noqa: E402  (the binary TEAM.nnn)
WORLD = os.path.join(REPO, "bin", "data_json", "worlds", "2627")
MOD = os.path.join(WORLD, "mod")
BASE = os.path.join(REPO, "bin", "data_json")

NAMESPACE = uuid.UUID("af19cc5a-6529-537b-935b-407bd73b75fb")   # assign_team_uuids.py's
SKILLS = ("passing", "velocity", "heading", "tackling", "control", "speed", "finishing")
NATIONS_NAMED = 153         # the game's short country names (menus.c, _shortCountryNames)
PURPLE, MAROON = 10, 6
counts = collections.Counter()


def checksum(players):
    data = bytearray()
    for p in players:
        data.append(p["position"] & 7)
        data.extend(p[k] & 15 for k in SKILLS)
        data.append(p["price"] & 255)
    return hashlib.sha1(bytes(data)).hexdigest()[:16]


def player(p, home_nation):
    """The Mod's packed player record, as our split one."""
    pf, ci = p["position_and_face"] & 255, p["cards_injuries"] & 255
    face = (pf >> 3) & 3
    if face == 3:
        face = 2
        counts["face 3 -> 2"] += 1
    nat = p["nationality"] & 255
    if nat >= NATIONS_NAMED:
        nat = home_nation
        counts["nationality without a name -> the club's country's"] += 1
    vh, tc, sf = p["velocity_heading"] & 255, p["tackling_control"] & 255, p["speed_finishing"] & 255
    return {
        "nationality": nat,
        "cached_match_value": p["field_1"] & 255,
        "shirt_number": p["shirt_number"] & 255,
        "player_name": p["player_name"],
        "position": pf >> 5,
        "face": face,
        "flags": pf & 7,
        "cards": ci & 3,
        "injury": (ci >> 2) & 7,
        "status_bucket": ci >> 5,
        "passing": p["passing"] & 255,
        "velocity": vh >> 4, "heading": vh & 15,
        "tackling": tc >> 4, "control": tc & 15,
        "speed": sf >> 4, "finishing": sf & 15,
        "price": p["price"] & 255,
        "goal_stats": [g & 255 for g in p["goal_stats"]],
        "form": p["some_flag"] & 255,
    }


def shirt_numbers(players):
    """Numbers above 16 (the game draws only 1..16) take the free ones, in order."""
    used = {p["shirt_number"] for p in players if 1 <= p["shirt_number"] <= 16}
    free = [n for n in range(1, 17) if n not in used]
    for p in players:
        if not 1 <= p["shirt_number"] <= 16:
            p["shirt_number"] = free.pop(0)
            counts["shirt number above 16 -> a free one"] += 1


def team(entry, country, ordinal, global_number, league, home_nation):
    t, x = entry["team"], entry.get("x_swos9697", {})
    kits = [k & 255 for k in t["kit_colors"]]
    if PURPLE in kits:
        kits = [MAROON if k == PURPLE else k for k in kits]
        counts["purple kit -> maroon"] += 1
    players = [player(p, home_nation) for p in entry["players"]]
    shirt_numbers(players)
    club = x.get("club_id") or "club:%d.%d" % (country, ordinal)
    tid = str(uuid.uuid5(NAMESPACE, "2627/team/%s" % club))
    for i, p in enumerate(players):
        p_out = {"uuid": str(uuid.uuid5(NAMESPACE, "2627/team/%s/player/%d" % (club, i)))}
        p_out.update(p)
        players[i] = p_out
    names = ("shirt_type", "stripes_color", "basic_color", "shorts_color", "socks_color")
    head = {
        "country_number": country, "team_ordinal": ordinal,
        "global_team_number": global_number, "team_controls": t["team_controls"] & 255,
        "team_name": t["team_name"], "writes_zero_here": t["writes_zero_here"] & 255,
        "and_with_0xfe": t["and_with_0xfe"] & 255, "tactics": t["tactics"] & 255,
        "league": league,
    }
    for i, n in enumerate(names):
        head["primary_" + n] = kits[i]
    for i, n in enumerate(names):
        head["secondary_" + n] = kits[5 + i]
    head["coach_name"] = t["coach_name"]
    head["player_values_dirty"] = t["some_flag"] & 255
    head["player_numbers"] = [n & 255 for n in t["player_numbers"]]
    return {"uuid": tid, "checksum": checksum(players), "team": head, "players": players}


SERIE_C_GROUPS = 3          # Serie C: three groups side by side under Serie B


def mod_division(e):
    div = e.get("x_swos9697", {}).get("division")    # a string in the Mod's data ("0")
    return int(div) if div not in (None, "") else None


def italy():
    """Italy whole, 256 clubs (the country's range of global numbers grows to them:
    world_data.c, resize_country), in the Mod's divisions, the league's fourteen: Serie A 0,
    Serie B 1, Serie C's groups 2..4, Serie D's 5..13."""
    base = json.load(open(os.path.join(BASE, "teams", "ITALY.json"), encoding="utf-8"))
    mod = json.load(open(os.path.join(MOD, "teams", "ITA.json"), encoding="utf-8"))
    slots = base["teams"]
    country = slots[0]["team"]["country_number"]
    home = collections.Counter(p["nationality"] for t in slots for p in t["players"]).most_common(1)[0][0]
    by_div = collections.defaultdict(list)
    for e in mod["teams"]:
        by_div[mod_division(e)].append(e)
    sizes = [len(by_div[d]) for d in range(2 + SERIE_C_GROUPS)]
    if sizes != [20] * (2 + SERIE_C_GROUPS) or None in by_div:
        sys.exit("Italy: expected Serie A, B and C's groups of 20 and every club in a division,"
                 " got %s" % sizes)
    picked = [(e, d) for d in sorted(by_div) for e in by_div[d]]
    first = slots[0]["team"]["global_team_number"]   # recomputed at load (SetTeamGlobalNumbers)
    teams = [team(e, country, i, first + i, league, home) for i, (e, league) in enumerate(picked)]
    out = copy.copy(base)
    out["teams"], out["count"] = teams, len(teams)
    return out, picked


# The world's countries onto ours (include/world.h): a Mod country by its name (as 96/97 names
# it), or new -- numbers no club country has (src/strings_data.c _countriesTable: free).
COUNTRY_ALIASES = {"NETHERLANDS": "HOLLAND", "CZECH REP.": "CZECH REPUBLIC", "FAROE IS.": "FAROE ISLANDS",
                   "EIRE": "REPUBLIC IRELAND", "N. IRELAND": "NORTHERN IRELAND", "SERBIA": "YUGOSLAVIA"}
# A new country's number is in every save that holds it: fixed here, never by order. Free:
# 99 (and, if 96/97's national teams never need them, 100-251).
NEW_COUNTRY_NUMBERS = {
    "AND": 47, "ARM": 52, "AZE": 53, "BIH": 54, "CAN": 56, "CHN": 57, "CRC": 58, "EGY": 59,
    "GEO": 61, "GIB": 63, "GUA": 68, "HON": 70, "IRN": 74, "KAZ": 86, "KOR": 87, "KOS": 88,
    "KSA": 89, "LIE": 90, "MAR": 91, "MDA": 92, "MKD": 93, "MNE": 94, "PAN": 95, "QAT": 96,
    "TUN": 97, "UAE": 98,
}


def mod_counts():
    """The Mod's countries: code, name, its team files (Davide's COUNTS.json, beside the teams)."""
    for path in (os.path.join(MOD, "teams", "COUNTS.json"),):
        if os.path.exists(path):
            return json.load(open(path, encoding="utf-8"))
    out = []                    # (no COUNTS.json: from the team files themselves)
    for f in sorted(os.listdir(os.path.join(MOD, "teams"))):
        if re.fullmatch(r"[A-Z]{3}\.json", f):
            d = json.load(open(os.path.join(MOD, "teams", f), encoding="utf-8"))
            out.append({"code": f[:3], "country": d.get("country_name"), "json": f[:3], "files": 1,
                        "clubs": len(d["teams"])})
    return out


def world_countries():
    """[{code, name, number, new, base}] -- base: our team file's name (teams/<base>.json), or
    None for a country new to the game."""
    ours = {c["name"].replace("_", " "): c for c in json.load(open(os.path.join(
        BASE, "competitions", "countries.json"), encoding="utf-8"))["countries"] if c["number"] < 80}
    out = []
    for c in sorted(mod_counts(), key=lambda c: c["code"]):
        name = COUNTRY_ALIASES.get(c["country"], c["country"])
        o = ours.get(name)
        if o:
            out.append({"code": c["code"], "name": name, "number": o["number"], "new": False,
                        "base": os.path.splitext(os.path.basename(o["team_file"]))[0], "mod": c})
        else:
            if c["code"] not in NEW_COUNTRY_NUMBERS:
                sys.exit("world countries: %s (%s) is new: give it a number (NEW_COUNTRY_NUMBERS)"
                         % (c["code"], c["country"]))
            out.append({"code": c["code"], "name": c["country"], "number": NEW_COUNTRY_NUMBERS[c["code"]],
                        "new": True, "base": None, "mod": c})
    return out


def country_teams(wc):
    """A country whole (as italy(): the Mod's clubs in their divisions' order), our team file's
    form; a new country's from nothing. Returns (the file, [(entry, division)])."""
    league_file = wc["mod"]["json"].split(",")[0].strip()      # ("ESP, ESP_POOL": the pool's apart)
    mod = json.load(open(os.path.join(MOD, "teams", league_file + ".json"), encoding="utf-8"))
    if wc["base"]:
        base = json.load(open(os.path.join(BASE, "teams", wc["base"] + ".json"), encoding="utf-8"))
        home = collections.Counter(p["nationality"] for t in base["teams"]
                                   for p in t["players"]).most_common(1)[0][0]
    else:
        base = {"type": "team", "country": wc["number"], "country_name": wc["name"], "count": 0, "teams": []}
        named = [p["nationality"] & 255 for t in mod["teams"] for p in t["players"]
                 if (p["nationality"] & 255) < NATIONS_NAMED]
        home = collections.Counter(named).most_common(1)[0][0] if named else 0xFF
    by_div = collections.defaultdict(list)
    for e in mod["teams"]:
        by_div[mod_division(e)].append(e)
    if None in by_div:
        sys.exit("%s: a club in no division" % wc["code"])
    picked = [(e, d) for d in sorted(by_div) for e in by_div[d]]
    if len(picked) > 256:
        sys.exit("%s: %d clubs, a file holds 256" % (wc["code"], len(picked)))
    teams = [team(e, wc["number"], i, i, league, home) for i, (e, league) in enumerate(picked)]
    out = copy.copy(base)
    out["country"], out["teams"], out["count"] = wc["number"], teams, len(teams)
    return out, picked


def area_codes(entry):
    """A club's areas at the levels of groups (the Mod's "X/Y"), each 0.. or None."""
    out = []
    for letter in ((entry.get("x_swos9697", {}).get("area") or "").split("/") + ["", ""])[:2]:
        out.append(ord(letter) - ord("A") if len(letter) == 1 and "A" <= letter <= "Z" else None)
    return out


def country_pool(code, name, country, home):
    """A league's off-file pool below its lowest division (the Mod's <CODE>_POOL.json, the
    pack's league 'reserve': Italy's Eccellenza; Spain's, Germany's, the Netherlands' and
    Portugal's, which only a B team forced out of the lowest division goes down to): its
    clubs in our format, in the pack's first order (the queue a relegated slot takes from:
    the file's own order), each one's areas -- the group a slot that takes it plays in
    (src/team_pool.c). The pool's file number is the Mod's (pool_file). None: no pool."""
    path = os.path.join(MOD, "teams", code + "_POOL.json")
    lg = pack_league(mod_pack(code))
    if not (lg.get("reserve") or {}).get("off_file") or not os.path.exists(path):
        return None, None
    mod = json.load(open(path, encoding="utf-8"))
    file = mod["x_swos9697"]["pool_file"]
    order = lg["reserve"]["first"]
    if len(order) != len(mod["teams"]):
        sys.exit("%s's pool: %d clubs, the pack's first order %d" % (code, len(mod["teams"]), len(order)))
    teams = [team(e, file, i, 0, 0, home) for i, e in enumerate(mod["teams"])]
    out = {"type": "team", "country": file, "count": len(teams), "teams": teams}
    counts["pool clubs"] += len(teams)
    return out, {"name": name, "country": country, "file": file, "json": name.upper() + "_POOL.json",
                 "count": len(teams), "areas": [area_codes(e) for e in mod["teams"]],
                 "ids": [e.get("x_swos9697", {}).get("club_id") for e in mod["teams"]]}


def home_nation(teams):
    return collections.Counter(p["nationality"] for t in teams["teams"] for p in t["players"]).most_common(1)[0][0]


def mod_league():
    pack = json.load(open(os.path.join(MOD, "packs", "ita", "pack.json"), encoding="utf-8"))
    return next(c for c in pack["competitions"] if c["key"] == "league")


def italian_league():
    """96/97's record, as 2026-27's pyramid: Serie A 20 (3 down), Serie B 20 (3 up, 3 down),
    Serie C's three groups of 20 (each champion up, 3 down) and Serie D's nine groups of 17
    or 18 (each champion up; none down, while no pool below takes them). The Mod's play-offs
    and play-outs (Serie B's third up, Serie C's and D's) are not played yet."""
    leagues = json.load(open(os.path.join(BASE, "competitions", "leagues.json"), encoding="utf-8"))
    rec = copy.deepcopy(next(l for l in leagues["leagues"] if l["id"] == "_italianLeague"))
    a, b = rec["divisions"]
    a.update(teams=20, relegated=3)
    b.update(teams=20, promoted=3, relegated=3)
    mod = mod_league()["divisions"]
    for d in mod[2:2 + SERIE_C_GROUPS]:
        rec["divisions"].append(dict(b, teams=d["teams"], promoted=1, relegated=3))
    for d in mod[2 + SERIE_C_GROUPS:]:
        rec["divisions"].append(dict(b, teams=d["teams"], promoted=1, relegated=0))
    for d in mod[2:]:
        # Display text: no string symbol has it (world_data_c bakes one).
        rec["name"] += [d["names"]["*"][0]] * 2
    rec["divisions_count"] = len(rec["divisions"])
    return {"type": leagues.get("type", "leagues"), "count": 1, "leagues": [rec]}


def italian_groups(picked, country, league):
    """The league's levels and each club's group at the levels of groups by its area (the
    Mod's "X/Y": X its Serie C group, Y its Serie D one)."""
    rec = league["leagues"][0]
    sizes = [d["teams"] for d in rec["divisions"]]
    levels, first = [], 0
    for n in (1, 1, SERIE_C_GROUPS, len(sizes) - 2 - SERIE_C_GROUPS):
        name = None
        if n > 1:       # the groups' names without the group: "SERIE D GIRONE A" -> "SERIE D"
            names = [rec["name"][2 * d] for d in range(first, first + n)]
            name = re.sub(r"\s*\bGIRONE\s*$", "", os.path.commonprefix(names)).strip()
        levels.append((first, n, sizes[first:first + n], name))
        first += n
    clubs = []
    for ordinal, (e, division) in enumerate(picked):
        areas = []
        for letter in ((e.get("x_swos9697", {}).get("area") or "").split("/") + ["", ""])[:2]:
            if len(letter) == 1 and "A" <= letter <= "Z":
                areas.append(ord(letter) - ord("A"))
            else:
                areas.append(None)
                counts["club without an area at a level of groups"] += 1
        clubs.append((ordinal, division, areas))
    return {"name": "Italy", "country": country, "levels": levels, "clubs": clubs}


# The Mod's knockout play-offs (pack.json 'ko_playoffs'), compiled as tools/gplay.py's
# compile_spec compiles them -- ported, the same bytes (league_playoffs_check.sh compares
# them for every pack), so src/league_playoffs.c walks them as gplay.c does. Every kind is
# compiled: the season end moves clubs by promotion, play-out and promedio; the others it
# walks past (a placement is the career club's contest; title, europe, stage_title need the
# splits and stages still to come).
KINDS = {"promotion": 1, "europe": 2, "placement": 3, "playout": 4, "title": 5, "continental": 6,
         "promedio": 7, "stage_title": 8}
HOSTS = {"listed": 0, "better": 1, "drawn": 2, "neutral": 3}
LEVELS = {"better": 0, "et_pens": 1, "et_better": 2, "pens": 3, "away_pens": 4, "away_better": 5}
RESERVE = 0xFE
SKIP_SAME = 0x80            # (host byte bit 7: a tie of one club with itself is not played)
BEST_LOSER = 0x3F           # (member 'BL': the best placed loser of the previous round)


def compile_spec(pos, ndiv):
    """pack.json 'ko_playoffs' -> the bytes gplay.c walks, ended by a 0 (the Mod's
    tools/gplay.py compile_spec, MIT, Davide Lorigliola: its bytes only, not its tables of
    names, stages and round names)."""
    out = bytearray()
    for po in pos:
        key = po.get("key", "?")
        kind = KINDS.get(po.get("kind"))
        if not kind:
            sys.exit("%s: kind %s" % (key, po.get("kind")))
        if kind == 7:                           # (Argentina: matches this season, loser_div)
            m, ld = po.get("matches"), po.get("loser_div")
            if not (isinstance(m, int) and 1 <= m < 256 and isinstance(ld, int) and 0 < ld < ndiv):
                sys.exit("%s: promedio needs matches 1..255 and loser_div 1..%d" % (key, ndiv - 1))
            out += bytes((7, 0, 0, m, ld, 0))
            continue
        ents = []
        for e in po["entrants"]:
            if isinstance(e, list) and len(e) == 2:
                d, p = e
                if not (0 <= d < ndiv and p >= 1):
                    sys.exit("%s: entrant %s" % (key, e))
                ents.append((0, d, p, 0))
            elif isinstance(e, dict) and e.get("best_without_europe"):
                ents.append((1, e["div"], 0, 0))
            elif isinstance(e, dict) and "stage_place" in e:
                d, st, a_ = e.get("div", 0), e["stage_place"], e.get("place", 1)
                if not (0 <= d < ndiv and (0 <= st < 2 or 4 <= st < 8) and a_ >= 1):
                    sys.exit("%s: entrant %s" % (key, e))
                ents.append((3, d, a_, st))
            elif isinstance(e, dict) and "split_seed" in e:
                d, st, k = e.get("div", 0), e.get("stage", 0), e["split_seed"]
                if not (0 <= d < ndiv and 0 <= st < 2 and 1 <= k <= 4):
                    sys.exit("%s: entrant %s" % (key, e))
                ents.append((6, d, k, st))
            elif isinstance(e, dict) and "title_seed" in e:
                d, k, r_ = e.get("div", 0), e["title_seed"], {"uru": 1, "per": 2}.get(e.get("rule"))
                if not (0 <= d < ndiv and 1 <= k <= 4 and r_):
                    sys.exit("%s: entrant %s" % (key, e))
                ents.append((7, d, k, r_))
            elif isinstance(e, dict) and e.get("loser_of_previous"):
                ents.append((4, 0, 0, 0))
            elif isinstance(e, dict) and "stage_title" in e:
                c, d = e["stage_title"], e.get("div", 0)
                if not (c in (1, 2, 3, 4) and 0 <= d < ndiv):
                    sys.exit("%s: entrant %s" % (key, e))
                ents.append((5, d, c, 0))
            elif isinstance(e, dict) and e.get("league_cup_winner"):
                a_, b_ = e.get("if_places", [0, 0])
                ents.append((2, 0, a_, b_))
            else:
                sys.exit("%s: entrant %s" % (key, e))
        if not 2 <= len(ents) <= 63:
            sys.exit("%s: 2..63 entrants" % key)
        rounds, nt, npl, nrd = bytearray(), 0, 0, 0

        def code(m):
            if m == "BL":
                if not nt:
                    sys.exit("%s: BL needs an earlier knockout round" % key)
                return BEST_LOSER
            if isinstance(m, int):
                if not 0 <= m < len(ents):
                    sys.exit("%s: member %s" % (key, m))
                return m
            mm = re.fullmatch(r"([WLP])(\d+)", str(m))
            if not mm or int(mm.group(2)) >= (npl if mm.group(1) == "P" else nt):
                sys.exit("%s: member %s" % (key, m))
            return {"W": 0x80, "L": 0xC0, "P": 0x40}[mm.group(1)] | int(mm.group(2))

        for rd in po["rounds"]:
            for c in list(rd.get("clubs") or []):     # ({'best_of': [P..]}: a ranking round first)
                if isinstance(c, dict) and "best_of" in c:
                    bo = c["best_of"]
                    if not (2 <= len(bo) <= 24 and all(re.fullmatch(r"P\d+", str(m)) for m in bo)):
                        sys.exit("%s: best_of: 2..24 earlier placings" % key)
                    rounds += bytes((len(bo), 0x80, HOSTS["neutral"], 0, 0, 0)) + bytes(code(m) for m in bo)
                    rounds += bytes(len(bo))
                    rd = dict(rd, clubs=["P%d" % npl if x is c else x for x in rd["clubs"]])
                    npl += len(bo)
                    nrd += 1
            nrd += 1
            if "group" in rd:                           # (a round robin)
                g, mem = rd["group"], rd["members"]
                meet, bonus = g.get("rounds", 2), g.get("bonus", [0] * len(mem))
                if not (2 <= len(mem) <= 24 and meet in (1, 2, 3, 4) and len(bonus) == len(mem)
                        and all(0 <= x < 256 for x in bonus) and g.get("host", "listed") in ("listed", "neutral")):
                    sys.exit("%s: a group of 2..24 members" % key)
                rounds += bytes((len(mem), 0x80 | meet, HOSTS[g.get("host", "listed")], 0, 0, 0))
                rounds += bytes(code(m) for m in mem) + bytes(bonus)
                npl += len(mem)
                continue
            tl = list(rd.get("ties") or [])             # (a best_worst / draw block first, then listed ties)
            blk = rd if rd.get("pairing") else (tl.pop(0) if tl and isinstance(tl[0], dict) else None)
            if blk is not None and (blk.get("pairing") not in ("best_worst", "draw") or len(blk["clubs"]) % 2):
                sys.exit("%s: pairing best_worst / draw with an even number of clubs" % key)
            if any(isinstance(t, dict) for t in tl):
                sys.exit("%s: a best_worst block only as the first tie" % key)
            bm = list(blk["clubs"]) if blk is not None else []
            mem = bm + [m for t in tl for m in t]
            ties = len(mem) // 2
            pairing = len(bm) | (0x80 if blk is not None and blk.get("pairing") == "draw" else 0)
            codes = [code(m) for m in mem]
            legs = rd.get("legs", 1)
            if rd.get("series"):
                if rd["series"] != 3 or legs != 1:
                    sys.exit('%s: "series": 3 (single matches)' % key)
                legs = 3
            if legs not in (1, 2, 3) or rd.get("host", "listed") not in HOSTS or rd.get("level", "et_pens") not in LEVELS:
                sys.exit("%s: legs, host, level" % key)
            gap = rd.get("gap", 0)
            host = HOSTS[rd.get("host", "listed")] | (SKIP_SAME if rd.get("skip_if_same") else 0)
            rounds += bytes((ties, legs, host, LEVELS[rd.get("level", "et_pens")], pairing, gap))
            rounds += bytes(codes)
            nt += ties
        if nt > 63 or npl > 64:
            sys.exit("%s: at most 63 ties, 64 group placings" % key)
        win = po.get("winners", 1)
        wd, ld = po.get("winner_div", 0), po.get("loser_div", 0)
        ld = RESERVE if ld == "reserve" else ld
        if kind == 8:                               # (stage_title: the stage | its division << 2)
            wd, ld = po.get("stage"), 0
            sdv = {e.get("div", 0) for e in po.get("entrants", []) if isinstance(e, dict)}
            wd |= (sdv.pop() if sdv else 0) << 2
        flags = ((1 if po.get("skip_qualified") else 0) | (2 if po.get("host_by_record") else 0)
                 | ((win if win > 1 else 0) << 4) | (8 if po.get("draw_pot") else 0))
        out += bytes((kind, len(ents), nrd, wd, ld, flags))
        out += b"".join(bytes(e) for e in ents) + rounds
    return bytes(out + b"\0")


def compile_playoff(po, ndiv):
    """One play-off's bytes (compile_spec of one, without the end)."""
    return compile_spec([po], ndiv)[:-1]


SECOND_RULES = {             # the Mod's mkseason.SECOND_RULES: the top level a B team may reach
    "ita": (1, 1),          # (0xFF: any, the first team's always a bound), play-offs allowed
    "esp": (0xFF, 0), "ger": (2, 0), "ned": (1, 0), "por": (0xFF, 0),
}


def italian_playoffs(country, league, picked=None):
    """Italy's play-off rules: the Mod's counts per division, its Italian rules (dplay.c:
    Serie B's play-off and play-out, Serie C's play-off) and the pack's knockouts played
    at the season end (its groups' play-outs; Serie D's go to the pool below, not yet)."""
    mod = mod_league()
    ndiv = len(mod["divisions"])
    spec = compile_spec(mod.get("ko_playoffs", []), ndiv)
    counts["play-offs compiled"] += len(mod.get("ko_playoffs", []))
    seconds, top, flags = [], 0xFF, 0
    rules = (mod.get("second_teams") or {}).get("rules")
    if rules in SECOND_RULES and picked:
        top, po = SECOND_RULES[rules]
        flags = po | (2 if (mod.get("reserve") or {}).get("off_file") else 0)
        ordinal = {e.get("x_swos9697", {}).get("club_id"): o for o, (e, _) in enumerate(picked)}
        for o, (e, _) in enumerate(picked):
            first = e.get("x_swos9697", {}).get("second_of")
            if first:
                seconds.append((o, ordinal.get(first, 0xFF)))
        counts["B teams"] += len(seconds)
    return {"name": "Italy", "country": country, "divisions": ndiv,
            "seconds": seconds, "secondTop": top, "secondFlags": flags,
            "promoted": [d["promoted"] for d in mod["divisions"]],
            "relegated": [d["relegated"] for d in mod["divisions"]],
            "italy": mod.get("playoffs") == "ita", "serieB": 1, "groupFirst": 2,
            "groups": SERIE_C_GROUPS, "spec": spec}


CUP_ROUND = {"two_legs": 0x94, "two_legs_pens": 0x84, "single": 0x14, "single_et": 0x54}
# (the Mod's mkseason.ROUND, the bytes its DOS build plays: legs 7-6 -- 2 two legs, 1 a
#  single match at a ground, 0 a single match --, extra time 5-4, penalties 3-2)
LEAGUE_CUP_ID = 163         # a free competition id (include/comp_identity.h: 162..255)


def bracket_order(rows, rounds):
    """A cup's real draw (ties.csv: round, home, away; a bye has no away) as the engine plays
    a fixed bracket: from the final down, each tie the winners of the earlier round's two
    ties holding its clubs, its home club's first -- so tie k of each later round is the
    winners of ties 2k and 2k + 1 of the one before. Round 1's real ties are played first
    (the engine plays a round's first ties, the byes wait after them): `order1` gives each of
    round 1's ties, as played, its place in the bracket (world_cups.c puts the winners there
    after round 1). Returns (round 1's ties as played, the flips -- none: every later tie
    with its home club first --, the rounds the draw covers, order1)."""
    by_round = collections.defaultdict(list)
    for r in rows:
        by_round[int(r["round"])].append((r["home"], r["away"] or None))
    last = max(by_round)
    if len(by_round[last]) != 1 or sorted(by_round) != list(range(1, last + 1)):
        sys.exit("a cup's draw: rounds 1..%d, one tie in the last (%s)" % (last, sorted(by_round)))

    def holds(r, club):
        for i, t in enumerate(by_round[r]):
            if club in t:
                return i
        sys.exit("a cup's draw: %s not in round %d" % (club, r))

    def lay(r, i):
        if r == 1:
            return [i]
        h, a = by_round[r][i]
        return lay(r - 1, holds(r - 1, h)) + lay(r - 1, holds(r - 1, a))

    order = lay(last, 0)
    if sorted(order) != list(range(len(by_round[1]))):
        sys.exit("a cup's draw: round 1's ties do not make one bracket")
    real = [i for i in order if by_round[1][i][1]]
    played = real + [i for i in order if not by_round[1][i][1]]
    order1 = [order.index(i) for i in played]
    return [by_round[1][i] for i in played], [0] * (last - 1), last, order1


def italian_cups(picked, country):
    """Italy's cups as the Mod's pack has them (packs/ita: pack.json's cup and league_cup,
    ties.csv): the Coppa Italia from its round of 16, the Coppa Italia Serie C from its
    first round (one tie, the others' byes), each club list in the order of the real
    bracket. Returns their records and the world's table of them (src/world_cups.c)."""
    pack = json.load(open(os.path.join(MOD, "packs", "ita", "pack.json"), encoding="utf-8"))
    names = {r["key"]: r["name"] for r in csv.DictReader(open(os.path.join(MOD, "packs", "ita", "clubs.csv"),
                                                              encoding="utf-8"))}
    ordinal = {e["team"]["team_name"]: o for o, (e, _) in enumerate(picked)}
    division = {o: d for o, (_, d) in enumerate(picked)}
    rows = list(csv.DictReader(open(os.path.join(MOD, "packs", "ita", "ties.csv"), encoding="utf-8")))
    cups = json.load(open(os.path.join(BASE, "competitions", "cups.json"), encoding="utf-8"))
    base = next(c for c in cups["competitions"] if c["id"] == "_italianCup")
    out = []
    for key, slot in (("cup", 1), ("league_cup", 2)):
        spec = next(c for c in pack["competitions"] if c["key"] == key)
        ties, flips, rounds, order1 = bracket_order([r for r in rows if r["competition"] == key], len(spec["rounds"]))
        if rounds != len(spec["rounds"]):
            sys.exit("%s: %d rounds in the draw, %d in the pack" % (key, rounds, len(spec["rounds"])))
        entrants = [c for t in ties for c in t if c]
        size = 1 << (len(ties) * 2 - 1).bit_length()
        if size > 32 or len(entrants) != len(set(entrants)):
            sys.exit("%s: %d clubs in a bracket of %d" % (key, len(entrants), size))
        rec = copy.deepcopy(base)
        rec["comp"].update(team_list_offset=21, seed_reorder_flag=1)   # (the clubs as listed)
        if slot == 2:
            rec["id"] = "_italianLeagueCup"
            rec["comp"].update(comp_id=LEAGUE_CUP_ID - 256, end_month=16)  # (as England's: before the league's end)
            rec["name"] = spec["names"]["*"]
        rec.update(teams=size, away_goals_mode=0,
                   round_rule_flags=[{"legs": b >> 6, "extra_time": b >> 4 & 3, "penalties": b >> 2 & 3}
                                     for b in [CUP_ROUND[x] for x in spec["rounds"]] + [0] * (6 - rounds)],
                   team_seed_list=[{"country": country, "ordinal": ordinal[names[c]]} for c in entrants])
        real = sum(1 for t in ties if t[1])
        # From the second season (the Mod's 4.0.1, tools/cupsf.py): per division of the
        # real list, its clubs and how many of them wait for round 2
        byes = {c for t in ties if not t[1] for c in t if c}
        quota = collections.OrderedDict()
        for c in entrants:
            q = quota.setdefault(division[ordinal[names[c]]], [0, 0])
            q[0] += 1
            q[1] += c in byes
        out.append({"slot": slot, "record": rec, "n": len(entrants), "ties": real,
                    "europe": 0, "flips": flips, "order1": order1, "numbered": 0, "first_round": 1,
                    "quota": [(d, n, b) for d, (n, b) in sorted(quota.items())]})
        counts["%s: clubs (byes)" % key] += len(entrants)
        counts["%s: first round byes" % key] += len(ties) - real
    return out


# ---- Any country (phase 8: the whole world) ---------------------------------------------

# A cup a country has in the world and not in 96/97: its competition id, fixed (in saves:
# never by order). Free: 162..255 but the ones taken (include/comp_identity.h).
NEW_COMP_IDS = {
    ("BOL", "cup"): 164, ("CHI", "cup"): 165, ("CHI", "league_cup"): 166, ("GHA", "cup"): 167,
    ("IND", "cup"): 168, ("RSA", "league_cup"): 169, ("SUR", "cup"): 170, ("VEN", "cup"): 171,
    ("NZL", "cup"): 172, ("POR", "league_cup"): 173,
    # (the countries new to the game)
    ("AND", "cup"): 203, ("ARM", "cup"): 204, ("AZE", "cup"): 205, ("BIH", "cup"): 206, ("CAN", "cup"): 207,
    ("CHN", "cup"): 208, ("CRC", "cup"): 209, ("EGY", "cup"): 210, ("EGY", "league_cup"): 211,
    ("GEO", "cup"): 212, ("GIB", "cup"): 213, ("IRN", "cup"): 214, ("KAZ", "cup"): 215, ("KOR", "cup"): 216,
    ("KOS", "cup"): 217, ("KSA", "cup"): 218, ("LIE", "cup"): 219, ("MAR", "cup"): 220, ("MDA", "cup"): 221,
    ("MKD", "cup"): 222, ("MNE", "cup"): 223, ("QAT", "cup"): 224, ("QAT", "league_cup"): 225,
    ("TUN", "cup"): 226, ("UAE", "cup"): 227, ("UAE", "league_cup"): 228,
}
# A new country's league: an id of its own (fixed: saves hold it), its record's shape a 96/97
# country's of the same continent (its season's months), its continent's byte (the first of
# its _countriesTable name: 0 Europe, the cups' test) and region (the picker's 80..85).
NEW_LEAGUE_IDS = {
    "AND": 174, "ARM": 175, "AZE": 176, "BIH": 177, "CAN": 178, "CHN": 179, "CRC": 180, "EGY": 181,
    "GEO": 182, "GIB": 183, "GUA": 184, "HON": 185, "IRN": 186, "KAZ": 187, "KOR": 188, "KOS": 189,
    "KSA": 190, "LIE": 191, "MAR": 192, "MDA": 193, "MKD": 194, "MNE": 195, "PAN": 196, "QAT": 197,
    "TUN": 198, "UAE": 199,
}
CONTINENTS = {          # pack 'continent': (name byte, region, the 96/97 league its record is shaped on)
    "europe": (0, 80, "_albanianLeague"), "north_america": (1, 83, "_mexicanLeague"),
    "south_america": (2, 82, "_bolivianLeague"), "asia": (3, 84, "_japaneseLeague"),
    "oceania": (4, 85, "_newZealandLeague"), "africa": (5, 81, "_ghanaLeague"),
}
REGION_TABLES = {80: "_europeTable", 81: "_africaTable", 82: "_southAmericaTable",
                 83: "_northAmericaTable", 84: "_asiaTable", 85: "_oceaniaTable"}


def file_name(name):
    """A new country's team file name (teams/<it>.json): its name in capitals and _."""
    return re.sub(r"[^A-Z0-9]+", "_", name.upper()).strip("_")


def our_lists():
    """96/97's continent lists (the picker's: competition_data.c's QualifyingGroups) and its
    season end's countries (_careerCompetitionIdRemapTable), from the source."""
    src = open(os.path.join(REPO, "src", "competition_data.c"), encoding="utf-8").read()
    regions = {}
    for region, sym in REGION_TABLES.items():
        m = re.search(r"QualifyingGroup %s = \{.*?\},\s*\{([^}]*)\}" % sym, src, re.S)
        regions[region] = [int(x) for x in m.group(1).replace(" ", "").split(",") if x and int(x) != 255]
    m = re.search(r"_careerCompetitionIdRemapTable\[\d+\] = \{([^}]*)\}", src, re.S)
    season = [int(x) for x in m.group(1).split(",") if x.strip() and int(x) != 255]
    return regions, season


def world_lists(news):
    """The world's continent lists: 96/97's with each new country put in by its name (the
    lists are in name order), and its season end's countries: 96/97's, the new ones after
    (the old ones' order, and so their seasons, as they were)."""
    regions, season = our_lists()
    names = {c["number"]: c["name"].replace("_", " ") for c in json.load(open(os.path.join(
        BASE, "competitions", "countries.json"), encoding="utf-8"))["countries"]}
    for wc in news:
        lst = regions[wc["region"]]
        at = next((i for i, n in enumerate(lst) if names.get(n, "") > wc["name"]), len(lst))
        lst.insert(at, wc["number"])
        names[wc["number"]] = wc["name"]
        season.append(wc["number"])
    return regions, season



def mod_pack(code):
    return json.load(open(os.path.join(MOD, "packs", code.lower(), "pack.json"), encoding="utf-8"))


def pack_league(pk):
    return next(c for c in pk["competitions"] if c["key"] == "league")


def text_names(names):
    """A pack's names {lang: [long, short]} -> [long, short] (the game's English)."""
    n = names.get("*") or names.get("en") or next(iter(names.values()))
    return [n[0], n[1] if len(n) > 1 else n[0]]


def our_records(kind):
    path = os.path.join(BASE, "competitions", kind + ".json")
    d = json.load(open(path, encoding="utf-8"))
    return {r["id"]: r for r in d["leagues" if kind == "leagues" else "competitions"]}


def our_country_table(number):
    for c in json.load(open(os.path.join(BASE, "competitions", "country_competitions.json"),
                            encoding="utf-8"))["countries"]:
        if c["number"] == number:
            return c
    return {}


def country_league(wc, pk, picked):
    """The country's 96/97 league record as the pack's pyramid: its divisions' clubs, the
    Mod's promotion and relegation counts (its play-offs add the rest: league_playoffs.c),
    the real names; no 96/97 play-off links (the qualifier offsets 0: the world's play-offs
    are the pack's)."""
    lg = pack_league(pk)
    if wc["new"]:
        rec = copy.deepcopy(our_records("leagues")[CONTINENTS[pk["continent"]][2]])
        rec["id"] = "_world%sLeague" % wc["code"].title()
        rec["new"] = {"country": wc["number"], "name": wc["name"], "byte": CONTINENTS[pk["continent"]][0]}
        rec["comp"].update(comp_id=NEW_LEAGUE_IDS[wc["code"]], team_number=wc["number"])
        rec["playoff_links"], rec["promotion_link_data"] = [], []
    else:
        rec = copy.deepcopy(our_records("leagues")[our_country_table(wc["number"])["leagues"][0]])
    mdivs = lg.get("divisions") or [{"teams": len(picked), "promoted": 0, "relegated": 0,
                                    "names": lg["names"]}]
    sizes = collections.Counter(d for _, d in picked)
    template = rec["divisions"][0]
    rec["divisions"], names = [], []
    for i, d in enumerate(mdivs):
        if sizes[i] != d["teams"]:
            sys.exit("%s: division %d has %d clubs in the file, %d in the pack" % (wc["code"], i, sizes[i], d["teams"]))
        # BALANCED counts (the original's season end checks each division's moves sum to
        # zero, DIY_ValidateLeagueScratchBuffer): as many up from it as come down from the
        # one above, as Italy's; the Mod's own counts (the play-offs add the rest) are its
        # play-off rules' (country_playoffs, league_playoffs.c)
        rec["divisions"].append(dict(template, teams=d["teams"],
                                     promoted=mdivs[i - 1].get("relegated", 0) if i else 0,
                                     relegated=d.get("relegated", 0) if i + 1 < len(mdivs) else 0,
                                     promoted_po=0, relegated_po=0, qualifier_link_offset=0))
        names += text_names(d.get("names") or lg["names"])
    rec["name"] = names
    rec["comp"]["name_table_offset"] = 33               # (the league layout's: names follow)
    rec["divisions_count"] = len(rec["divisions"])
    rec["rounds"] = lg.get("games", rec["rounds"])
    rec["points_for_win"] = lg.get("win_points", rec["points_for_win"])
    return rec


def country_playoffs(wc, pk, picked):
    """A country's play-off rules (as italian_playoffs, any country): the Mod's counts per
    division, the pack's knockouts compiled, its B teams' rules."""
    lg = pack_league(pk)
    mdivs = lg.get("divisions") or [{"teams": len(picked), "promoted": 0, "relegated": 0}]
    ndiv = len(mdivs)
    spec = compile_spec(lg.get("ko_playoffs", []), ndiv)
    counts["play-offs compiled"] += len(lg.get("ko_playoffs", []))
    seconds, top, flags = [], 0xFF, 0
    rules = (lg.get("second_teams") or {}).get("rules")
    if rules in SECOND_RULES:
        top, po = SECOND_RULES[rules]
        flags = po | (2 if (lg.get("reserve") or {}).get("off_file") else 0)
        ordinal = {e.get("x_swos9697", {}).get("club_id"): o for o, (e, _) in enumerate(picked)}
        for o, (e, _) in enumerate(picked):
            first = e.get("x_swos9697", {}).get("second_of")
            if first:
                seconds.append((o, ordinal.get(first, 0xFF)))
        counts["B teams"] += len(seconds)
    if spec == b"\0" and not seconds:
        return None
    return {"name": wc["name"].title().replace(" ", ""), "country": wc["number"], "divisions": ndiv,
            "seconds": seconds, "secondTop": top, "secondFlags": flags,
            "promoted": [d.get("promoted", 0) for d in mdivs], "relegated": [d.get("relegated", 0) for d in mdivs],
            "italy": 0, "serieB": 0, "groupFirst": 0, "groups": 0, "spec": spec}


def country_cups(wc, pk, picked):
    """A country's national cup (slot 1) and league cup (slot 2) as its pack has them (as
    italian_cups, any country): its 96/97 records reshaped, or new ones."""
    rows = []
    tp = os.path.join(MOD, "packs", wc["code"].lower(), "ties.csv")
    if os.path.exists(tp):
        rows = list(csv.DictReader(open(tp, encoding="utf-8")))
    names = {r["key"]: r["name"] for r in csv.DictReader(open(os.path.join(MOD, "packs", wc["code"].lower(),
                                                                           "clubs.csv"), encoding="utf-8"))}
    ordinal = {e["team"]["team_name"]: o for o, (e, _) in enumerate(picked)}
    division = {o: d for o, (_, d) in enumerate(picked)}
    ours = our_records("cups")
    table = our_country_table(wc["number"]).get("cups", [])
    template = ours["_italianCup"]
    out = []
    for key, slot in (("cup", 1), ("league_cup", 2)):
        if pk.get("season", {}).get(key) != key:
            continue
        spec = next(c for c in pk["competitions"] if c["key"] == key)
        cup_rows = [r for r in rows if r["competition"] == key]
        if not cup_rows:
            counts["cups without a draw (96/97's kept)"] += 1
            continue
        try:
            ties, flips, rounds, order1 = bracket_order(cup_rows, len(spec["rounds"]))
        except SystemExit as e:
            counts["cups whose draw is no bracket (96/97's kept)"] += 1
            print("  %s %s: %s" % (wc["code"], key, e))
            continue
        entrants = [c for t in ties for c in t if c]
        if any(names.get(c) not in ordinal for c in entrants):
            counts["cups with a club outside the file (96/97's kept)"] += 1
            continue
        size = 1 << (len(ties) * 2 - 1).bit_length()
        if rounds != len(spec["rounds"]) or size > 64 or len(entrants) != len(set(entrants)):
            counts["cups of another shape (96/97's kept)"] += 1
            continue
        if len(table) >= slot:
            rec = copy.deepcopy(ours[table[slot - 1]])
        else:
            if (wc["code"], key) not in NEW_COMP_IDS:
                sys.exit("%s %s: a new cup: give it an id (NEW_COMP_IDS)" % (wc["code"], key))
            rec = copy.deepcopy(template)
            rec["id"] = "_world%s%s" % (wc["code"].title(), "LeagueCup" if slot == 2 else "Cup")
            rec["comp"].update(comp_id=NEW_COMP_IDS[(wc["code"], key)] - 256, team_number=wc["number"])
            if slot == 2:
                rec["comp"]["end_month"] = 16           # (as England's: before the league's end)
        rec["comp"].update(team_list_offset=21, seed_reorder_flag=1)
        rec["name"] = text_names(spec["names"])
        rec.update(teams=size, away_goals_mode=0,
                   round_rule_flags=[{"legs": b >> 6, "extra_time": b >> 4 & 3, "penalties": b >> 2 & 3}
                                     for b in [CUP_ROUND[x] for x in spec["rounds"]] + [0] * (6 - rounds)],
                   team_seed_list=[{"country": wc["number"], "ordinal": ordinal[names[c]]} for c in entrants])
        real = sum(1 for t in ties if t[1])
        byes = {c for t in ties if not t[1] for c in t if c}
        quota = collections.OrderedDict()
        for c in entrants:
            q = quota.setdefault(division[ordinal[names[c]]], [0, 0])
            q[0] += 1
            q[1] += c in byes
        out.append({"slot": slot, "record": rec, "n": len(entrants), "ties": real,
                    "europe": int(bool(slot == 2 and pk.get("season", {}).get("league_cup_europe"))),
                    "flips": flips, "order1": order1,
                    "quota": [(d, n, b) for d, (n, b) in sorted(quota.items())],
                    "numbered": int(spec.get("round_names") == "numbered"),
                    "first_round": spec.get("first_round", 1)})
        counts["cups"] += 1
    return out


MONTH_DAYS = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)
CUPD_HOST = {"listed": 0, "drawn": 2, "alternate": 4}
CUPD_DRAWN = 0xFEFF
CUPD_MAXT = 168             # (gplay.c CDT)


def week_of(date):
    """'dd/mm' (or 'dd/mm+1': the next calendar year) -> the career calendar's week: 8 a
    month from January of the season's first year, 96 a year (the Mod's split.week_of; the
    competitions' begin and end bytes, the calendar's keys)."""
    d, m = date.split("+")[0].split("/")
    y = int(date.split("+")[1]) if "+" in date else 0
    d, m = int(d), int(m)
    return 96 * y + 8 * (m - 1) + (d - 1) * 8 // MONTH_DAYS[m - 1]


def cupd_table(d, word):
    """The Coppa Italia Serie D's rounds and ties (the Mod's tools/cupd.py compile_table, as
    it is): per round its weeks, legs, host rule (0 listed, 2 drawn, 4 alternate), ties,
    flags (1 drawn pairs, 2 penalties without extra time) and first tie; per tie its sides --
    a team word (file | ordinal << 8), a previous round's tie (index << 8 | FFh), drawn
    (FEFFh), 0 a club outside the file -- and its real winner (1, 2; 0 unknown)."""
    def ref(i):
        assert 0 <= i < 0xFE, i
        return i << 8 | 0xFF

    def weeks(date):
        ds = [x for x in str(date).split() if "/" in x and x[0].isdigit()]
        ws = [week_of(x.strip(",")) for x in ds[:2]]
        return ws + [0] * (2 - len(ws))

    rounds, ties, nprev = [], [], 0
    for ri, r in enumerate(d["rounds"]):
        ms = list(r.get("matches") or [])
        w1, w2 = weeks(r.get("date", ""))
        legs = r.get("legs", 1)
        draw = r.get("pairing") == "draw"
        n = len(ms) if ri < 2 else nprev // 2       # (rounds 0-1 as listed, then halving)
        first, used = len(ties), set()
        for m in ms:
            for side in ("home", "away"):
                f = m.get(side + "_from")
                if f:
                    used.add(int(f[1:]))
        free = [i for i in range(nprev) if i not in used] if ri else []
        for k in range(n):
            if k < len(ms) and not draw:
                m = ms[k]
                ab = []
                for side in ("home", "away"):
                    f, key = m.get(side + "_from"), m.get(side + "_key")
                    ab.append(ref(int(f[1:]) + (first - nprev)) if f and ri else (word(key) or 0))
                win = 0
                if m.get("played") and m.get("winner") is not None:
                    win = 1 if m["winner"] == m.get("home") else 2 if m["winner"] == m.get("away") else 0
                ties.append((ab[0], ab[1], win))
            else:                   # (a tie the source leaves out / a drawn round)
                if draw:
                    a = b = CUPD_DRAWN
                else:
                    a, b = ref(free.pop(0) + first - nprev), ref(free.pop(0) + first - nprev)
                ties.append((a, b, 0))
        if r.get("legs", 1) == 2 and not w2:
            w2 = w1 + 2
        flags = (1 if draw else 0) | (2 if "pens" in str(r.get("rule", "pens")) else 0)
        rounds.append((w1, w2 if legs == 2 else 0, legs, CUPD_HOST.get(r.get("host", "drawn"), 2), n, flags, first))
        nprev = n
    assert len(ties) <= CUPD_MAXT, len(ties)
    return rounds, ties


def italian_cupd(picked, country, pool_ids):
    """The Coppa Italia Serie D (the pack's cupd.json, the Mod's gplay.c cd_*): its table, the
    Serie D level (first division, groups), each ordinal's base division, and the strengths of
    the ties nobody plays (the best 11 players' prices, players.csv: the file's 256 ordinals,
    then the pool's clubs in its order)."""
    pk = os.path.join(MOD, "packs", "ita")
    d = json.load(open(os.path.join(pk, "cupd.json"), encoding="utf-8"))
    clubs = list(csv.DictReader(open(os.path.join(pk, "clubs.csv"), encoding="utf-8")))
    name_of = {r["key"]: r["name"] for r in clubs}
    ordinal = {e["team"]["team_name"]: o for o, (e, _) in enumerate(picked)}

    def word(key):              # (a club of the league's file; the pool's are off-file: 0)
        if not key or key not in name_of or name_of[key] not in ordinal:
            return None
        return country | ordinal[name_of[key]] << 8
    rounds, ties = cupd_table(d, word)
    prices = collections.defaultdict(list)
    for r in csv.DictReader(open(os.path.join(pk, "players.csv"), encoding="utf-8")):
        prices[r["club"]].append(int(r.get("price") or 0))

    def strength(key):
        return max(5, min(0xFFFF, sum(sorted(prices.get(key, []), reverse=True)[:11])))
    key_of = {name_of[k]: k for k in name_of}
    st = [5] * 256
    for o, (e, _) in enumerate(picked):
        k = key_of.get(e["team"]["team_name"])
        if k:
            st[o] = strength(k)
    pool_names = [t["team"]["team_name"] for t in json.load(open(os.path.join(MOD, "teams", "ITA_POOL.json"),
                                                                   encoding="utf-8"))["teams"]]
    pst = [strength(key_of[n]) if n in key_of else 5 for n in pool_names]
    mod = mod_league()
    par = mod["parallel"]
    df, dc = par[-1]["first"], par[-1]["count"]
    base = [div for _, div in picked] + [0xFF] * (256 - len(picked))
    nd = sum(1 for b in base if df <= b < df + dc)
    assert 2 << (len(rounds) - 2) < nd <= (2 << (len(rounds) - 2)) + 41, (nd, len(rounds))
    names = (d.get("names") or {}).get("*") or ["COPPA ITALIA SERIE D", "COPPA SERIE D"]
    counts["Coppa Italia Serie D: rounds, ties"] = len(rounds) * 1000 + len(ties)
    return {"country": country, "rounds": rounds, "ties": ties, "first": df, "groups": dc, "base": base,
            "strength": st, "pool_strength": pst, "names": names}


def team_file_binary(teams):
    """The binary TEAM.nnn of a team file, as scripts/emit_teams.py makes 96/97's."""
    return tf.dict_filename(teams), tf.rnc_pack(tf.dict_to_blob(teams), tf.dict_rnc_method(teams))



WORLD_DATA_C = """/*
 * world_data.c -- the worlds' baked competition records (include/world.h).
 *
 * GENERATED by scripts/world_import.py from data_json/worlds/<tag>/: edit
 * the JSON (or the script), not this. World_InstallBaked copies each world's records over
 * 96/97's compiled ones at start-up, on both targets (frontend.c) -- the Amiga has no
 * JSON -- keeping each league's compiled playoff plumbing, as the SDL JSON install does.
 */
#include "game_types.h"
#include "competition_data.h"
#include "world.h"
#include "league_groups.h"
#include "league_playoffs.h"
#include "team_pool.h"
#include "fixture_rounds.h"
#include "world_cups.h"
#include "world_cupd.h"

%(externs)s

%(records)s

/* The countries new to the game (96/97 has no clubs there): each one's name (its first
 * byte its continent's: 0 Europe), its table (its league; its cups go in with the world's,
 * world_cups.c); the world's continent lists (the career picker's: 96/97's with the new
 * countries in, by name) and its season end's countries (96/97's order, the new ones
 * after). */
%(countries)s

const uint8_t *World_ContinentCountries(uint8_t region)
{
    switch (World_Current()) {
%(region_cases)s
    default:
        return 0;
    }
}

const uint8_t *World_SeasonCountries(void)
{
    switch (World_Current()) {
%(season_cases)s
    default:
        return 0;
    }
}

const char *World_CountryTeamFile(uint8_t country)
{
    switch (World_Current()) {
%(file_cases)s
    default:
        return 0;
    }
}

/* A country's range of global team numbers (base + ordinal, _teamsCountryNumbers) sized
 * to the world's clubs: every later country's base moves by the difference. The league
 * state table holds GLOBAL_TEAM_NUMBERS; past it is a broken world, said so. */
extern uint16_t _teamsCountryNumbers[256];
extern volatile uint16_t _panicReasonCode;
extern void PanicHaltLoop(void);

static void resize_country(uint16_t country, uint16_t teams)
{
    const int16_t delta = (int16_t)(teams - (_teamsCountryNumbers[country + 1] -
                                             _teamsCountryNumbers[country]));
    for (uint16_t i = country + 1; i < 256; i++)
        _teamsCountryNumbers[i] = (uint16_t)(_teamsCountryNumbers[i] + delta);
    if (_teamsCountryNumbers[255] > GLOBAL_TEAM_NUMBERS) {
        _panicReasonCode = 0x8F02;
        PanicHaltLoop();
    }
}

static void install_league(CompetitionLeague *dst, const CompetitionLeague *src)
{
    char *p0 = dst->playoffLinkPtrs[0], *p1 = dst->playoffLinkPtrs[1];
    *dst = *src;
    dst->playoffLinkPtrs[0] = p0;
    dst->playoffLinkPtrs[1] = p1;
}

void World_InstallBaked(void)
{
    switch (World_Current()) {
%(cases)s
    default:
        break;
    }
}

/* The leagues with groups side by side (src/league_groups.c): each one's levels, and its
 * clubs with their division in the team file and their area at each level of groups. */
%(groups)s

const LeagueGroups *World_LeagueGroups(int32_t *count)
{
    switch (World_Current()) {
%(group_cases)s
    default:
        *count = 0;
        return 0;
    }
}

/* The leagues' play-offs and play-outs (src/league_playoffs.c): the Mod's counts per
 * division, its Italian rules, its knockouts compiled as tools/gplay.py compiles them. */
%(playoffs)s

const LeaguePlayoffs *World_LeaguePlayoffs(int32_t *count)
{
    switch (World_Current()) {
%(po_cases)s
    default:
        *count = 0;
        return 0;
    }
}

/* The off-file pools (src/team_pool.c): the team file each serves, its own file, its
 * clubs' areas at the levels of groups. */
%(pools)s

const TeamPool *World_TeamPools(int32_t *count)
{
    switch (World_Current()) {
%(pool_cases)s
    default:
        *count = 0;
        return 0;
    }
}

/* The national cups (src/world_cups.c): their records, the clubs in the real bracket's
 * order, round 1's real ties and the later rounds' ties played the other way round; from
 * the second season, per division its clubs and how many of them have a bye. */
%(cups)s

const WorldCup *World_Cups(int32_t *count)
{
    switch (World_Current()) {
%(cup_cases)s
    default:
        *count = 0;
        return 0;
    }
}

/* The Coppa Italia Serie D (src/world_cupd.c): its rounds and ties as the Mod's cupd.py
 * compiles them, the Serie D level, each ordinal's base division, the strengths. */
%(cupd)s

const WorldCupD *World_CupD(void)
{
    switch (World_Current()) {
%(cupd_cases)s
    default:
        return 0;
    }
}
"""


def c_string(text):
    return '"%s"' % text.replace("\\", "\\\\").replace('"', '\\"')


def world_data_c(tag, leagues, resized, groups, playoffs, pools, cups, cupd, news=()):
    externs, strings, records, installs, new_countries = set(), [], [], [], []
    symbols = {}

    def symbol(text):
        if text not in symbols:
            symbols[text] = "kWorld%s_name%d" % (tag, len(symbols))
        return symbols[text]

    for country, n in resized:
        installs.append("        resize_country(%d, %d);" % (country, n))
    for rec in leagues["leagues"]:
        names = []
        for n in rec["name"]:
            if n and not n.startswith("_"):          # display text, not a string symbol
                if n not in symbols:
                    strings.append("static char %s[] = %s;" % (symbol(n), c_string(n)))
                n = symbols[n]
            elif n:
                externs.add("extern char %s[];" % n)
            names.append(n)
        baked = dict(rec, id="kWorld%s%s" % (tag, rec["id"]), name=names)
        records.append("static " + emit_competitions.emit_league(baked))
        if rec.get("new"):      # (a country new to the game: its own table, its name)
            nw = rec["new"]
            name_sym, table_sym = "kWorld%s_country%d" % (tag, nw["country"]), "kWorld%s%sTable" % (tag, rec["id"])
            new_countries.append("static const char %s[] = \"\\%03o\" %s \"\\0\" %s;" % (
                name_sym, nw["byte"], c_string(nw["name"]), c_string(nw["name"])))
            new_countries.append("static Competition *%s[] = { (Competition *)&%s, (Competition *)-2, "
                                 "(Competition *)-1 };" % (table_sym, baked["id"]))
            installs.append("        _countriesTable[%d] = (void *)%s;   /* %s */" % (nw["country"], name_sym, nw["name"]))
            installs.append("        _competitionsTable[%d] = %s;" % (nw["country"], table_sym))
            installs.append("        Comp_Register((Competition *)&%s);" % baked["id"])
            continue
        externs.add("extern CompetitionLeague %s;" % rec["id"])
        installs.append("        install_league(&%s, &%s);" % (rec["id"], baked["id"]))
    installs.append("        FixtureRounds_Install();")
    regions, season = world_lists(news)
    lists = []
    for region in sorted(regions):
        lists.append("static const uint8_t kWorld%s_region%d[] = { %s, 255 };" % (
            tag, region, ", ".join(map(str, regions[region]))))
    lists.append("static const uint8_t kWorld%sSeasonCountries[] = { %s, 255 };" % (
        tag, ", ".join(map(str, season))))
    region_cases = ("    case WORLD_%s:\n        return region >= 80 && region <= 85 ? kWorld%sRegions[region - 80] : 0;"
                    % (tag, tag))
    lists.append("static const uint8_t *const kWorld%sRegions[6] = { %s };" % (
        tag, ", ".join("kWorld%s_region%d" % (tag, r) for r in range(80, 86))))
    season_cases = "    case WORLD_%s:\n        return kWorld%sSeasonCountries;" % (tag, tag)
    file_cases = "    case WORLD_%s:\n        switch (country) {\n%s\n        default:\n            return 0;\n        }" % (
        tag, "\n".join('        case %d:\n            return "teams/%s.json";' % (wc["number"], wc["base"])
                       for wc in news))
    cases = "    case WORLD_%s:\n%s\n        break;" % (tag, "\n".join(installs))
    group_tables, group_entries = [], []
    for g in groups:
        sym = "kWorld%s_%s" % (tag, g["name"].lower())
        rows = []
        for first, n, sizes, name in g["levels"]:
            if name:
                strings.append("static char %s[] = %s;" % (symbol(name), c_string(name)))
            rows.append("    { %d, %d, { %s }, %s }," % (first, n, ", ".join(map(str, sizes)),
                                                       symbol(name) if name else "0"))
        group_tables.append("static const LeagueLevel %sLevels[] = {\n%s\n};" % (sym, "\n".join(rows)))
        rows = ["    { %d, %d, { %s } }," % (o, b, ", ".join(
            "LEAGUE_GROUPS_NO_AREA" if x is None else str(x) for x in a)) for o, b, a in g["clubs"]]
        group_tables.append("static const LeagueGroupClub %sClubs[] = {\n%s\n};" % (sym, "\n".join(rows)))
        group_entries.append("    { %d, %d, %sLevels, %d, %sClubs },   /* %s */" % (
            g["country"], len(g["levels"]), sym, len(g["clubs"]), sym, g["name"]))
    group_tables.append("static const LeagueGroups kWorld%sGroups[] = {\n%s\n};" % (
        tag, "\n".join(group_entries)))
    group_cases = ("    case WORLD_%s:\n        *count = %d;\n        return kWorld%sGroups;"
                   % (tag, len(groups), tag))
    po_tables, po_entries = [], []
    for po in playoffs:
        sym = "kWorld%s_%s" % (tag, po["name"].lower())
        po_tables.append("static const uint8_t %sPromoted[] = { %s };" % (sym, ", ".join(map(str, po["promoted"]))))
        po_tables.append("static const uint8_t %sRelegated[] = { %s };" % (sym, ", ".join(map(str, po["relegated"]))))
        spec = po["spec"]
        po_tables.append("static const uint8_t %sSpec[%d] = {\n%s\n};" % (sym, len(spec), "\n".join(
            "    " + ", ".join("0x%02X" % b for b in spec[i:i + 16]) + "," for i in range(0, len(spec), 16))))
        po_tables.append("static const uint8_t %sSeconds[][2] = {\n%s\n};" % (sym, "\n".join(
            "    { %d, %d }," % (b, f) for b, f in po["seconds"]) or "    { 0, 0 },"))
        po_entries.append("    { %d, %d, %sPromoted, %sRelegated, %d, %d, %d, %d, %sSpec, %d, %sSeconds, %d, %d },"
                          "   /* %s */" % (
            po["country"], po["divisions"], sym, sym, int(po["italy"]), po["serieB"], po["groupFirst"],
            po["groups"], sym, len(po["seconds"]), sym, po["secondTop"], po["secondFlags"], po["name"]))
    po_tables.append("static const LeaguePlayoffs kWorld%sPlayoffs[] = {\n%s\n};" % (tag, "\n".join(po_entries)))
    po_cases = ("    case WORLD_%s:\n        *count = %d;\n        return kWorld%sPlayoffs;"
                % (tag, len(playoffs), tag))
    pool_tables, pool_entries = [], []
    for pl in pools:
        sym = "kWorld%s_%sPool" % (tag, pl["name"].lower())
        pool_tables.append("static const uint8_t %sAreas[][2] = {\n%s\n};" % (sym, "\n".join(
            "    { %s }," % ", ".join("LEAGUE_GROUPS_NO_AREA" if a is None else str(a) for a in ar)
            for ar in pl["areas"])))
        pool_entries.append('    { %d, %d, "%s", %d, %sAreas },   /* %s */' % (
            pl["country"], pl["file"], pl["json"], pl["count"], sym, pl["name"]))
    pool_tables.append("static const TeamPool kWorld%sPools[] = {\n%s\n};" % (tag, "\n".join(pool_entries)))
    pool_cases = ("    case WORLD_%s:\n        *count = %d;\n        return kWorld%sPools;"
                  % (tag, len(pools), tag))
    cup_tables, cup_entries = [], []
    for c in cups:
        rec = c["record"]
        names = []
        for n in rec["name"]:
            if n and not n.startswith("_"):
                if n not in symbols:
                    strings.append("static char %s[] = %s;" % (symbol(n), c_string(n)))
                n = symbols[n]
            elif n:
                externs.add("extern char %s[];" % n)
            names.append(n)
        sym = "kWorld%s%s" % (tag, rec["id"])
        seeds = rec["team_seed_list"]
        if len(seeds) > 32:                 # (a list of more than the record's 32: carried after it)
            body = emit_competitions.emit_cup(dict(rec, id="rec", name=names, team_seed_list=seeds[:32]))
            more = ",\n".join("        { %d, %d }" % (x["country"], x["ordinal"]) for x in seeds[32:])
            cup_tables.append("static struct __attribute__((packed)) { CompetitionCup rec; TeamId more[32]; } %s = {\n"
                              "    %s,\n    {\n%s\n    }\n};" % (
                sym + "Ext", body.split(" = ", 1)[1].rstrip(";").replace("\n", "\n    "), more))
            cup_tables.append("#define %s (%sExt.rec)" % (sym, sym))
        else:
            cup_tables.append("static " + emit_competitions.emit_cup(dict(rec, id=sym, name=names)))
        cup_tables.append("static const uint16_t %sFlips[] = { %s };" % (
            sym, ", ".join(map(str, c["flips"])) or "0"))
        cup_tables.append("static const uint8_t %sOrder1[] = { %s };" % (
            sym, ", ".join(map(str, c.get("order1") or [0]))))
        cup_tables.append("static const uint8_t %sQuota[][3] = { %s };" % (
            sym, ", ".join("{ %d, %d, %d }" % q for q in c["quota"])))
        cup_entries.append("    { %d, %d, &%s, %d, %d, %d, %d, %sFlips, %d, %sQuota, %s, %d, %d },   /* %s */" % (
            rec["comp"]["team_number"], c["slot"], sym, c["n"], c["ties"], c["europe"],
            len(c["flips"]) + 1, sym, len(c["quota"]), sym, sym + "Order1" if c.get("order1") else "0",
            c.get("numbered", 0), c.get("first_round", 1), rec["id"]))
    cup_tables.append("static const WorldCup kWorld%sCups[] = {\n%s\n};" % (tag, "\n".join(cup_entries)))
    cup_cases = ("    case WORLD_%s:\n        *count = %d;\n        return kWorld%sCups;"
                 % (tag, len(cups), tag))
    cd_tables, cd_cases = [], ""
    if cupd:
        sym = "kWorld%sCupD" % tag
        names = []
        for n in cupd["names"]:
            if n not in symbols:
                strings.append("static char %s[] = %s;" % (symbol(n), c_string(n)))
            names.append(symbols[n])

        def rows(data, per):
            return "\n".join("    " + ", ".join(map(str, data[i:i + per])) + "," for i in range(0, len(data), per))
        rb = []
        for w1, w2, legs, host, n, flags, first in cupd["rounds"]:
            rb.append("    { %d, %d, %d, %d, %d, %d, %d, %d }," % (w1, w2, legs, host, n, flags, first & 0xFF, first >> 8))
        cd_tables.append("static const uint8_t %sRounds[][8] = {\n%s\n};" % (sym, "\n".join(rb)))
        tb = []
        for a, b, win in cupd["ties"]:
            tb.append("    { %d, %d, %d, %d, %d, 0 }," % (a & 0xFF, a >> 8, b & 0xFF, b >> 8, win))
        cd_tables.append("static const uint8_t %sTies[][6] = {\n%s\n};" % (sym, "\n".join(tb)))
        cd_tables.append("static const uint8_t %sBase[256] = {\n%s\n};" % (sym, rows(cupd["base"], 32)))
        cd_tables.append("static const uint16_t %sStrength[256] = {\n%s\n};" % (sym, rows(cupd["strength"], 16)))
        cd_tables.append("static const uint16_t %sPoolStrength[] = {\n%s\n};" % (sym, rows(cupd["pool_strength"], 16)))
        cd_tables.append("static const WorldCupD %s = {\n    %d, %d, %sRounds, %d, %sTies, %d, %d, %sBase, %sStrength,\n"
                         "    %d, %sPoolStrength, { %s, %s }\n};" % (
            sym, cupd["country"], len(cupd["rounds"]), sym, len(cupd["ties"]), sym, cupd["first"], cupd["groups"],
            sym, sym, len(cupd["pool_strength"]), sym, names[0], names[1]))
        cd_cases = "    case WORLD_%s:\n        return &%s;" % (tag, sym)
    if news:
        externs.update(("extern void *_countriesTable[262];", "extern void *_competitionsTable[256];"))
    return WORLD_DATA_C % {"countries": "\n".join(new_countries + lists), "region_cases": region_cases,
                           "season_cases": season_cases, "file_cases": file_cases,
                           "externs": "\n".join(sorted(externs)),
                           "records": "\n\n".join(strings + records), "cases": cases,
                           "groups": "\n\n".join(group_tables), "group_cases": group_cases,
                           "playoffs": "\n\n".join(po_tables), "po_cases": po_cases,
                           "pools": "\n\n".join(pool_tables), "pool_cases": pool_cases,
                           "cups": "\n\n".join(cup_tables), "cup_cases": cup_cases,
                           "cupd": "\n\n".join(cd_tables), "cupd_cases": cd_cases}


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("wrote", os.path.relpath(path, REPO))


# The countries imported so far (docs/WORLD_2026_27.md, phase 8): Italy whole (its own
# functions), then the plain countries 96/97 has -- their divisions, the pack's knockouts,
# their cups (the others keep 96/97's until their league's features are in).
PLAIN = ("ALG BEL BLR BOL CHI CRO ENG EST FRA FRO GHA HUN IND IRL JPN LTU LUX LVA NOR POL RSA "
         "RUS SCO SUR SVN SWE TPE TUR UKR VEN "
         # (B teams, an off-file pool below the lowest division: src/team_pool.c)
         "ESP GER NED POR NZL").split()


def write_team_file(teams, json_name):
    write(os.path.join(WORLD, "teams", json_name + ".json"), teams)
    name, packed = team_file_binary(teams)
    path = os.path.join(WORLD, "data", name.upper())
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(packed)


def main():
    if not os.path.isdir(MOD):
        sys.exit("no %s: the Mod's data (Davide's private copies) is not here" % os.path.relpath(MOD, REPO))
    (teams, picked), leagues = italy(), italian_league()
    write_team_file(teams, "ITALY")
    country = teams["teams"][0]["team"]["country_number"]
    pools, news = [], []

    def add_pool(code, name, number, base):     # (home: as country_teams, 96/97's file's)
        home = home_nation(json.load(open(os.path.join(BASE, "teams", base + ".json"), encoding="utf-8")))
        pool, info = country_pool(code, name, number, home)
        if pool:
            write(os.path.join(WORLD, "teams", info["json"]), pool)
            pname, packed = team_file_binary(pool)
            open(os.path.join(WORLD, "data", pname.upper()), "wb").write(packed)
            pools.append(info)
        return info
    pool_info = add_pool("ITA", "Italy", country, "ITALY")
    resized = [(country, teams["count"])]
    playoffs = [italian_playoffs(country, leagues, picked)]
    cups = italian_cups(picked, country)
    for wc in world_countries():
        if wc["code"] not in PLAIN and not wc["new"]:
            continue
        pk = mod_pack(wc["code"])
        t, pk_picked = country_teams(wc)
        if wc["new"]:           # (its team file's name, after country_teams: 96/97 has none)
            wc["base"] = file_name(wc["name"])
            wc["region"] = CONTINENTS[pk["continent"]][1]
            news.append(wc)
        write_team_file(t, wc["base"])
        leagues["leagues"].append(country_league(wc, pk, pk_picked))
        po = country_playoffs(wc, pk, pk_picked)
        if po:
            playoffs.append(po)
        cups += country_cups(wc, pk, pk_picked)
        if not wc["new"]:
            add_pool(wc["code"], wc["name"].title().replace(" ", ""), wc["number"], wc["base"])
        resized.append((wc["number"], t["count"]))
        counts["countries imported"] += 1
    for rec in leagues["leagues"]:          # (the world's play-offs are the pack's: no 96/97 links)
        if rec["id"] != "_italianLeague":
            rec.pop("playoff_links", None)
            rec.pop("promotion_link_data", None)
    leagues["count"] = len(leagues["leagues"])
    write(os.path.join(WORLD, "competitions", "leagues.json"), leagues)
    with open(os.path.join(REPO, "src", "world_data.c"), "w") as f:
        f.write(world_data_c("2627", leagues, resized,
                             [italian_groups(picked, country, leagues)],
                             playoffs, pools, cups, italian_cupd(picked, country, None), news))
    print("wrote src/world_data.c")
    for what, n in sorted(counts.items()):
        print("  %-52s %d" % (what, n))


if __name__ == "__main__":
    main()
