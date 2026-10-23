#!/usr/bin/env python3
"""Offline entity catalog: resolve clubs/national teams to the expected
picker label path BEFORE executing it (docs/TESTING_FLOWS_DESIGN.md,
"Entity resolution" — offline first, verify online).

Sources (all explicit, all in the digest):
  <catalog>/competitions/countries.json             country number -> name
  <catalog>/competitions/qualifying.json            _worldTable/_<continent>Table
                                                    -> continent membership
  <catalog>/competitions/country_competitions.json  country -> league ids
  <catalog>/competitions/leagues.json               league id -> division name
                                                    symbols + divisions_count
  <teams>/teams/<COUNTRY>.json                      club entries (uuid, name,
                                                    team_ordinal, league)
  <repo>/src/strings_data.c                         _aXxx symbol -> display
                                                    string (division names)

The teams tree is setup-built game data: found via $SWOS_ASSETS /
~/Documents/SWOS/assets first, then beside the competitions root. Offline
files PREDICT navigation; every hop is verified against the live snapshot by
the executor, and runtime wins on any disagreement.
"""
import hashlib
import json
import os
import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from testing_support.goals import GoalError, S_NOT_FOUND, S_CLARIFY

_REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Continent "country" numbers (CN_EUROPE..CN_OCEANIA, include/game_types.h).
CONTINENT_NUMBERS = range(80, 86)

# Fallback division names when a league's name table is empty — mirrors
# _divisionStringTable (src/swos_amiga_ptr_imports.c), resolved through
# strings_data.c like the authored names.
DIVISION_FALLBACK_SYMBOLS = ["_aPremierDivisio", "_aDivision1_0",
                             "_aDivision2_0", "_aDivision3_0"]


def normalize(s: str) -> str:
    """Punctuation/case/whitespace normalization — the SAFE transforms only."""
    s = (s or "").upper().replace("_", " ")
    s = re.sub(r"[^\w ]", "", s)
    return re.sub(r"\s+", " ", s).strip()


@dataclass
class ResolvedTeam:
    name: str                    # exact in-game team_name
    uuid: str
    kind: str                    # club | national
    country_number: int
    country_label: str           # picker label for the country hop
    continent_label: str         # picker label for the continent hop
    team_ordinal: int
    division_index: Optional[int] = None    # club only (team.league)
    division_label: Optional[str] = None    # may be None => bind at runtime
    aliased_from: Optional[str] = None

    def to_dict(self):
        return dict(self.__dict__)


