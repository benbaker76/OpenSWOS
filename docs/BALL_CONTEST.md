# BALL_CONTEST.md — "ghosting", dead balls and "magnet" tackles: what the game does, and the proof

Testers playing online report players running through each other (ghosting), the ball
stopping dead with nobody taking it, and the ball being taken by a player who never slid
in ("magnet" and "back" tackles, "ball control lost"). The team's question was whether
that is the netcode (GGPO) or the game (SWOS).

**It is the game: four rules of the original, reproduced exactly.** This document
explains the rules, proves each is the original's, and gives the measurements from 37
recorded online matches and 11 recordings of the original PC game. It collects three
reports written for the testers on 2026-10-01/02 (ReportBot 0015, 0016, 0017). The tool
that produced the numbers is [CONTEST_LOG.md](CONTEST_LOG.md).

The gameplay is not being changed: OpenSWOS plays the original's rules.

## Summary

| | |
|---|---|
| the roll | Two men at the ball: a dice, weighted by tackling and ball control. The loser's whole TEAM is locked out of the ball for up to 24 frames, and the ball is stopped dead |
| the trap | A fast ball within about 8 px of a player is stopped and placed at his feet |
| the slide tackle | A slide that reaches the ball locks the other team out, with no dice |
| the over-turn | A dribbler who turns for longer than his ball control allows loses the ball |
| was the dice ever skipped? | No: 1,037 contacts, 974 rolled for, the other 63 each for a reason the rules give |
| are the outcomes the rule's? | Yes: 1,169 rolls, 0 not what the rule gives for the number drawn; weaker side lost 616 times against 609 expected |
| whose ratings? | One side's own; the other side's is its player with the same line-up number as the first man — an oddity of both originals |
| the network | Changed nothing the match decided. A player's screen was briefly out by half a player or more after 4% of rolls |
| did rollback show a different roll? | Rarely: in 102 of 2,785 cases a player's own game showed the event late, or one that never was; once with the other side winning. None of the 34 reported moments |
| the reported moments | All 34 accounted for: 28 rolls, 1 slide tackle, 1 lock-out still in force, 4 traps |

## The rules

### Rule 1: the roll for a contested ball

SWOS has no bodies. Two players can stand on the same spot; only the ball is contested.

When one player has the ball at his feet and an opponent runs onto it — no slide tackle,
just running into the ball — the game does not let both of them play it. It **rolls a
dice**:

1. It rates the two sides: the average of **tackling** and **ball control**.
2. With equal ratings it is a coin toss, 50/50. Each point of difference moves it about
   3% toward the better side, up to 72/28 at the largest possible difference.
3. The loser is **locked out of the ball**: for up to about half a second he cannot play
   it, although he keeps running wherever his stick points. The lock ends early if the
   winner leaves the ball.
4. **The ball is stopped dead** where it is, at the moment of the roll.

The roll happens only when all of this is true at once:

- the match is in play, and the ball is on the ground or low;
- one side's controlled player is "on the ball" (within about 8 pixels of it — a player
  is about 16 tall), holding a direction, and not pressing fire;
- the other side's controlled player reaches within about 6 pixels of it, also holding a
  direction, and is not already locked out.

In the two reported matches it happened 31 and 28 times: about once every 9 to 11
seconds of play.

**The lock-out is the team's, not the man's.** For those 24 frames no player of the
losing side can play the ball. In the 37 matches a *different* team-mate reached the ball
inside a lock-out 74 times and ran through the man on it.

### Rule 2: the trap of a fast ball

A player takes over a low ball when it is within about 6 pixels of him. For a **fast**
ball — a pass or a shot — the reach is wider, about 8 pixels. When that happens the ball
is **stopped and placed at his feet**, so a fast ball that was passing up to 7 pixels to
one side of him jumps to him.

It applies to both teams alike. So a pass struck with an opponent standing right beside
or just behind the passer can be taken by that opponent on the next frame, the ball
jumping back to him: no tackle, and it looks as if he pulled it in.

### Rule 3: a slide tackle that reaches the ball

The other team is locked out for the same 24 frames, with no dice (303 times in the 37
matches).

### Rule 4: the over-turn

Each step a dribbler spends turning with the ball counts against his ball control
(`ballControlTurnThresholds`: 4, 5, 6, 8, 11, 14, 17, 21 steps for ball control 0 to 7).
Past it he loses the ball and is locked out himself for 16 frames (148 times).

### What each complaint is

| reported as | what happened |
|---|---|
| **Ghosting** | A roll the challenger loses — or a team-mate arriving inside the lock-out. He cannot play the ball and runs straight through the man on it |
| **Dead ball** | A roll after which *neither* takes the stopped ball: the loser is locked out, and the winner was already running past it |
| **Back tackle, ball control lost** | A roll the man on the ball loses: the challenger, who never slid, takes the stopped ball from any side |
| **Magnet tackle** | The trap: a pass or shot pulled up to 8 px sideways, or backwards, to an opponent's feet |

### One of each, frame by frame

