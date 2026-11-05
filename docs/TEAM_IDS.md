# TEAM_IDS — canonical ids for the team database

> **Status: current** for the ids and checksums in the database, the game's checks, custom
> teams linking by id, and the matchmaking server's team check (§5); **plan** for
> downloadable teams (§6). 2026-10-04.

Every team and every player of the team database has an id, and every team a checksum of
what its id stands for. A custom team's file links to them instead of carrying skills
(docs/CUSTOM_TEAMS.md); a downloadable team will be known by them.

## 1. The rule: an id names fixed data

A team whose players, positions, skills or values change is a **new team with a new id**.
That is what lets a file, the other player or the server say "team X" and mean one exact set
of numbers. Renaming a team or a player, or changing a kit, does not make a new team.

## 2. What is in the database

`bin/data_json/teams/*.json` (the 72 files of `"type": "team"`; the three tournament copies
and the player pool link to nothing and have none):

```json
{
  "uuid": "30e26617-4001-56c3-a068-8d88849c8398",
  "checksum": "f2f9140ad5127ab1",
  "team": { "country_number": 82, "team_ordinal": 2, "team_name": "BRAZIL", … },
  "players": [ { "uuid": "3d5c7af0-764f-5b50-95fe-7c859e4c452d", "player_name": "TAFFAREL", … }, … ]
}
```

1,631 teams, 26,096 players, 27,727 ids. The game's own team loader ignores both keys, and
`scripts/emit_teams.py` still rebuilds `bin/data/TEAM.nnn` byte for byte.

**The checksum** is the first 16 hex digits of the SHA-1 of what a match plays the team by
and nothing else: for each of the sixteen players in order, nine bytes — position, passing,
velocity, heading, tackling, control, speed, finishing, price. Names, faces, nationalities,
shirt numbers and kits are not in it. All 1,631 are different.

## 3. Where the ids of the 96/97 database come from

They are **derived**, not drawn at random, so that every copy of the game agrees on them
without being told, and the binary `TEAM.nnn` files — which have nowhere to keep one — have
them too:

```
NAMESPACE = uuid5(NAMESPACE_URL, "https://us.swos.eu/team-data")
          = af19cc5a-6529-537b-935b-407bd73b75fb          never change this
team      = uuid5(NAMESPACE, "96-97/team/<country>/<ordinal>")
player    = uuid5(NAMESPACE, "96-97/team/<country>/<ordinal>/player/<index 0..15>")
```

`scripts/assign_team_uuids.py` writes them (and the checksums) into the JSON; `src/team_db.c`
computes the same for a team whose file states none. **What a file states is what is used**:
a team added later has an id of its own that no formula gives, written in its JSON by
whoever publishes it. The script never touches an id that is already there.

`scripts/decode_data.py --dump-json` rebuilds the JSON from the binary files and so writes
no ids: run `scripts/assign_team_uuids.py` after it (the ids come back the same).

## 4. What the game checks (`src/team_db.c`)

- **On loading a country's teams** it recomputes each checksum. A team where the stated and
  the computed differ — a skill edited, the id kept — is logged (`[team-db] …`) and is **not
  canonical**: it is not offered as what its id stands for, to a custom team or online.
- **A custom team** (`made_from`: uuid + checksum) resolves only if the database here has
  that id, canonical, with that checksum.
- **A team from the other player** is held to the database team of its number, which has to
  be canonical (`CustomTeam_Allowed`).

`scripts/assign_team_uuids.py --check` says the same of the files: every original team has
its derived id, no id is used twice, and every checksum is the one its players make.
`bin/SWOS.x86_64 --custom-team-selftest` checks the game's side: its uuid5 against Python's,
all 27,727 stated ids against the derived ones, every team canonical and found by its id —
on the JSON database and, with `SWOS_DATA_JSON=0`, the binary one.

## 5. Authority: the matchmaking server's team check

Everything above makes a database agree with itself. A player who edits a skill **and** the
checksum beside it has a database that still does. Only a list kept somewhere the player
cannot edit can say that team is not the canonical one — and the matchmaking server has it.

**The list.** `scripts/assign_team_uuids.py --registry` writes every team's id, checksum
and name to `third_party/matchmaker/Server/include/TeamRegistry.inc`, which is **compiled
into the server** (`Server/TeamDesk.cpp`): updating the server updates the list, with no
option to remember. `--teams FILE` adds more, one `uuid checksum name` a line, for teams
published after the build. Run the script with `--registry` whenever the database's teams
change, and rebuild the server.