@dataclass
class Catalog:
    comp_root: str
    teams_root: Optional[str]
    countries: Dict[int, str] = field(default_factory=dict)       # num -> name
    continent_of: Dict[int, int] = field(default_factory=dict)    # country -> continent num
    country_leagues: Dict[int, List[str]] = field(default_factory=dict)
    league_divnames: Dict[str, List[Optional[str]]] = field(default_factory=dict)
    teams: Dict[int, List[dict]] = field(default_factory=dict)    # country -> entries
    aliases: Dict[str, dict] = field(default_factory=dict)
    digest: str = ""
    missing: List[str] = field(default_factory=list)

    # ---- resolution --------------------------------------------------------
    def country_number(self, name: str) -> Optional[int]:
        want = normalize(name)
        for num, nm in self.countries.items():
            if normalize(nm) == want:
                return num
        return None

    def _candidates(self, name: str, kind: Optional[str],
                    country: Optional[int]) -> List[dict]:
        want = normalize(name)
        out = []
        for cnum, entries in self.teams.items():
            is_nat_file = cnum in CONTINENT_NUMBERS
            if kind == "club" and is_nat_file:
                continue
            if kind == "national" and not is_nat_file:
                continue
            if country is not None and not is_nat_file and cnum != country:
                continue
            for e in entries:
                if normalize(e["team"]["team_name"]) == want:
                    out.append(e)
        return out

    def resolve_team(self, name: str, kind: Optional[str] = None,
                     country: Optional[str] = None) -> ResolvedTeam:
        """Exact normalized match + explicit aliases; ambiguity and misses are
        structured errors with the actual choices — never the closest row."""
        if self.teams_root is None:
            raise GoalError(S_NOT_FOUND,
                            "no teams catalog found: " + "; ".join(self.missing),
                            ["Supply a catalog path holding data_json/teams/"])
        aliased_from = None
        a = self.aliases.get(normalize(name))
        if a:
            aliased_from, name = name, a.get("team_name", name)
            kind = a.get("kind", kind)
            country = a.get("country", country)

        cnum = None
        if country:
            cnum = self.country_number(country)
            if cnum is None:
                raise GoalError(S_NOT_FOUND,
                                f"country {country!r} not in catalog",
                                [f"known countries: "
                                 f"{sorted(self.countries.values())[:12]}..."])
        cands = self._candidates(name, kind, cnum)
        if not cands:
            raise GoalError(S_NOT_FOUND,
                            f"team {name!r} not found"
                            + (f" in {country}" if country else "")
                            + (f" (kind={kind})" if kind else ""),
                            ["No exact in-game name matches; an explicit alias "
                             "entry is the supported route for nicknames "
                             "(testing_support/aliases.json)"])
        if len(cands) > 1:
            where = [f'{c["team"]["team_name"]} '
                     f'({self.countries.get(c["team"]["country_number"], "?")})'
                     for c in cands]
            raise GoalError(S_CLARIFY, f"team {name!r} is ambiguous",
                            [f"candidates: {where}"])
        e = cands[0]
        t = e["team"]
        tn = t["country_number"]
        is_nat = tn in CONTINENT_NUMBERS
        cont = tn if is_nat else self.continent_of.get(tn)
        if cont is None:
            raise GoalError(S_NOT_FOUND,
                            f"no continent membership for country {tn} "
                            f"({self.countries.get(tn)}) in qualifying.json")
        res = ResolvedTeam(
            name=t["team_name"], uuid=e.get("uuid", ""),
            kind="national" if is_nat else "club",
            country_number=tn,
            country_label=normalize(self.countries.get(tn, "")),
            continent_label=normalize(self.countries.get(cont, "")),
            team_ordinal=t.get("team_ordinal", 0),
            aliased_from=aliased_from)
        if not is_nat:
            res.division_index = t.get("league", 0)
            res.division_label = self.division_label(tn, res.division_index)
        return res

    def division_label(self, country_num: int, div_index: int) -> Optional[str]:
        """Expected division picker label, or None (bind at runtime). Assumes
        divisions enumerate in league-list order, as SelectTeamsFinalMenu
        does; a wrong prediction fails online verification, never a press."""
        i = div_index
        for lid in self.country_leagues.get(country_num, []):
            names = self.league_divnames.get(lid)
            if names is None:
                return None
            count = len(names)
            if i < count:
                if names[i] is not None:
                    return normalize(names[i])
                if i < len(DIVISION_FALLBACK_SYMBOLS):
                    s = self._strings.get(DIVISION_FALLBACK_SYMBOLS[i])
                    return normalize(s) if s else None
                return None
            i -= count
        return None

    _strings: Dict[str, str] = None


def _load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _parse_strings_c(path) -> Dict[str, str]:
    """_aXxx[] = "..." string constants from src/strings_data.c."""
    out = {}
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError:
        return out
    for m in re.finditer(r'const\s+char\s+(_a\w+)\[\]\s*=\s*"((?:[^"\\]|\\.)*)"',
                         text):
        out[m.group(1)] = m.group(2)
    return out