**Ghosting — recording 1, frame 4685.** Mladen's number 11 is dribbling left with the
ball 4 pixels from his feet. Predi's number 3 runs down onto it and reaches 4 pixels
from the ball. The roll is thrown; Predi's man loses. The ball stops for one frame, then
Mladen's man plays it on, still dribbling left. Predi's man, locked out, keeps running
down through the spot where the two met.

**Dead ball — recording 1, frame 1102.** A loose ball. Both men arrive running the same
way, down and to the right, both already slightly past it (the ball is 5 to 7 pixels
behind each of them). The roll is thrown; Predi's man loses and is locked out, and the
ball is stopped. Two frames later Mladen's man is 8 pixels past it, too far to touch.
Both run on. The ball lies still for 49 frames, until one of Predi's players comes back
for it.

**Back tackle — recording 2, frame 8997.** Predi's E. Chiesa is running up the pitch
with the ball. Mladen's F. Asprilla arrives at the ball from the side. The roll is thrown
and Mladen's man is the one locked out — but Predi's man was already moving away from
the stopped ball, so the lock ends at once, and two frames later Mladen's man takes it.
No slide was involved at any point.

**Magnet tackle — recording 2, frame 11708.** Mladen's man is dribbling right and kicks
the ball; on frame 11707 it has left his foot and is 4 pixels ahead of him, at speed.
Predi's R. Mussi is standing 4 pixels behind him. On the next frame the ball is 7 pixels
back, stopped, at Mussi's feet.

## The roll, exactly

`CalculateIfPlayerWinsBall` (`src/ball_possession.c`) and the gates before it in
`UpdatePlayers` (`src/update_players.c`); `data/SWOS2.s` lines 23430 and 30256.

**When it runs.** The game updates ONE team a frame, alternating (`frameCount` in the
Amiga release, `teamSwitchCounter` in the PC one). On team A's turn, with A's controlled
player P, there is a roll only if all of these hold:

| # | condition | if it fails |
|---|---|---|
| 1 | the match is in open play | nothing |
| 2 | P holds a direction | P does not go for the ball |
| 3 | team A is not locked out — after its lock is cleared if B's man is no longer on the ball | P cannot play the ball (`self-locked`) |
| 4 | P is within reach: squared distance to the ball ≤ 32, about 5.7 px | P does not touch it (`not-on-it`) |
| 5 | the ball is low: height under 12 | no touch (`ball-high`) |
| 6 | team B is not locked out | no roll: P plays the ball freely (`opp-locked`) |
| 7 | B's controlled player Q is "on the ball": squared distance ≤ 72 (about 8.5 px), B not pressing fire, not shooting. The flag is set on B's own turn | no roll: P plays the ball (`opp-not-on-ball`) |
| 8 | Q holds a direction | no roll: P plays the ball (`opp-still`) |

(The words in brackets are the reasons the contest log prints, [CONTEST_LOG.md](CONTEST_LOG.md).)

**The roll.**

```
ratingA = (tackling + ball control) >> 1   of team A's player in Q's LINE-UP SLOT
ratingB = (tackling + ball control) >> 1   of Q himself

diff = ratingA - ratingB
if diff >= 0:  weaker = B                  ; a tie counts B as the weaker
else:          weaker = A,  diff = -diff

odds = plAvgTacklingBallControlDiffChance[diff]     ; 16, 17, 18, 19, 20, 21, 22, 23
r    = Rand() & 31                                  ; 0..31

if r < odds:   loser = weaker
else:          loser = the other team

loser.wonTheBallTimer = 12                 ; the lock: the name notwithstanding
ball.speed = 0;  ball.dest = ball.position ; the ball is stopped dead
```

Tackling and ball control are the skills as they stand in the match, 0 to 7, after the
game's form and position adjustment. `Rand()` is the match's own generator, part of the
state two netplay peers share.

| ratings apart | odds the weaker side is locked out |
|---|---|
| 0 | 16 in 32 = 50.0% |
| 1 | 17 in 32 = 53.1% |
| 2 | 18 in 32 = 56.3% |
| 3 | 19 in 32 = 59.4% |
| 4 | 20 in 32 = 62.5% |
| 5 | 21 in 32 = 65.6% |
| 6 | 22 in 32 = 68.8% |
| 7 | 23 in 32 = 71.9% |

Skill moves the odds very little: the rating is two skills averaged and rounded down, so
two whole points of tackling shift the chance by about 3%. In the 37 recorded matches
1,027 of 1,169 rolls had ratings 0 or 1 apart.

**`ratingA` is the original's oddity**: not P's rating but that of A's player in the same
line-up slot as Q (next section). The slot is the player's place in the eleven on the
pitch, 1 to 11 with the keeper at 1 (`Sprite.playerOrdinal`) — not the shirt number
printed on him, which is a separate value the roll never reads. It follows the slot, not
the person: a player moved in the line-up, or a substitute, takes the slot over.

