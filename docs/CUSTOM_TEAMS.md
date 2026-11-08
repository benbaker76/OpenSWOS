# MY CUSTOM TEAM — a team of your own for online play

> **Status: current** (2026-10-02). SDL builds only.

Three slots. A custom team is **a team of the database, renamed and redressed**: its owner
may change

- the team's name and the coach's,
- each player's name, face, nationality and shirt number,
- both kits,
- the line-up and the formation it starts in,

and nothing else. **No player's position, skills or value can be changed** —
that is the rule the whole thing is built round, and it is kept in three places, so that no
one of them has to be trusted.

## 1. The file has no player data: it links to the database by id

`CUSTOM1.TEAM` … `CUSTOM3.TEAM` in the save folder. JSON, like a career save is. It is a
`CustomTeam` (`include/custom_team.h`): **the id of the database team it was made from**,
that team's checksum, and what the owner changed. No position, no skill, no value.

```json
{
  "format": "openswos-team",
  "version": 3,
  "made_from": { "uuid": "30e26617-4001-56c3-a068-8d88849c8398", "checksum": "f2f9140ad5127ab1", "name": "BRAZIL" },
  "team": {
    "team_name": "BORUSSIA FULDA",
    "coach_name": "MARTIN HOHMANN",
    "tactics": 3,
    "primary_shirt_type": 2, "primary_stripes_color": 2, "primary_basic_color": 4, "primary_shorts_color": 2, "primary_socks_color": 4,
    "secondary_shirt_type": 0, "secondary_stripes_color": 9, "secondary_basic_color": 5, "secondary_shorts_color": 2, "secondary_socks_color": 1,
    "player_numbers": [0, 2, 11, 5, 3, 7, 4, 13, 9, 14, 15, 1, 6, 10, 12, 8]
  },
  "players": [
    { "uuid": "3d5c7af0-764f-5b50-95fe-7c859e4c452d", "player_name": "CESAR THIER", "face": 1, "nationality": 70, "shirt_number": 1, "was": "TAFFAREL" },
    …sixteen of them…
  ]
}
```

- The ids and the checksum are the team database's (docs/TEAM_IDS.md). `made_from.name` and
  each player's `was` are there for whoever reads the file; nothing reads them back.
- The field names are the database's own where the two have the same thing (`team_name`,
  `coach_name`, `tactics`, the kits, `player_numbers`, `player_name`, `face`, `nationality`,
  `shirt_number`). Shirt types are 0-3 (plain, sleeves, stripes, hoops), colours 0-9 (grey,
  white, black, orange, red, blue, brown, light blue, green, yellow), faces 0-2,
  nationalities 0-154, shirt numbers 1-99.
- A player's place in `players` is his place in the team's record; his `uuid` says which of
  the database team's players stands there.

**Loading resolves it** (`read_json` then `CustomTeam_Resolve`, `src/custom_team.c`): the
team is looked up by its id, has to be canonical and have the checksum the file states, and
gives every player his position, skills and value; the file's changes go on top. Whatever a
file leaves out is what the database team has, so a hand-made file can be as short as its
link and one name. It is not used if the database has no such id, if the team with that id
here is not the data the file was made from, or if one of its players is not that team's (or
is in it twice). A skill written into the file by hand is simply not read.

Versions 1 and 2 (2026-10-02 to 04) were never in a published build's hands for long and
nothing reads them; nobody had files in them.

**A `.TEAM` file** — the raw 684-byte team record SWOS 2020 and the team editors use — put
in the save folder under a slot's name (`CUSTOM1.TEAM` …) is **converted** the first time the
slot is read (`load_record`): the database team it was made from is found **by its players**,
the differences become a `CustomTeam` (`CustomTeam_FromTeam`), and the slot is rewritten as
the JSON above, the original kept beside it as `CUSTOMn.TEAM.684`. The number inside the record is no
guide — SWOS 2020 gives a custom team a number of its own: Playaveli's `BORUSSIA FULDA` says
country 72, team 17 (GAMESMASTER here) and is BRAZIL (country 82, team 2) with its names,
nationalities, numbers, faces, kits and line-up changed. A record whose sixteen players are
not, with their positions, skills and values, the players of one database team is refused.

`bin/SWOS.x86_64 --convert-teams=<in>,<out>` runs a folder of them through the same
conversion, writing each under its own name (`.TEAM`, JSON inside, as a career is JSON in a
`.CAR`) and reading each back to require the same team. A record that is not a database team
has nothing to link to and is not written; the log names the database team it comes closest
to. On 2,522 community `.TEAM` files (2026-10-04): 2,519 converted, every one loading back
identical — 808 of them carried a number that is not their base team's, so only finding the
base by its players could convert them — and 3 are not database teams: ARGENTINA 1978,
GERMANY 1992 and M.F.T., historical squads whose players are in no team of the 96/97
database (1 of 16 match the closest). The bases most made from: ITALY 660, BRAZIL 406, REAL
MADRID 228, AC MILAN 163, BARCELONA 156.

