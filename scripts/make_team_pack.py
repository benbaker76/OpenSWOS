#!/usr/bin/env python3
"""make_team_pack.py -- make a team pack ready for a mod (include/team_packs.h, docs/TEAM_IDS.md).

    scripts/make_team_pack.py PACK.json --number 210 [--name "FUN CUP"]
    scripts/make_team_pack.py PACK.json --from bin/data_json/teams/ENGLAND.json --teams "ARSENAL,CHELSEA"

A pack is a file in the database's own team format (bin/data_json/teams/<COUNTRY>.json: "type":
"team", a "country" number, "teams": [...]) that a mod carries as data_json/teampacks/<name>.json.
This puts the pack's number on it and on every team (country_number; team_ordinal in order), gives
every team and player that has no uuid a NEW one (uuid4: a pack's team is not the database's, even
when it starts as a copy), computes every team's checksum as the game does (src/team_db.c), and
writes PACK.registry.inc beside it: the matchmaking server's lines for the pack's teams
(TeamRegistry.inc's format), for a server that is to accept them.

--from copies teams out of a database file to start a pack from (new ids, as above).
A pack's number is 210..249 (TEAMPACK_FIRST..LAST): pick one no other pack you play with uses.
"""
import argparse
import json
import os
import sys
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from assign_team_uuids import checksum  # noqa: E402  (the game's checksum, one copy of it)

FIRST, LAST = 210, 249


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack", help="the pack file (made by --from when it does not exist)")
    ap.add_argument("--number", type=int, help="its country number, %d..%d" % (FIRST, LAST))
    ap.add_argument("--name", help="its name (country_name)")
    ap.add_argument("--from", dest="src", help="a database file to copy teams from")
    ap.add_argument("--teams", help="with --from: the teams' names, comma separated (all of them when left out)")
    args = ap.parse_args()

    if args.src:
        src = json.load(open(args.src, encoding="utf-8"))
        want = [t.strip().upper() for t in args.teams.split(",")] if args.teams else None
        teams = [json.loads(json.dumps(t)) for t in src["teams"]
                 if want is None or t["team"]["team_name"].upper() in want]
        for t in teams:                     # new ids: not the database's teams any more
            t.pop("uuid", None)
            t.pop("checksum", None)
            for p in t["players"]:
                p.pop("uuid", None)
        pack = {"type": "team", "country": args.number or FIRST, "country_name": args.name or "TEAM PACK",
                "count": len(teams), "teams": teams}
    else:
        pack = json.load(open(args.pack, encoding="utf-8"))
    if pack.get("type") != "team":
        sys.exit("make_team_pack: %s is not a team file (\"type\": \"team\")" % args.pack)
    number = args.number if args.number is not None else pack.get("country")
    if not isinstance(number, int) or not FIRST <= number <= LAST:
        sys.exit("make_team_pack: a pack's number is %d..%d (--number)" % (FIRST, LAST))
    pack["country"] = number
    if args.name:
        pack["country_name"] = args.name
    pack["count"] = len(pack["teams"])

    lines = ["// %s: a team pack's teams for the matchmaking server (scripts/make_team_pack.py)." %
             os.path.basename(args.pack), "// Its id, the checksum of its positions, skills and values, its name."]
    out = []
    for ordinal, t in enumerate(pack["teams"]):
        t["team"]["country_number"] = number
        t["team"]["team_ordinal"] = ordinal
        for p in t["players"]:
            if not p.get("uuid"):
                p["uuid"] = str(uuid.uuid4())
        entry = {"uuid": t.get("uuid") or str(uuid.uuid4()), "checksum": checksum(t["players"])}
        entry.update((k, v) for k, v in t.items() if k not in ("uuid", "checksum"))
        out.append(entry)
        name = t["team"]["team_name"].replace("\\", "").replace('"', "'")
        lines.append('{"%s", "%s", "%s"},' % (entry["uuid"], entry["checksum"], name))
    pack["teams"] = out
    with open(args.pack, "w", encoding="utf-8", newline="") as f:
        json.dump(pack, f, indent=1)
        f.write("\n")
    reg = os.path.splitext(args.pack)[0] + ".registry.inc"
    with open(reg, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines) + "\n")
    print("%s: number %d, %d teams; the server's lines in %s" % (args.pack, number, len(out), reg))
    return 0


if __name__ == "__main__":
    sys.exit(main())