**The lock.** `wonTheBallTimer` counts down once each time its team is updated, so 12
is up to **24 frames**: 0.48 s in Amiga gameplay (50 frames a second), 0.34 s in PC
gameplay (70). It is the TEAM's — no player of that side can play the ball — and while
either side's is running there is no new roll. It ends early, on the locked team's turn,
if the other side's man is no longer on the ball (more than 8.5 px away, or pressing
fire). The two other locks are a slide tackle that reaches the ball (the other team, 12)
and an over-turn (the dribbler's own team, 8 = up to 16 frames).

**When the dice is rolled again.** As soon as the conditions hold with neither side
locked. In the 37 matches the same two men rolled again within about a second 99 times,
in two groups: 23 to 25 frames later (about 35 cases: they stayed together for the whole
lock) and 3 to 8 frames later (about 30: the winner came off the ball for a moment, which
cleared the lock, and came straight back).

## Whose ratings the roll uses

A roll between two players should use those two players' tackling (T) and ball control
(C). The original does not: one side is rated by a team-mate. It looks like a slip in the
original — one register not switched before the first lookup — and it is in both
releases, so OpenSWOS keeps it.

### How the game picks the number

The game updates **one team per frame**, alternating. A roll can only be triggered on a
team's own turn, when:

- its controlled player is within reach of the ball (about 6 pixels), holding a
  direction, and not locked out; and
- the other team's controlled player is "on the ball" (within about 8 pixels, not
  pressing fire) and holding a direction.

Call the team whose turn triggers the roll **A**, and the other **B**. Both ratings are
then looked up with **B's man's line-up number**:

| side | rated by |
|---|---|
| B | B's man himself: (T + C) / 2 |
| A | **A's player with B's man's number**: (T + C) / 2 — not the man at the ball |

#### Playaveli's example: Shearer (9) meets Hierro (4)

| whose turn triggers the roll | what is compared |
|---|---|
| Shearer's team | Shearer's team's **number 4** against Hierro — "4 v 4" |
| Hierro's team | Hierro's team's **number 9** against Shearer — "9 v 9" |

So the game never chooses between "9 v 9" and "4 v 4" on any football ground. It is
whichever team's turn comes on the first frame both conditions hold: a frame or two of
timing as the two men close on the ball.

### The original PC game's own recordings

We test OpenSWOS by replaying recordings of the **original PC game** frame by frame and
comparing 786 values on every frame against what the original had. A recording made with
the random numbers fixed is a clean test of the ratings: the number drawn is always the
same, so who is locked out depends only on which side the game rated lower.

| recording | with the original's lookup | with "the two players' own T and C" |
|---|---|---|
| `playa-10090613` | matches all 22,497 frames | stops matching at frame 7,009 |
| `playa-10095214` | matches all 22,401 frames | stops matching at frame 3,012 |
| `playa-10144556` | matches all 21,061 frames | stops matching at frame 1,383 |

The right-hand column was a one-off test — the rule changed for a single run, then put
back. OpenSWOS plays the left-hand column; it has to, to reproduce the original.

#### One roll from `playa-10090613`, frame 7007

K. MOLLER (T3 C5) meets D. ALBERTINI (T7 C4). The draw in this recording is always 13.

| | Moller's side | Albertini's side | result |
|---|---|---|---|
| the two players' own ratings | 4 | 5 | Moller is weaker; 13 is under 17, so **Moller's side is locked out** |
| what the original does | **5** — read from Z. RADIC, Moller's team-mate with Albertini's number (8) | 5 | a tie; 13 is under 16, so **Albertini's side is locked out** |

Two frames later, the "own ratings" version no longer matches the recording.

#### How often it matters

| | rolls | one side's rating was a team-mate's and differed from the man's own | the side locked out is not the one the two men's own ratings give |
|---|---|---|---|
| 11 original PC recordings (fixed draws) | 209 | 142 | 50 |
| 37 online matches on OpenSWOS | 1,169 | 673 | — |

(For the online matches the draw is random, so "who would have lost" cannot be counted
the same way. In 1,111 of the 1,169 rolls the two men had different line-up numbers; in
the other 58 they had the same one, and the lookup happens to be right.)

### The fix, as a setting

`[tweaks] contest_own_ratings = true` (`--contest-own-ratings=on`; RULES OPTIONS' BALL CONTEST)
rates each side by the man actually at the ball. **On by default since 2026-11-10** (a tweak: off
online and under the rigs, which play the original; it was `[gameplay] contest_own_ratings`,
default false). In the Amiga code it is one
instruction — put this team's own man back in `a1` before the first lookup:

```
        movea.l TEAMGENERALINFO_CONTROLLEDPLAYER(a0),a1
        cmpa.l  #0,a1
        beq.w   skip_ball_possession_calc
+       movea.l 8(sp),a1                        ; our own man again (saved by the movem above)
        bsr.w   GetPlayerPointerFromShirtNumber
```

and in C (`src/ball_possession.c`) the first lookup takes `player` instead of the
opponent's man. A build with no `SWOS.ini` (the Amiga's) takes it from
`-DSWOS_CONTEST_OWN_RATINGS=1`.

