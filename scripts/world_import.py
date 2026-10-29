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
    mod = json.load(open(os.path.join(MOD, "teams", wc["mod"]["json"] + ".json"), encoding="utf-8"))
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


POOL_FILE = 194             # the Mod's pseudo team file of Italy's pool (ITA_POOL.json "pool_file")


def area_codes(entry):
    """A club's areas at the levels of groups (the Mod's "X/Y"), each 0.. or None."""
    out = []
    for letter in ((entry.get("x_swos9697", {}).get("area") or "").split("/") + ["", ""])[:2]:
        out.append(ord(letter) - ord("A") if len(letter) == 1 and "A" <= letter <= "Z" else None)
    return out


def italian_pool(country):
    """The off-file pool below Serie D (the Mod's Eccellenza, ITA_POOL.json): its clubs in our
    format, in the pack's first order (the queue a relegated Serie D slot takes from), and
    each one's areas -- the group a slot that takes it plays in (src/team_pool.c)."""
    base = json.load(open(os.path.join(BASE, "teams", "ITALY.json"), encoding="utf-8"))
    mod = json.load(open(os.path.join(MOD, "teams", "ITA_POOL.json"), encoding="utf-8"))
    home = collections.Counter(p["nationality"] for t in base["teams"] for p in t["players"]).most_common(1)[0][0]
    order = mod_league()["reserve"]["first"]
    pack_ids = [e.get("x_swos9697", {}).get("club_id") for e in mod["teams"]]
    if len(order) != len(mod["teams"]):
        sys.exit("Italy's pool: %d clubs, the pack's first order %d" % (len(mod["teams"]), len(order)))
    teams = [team(e, POOL_FILE, i, 0, 0, home) for i, e in enumerate(mod["teams"])]
    out = copy.copy(base)
    out["country"], out["teams"], out["count"] = POOL_FILE, teams, len(teams)
    out.pop("country_name", None)
    areas = [area_codes(e) for e in mod["teams"]]
    counts["pool clubs"] += len(teams)
    return out, {"name": "Italy", "country": country, "file": POOL_FILE, "json": "ITALY_POOL.json",
                 "count": len(teams), "areas": areas, "ids": pack_ids}


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
# compile_spec compiles them -- the same bytes, so src/league_playoffs.c walks them as
# gplay.c does -- for the kinds played at our season end (promotion, play-out) and the
# placement play-offs the career club plays (gplay.c leaves them alone at the season end):
# entrants [division, place], rounds of ties.
KINDS = {"promotion": 1, "placement": 3, "playout": 4}   # (placement: the career club's
                                                          # contest only -- no move, no draw)
HOSTS = {"listed": 0, "better": 1, "drawn": 2, "neutral": 3}
LEVELS = {"better": 0, "et_pens": 1, "et_better": 2, "pens": 3, "away_pens": 4, "away_better": 5}
RESERVE = 0xFE


def compile_playoff(po, ndiv):
    kind = KINDS[po["kind"]]
    ents = []
    for e in po["entrants"]:
        d, p = e
        if not (0 <= d < ndiv and p >= 1):
            sys.exit("%s: entrant %s" % (po["key"], e))
        ents.append((0, d, p, 0))
    rounds, nt = bytearray(), 0

    def code(m):
        if m == "BL":
            return 0x3F
        if isinstance(m, int):
            return m
        mm = re.fullmatch(r"([WL])(\d+)", str(m))
        if not mm or int(mm.group(2)) >= nt:
            sys.exit("%s: member %s" % (po["key"], m))
        return {"W": 0x80, "L": 0xC0}[mm.group(1)] | int(mm.group(2))

    for rd in po["rounds"]:
        if "group" in rd or rd.get("pairing") or rd.get("clubs"):
            sys.exit("%s: only rounds of listed ties here" % po["key"])
        mem = [m for t in rd["ties"] for m in t]
        legs = 3 if rd.get("series") else rd.get("legs", 1)
        host = HOSTS[rd.get("host", "listed")] | (0x80 if rd.get("skip_if_same") else 0)
        rounds += bytes((len(mem) // 2, legs, host, LEVELS[rd.get("level", "et_pens")], 0,
                         rd.get("gap", 0)))
        rounds += bytes(code(m) for m in mem)
        nt += len(mem) // 2
    ld = po.get("loser_div", 0)
    ld = RESERVE if ld == "reserve" else ld
    flags = 2 if po.get("host_by_record") else 0
    return (bytes((kind, len(ents), len(po["rounds"]), po.get("winner_div", 0), ld, flags)) +
            b"".join(bytes(e) for e in ents) + rounds)


SECOND_RULES = {"ita": (1, 1)}   # the Mod's mkseason.SECOND_RULES: B teams up to Serie B, play-offs too


def italian_playoffs(country, league, picked=None):
    """Italy's play-off rules: the Mod's counts per division, its Italian rules (dplay.c:
    Serie B's play-off and play-out, Serie C's play-off) and the pack's knockouts played
    at the season end (its groups' play-outs; Serie D's go to the pool below, not yet)."""
    mod = mod_league()
    ndiv = len(mod["divisions"])
    spec = b"".join(compile_playoff(po, ndiv) for po in mod.get("ko_playoffs", [])
                    if po["kind"] in KINDS) + b"\0"
    counts["play-offs compiled (promotion / placement / play-out)"] += sum(
        1 for po in mod.get("ko_playoffs", []) if po["kind"] in KINDS)
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


CUP_ROUND = {"single_et": 0x14, "two_legs": 0x94}   # legs 7-6 (2 two), e.t. 5-4, penalties 3-2
LEAGUE_CUP_ID = 163         # a free competition id (include/comp_identity.h: 162..255)


