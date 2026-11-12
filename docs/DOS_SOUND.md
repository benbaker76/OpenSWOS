# The PC game's sounds: effects, crowd, commentary and music

SDL builds only. A match can be heard with the Amiga's sounds (the default) or with the
PC game's: its sampled kick, bounce and whistles, its crowd, its chants, and its
commentary. The commentary can also be played over the Amiga's sounds.

```
[audio]
style = amiga         ; amiga | pc
commentary = auto     ; auto | true | false
```

| `style` | `commentary` | what a match sounds like |
|---|---|---|
| `amiga` | `auto` or `false` | the Amiga's sounds, as the Amiga game (the default) |
| `amiga` | `true` | the Amiga's sounds with the PC game's commentary over them |
| `pc` | `auto` or `true` | the PC game's effects, crowd, chants and commentary |
| `pc` | `false` | the PC game's effects, crowd and chants |

`--sound-style=amiga|pc` and `--commentary=auto|on|off` do the same for one run. The menus'
music is the Amiga's either way. There is no row for it in the OPTIONS menu yet, and no
key for the PC game's F8 (commentary off during a match).

It changes nothing in a match. Online, each player hears their own style.

## The files

`bin/sound_dos/`: 156 sample files, 9.3 MB, all unsigned 8-bit mono at 22,050 Hz, and the three
files of the music (4.1 MB):

| folder | files | what |
|---|---|---|
| `fx/` | 17 | kick, bounce, three whistles, the crowd loop, goal cheer, the sigh, three chants, six "come on you ..." chants by shirt colour |
| `pierce/` | 76 | the commentary the PC game keeps in memory: goals, saves, misses, fouls, penalties, tackles, passes |
| (top) | 3 | `menu.xmi`, the menu music, and `sample.ad`, its instruments ("The music" below); `title.mp3`, the CD release's title song ("The CD track") |
| `hard/` | 63 | the commentary it reads from disk when needed (corners, throw-ins, cards, substitutions, tactics, full time), the chants about the score, the crowd at full time |