It applies to **offline matches only**. Two netplay peers must roll the same dice and a
recording must be replayed by the rule it was played with, and neither the handshake nor
the `.SRP` says which rule that is: the handshake's rules byte is full, and the server's
recorder would have to carry it too. So an online match, every `--srp-resim` and the
replay rig play the original's whatever the setting says. Making it selectable online
means a second rules byte in the handshake (its spare byte 19), in `SIMV` (its spare
byte 3) and in the recorder's protocol.

## Proof that this is the original game

### The roll

The routine is `CalculateIfPlayerWinsBall`. Its core, from the Amiga original
(`data/SWOS2.s`, line 23430):

```
        tst.w   TEAMGENERALINFO_WONTHEBALLTIMER(a0)   ; opponent locked out?  no roll
        bne.w   set_default_ball_target
        tst.w   TEAMGENERALINFO_PLAYERHASBALL(a0)     ; opponent not on the ball?  no roll
        beq.w   set_default_ball_target
        tst.w   TEAMGENERALINFO_CURRENTALLOWEDDIRECTION(a0)
        bmi.w   set_default_ball_target               ; opponent standing still?  no roll
        ...
        move.b  PLAYERINFOHEADER_TACKLING(a4),d1      ; (tackling + ball control) / 2
        add.b   PLAYERINFOHEADER_BALLCONTROL(a4),d1
        lsr.b   #1,d1
        ...                                           ; the same for the other side, in d0
        sub.b   d0,d1                                 ; the difference picks the odds
        ...
        lea     plAvgTacklingBallControlDiffChance(pc),a0
        jsr     Rand(pc)
        andi.w  #$1F,d0                               ; a number from 0 to 31
        cmp.b   (a0,d1.w),d0                          ; against 16, 17, ... 23
        ...
award_ball_possession:
        move.w  #12,TEAMGENERALINFO_WONTHEBALLTIMER(a6)   ; the 12-step lock
        clr.w   SPRITE_SPEED(a2)                          ; the ball is stopped
        move.w  SPRITE_X(a2),SPRITE_DESTX(a2)
        move.w  SPRITE_Y(a2),SPRITE_DESTY(a2)
```

The PC original (`swos-port/swos/swos.asm`, line 108037) is the same routine with the
same numbers: the same three tests, the same average, the same table
`16, 17, 18, 19, 20, 21, 22, 23`, the same `wonTheBallTimer = 12`, and its own comment
on the last step reads `; stop the ball`.

OpenSWOS's version (`src/ball_possession.c`, and its caller in
`src/update_players.c`) was compared with both, line by line, on 2026-10-01. They agree.
An earlier check replayed 186 of these rolls from recorded Amiga, PC and SWOS 2020
matches and got the same result for every one (docs/NETPLAY_GHOSTING.md).

### Whose ratings

**The Amiga original.**

`CalculateIfPlayerWinsBall` is entered with `a6` = the team whose turn it is and `a1` = its
man. From the disassembly (`SWOS2.s`, line 23430):

```
        movea.l 0(a6),a0                                ; a0 = the OTHER team
        ...
        movem.l d0-d1/a1/a6,-(sp)                       ; its own man (a1) is put away
        movea.l TEAMGENERALINFO_CONTROLLEDPLAYER(a0),a1 ; a1 = the OTHER team's man
        ...
        bsr.w   GetPlayerPointerFromShirtNumber         ; a1 = their man, a6 = STILL OUR team
        move.b  PLAYERINFOHEADER_TACKLING(a4),d1
        add.b   PLAYERINFOHEADER_BALLCONTROL(a4),d1
        lsr.b   #1,d1                                   ; "our" rating
        movea.l TEAMGENERALINFO_OPPONENTSTEAM(a6),a6    ; a6 = the other team
        movea.l TEAMGENERALINFO_CONTROLLEDPLAYER(a6),a1 ; a1 = their man again
        ...
        bsr.w   GetPlayerPointerFromShirtNumber         ; a1 = their man, a6 = their team
        move.b  PLAYERINFOHEADER_TACKLING(a4),d0
        add.b   PLAYERINFOHEADER_BALLCONTROL(a4),d0
        lsr.b   #1,d0                                   ; their man's own rating
        sub.b   d0,d1                                   ; the difference picks the odds
```

and the lookup itself (line 23871):

```
GetPlayerPointerFromShirtNumber:
        move.w  SPRITE_PLAYERORDINAL(a1),d0             ; the NUMBER comes from the player in a1
        subq.w  #1,d0
        ...
        movea.l TEAMGENERALINFO_INGAMETEAMPTR(a6),a4    ; the TEAM comes from a6
        lea     (a4,d0.w),a4
        rts
```

The first call passes the other team's man and our own team: it returns **our player with
their man's number**. Our own man at the ball was saved on the stack and is not used.

**The PC original.**

The same routine in the PC version's disassembly (`swos.asm`, line 108037) has the same
instructions. The person who disassembled it noticed the oddity and left his own comments:

```
mov     eax, [esi+TeamGeneralInfo.controlledPlayerSprite]
mov     A1, eax
...
call    GetPlayerPointerFromShirtNumber ; A1 -> opponent's controlled player, but A6 -> this team?
...
shr     byte ptr D1, 1 ; D1 = our(?) controlled player tackling and ball control average
```