**The question** (`include/team_proto.h`). In a match the server arranged, each game asks
about **both** teams — its own and the one the other player sent it — each as the id of the
database team it is and the checksum of its players *as they are in the record it would
play* (`CustomTeam_Claim`). So a player who sends a doctored team is found out by the honest
game that received it, whatever their own game says. One 62-byte datagram on the server's
own UDP port, told apart from the matchmaker's and the relay's by its magic and exact length
(`SWTC`), answered with a status per team: known, unknown id, or known id with other data.
It goes from the socket the lobby already has open, so it needs no port, and a browser's
reaches the server through the WebSocket gate like any other datagram.

**What the lobby does with the answer** (`team_check`, `src/netplay_lobby.c`):

| the server says | the lobby |
|---|---|
| both teams known | the match can start; `[lobby] team check: player N's X is canonical` |
| a team unknown, or other data | nobody stays ready, the host cannot START and the joiner refuses one; the lobby says THE SERVER DOES NOT KNOW YOUR TEAM / YOUR OPPONENT'S TEAM; the server logs `[teams] … REFUSED` and writes a `teamcheck` ledger line |
| nothing yet | START waits — a start that did not would begin on a team whose refusal was still on its way |
| nothing after eight tries (about 3 s) | the match is playable **unverified** (`[lobby] team check: the server did not answer`) |

Unverified is deliberate: an older server does not answer at all, and a server that is down
should not stop a match the matchmaker already arranged. It does not open a hole a cheat can
use — a player can silence their *own* game's question, not the other game's. A direct-IP
match (the `p2p` menu) has no server and asks nobody; the local checks of §4 still hold.

`scripts/relay_test.sh` requires the server to vouch for both teams on both sides of every
match it plays. `HOST_ENV="SWOS_TEAMCHECK_CORRUPT=1" TEAMCHECK_REFUSE=1 scripts/relay_test.sh`
asks about the other player's team with a checksum that is not its data's, and requires the
refusal, and that no match is played.

## 6. Team packs: teams a mod brings (built 2026-11-04)

A **team pack** is a mod's `data_json/teampacks/<name>.json` in the database's own team format
(the header of `bin/data_json/teams/<COUNTRY>.json`: `"type": "team"`, `"country"`, `"teams"`),
with a country number of its own, **210-249** (`TEAMPACK_FIRST..LAST`, include/team_packs.h) --
outside every country's and every world file's (countries go to 85, second files to 197,
`kOwnCustoms` is 100). `src/team_packs.c` finds them in the mods that are on, and
`dj_slurp_country_teams` hands a pack's file to the game for its number when neither
`countries.json` nor the world has one. So the team database, match loading (`LoadTeamFile`) and
the online rule (`CustomTeam_Allowed`) take a pack's teams exactly as the database's. A number
outside the range, or one an earlier pack took, is refused in the log.

`scripts/make_team_pack.py PACK.json --number 210 [--name ...] [--from <database file> --teams
"A,B"]` makes one: the number on the pack and its teams (ordinals in order), a **new uuid** (uuid4)
for every team and player without one -- a pack's team is not the database's, even when it starts
as a copy -- the checksum as the game computes it, and `PACK.registry.inc` beside it: the
matchmaking server's lines for those teams. A mod of packs is `"changes": "data"`: online, both
players need it alike (the mods' signature), and a server that is to verify its teams compiles its
lines in (SWOS United's server keeps its own). Checked: a pack of two teams copied from England's
file (number 210) -- CHELSEA, from the pack, plays a full match (`SWOS_MOD_MATCH_TEAMS=d201,5502
SWOS_MOD_MATCH_TEST=1`) and the online rule accepts it. SWOS United's fun cup is built on it.

**Choosing a pack's teams** (H11): CHOOSE TEAMS has a **TEAM PACKS** row (entry 5, the title
moved to 6) while a mod that is on has a pack -- never on the Amiga. One pack: its teams; more: a
list of them (`openswos/teamPacksMenu`, each its `country_name`, the cursor's its `description`).
The teams then list as ORIGINAL CUSTOM TEAMS do, through the game's own team list: at start-up each
pack gets a country name and a league of its own at its number (`TeamPacks_Install`: a copy of the
customs league, its team file the pack's), and `SelectTeamsFinalMenu` takes the pack's number for
the new team type `TEAM_PACK_TEAMS` (4) where it takes 72 for the custom teams. A mod goes there
straight with `ModApi_ChooseFromPack(number)`; `ModApi_PackCount / PackInfo` list them. Checked:
the row with a pack and without one, and a pack's two teams in the team list (CPU / player as for
any team).

## 7. Still a plan: downloadable teams

- **Downloadable teams** (the DLC manager, SWOS-Launcher) arrive as team JSON with their ids
  and checksums; the server's list is what a download is verified against, and what tells a
  client "you do not have team X — it is in pack Y". Today the DLC catalogue knows a pack
  only by its title, author and version, and the server keeps ids and checksums, not the
  teams themselves.
- **Online**, the two games still exchange whole teams. Sending (id, checksum, changes)
  instead is possible now that both ends can look a team up by id, and smaller.
