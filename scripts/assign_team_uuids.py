#!/usr/bin/env python3
"""assign_team_uuids.py -- give every team and player of the team database its id.

    scripts/assign_team_uuids.py            # add the ids that are missing
    scripts/assign_team_uuids.py --check    # change nothing; exit 1 if any is missing or wrong
    scripts/assign_team_uuids.py --registry # also write the matchmaking server's list
    scripts/assign_team_uuids.py --registry --worlds   # ... with the worlds' teams too

docs/TEAM_IDS.md is the design. In short: a "uuid" on each team of
bin/data_json/teams/*.json (type "team") and on each of its sixteen players is the
CANONICAL name of that data -- what a custom team's file links to instead of carrying
skills, and what a downloadable team will be known by. An id names FIXED data: a team
whose players, positions, skills or values change is a new team with a new id.

The ids of the original 96/97 database are DERIVED, not drawn at random, so that every
copy of the game -- and the binary TEAM.nnn files, which have nowhere to keep one --
agree on them without being told (src/team_db.c computes the same):

    team    uuid5(NAMESPACE, "96-97/team/<country>/<ordinal>")
    player  uuid5(NAMESPACE, "96-97/team/<country>/<ordinal>/player/<index 0..15>")

Each team also gets a "checksum": what its id STANDS FOR. It is the first 16 hex digits
of the SHA-1 of what a match plays the team by and nothing else -- for each of the
sixteen players in order, nine bytes: position, passing, velocity, heading, tackling,
control, speed, finishing, price. Names, faces and kits are not in it. --check recomputes
it, so a skill edited without a new id is caught here (and by the game, when it loads
the team: src/team_db.c).

--registry writes third_party/matchmaker/Server/include/TeamRegistry.inc: every team's id,
checksum and name, compiled into the matchmaking server, which is what answers the team
check before an online match (include/team_proto.h, docs/TEAM_IDS.md section 5). Run it
whenever the database's teams change, and rebuild the server.

A team added later (a new or modified team, a downloadable one) gets an id of its own
from whoever publishes it, written in its JSON; this script never touches an id that is
already there, and --check only insists that the original teams carry the derived ones.
The files are rewritten in the layout they already have (json.dump, indent 2), so the
only lines that change are the ids added.
"""
import glob
import hashlib
import json
import os
import sys
import uuid

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JSON_DIR = os.path.join(REPO, "bin", "data_json", "teams")
REGISTRY = os.path.join(REPO, "third_party", "matchmaker", "Server", "include", "TeamRegistry.inc")

# uuid5(NAMESPACE_URL, "https://us.swos.eu/team-data"). Never change it: every id of
# the original database follows from it.
NAMESPACE = uuid.UUID("af19cc5a-6529-537b-935b-407bd73b75fb")


def team_uuid(country, ordinal):
    return str(uuid.uuid5(NAMESPACE, "96-97/team/%d/%d" % (country, ordinal)))


def player_uuid(country, ordinal, index):
    return str(uuid.uuid5(NAMESPACE, "96-97/team/%d/%d/player/%d" % (country, ordinal, index)))


SKILLS = ("passing", "velocity", "heading", "tackling", "control", "speed", "finishing")


def checksum(players):
    data = bytearray()
    for p in players:
        data.append(p["position"] & 7)
        data.extend(p[k] & 15 for k in SKILLS)
        data.append(p["price"] & 255)
    return hashlib.sha1(bytes(data)).hexdigest()[:16]


def with_checksum(entry, value):
    """The same team with "checksum" straight after "uuid"."""
    out = {}
    for k, v in entry.items():
        if k == "checksum":
            continue
        out[k] = v
        if k == "uuid":
            out["checksum"] = value
    return out


def with_uuid_first(obj, value):
    """The same object with "uuid" as its first key."""
    out = {"uuid": value}
    out.update((k, v) for k, v in obj.items() if k != "uuid")
    return out