Two independent disassemblies of two releases agree.

### The trap

From the Amiga original (`data/SWOS2.s`, line 31146, and `UpdateControllingPlayer` at
27278):

```
pass_chase_try_autoswitch:
        tst.b   TEAMGENERALINFO_BALLLESSEQUAL4(a6)       ; only a low ball
        beq.w   pass_follow_decide_controller_or_anchor
        cmpi.w  #1536,(ballSprite+SPRITE_SPEED).l        ; a fast ball (6.0)...
        bcs.s   autoswitch_require_very_close_gate
        tst.b   TEAMGENERALINFO_PLCLOSETOBALL(a6)        ; ...is taken from "close"
        bne.s   autoswitch_commit_to_receiver
autoswitch_require_very_close_gate:
        tst.b   TEAMGENERALINFO_PLVERYCLOSETOBALL(a6)    ; a slow one from "very close"
        beq.w   pass_follow_decide_controller_or_anchor
autoswitch_commit_to_receiver:
        ...
        bsr.w   UpdateControllingPlayer

UpdateControllingPlayer:
        ...
        clr.w   ballSprite+SPRITE_SPEED-ballSprite(a0)   ; the ball is stopped
        move.w  d1,SPRITE_X(a0)                          ; and placed at his feet
        move.w  d2,SPRITE_Y(a0)
```

"Very close" is a squared distance of 32 or less (5.7 pixels) and "close" 72 or less
(8.5 pixels); the same two thresholds decide who is "on the ball" for rule 1. SWOS
United's version is in `src/update_players.c` and `src/ball_physics.c`.

The same code runs for every match: against the computer, two players at one machine, or
online. The team whose turn it is each frame alternates on `frameCount` in the Amiga
release and on its own counter in the PC one (`src/team_control.c`).

## The measurements: 37 online matches

Every recording from 28 September to 2 October 2026 that the build could replay: 540,648
frames, 180 minutes of play. Each was re-simulated from its inputs — which must match the
state both players' machines computed, frame for frame — with the game writing a line for
every decision ([CONTEST_LOG.md](CONTEST_LOG.md)).

### Was the dice ever skipped?

A *contact* is a run of frames in which both sides' controlled players are within reach
of the ball in open play.

| what the game decided | contacts |
|---|---|
| **rolled the dice** | **974** |
| no roll: the other man was not "on the ball" — he was pressing fire (a pass, a shot, a slide) | 32 |
| no roll: the ball was in the air | 15 |
| no roll: one side was still locked out from a roll, a slide tackle or an over-turn just before | 12 |
| no roll: the other man was standing still (no direction held), so the ball is simply taken | 4 |
| no roll, for no reason the rules give | **0** |

So the case to look for — two men at the ball, moving, on the ground, nobody locked out,
and no roll — did not happen once.

### Are the outcomes the rule's?

Each side is rated: (tackling + ball control) / 2. The weaker side is locked out if the
number drawn (0 to 31) is under the odds; otherwise the stronger side is. On equal ratings it is an even toss.

| ratings apart | rolls | weaker side locked out | the rule's odds |
|---|---|---|---|
| 0 | 540 | 277 = 51.3% | 16 in 32 = 50.0% |
| 1 | 487 | 260 = 53.4% | 17 in 32 = 53.1% |
| 2 | 136 | 77 = 56.6% | 18 in 32 = 56.2% |
| 3 | 6 | 2 | 19 in 32 = 59.4% |
| all | 1,169 | 616 | 608.8 expected, give or take 17 |

The numbers drawn split 596 under 16 and 573 from 16 up, as an even dice should.

One example, as the log has it (recording 1 below, frame 4685 — reported as ghosting):