They are the PC game's own files under lower-case names. `scripts/gen_dos_sound_tables.py
<swos.asm> <PC game folder> --copy bin/sound_dos` copies exactly the files the game's
tables name and writes `src/dos_sound_tables.h`. The PC game addresses a commentary line
as (offset, size) into one block of memory holding every `pierce` file end to end; the
script turns each pair back into a file from the files' real sizes, and stops if a pair
is not a whole file. One entry of the "keeper claimed" table has size 0: the PC game
plays nothing for it, and neither do we.

`bin/SWOS.x86_64 --dos-sound-selftest` checks that every file is there and every table
entry names one. If a file is missing at kick-off the match is played with the Amiga's
sounds and `SWOS.log` says so.

The web build does not carry the folder (9 MB more to download), so it always plays the
Amiga's sounds. The Amiga build has none of this.

## Your own commentary

A folder per category under `sound_dos/commentary/`, any number of `.wav`, `.mp3` or `.raw`
files in each, of any length (`bin/sound_dos/commentary/README.txt` is the player's guide
and lists the 26 categories). The arrangement and the category names are SWOS 2020's
(swos-port's before it), so a pack made for either can be copied in.
`[audio] commentary_dir` / `--commentary-dir` names another folder, relative to the game's
data or absolute.

- **Per category.** A category with files is spoken from them; one without keeps the PC
  game's lines. SWOS 2020 is all or nothing.
- **Chance.** `_xN` or `-xN` at the end of a file's name makes it N times as likely
  (SWOS 2020's rule). The file just played from a category is not played again while
  there is another.
- **Three categories have only a folder.** `header` and `injury` are SWOS 2020's
  additions (`DOS_EV_HEADER` at both header hits, `DOS_EV_INJURY` where a tackle injures;
  for a human's team only; a header line never over another line). `end_game_so_close`
  is the line the PC game never reaches; with files in its folder a draw gets it, and
  without, the PC game's behaviour is kept (a draw of four goals or more is
  "sensational").
- **Formats.** `.raw` is played as the PC game's files are. `.wav` and `.mp3` are decoded
  to 16-bit mono at their own rate (`AudioSDL_PcmPlay16`) and resampled by the mixer.
  `.ogg` and `.flac` are not read; the log names any file left out.
- **When.** The folders are scanned on the loading thread at start-up, after the PC
  game's files; a line whose file has not arrived is not spoken. At most 64 files a
  category and 96 MB of decoded sound.
- **Not there:** a description file for a pack (a name, per-file settings). The file name
  carries the one setting there is.

`SWOS_DOS_SOUND_TRACE=1` names each line as it is started (`commentary/<category>, file N
of M`). Under the replay rig a match is over before the loading thread is: test a pack
with `--view-replay=<file> --replay-paced`.

## The music

`[audio] music = pc` (or `--music=pc`; default `amiga`) plays the PC game's menu music in
the menus: `sound_dos/menu.xmi` on an emulated AdLib chip with the instruments of
`sound_dos/sample.ad`, as the PC game's Miles Sound System driver did. The same setting
plays the PC CD release's title song in the intro ("The CD track" below).

`src/dos_music.c` is the whole player, written here; no copyleft code is in it:

| part | what |
|---|---|
| XMI reader | MIDI events in IFF chunks. A delay is the sum of the bytes below 0x80 before an event; a note-on carries its own length, so there are no note-offs. |
| `sample.ad` reader | a Miles "global timbre library": an index of {patch, bank, offset} ended by FF FF, each offset a 14-byte record -- length, a transposition, eleven register values. Bank 0 is the melodic instruments by program, bank 127 the drums by key. |
| driver | eighteen two-operator voices (an OPL3, the driver of a Sound Blaster Pro 2 or 16). A silent voice is used, the longest silent first; with none the oldest note is cut. |
| chip | `third_party/opal`: the Opal OPL3 emulator, public domain, C. |
| arithmetic | `third_party/opl_models/model_ail.c`, MIT: the Miles driver's note-to-frequency table and velocity-to-level formula. |

**libADLMIDI is not used and must not be** -- SWOS 2020 plays this file with it, and its
player is GPL v3. Only its two permissive pieces above were taken
(`third_party/opal/OPENSWOS.md`).

What the song is (`--dos-music-selftest` plays it through and reports): 2,838 notes in
9,163 ticks, then it repeats; four melodic channels (programs 4, 35, 50, 63) and the
drum channel; channel volume set five times and no other controller -- no pitch bend, no
pan, no sustain, so the player has none. Thirteen voices at most, so no note is ever cut.
796 of the notes are for drum keys 31 and 33, which `sample.ad` has no sound for: they
are silent here, as they were on an AdLib card (the song was written for a module that
has them).

**The tempo.** XMI's unit is nominally a 120th of a second, and at that rate the song is
over in 76 seconds -- far too fast. Three numbers bear on the real rate:

- the PC game sets the Miles sequencer's service rate to its frame timer
  (`MDI_SERVICE_RATE` = 1193181 / 17045 = **70**, `InitAIL22khz` in swos.asm), and the
  sequencer steps one tick a service;
- the song itself agrees: it is in 4/4, a bar is 191 ticks, and its tempo event says 88
  beats a minute -- 47.75 ticks a beat at 88 a minute is **70.0** ticks a second;
- a recording of the PC menu music (SWOS 2020's `menu.mp3`) has a beat of 0.6417 s,
  93.5 a minute, at the same pitch as ours to the cent: **74.4** ticks a second.

The player uses 74.4 (`TICKS_PER_10_SECONDS` 744), the recording's, because that is what
a PC was heard to do; `SWOS_MUSIC_TICKS=700` plays it at the game's own arithmetic to
compare. Why the recording is a sixteenth faster than the arithmetic is not known, and
neither is how the recording was made. Lining the recording up against the song note
for note did NOT give a clear answer (every attempt correlated at about 0.1), so the
74.4 rests on the beat alone.

`[audio] menu_music = mp3` plays that recording itself, `sound_dos/menu.mp3`, in place of
the live player (`midi`, the default).

Not known to be the PC game's: the chip's tremolo and vibrato depth are left at 0
(register BD), and the mix level (`PC_MUSIC_GAIN` in `src/audio_sdl.c`, set so the song is
about as loud as the Amiga's menu music). Nobody has compared it by ear with the PC game
yet.

It is rendered on the audio thread, inside the mixer's tick, under the master volume the
menus fade. A match's sounds stop it, as they stop the Amiga's menu song.

Without `menu.xmi` or `sample.ad` the menus play `sound_dos/menu.mp3` when it is there (the
recording, as `menu_music = mp3` does), and only then the Amiga's song. That is the data OpenSWOS
Setup makes from SWOS 2020 alone (no PC game, no Amiga version; ASSETS.ini `pc_swos2020`): its
`menu.mp3` and its "PC DOS - Goalscoringsuperstarhero.mp3" as `title.mp3`, its 17 effects and 11
crowd sounds (`audio/fx`, the PC game's format: 22,050 Hz unsigned 8-bit; 10 of the 28 are the PC
game's own, 18 SWOS 2020's -- the chants, longer, and others), no commentary and no other crowd. `--dos-sound-selftest` then requires only
`fx/` and says how many of the rest are missing, as expected.

## The CD track

The PC CD release plays its title song, "Goal Scoring Superstar Hero", from the disc.
With `[audio] music = pc` the intro plays `sound_dos/title.mp3` (4.1 MB) in place of the
Amiga's HERO module; without the file it plays the Amiga's.

`src/music_stream.c` reads the file whole and decodes it as it plays, a tick's worth at a
time on the audio thread, with dr_mp3 (`third_party/dr_mp3`, public domain), bringing it
to the device's rate by linear interpolation when the two differ. It is under the master
volume, so the intro's fades apply, and anything that starts or stops an Amiga song
stops it (`AudioSDL_StreamStart` in `src/audio_sdl.c`).

Any `sound_dos/<name>.mp3` can be played this way; `title` is the only name asked for.

**`[audio] title_music = midi`** (`--title-music=midi`, the AUDIO screen's PC TITLE MUSIC; default
`mp3`) plays `sound_dos/title.xmi` instead -- SWOS 2020's floppy title song, which a mod brings
(a `title_music` slot mod's `sound_dos/title.xmi`, found through the mods' overlay) -- on the
emulated card with `sample.ad`, the menu music's player (`DosMusic_StartSong("title", 0)`): once,
not repeating. Without title.xmi it plays title.mp3, and without that the Amiga's HERO.
`--dos-music-selftest` plays it through too when one is installed.

## How it works

**An event is not a sound.** The match code says what happened, with
`DOS_SOUND(event, arg)` (`include/dos_sound.h`), at the places the PC game calls its sound
routines. The event goes through the same gate as the Amiga's effects
(`Audio_DosEvent` in `src/audio.c`, `include/audio_sfx_filter.h`):

1. the **observer** -- the replay recorder keeps it (`AUDI` kind 4,
   docs/REPLAY_FORMAT_V2.md);
2. the **filter** -- netplay holds back an event of a frame that is being played again
   (docs/NETPLAY.md, "Sound during a rollback");
3. `DosSound_Event` (`src/dos_sound.c`), which does nothing unless the style is `pc` or
   the commentary is on.

Events are sent whatever the style. So a recording made with the Amiga's sounds plays
with the PC's and the other way round, and both players of an online match record the
same events whatever each one hears.

**Everything after the event is outside the match.** Which line of a table is spoken, the
"not the same line twice" memory, the delays before a corner or a card is mentioned, the
count to every fifth pass, the "a penalty is being taken" flag: all of it lives in
`dos_sound.c`, is driven only by events that passed the filter, and is never read back by
the match. That is why nothing was added to the rollback registry (`src/sim_state.c`) and
no state hash changed.

`MATCH_START` is sent before the match's first frame. The recorder keeps what was sent
before the first frame when frame 0 is played again (`audioKeep` in `src/replay_v2.c`):
online, a rollback to frame 0 used to discard it with the rest of that frame's sounds,
and nothing sends it a second time.

While the PC's effects have a match, the Amiga's effects are not started (`DosSound_Live`
in the two trigger gates of `audio.c`); with commentary alone they play as ever. A recording made before this existed has no
`MATCH_START` event, so it plays with the Amiga's sounds whatever the style.

The samples are mixed by eight PCM voices in `src/audio_sdl.c`: crowd loop, chant,
commentary, crowd reaction, four for effects. They are under the same master volume as
the Amiga's sound, so the half-time and full-time fades apply.

## Where the events are

The Amiga's sound calls sit at the same places as the PC game's, under other names, so
most events are sent from the Amiga's routine (`src/sfx_crowd.c`):

| PC routine | event | sent from |
|---|---|---|
| `PlayEnqueuedSamples` | `TICK` | `PlayEnqueuedSamples` |
| `PlayKickSample` (7 callers) | `KICK` | `PlayKickSample`; `DoPass` |
| `PlayBallBounceSample` | `BOUNCE` | `PlaySfxSample16` |
| `PlayRefereeWhistleSample` / `PlayFoulWhistleSample` / `PlayEndGameWhistleSample` | `WHISTLE` / `FOUL_WHISTLE` / `END_WHISTLE` | the routines of the same names |
| `PlayGoalkeeperSavedComment` + `PlayMissGoalSample` | `KEEPER_SAVED`, `MISS_CROWD` | `PlayMissGoalSample` |
| `PlayNearMissComment` + `PlayMissGoalSample` | `NEAR_MISS`, `MISS_CROWD` | `PlayGoalKickOffSample` |
| `PlayPenaltyComment` | `PENALTY` | `PlayPenalty` |
| `PlayPostHitComment` / `PlayBarHitComment` + `PlayMissGoalSample` | `WOODWORK`, `MISS_CROWD` | `UpdateBall` (`ball_physics.c`) |
| `PlayGoalComment` / `PlayOwnGoalComment`, `PlayHomeGoalSample`, `LoadCrowdChantSample` | `GOAL` / `OWN_GOAL`, `GOAL_CROWD`, `SCORE` | the goal, in `game_timers.c` |
| `EnqueueCornerSample`, `EnqueueThrowInSample` | `CORNER`, `THROW_IN` | `game_timers.c` |
| `EnqueueYellowCardSample`, `EnqueueRedCardSample` | `YELLOW_CARD`, `RED_CARD` | `referee.c` |
| `PlayFoulComment`, `PlayDangerousPlayComment`, `PlayGoodTackleComment` | `FOUL`, `DANGEROUS_PLAY`, `GOOD_TACKLE` | `tackle_foul.c` |
| `PlayKeeperClaimedComment` (3 callers) | `KEEPER_CLAIMED` | `update_players.c` |
| `PlayStopGoodPassSampleIfNeeded` | `PASS` | `DoPass` |
| `EnqueueSubstituteSample`, `EnqueueTacticsChangedSample` | `SUBSTITUTION`, `TACTICS` | `bench_subs.c` (both copies of the bench code) |
| `LoadSoundEffects`, `LoadIntroChant`, `PlayCrowdNoiseSample` | `MATCH_START` | `GameLoop`, after the recorder is switched on |
| `LoadAndPlayEndGameComment` | `GAME_OVER` | `GameOver` |
| `StopAudio` | `MATCH_END` | `StopAudioAndClearFlags` |

Volumes are the PC game's: kick 25, bounce and whistles 42, crowd loop 100, a chant up
to 64, everything else 127, of 127.

Two files are recordings at HALF the rate, 11,025 Hz: `fx/foul.raw` and `fx/endgamew.raw`,
the foul and the end-of-half whistles. The PC game plays them through a routine of their
own (the disassembly calls it `PlayRefereeWhistleSample22Khz`; it sets 11,025), and so
does `play_long_whistle`. The kick-off whistle, `fx/whistle.raw`, is 22,050 like the rest.

## What is not the PC game's

- **The random numbers.** The PC game picks a commentary line, the post-or-bar line and
  the chant about the score with the GAME's random generator. Here they come from a
  private one, so the sound cannot change a match and two players need not hear the same
  line. (The PC game's sound code therefore changes the numbers its match draws; the
  Amiga's does not call the generator there. Our PC gameplay does not reproduce those
  extra calls -- the replay rig pins the generator, so it cannot see them.)
- **The delays count frames shown.** A corner is mentioned 100 frames after it is given,
  a card 100, a throw-in, substitution or tactics change 70, a good pass 10. Under a
  rollback a frame is simulated more than once; the `TICK` event is filtered like any
  other, so a delay counts each frame once.
- **A chant STARTS when the Amiga's chant machine starts one.** Both releases run the
  same chant state machine with different numbers, both call the game's random generator
  from it, and the match depends on those calls (`sim_state.c`). So the machine stays the
  Amiga's and only says when: the PC chant in the same place of the table is started
  (the shirt-colour chant or `chant4l`, `chant8l`, `chant10l`, `chant4l`). From there the
  chant has the PC game's own shape (`chant_tick`, the PC game's `PlayCrowdChants`): it
  repeats without end from volume 0, fades in over 128 PC ticks (1.8 s) to volume 64 --
  half, so under the crowd loop's 100 -- holds for 500 to 1010 ticks (7 to 14 s), fades
  out over 128 and is stopped. The Amiga's chant volume is not followed. (The PC game
  carries a second, uncalled copy of the routine -- to 127, held 250 to 760 -- which
  this followed until 2026-10-07: read which copy is CALLED.) After a
  goal the PC game's rule decides whether the next chants are about the score ("one
  nil" ... "five nil", "easy", "here we go", "you're not singing any more").
- **Mixing headroom.** The PCM voices are mixed at 160/256 (`PCM_HEADROOM`): crowd, chant,
  commentary and an effect sound together and each was recorded near full scale.

## What is the PC game's, mistakes included

- After a draw the PC game means to say "so close" and never does: its test for a goal
  difference of 0 comes after a jump taken for every difference of 0 or more. Kept.
- A good tackle that brings the opponent down gets the "dangerous play" line, not a foul.
- The good-pass and good-tackle lines never interrupt a line being spoken; every other
  line replaces it.
- In training there is no crowd and no commentary; the kick, bounce and whistles play.

## Checking a change

```bash
bin/SWOS.x86_64 --dos-sound-selftest
# every sample a match starts, on stderr:
cd bin && SWOS_DOS_SOUND_TRACE=1 ./SWOS.x86_64 --replay-test=../tests/replay/zkz-11054632-sprites.swr \
    --replay-skip=.starting_direction,.frame_switch_counter,.field_3e,.player_direction,.save_sprite \
    --sound-style=pc
# the mixer's output as a file of 16-bit stereo samples:
SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=/tmp/out.raw ./SWOS.x86_64 ... --sound-style=pc
```

The events are in every recording, so `scripts/replay_v2_test.sh --all` (a recording's
sounds equal the live match's) and `RECORD=1 RELAY=0 scripts/relay_test.sh` (the host's
`.SRP` equals the recorder's) cover them.
