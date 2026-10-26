# PENALTY_SHOOTOUT.md — a friendly that is nothing but the shootout

A training mode: the match is the engine's own penalty shootout — the one a
drawn cup tie already goes to — from its first frame. Best of five, sudden
death after, then the result screen. The defending side's keeper is its
player's to throw (below), which is the point of the drill: practise guessing.

Both targets. SDL and Amiga both get the mode, the instant start and the
manual keeper — all of it is shared match code (`src/penalty_shootout.c` has
no SDL guards).

## Using it

Main menu → **PRACTICE** (the left column's second row, under EDIT) →
**PENALTY SHOOTOUT**. The PRACTICE submenu is what the SHOOTING PRACTICE
button became on 2026-10-26, when this mode joined it; both modes sit in it,
built like the EDIT submenu (docs/MENUS.md §12).

From there it is **the friendly flow, unchanged**: the settings screen, team
selection, the PLAY MATCH screen, with EDIT TACTICS, VIEW OPPO, squad swaps
and substitutes all working as they always do. Any selection a friendly can
start, a shootout can — both sides shoot, so human v human and human v
computer both make sense and nothing extra is gated (contrast SHOOTING
PRACTICE, which needs exactly one computer side). The screens say so as you
go: the headers read PENALTY SHOOTOUT, the selection's confirm button PLAY
SHOOTOUT, the start button START SHOOTOUT, and the match is named PENALTY
SHOOTOUT on the scoreboard and in a saved highlight.

**START SHOOTOUT goes straight to the first penalty.** No walk-on, no
kick-off: the mode's per-frame hook calls `StartPenalties` on the match's
first frame — the same jump a drawn cup tie makes at minute 90, made at
minute 0. (It first waited for the kickoff to be taken, the arcade timer's
entry point, which opened every shootout with a pointless whistle.)
`StartPenalties → NextPenalty` rebuilds the scene wholesale — `InitTeamsData`,
`StopAllPlayers`, its own game state and camera — and everything it reads
exists before the loop's first frame (`StartingMatch` has already run), so
the ceremony it replaces was never load-bearing.

## The manual keeper

When the other side is shooting, **your keeper is yours**:

| held | he does |
|---|---|
| LEFT | full-stretch jump left |
| LEFT + DOWN | low dive left |
| RIGHT / RIGHT + DOWN | mirrored |
| nothing | stands — and still catches a shot hit straight at him |

Hold the direction before the kick to commit the dive the instant the ball is
struck, or flick it late as the ball travels. The reward for guessing the
side is that the dive is **immediate and at full dive speed** — the AI keeper
during penalties rolls dice on side, speed *and* timing (it deliberately
dives the wrong way about one attempt in four). The risk is that the height
is yours too — a low dive under a rising shot, or a jump over a daisy-cutter,
reaches nothing — and no guess means no dive.

A CPU-run side keeps the AI keeper, so shooting against the computer is
unchanged. In human v human the defending player gets the keeper each round.
Nothing outside the mode is touched: normal matches, in-match penalty kicks
and cup shootouts play exactly as before.

## How it works

| piece | where |
|---|---|
| the mode flag, the latch, the first-frame jump, the keeper input | `src/penalty_shootout.c` |
| the PRACTICE submenu (data) | `bin/data_json/menus/practiceMenu.json`, `_practiceMenu` in `src/menu_data.c` |
| the submenu's opener | `PracticeMenuSelected` (`src/menus.c`) |
| the flow it borrows | `FriendlyFlow` → `Friendly` → `ShowFriendlyMenu` (`src/menus.c`) |
| the words SHOOTOUT replaces FRIENDLY with | `FriendlyMenuInit`, `ChooseTeamsMenu` (`src/menus.c`), `SetGameStartButtonText` (`src/play_match_menu.c`), `InitAndPlayGame` (`src/game_init.c`) |
| latch set / per-frame jump | `InitializeInGameTeamsAndStartGame` (`src/game_init.c`) / the tick call in `GameLoop` (`src/game_loop.c`) |
| the engine's shootout (untouched) | `StartPenalties` (`src/match_setup.c`), `NextPenalty` (`src/goalkeeper.c`) |
| manual keeper: the ask | the `shot_at_goal` gate in the goalie update (`src/update_players.c`) |
| manual keeper: the dive | `GoalkeeperJumping` + `GoalkeeperForceDiveAnim` (`src/goalkeeper.c`) |

**The mode reuses the friendly flow whole**, for the reasons argued in
docs/SHOOTING_PRACTICE.md (RNG seed, gameType save/restore, the pristine
team-cache reload) — but through **`FriendlyFlow`, not `FriendlyMenu`**.
`FriendlyMenu` ends in `CommonMenuExit`, the *main menu's* fixup, and both
mode handlers now run under the 4-entry PRACTICE submenu, where that call
writes a competition name through a stale pointer (the EDIT submenu's
documented trap, MENUS.md §12). `FriendlyFlow` is the same flow without the
fixup; `PracticeMenuSelected` makes the call once, after the submenu closes.

**The mode is latched at kick-off** (`PenaltyShootout_BeginMatch`), like
shooting practice and for the same reason: what a match plays by is decided
once, never read live from a menu global.

**The manual keeper is asked for before any AI path.** The goalie update's
save gate (`shot_at_goal`, `src/update_players.c`) asks
`PenaltyShootout_KeeperDiveInput` above both the finisher-skill auto-react
roll and `ShouldGoalkeeperDive`, so in manual mode no dice ever move him. The
input is the engine's own 0..7 compass read from the defending side's
`TeamGeneralInfo.direction` — W/SW/E/SE are literally the pad's left,
left+down, right, right+down (`src/input.c`). The shooting side's keeper runs
the same goalie update, idle at the far end, and is explicitly excluded
(`_penaltyShooterSprite->teamNumber`), as is any CPU-run side
(`playerNumber == 0`).

## The bug log

**The REPLAY after a shootout played as a plain friendly** (found in testing,
fixed same day). The post-friendly REPLAY button restarts the match through
`InitGameRestoreTeams` + `GameLoop` directly (`game_loop.c`
`end_of_match_flow`), never re-entering `InitializeInGameTeamsAndStartGame` —
so the latch survived but the first-frame jump's one-shot stayed spent, and
the replayed match never went to penalties. That path now calls
`PenaltyShootout_MatchRestarted`, which re-arms the one-shot and leaves the
latch alone, so a replayed friendly stays a friendly. The general lesson:
**the REPLAY path skips the match-begin hooks** — anything latched in
`InitializeInGameTeamsAndStartGame` that must also reset per match needs its
own re-arm there. (Shooting practice never noticed, because its strip runs on
every walk-on rather than once.)

## Planned

- Shooter-side control refinements (aiming help, power), if the drill wants
  them — the shooter is the stock penalty taker today.