> ROLL between PREDI's F. HIERRO (T5 C4) and MLADEN's A. SHEARER (T6 C5). Ratings used:
> PREDI 6 (read from R. FERNANDEZ; F. HIERRO's own is 4), MLADEN 5. The weaker side
> (MLADEN) is locked out on a draw under 17 of 32; drew 18: PREDI is locked out.

Predi's side had the better rating and still lost: 18 was one too many. Locked out,
Hierro ran through Shearer, who kept the ball. (T and C are tackling and ball control.
The 6 is a team-mate's rating, not Hierro's own — see below.)

### The other three ways the ball changes hands

Over the 37 matches, with no roll involved:

| | times | what it is |
|---|---|---|
| slide tackle reaches the ball | 303 | the other side is locked out for up to 24 frames |
| dribbler over-turns | 148 | a man turning with the ball for longer than his ball control allows loses it and is locked out himself (ball control 0 allows 4 steps, 7 allows 21) |
| fast ball trapped off the other side, jumping 4 px or more | 221 | a pass or shot within about 8 px of an opponent is stopped and placed at his feet — the "magnet" |

## The 34 reported moments

Recordings: 1 = `20260930-233136-5fe4fab1`, 2 = `20261001-000430-944401ad`,
3 = `20260930-234310-f5a26a01`, 4 = `20261001-001031-cb908a04`; Predi v Mladen in all
four. Open a frame at `https://us.swos.eu/playback/SWOS.html?srp=<file>.SRP&f=<frame>`.

| rec. | reported as | frames | what the game decided | each screen in the 12 frames after |
|---|---|---|---|---|
| 1 | ghosting | 4678–4690 | **roll** at 4685: PREDI's F. HIERRO v MLADEN's A. SHEARER, ratings 6 v 5, weaker side out under 17, drew 18: **PREDI locked out** | PREDI as played; MLADEN up to 1 px off |
| 1 | ghosting | 4976–4996 | **roll** at 4983: PREDI's D. SUKER v MLADEN's A. SHEARER, ratings 6 v 5, weaker side out under 17, drew 0: **MLADEN locked out** | PREDI as played; MLADEN as played |
| 1 | ghosting | 5075–5116 | **trap** at 5093: PREDI's F. REDONDO takes MLADEN's ball at speed 1777, it jumps 7 px<br>**roll** at 5096: MLADEN's L. FERDINAND v PREDI's F. REDONDO, ratings 5 v 5, weaker side out under 16, drew 10: **PREDI locked out** | PREDI as played; MLADEN as played |
| 1 | dead ball | 1090–1190 | **roll** at 1102: MLADEN's F. ASPRILLA v PREDI's R. FERNANDEZ, ratings 5 v 6, weaker side out under 17, drew 21: **PREDI locked out**<br>**roll** at 1159: PREDI's M. LASA v MLADEN's L. FERDINAND, ratings 4 v 4, weaker side out under 16, drew 18: **PREDI locked out** | PREDI as played; MLADEN up to 7 px off |
| 1 | dead ball | 7915–7950 | **trap** at 7927: MLADEN's F. ASPRILLA takes PREDI's ball at speed 1905, it jumps 6 px<br>**roll** at 7930: PREDI's R. FERNANDEZ v MLADEN's F. ASPRILLA, ratings 6 v 5, weaker side out under 17, drew 13: **MLADEN locked out** | PREDI up to 2 px off; MLADEN as played |
| 1 | dead ball | 12460–12530 | **trap** at 12473: MLADEN's F. ASPRILLA takes PREDI's ball at speed 2331, it jumps 6 px<br>**roll** at 12476: PREDI's V. SANCHEZ v MLADEN's F. ASPRILLA, ratings 6 v 5, weaker side out under 17, drew 6: **MLADEN locked out** | PREDI as played; MLADEN as played |
| 1 | magnet tackle | 8465–8480 | **trap** at 8476: PREDI's D. SUKER takes MLADEN's ball at speed 2173, it jumps 7 px | PREDI up to 1 px off; MLADEN as played |
| 2 | ghosting | 3822–3838 | **roll** at 3830: MLADEN's F. ASPRILLA v PREDI's P. MALDINI, ratings 5 v 5, weaker side out under 16, drew 27: **MLADEN locked out** | PREDI as played; MLADEN up to 1 px off |
| 2 | ghosting | 4980–4990 | **inside a lock-out**: roll at 4964, MLADEN's A. SHEARER v PREDI's keeper A. PERUZZI, ratings 0 v 0, weaker side out under 16, drew 11: **PREDI locked out**. The lock is on the whole team for up to 24 frames, so PREDI's C. FERRARA, who reaches the ball at 4985, cannot play it and runs through | PREDI as played; MLADEN as played |
| 2 | ghosting | 6160–6175 | **slide tackle** at 6169: PREDI's C. FERRARA reaches the ball, MLADEN locked out (no roll) | PREDI as played; MLADEN as played |
| 2 | dead ball | 1924–1972 | **trap** at 1938: MLADEN's L. FERDINAND takes PREDI's ball at speed 1776, it jumps 6 px<br>**roll** at 1942: MLADEN's L. FERDINAND v PREDI's G. ZOLA, ratings 5 v 6, weaker side out under 17, drew 16: **MLADEN locked out**<br>**roll** at 1973: PREDI's R. MUSSI v MLADEN's L. FERDINAND, ratings 5 v 3, weaker side out under 18, drew 6: **MLADEN locked out** | PREDI as played; MLADEN up to 11 px off |
| 2 | dead ball | 12850–12875 | **roll** at 12864: PREDI's R. MUSSI v MLADEN's A. SHEARER, ratings 5 v 6, weaker side out under 17, drew 28: **MLADEN locked out** | PREDI up to 1 px off; MLADEN as played |
| 2 | dead ball | 14425–14466 | **roll** at 14433: MLADEN's A. SHEARER v PREDI's F. RAVANELLI, ratings 6 v 5, weaker side out under 17, drew 1: **PREDI locked out** | PREDI as played; MLADEN as played |
| 2 | magnet tackle | 834–850 | **trap** at 846: MLADEN's F. ASPRILLA takes PREDI's ball at speed 1872, it jumps 7 px | PREDI as played; MLADEN as played |
| 2 | magnet tackle | 11695–11720 | **trap** at 11708: PREDI's R. MUSSI takes MLADEN's ball at speed 1904, it jumps 7 px<br>**roll** at 11711: MLADEN's L. FERDINAND v PREDI's R. MUSSI, ratings 4 v 5, weaker side out under 17, drew 9: **MLADEN locked out** | PREDI as played; MLADEN up to 2 px off |
| 2 | back tackle | 8974–9010 | **roll** at 8997: MLADEN's F. ASPRILLA v PREDI's E. CHIESA, ratings 3 v 5, weaker side out under 18, drew 13: **MLADEN locked out** | PREDI as played; MLADEN as played |
| 2 | ball control lost | 11893–11988 | **roll** at 11932: PREDI's F. RAVANELLI v MLADEN's S. HOWEY, ratings 4 v 4, weaker side out under 16, drew 30: **PREDI locked out** | PREDI as played; MLADEN up to 2 px off |
| 3 | ghosting | 10705–10733 | **roll** at 10718: MLADEN's A. SHEARER v PREDI's M. SILVA, ratings 4 v 4, weaker side out under 16, drew 3: **PREDI locked out** | PREDI as played; MLADEN as played |
| 3 | ghosting | 11612–11632 | **roll** at 11627: PREDI's ALDAIR v MLADEN's A. SHEARER, ratings 5 v 6, weaker side out under 17, drew 12: **PREDI locked out** | PREDI as played; MLADEN as played |
| 3 | ghosting | 12339–12355 | **roll** at 12349: PREDI's BEBETO v MLADEN's L. FERDINAND, ratings 4 v 4, weaker side out under 16, drew 23: **PREDI locked out** | PREDI as played; MLADEN as played |
| 3 | dead ball | 2530–2595 | **roll** at 2542: PREDI's RONALDAO v MLADEN's A. SHEARER, ratings 5 v 6, weaker side out under 17, drew 27: **MLADEN locked out**<br>**roll** at 2584: PREDI's RONALDAO v MLADEN's A. SHEARER, ratings 5 v 6, weaker side out under 17, drew 11: **PREDI locked out** | PREDI up to 2 px off; MLADEN as played |
| 3 | dead ball | 2643–2700 | **roll** at 2673: MLADEN's L. FERDINAND v PREDI's RONALDAO, ratings 5 v 5, weaker side out under 16, drew 3: **PREDI locked out** | PREDI as played; MLADEN as played |
| 3 | dead ball | 15133–15194 | **roll** at 15144: MLADEN's F. ASPRILLA v PREDI's JUNINHO, ratings 5 v 4, weaker side out under 17, drew 21: **MLADEN locked out** | PREDI as played; MLADEN as played |
| 3 | back tackle | 9588–9620 | **roll** at 9612: MLADEN's F. ASPRILLA v PREDI's ZINHO, ratings 5 v 3, weaker side out under 18, drew 1: **PREDI locked out** | PREDI as played; MLADEN as played |
| 4 | ghosting | 3595–3622 | **roll** at 3611: PREDI's C. FERRARA v MLADEN's A. SHEARER, ratings 4 v 6, weaker side out under 18, drew 2: **PREDI locked out** | PREDI as played; MLADEN as played |
| 4 | ghosting | 8480–8510 | **roll** at 8494: PREDI's F. RAVANELLI v MLADEN's J. BERESFORD, ratings 5 v 5, weaker side out under 16, drew 17: **PREDI locked out** | PREDI as played; MLADEN as played |
| 4 | ghosting | 13997–14008 | **roll** at 14006: PREDI's P. MALDINI v MLADEN's L. FERDINAND, ratings 6 v 4, weaker side out under 18, drew 27: **PREDI locked out** | PREDI as played; MLADEN as played |
| 4 | dead ball | 1945–1988 | **roll** at 1961: PREDI's R. MUSSI v MLADEN's F. ASPRILLA, ratings 5 v 4, weaker side out under 17, drew 6: **MLADEN locked out**<br>**roll** at 1985: PREDI's F. RAVANELLI v MLADEN's K. GILLESPIE, ratings 5 v 4, weaker side out under 17, drew 6: **MLADEN locked out** | PREDI as played; MLADEN as played |
| 4 | dead ball | 3836–3922 | **roll** at 3856: MLADEN's J. BERESFORD v PREDI's A. DEL PIERO, ratings 3 v 4, weaker side out under 17, drew 3: **MLADEN locked out** | PREDI as played; MLADEN as played |
| 4 | dead ball | 10404–10438 | **roll** at 10420: PREDI's P. MALDINI v MLADEN's L. FERDINAND, ratings 6 v 4, weaker side out under 18, drew 4: **MLADEN locked out** | PREDI as played; MLADEN as played |
| 4 | dead ball | 11281–11406 | **roll** at 11308: PREDI's R. MUSSI v MLADEN's J. BERESFORD, ratings 5 v 5, weaker side out under 16, drew 24: **PREDI locked out**<br>**roll** at 11385: MLADEN's R. LEE v PREDI's E. CHIESA, ratings 4 v 6, weaker side out under 18, drew 24: **PREDI locked out** | PREDI as played; MLADEN as played |
| 4 | dead ball | 11575–11768 | **roll** at 11581: MLADEN's L. FERDINAND v PREDI's P. MALDINI, ratings 5 v 5, weaker side out under 16, drew 25: **MLADEN locked out**<br>**roll** at 11704: PREDI's P. MALDINI v MLADEN's F. ASPRILLA, ratings 6 v 4, weaker side out under 18, drew 22: **PREDI locked out**<br>**roll** at 11712: PREDI's P. MALDINI v MLADEN's F. ASPRILLA, ratings 6 v 4, weaker side out under 18, drew 20: **PREDI locked out** | PREDI as played; MLADEN as played |
| 4 | magnet tackle | 10301–10320 | **trap** at 10311: MLADEN's F. ASPRILLA takes PREDI's ball at speed 2173, it jumps 8 px | PREDI as played; MLADEN as played |
| 4 | magnet tackle | 11541–11570 | **trap** at 11568: PREDI's P. MALDINI takes MLADEN's ball at speed 1882, it jumps 7 px | PREDI as played; MLADEN as played |

By what was reported:

| reported as | moments | what they were |
|---|---|---|
| ghosting | 12 | 10 rolls, 1 slide tackle, 1 team lock-out from a roll 21 frames earlier |
| dead ball | 14 | 14 rolls |
| magnet tackle | 5 | 4 traps alone, 1 trap followed by a roll |
| back tackle | 2 | 2 rolls |
| ball control lost | 1 | 1 roll |

## What rollback did

The log is the match as played, so no roll in it was skipped or undone. What a player
*saw* can differ for a few frames, and the recording measures that:

- In the 34 reported moments, Predi's screen showed the match as played in 30; in the
  other 4 something was at most 2 pixels out. Mladen's was as played in 28; in the other 6 the worst was 11 pixels, in one dead ball.
- Over all 1,169 rolls: 274 were first played by at least one player's game on a wrong
  guess of the other's input, and corrected a frame or two later. In 48 (4%) that
  correction moved something on a screen by 8 pixels (half a player) or more.

So rollback does show a player a wrong picture now and then, briefly. It is not what
produced these 34 moments, and it never changed what the match decided.

## Did rollback show a player a different roll?

The theory (2026-10-02): on your screen the dice is rolled and you win; a frame later
GGPO rolls the game back, and with the other player's real input the roll comes out the
other way, or happens on another frame, or not at all. The match is right, but what you
watched was not.

It can happen, and it was measured. Each player's own game was replayed for every frame
it first played on a guess — 604,143 frames over the 31 matches that have both players'
data — and every one of them put every man and the ball exactly where the recording says
that player's screen drew them, so the replay is that screen ([CONTEST_LOG.md](CONTEST_LOG.md),
"Views"). Then, for each roll, slide tackle and over-turn (2,785, counting each once a
player):

