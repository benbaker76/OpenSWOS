# How to describe a challenge

Describe the position in ordinary words. You do not need to write JSON.
Start with **teams → minute → corner or kickoff → who has the ball**, then give
the score and the team you want to control.

```text
Teams: Partizan Belgrade vs Red Star, Yugoslavian league
Minute: 67
Situation: corner, left wing
Ball: Partizan
Score: Partizan 0–3 Red Star
I control: Partizan
Platform: Amiga
```

The same request in one sentence:

> Partizan Belgrade vs Red Star from the Yugoslavian league, minute 67,
> left-wing corner for Partizan, Red Star leading 3–0. I play Partizan. Amiga.

This is the working [PART67 example](challenge_specs/amiga_partizan_67.json).

## First the machine creates the file; then you play

There are two separate stages:

1. **Automatic recording / file creation.** The machine runs the game, sets up
   your requested position and saves it into a `.CHL` file. You do not need to
   control the players or press anything. On Amiga you may see kickoff, the first
   half, halftime and the players preparing the requested corner or kickoff.
   These are the machine's preparation steps. Once the position is saved, the
   generator closes its emulator. That planned closure is not a crash.
2. **You load the file and play.** Start the normal game, open **CHALLENGES** and
   select the generated file, such as `PART67.CHL`. It starts directly at your
   requested minute, score and situation. The preparation steps are not repeated.

“Recording” here means saving a playable game position, not recording a video or
a replay. SDL performs its preparation hidden and at high speed, so you normally
only see the finished result. Amiga preparation can be visible in the emulator.

## What you can choose

| Choice | Supported now | How to describe it |
| --- | --- | --- |
| Teams | Any two teams from the installed game data, including teams from different leagues or countries | Name both teams; add each team's country/league if the names could be confused. |
| Minute | Whole minutes **1–89**, in either half | “Minute 67.” The correct half and direction of play are set automatically. |
| Situation | **Corner** or **kickoff after a goal** | “Corner” or “kickoff.” |
| Ball | Either team takes the restart | “Partizan's ball” or “corner for Red Star.” |
| Score | Any score with **up to 16 goals in total**, subject to the kickoff rule below | “Partizan 0–3 Red Star.” A 0–0 corner is supported. |
| Human team | Either team; the other is controlled by the computer | “I play Partizan.” This is separate from who has the ball. |
| Corner wing | Left or right **as drawn on the pitch**, not relative to the team's attack | “Left-wing corner.” Default: left. |
| Goal scorers | A scorer from the relevant starting eleven and a minute for every goal | “Red Star number 9 scored at 15, 34 and 56.” Repeated scorers are supported. |
| Pitch condition | Frozen, muddy, wet, soft, normal, dry or hard | “Normal pitch.” Omit it for the game's random choice. |
| Tie rules | Extra time and penalties can each be enabled or disabled | “Extra time and penalties if level.” Both are on by default. |
| Output | Amiga or SDL `.CHL`, with an optional filename | “Amiga, save as PART67.CHL.” Each platform needs its own generated file. |

If scorers do not matter to you, say **“choose the scorers and goal times.”**
The generator still needs one goal entry per goal; the request can leave those
details to whoever prepares the spec. Goal times must be between minute 1 and
the challenge minute, and shirt numbers must belong to players in the starting
eleven. You can name a player and have their shirt number looked up.

Choosing clubs from a league selects those clubs' records. The generated match
is a standalone friendly challenge; it does not create a league fixture or alter
league standings.

**The teams do not need to belong to the same league or country.** Partizan
Belgrade from Yugoslavia can play Real Madrid from Spain, for example. Name each
team's league or country separately when useful; these identify the teams and
do not restrict who can play whom. Both teams just need to exist in the installed
game data.

## The kickoff rule

A kickoff is the restart **after the other team scored**. The team with the ball
must therefore have conceded at least one goal in the requested score.

- Partizan 0–3 Red Star, **Partizan kickoff**: supported.
- Partizan 0–3 Red Star, **Red Star kickoff**: not supported; Partizan has not scored.
- Partizan 1–3 Red Star, **either team's kickoff**: supported.
- 0–0 kickoff: not supported yet; choose a corner for a goalless challenge.

If the score contains goals by both teams, the generator uses the latest goal
by the opponent of the requested kickoff team as the restart. All other goals
are already included in the score. The saved clock is the requested challenge
minute, which may be later than the scorer's recorded goal time.