def default_roots() -> List[str]:
    roots = []
    env = os.environ.get("SWOS_ASSETS")
    if env:
        roots.append(os.path.join(env, "data_json"))
    home = os.path.expanduser("~")
    roots.append(os.path.join(home, "Documents", "SWOS", "assets", "data_json"))
    # Windows home when running under Cygwin/WSL
    for base in ("/cygdrive/c/Users", "/mnt/c/Users"):
        u = os.environ.get("USER") or os.environ.get("USERNAME") or ""
        roots.append(os.path.join(base, u, "Documents", "SWOS", "assets",
                                  "data_json"))
    roots.append(os.path.join(_REPO, "bin", "data_json"))
    return roots


def load_catalog(paths: Optional[List[str]] = None,
                 strings_c: Optional[str] = None) -> Catalog:
    """Build the catalog from the first root providing each part; the parts
    may come from different roots (teams are setup-built, competitions are
    tracked). Missing parts are recorded, not fabricated."""
    roots = [p for p in (paths or default_roots()) if p and os.path.isdir(p)]
    comp_root = next((r for r in roots if os.path.isfile(
        os.path.join(r, "competitions", "countries.json"))), None)
    teams_root = next((r for r in roots if os.path.isdir(
        os.path.join(r, "teams"))), None)
    missing = []
    if comp_root is None:
        raise GoalError(S_NOT_FOUND,
                        "no competitions catalog (countries.json) under any of: "
                        + "; ".join(paths or default_roots()))
    if teams_root is None:
        missing.append("teams/ not found under: " + "; ".join(roots))

    cat = Catalog(comp_root=comp_root, teams_root=teams_root, missing=missing)
    h = hashlib.sha256()

    def read(relroot, *parts):
        p = os.path.join(relroot, *parts)
        with open(p, "rb") as f:
            data = f.read()
        h.update(hashlib.sha256(data).digest())
        return json.loads(data.decode("utf-8"))

    cd = read(comp_root, "competitions", "countries.json")
    for c in cd["countries"]:
        cat.countries[c["number"]] = c["name"]

    q = read(comp_root, "competitions", "qualifying.json")
    groups = {g["id"]: g for g in q["groups"]}
    world = groups.get("_worldTable", {}).get("countries", [])
    for cont in world:
        if cont == 255:
            continue
        # group ids are _europeTable / _southAmericaTable etc.; countries.json
        # names are EUROPE / SOUTH_AMERICA — compare with separators removed.
        cname = cat.countries.get(cont, "")
        want = normalize(cname).replace(" ", "")
        tbl = None
        for gid, g in groups.items():
            if normalize(gid.replace("Table", "").lstrip("_")).replace(" ", "") == want:
                tbl = g
                break
        for member in (tbl or {}).get("countries", []):
            if member != 255:
                cat.continent_of[member] = cont

    cc = read(comp_root, "competitions", "country_competitions.json")
    for c in cc["countries"]:
        cat.country_leagues[c["number"]] = list(c.get("leagues", []))

    lg = read(comp_root, "competitions", "leagues.json")
    strings = _parse_strings_c(strings_c or
                               os.path.join(_REPO, "src", "strings_data.c"))
    cat._strings = strings
    for l in lg["leagues"]:
        names = []
        name_syms = l.get("name") or []
        for i in range(l.get("divisions_count", 0)):
            sym = name_syms[i * 2] if i * 2 < len(name_syms) else None
            names.append(strings.get(sym) if sym else None)
        cat.league_divnames[l["id"]] = names

    if teams_root:
        tdir = os.path.join(teams_root, "teams")
        for fn in sorted(os.listdir(tdir)):
            if not fn.endswith(".json"):
                continue
            d = read(teams_root, "teams", fn)
            if d.get("type") == "team" and "country" in d:
                cat.teams[d["country"]] = d.get("teams", [])

    alias_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                              "aliases.json")
    if os.path.isfile(alias_path):
        with open(alias_path, "rb") as f:
            raw = f.read()
        h.update(hashlib.sha256(raw).digest())
        for k, v in json.loads(raw.decode("utf-8")).items():
            cat.aliases[normalize(k)] = v

    cat.digest = h.hexdigest()[:16]
    return cat