def bracket_order(rows, rounds):
    """A cup's real draw (ties.csv: round, home, away; a bye has no away) as the engine plays
    a fixed bracket: round 1's ties in an order where tie k of each later round is the
    winners of ties 2k and 2k + 1 of the one before -- the tree from the final down, each
    tie's two from the earlier round's ties holding its clubs. Round 1's real ties must come
    first (the engine plays a round's first ties, the byes wait after them), so where a
    tie's away side holds more of them its two sides are laid out the other way round and
    the tie changes sides when played: a bit per tie of each later round (flips[r - 2]).
    Returns (round 1's ties in order, the flips, the rounds the draw covers)."""
    by_round = collections.defaultdict(list)
    for r in rows:
        by_round[int(r["round"])].append((r["home"], r["away"] or None))
    last = max(by_round)
    if len(by_round[last]) != 1 or sorted(by_round) != list(range(1, last + 1)):
        sys.exit("a cup's draw: rounds 1..%d, one tie in the last (%s)" % (last, sorted(by_round)))

    def holds(r, club):
        """round r's tie a club played (or waited) in, by index."""
        for i, t in enumerate(by_round[r]):
            if club in t:
                return i
        sys.exit("a cup's draw: %s not in round %d" % (club, r))

    level = collections.defaultdict(list)   # per round, its ties in the engine's order: (index, flipped)

    def real(r, i):
        """round r's tie i: how many real first-round ties lead to it."""
        if r == 1:
            return 1 if by_round[1][i][1] else 0
        h, a = by_round[r][i]
        return real(r - 1, holds(r - 1, h)) + real(r - 1, holds(r - 1, a))

    def lay(r, i):
        """round r's tie i laid out: its round-1 ties in order (each round's ties are met
        left to right, so level[r] is in the engine's order)."""
        if r == 1:
            return [i]
        h, a = by_round[r][i]
        home, away = holds(r - 1, h), holds(r - 1, a)
        flip = real(r - 1, away) > real(r - 1, home)
        level[r].append(flip)
        first, second = (away, home) if flip else (home, away)
        return lay(r - 1, first) + lay(r - 1, second)

    order, played = lay(last, 0), real(last, 0)
    if sorted(order) != list(range(len(by_round[1]))):
        sys.exit("a cup's draw: round 1's ties do not make one bracket")
    if any(not by_round[1][i][1] for i in order[:played]):
        sys.exit("a cup's draw: its real first-round ties cannot all come first")
    flips = [sum(1 << k for k, f in enumerate(level[r]) if f) for r in range(2, last + 1)]
    return [by_round[1][i] for i in order], flips, last


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
        ties, flips, rounds = bracket_order([r for r in rows if r["competition"] == key], len(spec["rounds"]))
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
                    "europe": 0, "flips": flips,
                    "quota": [(d, n, b) for d, (n, b) in sorted(quota.items())]})
        counts["%s: clubs (byes)" % key] += len(entrants)
        counts["%s: first round byes" % key] += len(ties) - real
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


def world_data_c(tag, leagues, resized, groups, playoffs, pools, cups, cupd):
    externs, strings, records, installs = set(), [], [], []
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
        externs.add("extern CompetitionLeague %s;" % rec["id"])
        installs.append("        install_league(&%s, &%s);" % (rec["id"], baked["id"]))
    installs.append("        FixtureRounds_Install();")
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
        cup_tables.append("static " + emit_competitions.emit_cup(dict(rec, id=sym, name=names)))
        cup_tables.append("static const uint16_t %sFlips[] = { %s };" % (
            sym, ", ".join(map(str, c["flips"])) or "0"))
        cup_tables.append("static const uint8_t %sQuota[][3] = { %s };" % (
            sym, ", ".join("{ %d, %d, %d }" % q for q in c["quota"])))
        cup_entries.append("    { %d, %d, &%s, %d, %d, %d, %d, %sFlips, %d, %sQuota },   /* %s */" % (
            rec["comp"]["team_number"], c["slot"], sym, c["n"], c["ties"], c["europe"],
            len(c["flips"]) + 1, sym, len(c["quota"]), sym, rec["id"]))
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
    return WORLD_DATA_C % {"externs": "\n".join(sorted(externs)),
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


def main():
    if not os.path.isdir(MOD):
        sys.exit("no %s: the Mod's data (Davide's private copies) is not here" % os.path.relpath(MOD, REPO))
    (teams, picked), leagues = italy(), italian_league()
    write(os.path.join(WORLD, "teams", "ITALY.json"), teams)
    write(os.path.join(WORLD, "competitions", "leagues.json"), leagues)
    name, packed = team_file_binary(teams)
    path = os.path.join(WORLD, "data", name.upper())
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(packed)
    print("wrote", os.path.relpath(path, REPO), "(%d bytes)" % len(packed))
    country = teams["teams"][0]["team"]["country_number"]
    pool, pool_info = italian_pool(country)
    write(os.path.join(WORLD, "teams", pool_info["json"]), pool)
    name, packed = team_file_binary(pool)
    path = os.path.join(WORLD, "data", name.upper())
    open(path, "wb").write(packed)
    print("wrote", os.path.relpath(path, REPO), "(%d bytes)" % len(packed))
    with open(os.path.join(REPO, "src", "world_data.c"), "w") as f:
        f.write(world_data_c("2627", leagues, [(country, teams["count"])],
                             [italian_groups(picked, country, leagues)],
                             [italian_playoffs(country, leagues, picked)], [pool_info],
                             italian_cups(picked, country), italian_cupd(picked, country, None)))
    print("wrote src/world_data.c")
    for what, n in sorted(counts.items()):
        print("  %-52s %d" % (what, n))


if __name__ == "__main__":
    main()