For a corner, either team can have the ball at any supported score. The engine
chooses the taker and places the players; a particular corner taker is not a
spec option yet.

## More request examples

> Partizan Belgrade from Yugoslavia vs Real Madrid from Spain, minute 67,
> left-wing corner for Partizan, Real Madrid leading 3–0. I play Partizan.
> Amiga. Choose the scorers and goal times.

> AC Milan vs Juventus, minute 88, kickoff for Juventus, Milan leading 2–0.
> I play Juventus. Amiga. Choose the scorers and goal times.

> Partizan vs Red Star from Yugoslavia, minute 30, right-wing corner for Red Star,
> score 0–0. I control Partizan. Normal pitch, no extra time or penalties. Amiga.

> AC Milan vs Juventus, minute 76, kickoff for Milan, Juventus leading 3–1.
> I play Milan. Choose the scorers and goal times. SDL.

## Not supported by the generator yet

- Free kicks, throw-ins, goal kicks, penalties as the starting situation, or a
  goalkeeper holding the ball.
- Open-play possession, such as “number 9 has the ball on the halfway line,” or
  manually chosen player positions.
- A starting position at minute 90 or later, during extra time or a penalty
  shootout. A loaded challenge can still progress into extra time and penalties.
- Specifying bookings, red cards, injuries, substitutions already used, custom
  starting lineups or tactics.
- Two human players, computer-only matches, or editing an existing `.CHL` by spec.

These limits describe generation from a request. The in-game **C** key can
capture other positions reached during normal play; see [saving and loading](CHALLENGE.md#saving-and-loading).

## For whoever prepares the JSON

**Side 1 is the first listed team; side 2 is the second.** Side numbers stay the
same after halftime. The human side and ball side are independent choices.

| Request | JSON field | Values/default |
| --- | --- | --- |
| Teams | `team1`, `team2` | Required. Native team names; Amiga also accepts `TEAM.nnn:ordinal`, SDL accepts UUIDs. |
| Minute | `minute` | Required integer, 1–89. |
| Situation | `situation` | `"corner"` or `"goal"` (**`"goal"` means kickoff**). If omitted: kickoff when there are goals, otherwise corner. |
| Ball | `ball` | `1` or `2`. If omitted: corner goes to the human side; kickoff goes to the conceder of the last goal. |
| Score | `scorers` | Array of `{ "side": 2, "shirt": 9, "minute": 15 }`. Up to 16 entries total; goals are sorted by minute. Use `[]` for 0–0. |
| Optional score check | `score` | `[team1Goals, team2Goals]`. Must agree with `scorers`; it cannot create goals on its own. |
| Human team | `human` | `1` or `2`; default `1`. |
| Corner wing | `corner.wing` | `"left"` or `"right"`; default `"left"`. Only for a corner. |
| Alternative corner owner | `corner.for` | `1` or `2`; prefer `ball`. If both are given they must agree. |
| Pitch condition | `pitch` | `0` frozen, `1` muddy, `2` wet, `3` soft, `4` normal, `5` dry, `6` hard. Omit for random. |
| Tie rules | `extra_time`, `penalties` | Booleans; both default to `true`. |
| Filename | `out` | A simple filename ending in `.CHL`. SDL defaults to the next free `CHALnn.CHL`; the Amiga helper defaults to `AMIGAGEN.CHL`. |

For the league-specific Partizan request, the native records are
`TEAM.041:3` (Partizan) and `TEAM.041:7` (Red Star). Use the
[complete working JSON](challenge_specs/amiga_partizan_67.json) as a template.
The other verified Amiga example is the
[Milan–Juventus kickoff](challenge_specs/amiga_milan_88.json).

The finished, user-tested Amiga files are included in the repository:

| File | Position | Human team |
| --- | --- | --- |
| [AMI88.CHL](../bin/save/AMI88.CHL) | AC Milan 2–0 Juventus, minute 88, Juventus kickoff | Juventus |
| [PART67.CHL](../bin/save/PART67.CHL) | Partizan 0–3 Red Star, minute 67, Partizan left-wing corner | Partizan |

They load from CHALLENGES with a compatible Amiga build. Both use standard Amiga
rules (cards enabled). Regenerate from their specs if a future build changes the
save layout.

Build/run commands and compatibility requirements are in
[Generating challenges](CHALLENGE.md#generating-challenges). Generate with the
target build and gameplay rules the player will use.