def main():
    check = "--check" in sys.argv
    registry = []
    added = wrong = teams = sums_added = stale = 0
    seen = {}
    for path in sorted(glob.glob(os.path.join(JSON_DIR, "*.json"))):
        text = open(path, encoding="utf-8").read()
        data = json.loads(text)
        if data.get("type") != "team":
            continue                    # tournament copies and the player pool link to nothing
        changed = False
        for i, entry in enumerate(data["teams"]):
            head = entry["team"]
            country, ordinal = head["country_number"], head["team_ordinal"]
            teams += 1
            want = team_uuid(country, ordinal)
            if "uuid" not in entry:
                data["teams"][i] = entry = with_uuid_first(entry, want)
                added += 1
                changed = True
            elif check and entry.get("origin", "96-97") == "96-97" and entry["uuid"] != want:
                print("  %s: %s has %s, the 96/97 id is %s"
                      % (os.path.basename(path), head["team_name"], entry["uuid"], want))
                wrong += 1
            if entry["uuid"] in seen:
                sys.exit("assign_team_uuids: %s is the id of both %s and %s"
                         % (entry["uuid"], seen[entry["uuid"]], head["team_name"]))
            seen[entry["uuid"]] = head["team_name"]
            for p, player in enumerate(entry["players"]):
                want = player_uuid(country, ordinal, p)
                if "uuid" not in player:
                    entry["players"][p] = player = with_uuid_first(player, want)
                    added += 1
                    changed = True
                elif check and entry.get("origin", "96-97") == "96-97" and player["uuid"] != want:
                    wrong += 1
                if player["uuid"] in seen:
                    sys.exit("assign_team_uuids: %s is used twice (%s)" % (player["uuid"], head["team_name"]))
                seen[player["uuid"]] = head["team_name"]
        for i, entry in enumerate(data["teams"]):
            registry.append((entry["uuid"], checksum(entry["players"]), entry["team"]["team_name"]))
            want = checksum(entry["players"])
            if "checksum" not in entry:
                data["teams"][i] = with_checksum(entry, want)
                sums_added += 1
                changed = True
            elif entry["checksum"] != want:
                print("  %s: %s has checksum %s, its players make %s -- its skills, positions or values "
                      "were changed: it needs a new uuid, or the change undone"
                      % (os.path.basename(path), entry["team"]["team_name"], entry["checksum"], want))
                stale += 1
        if changed and not check:
            out = json.dumps(data, indent=2)
            if text.endswith("\n"):
                out += "\n"
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(out)
    # A world's teams (bin/data_json/worlds/<tag>/teams, scripts/world_import.py: their own ids and
    # checksums, made there) join the server's list as they are: checked, never written.
    # Only with --worlds: their names publish the world's data (on hold until its author agrees).
    worlds = glob.glob(os.path.join(REPO, "bin", "data_json", "worlds", "*", "teams", "*.json")) \
        if "--worlds" in sys.argv else []
    for path in sorted(worlds):
        data = json.load(open(path, encoding="utf-8"))
        if data.get("type") != "team":
            continue
        for entry in data["teams"]:
            want = checksum(entry["players"])
            if entry.get("uuid") in seen:
                sys.exit("assign_team_uuids: %s is the id of both %s and %s"
                         % (entry["uuid"], seen[entry["uuid"]], entry["team"]["team_name"]))
            if "uuid" not in entry or entry.get("checksum") != want:
                print("  %s: %s has no id or a stale checksum: rerun scripts/world_import.py"
                      % (os.path.relpath(path, REPO), entry["team"]["team_name"]))
                stale += 1
                continue
            seen[entry["uuid"]] = entry["team"]["team_name"]
            registry.append((entry["uuid"], want, entry["team"]["team_name"]))
    if "--registry" in sys.argv and not stale:
        lines = ["// Generated by scripts/assign_team_uuids.py --registry from bin/data_json/teams and the",
                 "// worlds' teams (bin/data_json/worlds/<tag>/teams).",
                 "// Every canonical team: its id, the checksum of its positions, skills and values,",
                 "// and its name (for the log). docs/TEAM_IDS.md. Do not edit.",
                 ""]
        for u, c, name in sorted(registry):
            lines.append('{"%s", "%s", "%s"},' % (u, c, name.replace("\\", "").replace('"', "'")))
        with open(REGISTRY, "w", encoding="utf-8", newline="") as f:
            f.write("\n".join(lines) + "\n")
        print("registry: %d teams -> %s" % (len(registry), os.path.relpath(REGISTRY, REPO)))
    if check:
        print("%d teams, %d ids: %d missing, %d not the derived id; checksums: %d missing, %d stale"
              % (teams, len(seen), added, wrong, sums_added, stale))
        return 1 if (added or wrong or sums_added or stale) else 0
    print("%d teams: %d ids added (%d in all), %d checksums added, %d stale"
          % (teams, added, len(seen), sums_added, stale))
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