| what that player's game showed | events |
|---|---|
| played on real input: as it happened | 940 |
| played on a guess, and the same | 1,756 |
| the same, a frame or so early or late | 3 |
| **the other side locked out, then corrected** | **1** |
| not shown on that frame: it appeared with the correction, 1 to 3 frames later | 85 (24 rolls, 11 slide tackles, 50 over-turns) |
| shown, and never in the match (a phantom) | 16 (12 rolls, 3 slide tackles, 1 over-turn) |

So the theory's exact case — you see yourself win the roll and then lose it — happened
**once** in 31 matches (`20260930-235924-c0c7b282`, frame 2406: Predi's game showed
Mladen locked out, the match locked out Predi; corrected 3 frames later, 6 pixels).
Rollback did make a player's game show something else at the ball in 102 of 2,785 cases
(3.7%), nearly always by showing the event one to three frames late. The correction that
followed moved something on his screen by a median of 4 pixels; by 8 pixels (half a
player) or more in 21 of the 102.

**None of the 34 reported moments is one of them.** In every one, both players' games
showed the roll, tackle or trap as the match had it.

**The team that moves on a frame** is not disturbed by rollback. Which team is updated
alternates on a counter that is part of the match state (`frameCount` in the Amiga
release, `teamSwitchCounter` in the PC one — both in the rollback registry,
`src/sim_state.c`), so a frame played again is played for the same team as the first
time, and both machines' state hashes agree on every frame. What a wrong guess can change
is which team *triggers* a roll, because it changes who reaches the ball when — that is
what the 102 cases above are.

## What this does not show

- What the match did and what each screen was drawn from, not what a player perceived.
- Only recordings the build can replay: one from a build with other rules is refused or
  stops matching. 16 of 53 were left out (3 recorded to be watched only, 6 from before the
  view was recorded, 7 that stopped before their end).
- The replay test against the original PC recordings compares 786 values a frame but not
  the lock-out itself: a wrong lock-out shows through what it causes.
- Whether the original's rules are good online. Three things could still make them bite
  harder there than offline, and none has been measured: whether rolls are as frequent
  offline; line-ups left in their default order (a player out of position loses up to a
  quarter of every skill, ReportBot 0014); and input delay making challenges harder to
  time.