## 2. The editor cannot edit one

MY CUSTOM TEAM on the matchmaking menu opens the three slots (SWOS 2020's
`selectMyCustomTeams` menu). Choosing an empty one asks for a team of the database first
(clubs and national sides); choosing a filled one edits it.

The editor is the game's own EDIT TEAM screen (`_editTeamsMenu`), which never could edit
skills, with two changes while a custom team is on it (`CustomTeam_Editing()`,
`src/edit_tactics.c`), both as SWOS 2020's custom team editor has them:

- **IMPORT** takes another database team whole — its number included, since that is what the
  team's players are checked against — and stays available afterwards.
- **UNDO is LINEUP**: the team screen a match shows (`CustomTeam_Lineup`), where FIRE on two
  players swaps them and the formation is chosen. CONFIRM keeps it, EXIT drops it.

OK saves the slot. What is saved is rebuilt from the database team (`write_json` refuses a
team that is not one).

## 3. A team from the other player is checked

`CustomTeam_Allowed`, called where the lobby reads the other player's team
(`read_peer_state`, `src/netplay_lobby.c`): the team's number must be a team of the database
here, and each of its sixteen players must be one of that team's players — a different one
each — with the same position, skills and value. Names, faces, nationalities, shirt numbers,
kits, line-up and formation are free. (Shirt numbers have to be: the team screen swaps two
players' numbers when it swaps a starter and a substitute. The first version of this check,
in the `5b0be25` drop, compared them, and so refused any team arranged that way.) A team that fails has not been picked as far as this machine is
concerned: the other player shows as having no team, the lobby says YOUR OPPONENT'S TEAM IS NOT
A DATABASE TEAM, and a match cannot start. That covers every team, custom or not.

It reads the database into a buffer of its own (the JSON database, or the binary `TEAM.nnn`
where a country is not in it), because the lobby is polled from inside whatever menu the
player is in and the game's own team buffer belongs to that menu. The last answer is kept, as
the lobby asks several times a second.

Runtime fields a team screen writes (the cached match value, form, cards and injuries, goal
counters) are not part of the check: they differ between a team fresh from the list and one
that has been through the team screen.

## 4. A team is checked against its own database

A world (World 2026-27) replaces a country's team file in what the game reads, so while it is
played the team with a number is the world's. A custom team made from 96/97's team with that
number was being compared with the world's -- other players -- and refused. So `src/team_db.c`
keeps two databases, the world being played and 96/97's under it (`TeamDb_Team` / `Entry` the
world's, `TeamDb_Team9697` / `Entry9697` 96/97's, `TeamDb_ByUuid` the world's then 96/97's; each
`TeamDbEntry` says which it is in), and everything here checks a team against the one its
players are from: the world's team with its number first, then 96/97's. Resolving a custom team
loads its base from its own database (`game_team`: 96/97 on for the while). So a 96/97 custom
team plays with a world on, and one made from a world's club plays while that world is (with
96/97 alone it has no base: not used, said in the log). `SWOS_WORLD=2627 --custom-team-selftest`
runs every team of both (4,103 with World 2026-27). Replaces SWOS United's first fix, "a custom
team is always 96/97's" (2026-11-04), which left no way to base one on a world's club.

## On the team screen

A custom team is its database team's record under another name, so the line under the team's
name would name the country of the team it was made from — BORUSSIA FULDA, made from BRAZIL,
"(SOUTH AMERICA)". The team screen says **(CUSTOM)** there instead (`CustomTeam_IsCustom`):
for any database team whose name, coach, kits, or a player's name, face or nationality
differ. A line-up, a formation or swapped shirt numbers alone do not make a team custom.

## Picking one for a match

The lobby's TEAMS button: a player who has custom teams is shown the three slots first, with
OTHER TEAMS for the game's own list; a player with none goes straight to the list, as before
(`CustomTeam_Pick`).

## Checking it

`bin/SWOS.x86_64 --custom-team-selftest` first checks the ids (docs/TEAM_IDS.md §4), then
takes every team of the database (1,631) and requires: it is allowed as it is; it is refused
with one skill changed; renamed, redressed, with two players swapped in the line-up and two
in the record, two shirt numbers swapped and a nationality and a number changed, it is still
allowed, and comes back from the file's text as exactly the same team; the text has no
player data in it; a wrong checksum, an unknown team id and a player of another team are
each refused; and a skill written into the text by hand changes nothing. `SWOS_DATA_JSON=0`
runs it on the binary database. `JOIN_ENV="SWOS_LOBBY_LINEUP_SWAP=3:12"
scripts/relay_test.sh` plays a match whose joiner swapped a starter and a substitute.

`--menu=swos2020/selectMyCustomTeams` opens the slots, and what choosing one does, without the
matchmaking server.
