# OpenSWOS — one C source tree, two targets (Amiga m68k + portable SDL)

## What this project is

A pure C port of the Amiga **Sensible World of Soccer**, derived from the
original `SWOS2.s`. **One** source tree builds **both** targets:

| Target | Build | Output | Notes |
|---|---|---|---|
| SDL (Linux/Windows/macOS/WASM/ARM) | `make` | `bin/SWOS*` | the day-to-day dev target |
| Amiga m68k (A1200/AGA) | `make -f Makefile.amiga` | `bin/SWOS2` | runs under FS-UAE/WinUAE or real hardware |
| Nintendo Switch (NX-NXFP2-a64) | `scripts/build_switch.sh` | `bin/SWOS.nss` | a flavour of the SDL build; needs the NDA SDK + SDL fork, neither in this tree. **Not yet linked or run** — see `docs/SWITCH.md` |

`src/` and `include/` are **shared**. Platform divergence is expressed with
`#ifdef SWOS_SDL_BUILD` (defined by `include/swos_platform.h`, which the SDL
build force-includes via `-include`; the m68k toolchain never defines it).
A small set of SDL-only sources is excluded from the Amiga build by name —
see `SDL_ONLY_SRCS` in `Makefile.amiga`.

> **History:** until 2026-08-12 these were two separate repos (`SWOS_C_SDL`
> and `SWOS_C`) kept in step by a one-way copy script. They are now merged.
> **There is no sync step any more — edit the file once.** If you find a doc
> or comment referring to "the Amiga port" as a separate tree, it predates
> the merge; fix it when you touch it.
>
> **OpenSWOS** (github.com/benbaker76/OpenSWOS) is the open-source release. Its wire/file
> identifiers are its own (`VERSION:OPENSWOS/<NETPLAY_COMPAT>`, GGPO session `openswos`, custom-team format
> `openswos-team`, ggpo-x branch `openswos`): a build of another fork with different ones
> does not meet it online.

**Forks:** SWOS United (github.com/SWOS-United) is downstream of OpenSWOS and merges it. The
project's name, online identifiers, servers and player folder are ALL in
`include/project_identity.h` -- never write them anywhere else (docs/FORKING.md); shared work
lands here first.

**Docs map:** `docs/INDEX.md` lists every doc in `docs/` (what it is, whether it is current,
when to read it) and every path the project uses — repos, tools, servers, save folders.

## Building

Both targets build from a plain bash shell (WSL included) — the old
"must build from VSCode" restriction is gone.

```bash
# SDL, native
make                      # bin/SWOS (linux x86) / bin/SWOS.exe (cygwin)
make X86_64=1 -j$(nproc)  # bin/SWOS.x86_64
make WASM=1               # dist/web/SWOS.html (needs emsdk on PATH)

# Amiga m68k — toolchain ships inside the amiga-debug VSCode extension.
# ⚠️ It MUST be the owner's fork, github.com/benbaker76/vscode-amiga-debug —
# NOT the marketplace build. Its FS-UAE has the gdb WRITE packets (M/P/G) that
# let the AmigaScope harness drive the target (inject input, set vars, call
# functions) instead of only reading it, plus a log-spam fix. Both installs share
# the SAME dir name and the SAME publisher/repo in package.json, so they are
# indistinguishable after the fact — if gdb reads fine but writes silently no-op,
# that's the stock build. See scripts/amiga/amigascope/README.md.
# NOTE the path is the WINDOWS-side .vscode (VSCode is installed on the host),
# NOT ~/.vscode in the WSL home — there is no extension dir there. Getting this
# wrong fails as `Error 127` (compiler not found), and an up-to-date tree hides
# it behind "Nothing to be done for 'all'" until something actually recompiles.
EXT=/mnt/c/Users/headk/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/linux
PATH="$EXT/opt/bin:$EXT:$PATH" make -f Makefile.amiga -j$(nproc)
```

**A `-D` flag change rebuilds every object** on both targets: each makefile writes its
compile flags to a stamp (`build/<os>/<arch>/.build-flags`, `obj/.build-flags`) that every
object depends on. It used to be "toggle ⇒ `clean` first" — make compares timestamps, and
a toggled flag relinked stale objects. That is not theoretical: a Windows build with a
stale `tackle_foul.o` (no `SWOS_TEST_NO_INJURIES_CARDS`) booked players a clean Mac build
could not, and a live match desynced at the first booking (2026-09-19, docs/NETPLAY.md §7).

**Netplay** (`NETPLAY=1`, docs/NETPLAY.md) compiles the vendored C++ GGPO-X in
`third_party/ggpo-x` into the SDL binary. Default on for 64-bit Linux (`X86_64=1`),
Cygwin64, macOS (both slices) and WASM (below), off elsewhere. The C++
runtime is linked statically, so the DLL
and `lib/<arch>` lists below are unchanged — re-check with `cygcheck`/`ldd` if the link
changes. `bin/SWOS.x86_64 --netplay-selftest` runs the library with a toy game.
The state a rollback saves is the registry in `src/sim_state.c` (SIM + VIEW tiers,
pointer maps), proved by the rollback rig: run `scripts/rollback_test.sh --all` before
and after any change to it or to what the match writes (docs/ROLLBACK.md). A resync sends
it as a binary packet (`src/state_wire.c`: delta against a shared base, deflated with the
vendored miniz in `third_party/miniz`, update notes in its `OPENSWOS.md`) in the
PORTABLE encoding — typed fields big-endian, pointers as identities, like saves — so
32/64-bit and Windows builds interoperate (`scripts/rollback_xbuild.sh`). Adding or
renaming an animation table or frame array means rerunning `scripts/gen_sim_state_ids.py`.
A netplay match runs each frame as a step GGPO can call again (`src/netplay_match.c`):
`GameLoop_Frame` (the loop body, drawing included — render calls write match state), the
input and clock of `src/match_step.c` (shared with the replay rig), the buffer swap and the
VBlank interrupt's work, never presented on a re-simulation. `scripts/netplay_test.sh --all`
plays the corpus through it — directly, under GGPO's synctest, and as two peers over loopback
(`p2p`, with `--stall` to force rollbacks) — and requires every frame's state hashes to equal
a plain run's; run it after touching the game loop, the step, or anything the VBlank interrupt
does. A loop that waits for input inside a frame (pause, the old statistics overlay) hangs a
peer holding only a prediction: under `_gFrameStepInput` such a loop makes one pass per frame.
The main menu's MULTIPLAYER button opens SWOS 2020's online flow (matchmaking, `swos2020/logInMenu`,
docs/MENUS.md §10); OpenSWOS's own direct-IP menu (`src/p2p_menu.c`) is `--menu=p2p/p2pMenu`. Both
connect through `src/p2p_link.c` (docs/NETPLAY.md §6): a UDP handshake that refuses a peer whose build
or rules would desync the match (not the gameplay STYLE: a game plays its host's, picked at OPEN, and
the joiner borrows it until they leave -- `GameplayStyle_Borrow`; and not the screen width: the two
agree one simulation width), and shows the ping. `--netplay-selftest` runs it over
loopback. **"Build" means `NETPLAY_COMPAT`** (`include/netplay_compat.h`, since 2026-10-25), not the
commit: two commits with the same number play each other, and the server lists a game only to its
number. **Raise it with any change to how a match plays, or to what the games say to each other**;
`scripts/check_netplay_compat.sh` (the corpus's SIM hashes vs `tests/netplay_compat.txt`) fails when
a raise is missing, `--update` after raising -- run it after touching the match. It cannot see a
booking, a shoot-out, the lobby or the wire: those raises are yours to remember. A live match through the lobby, and a rematch after it, is `scripts/netplay_lobby_test.sh`
(two instances through the real menus); run it after touching the lobby, the link, or how a live
match starts or ends. A live match must never `exit()` or leave the frame loop: it ends through
`liveEnd` and goes back to the lobby. Closing the window is one of the ways it ends ("YOU CLOSED THE
GAME"), half time and the final whistle each save the match so far to `LATEST.SRP` (on a thread), and the recorder keeps the whole
frames of a stream that was cut (docs/NETPLAY.md §7, "A match that does not end properly").
The matchmaking server can also play a match THROUGH itself (`--relay`, docs/NETPLAY.md §7
"The relay"): for testing, the browser build and NATs that will not punch — players stay peer
to peer. `--relay-fallback` (OFF by default -- the bandwidth is the server's; the owner
keeps the relay for browsers) gives every desktop pair a session (`RELAYFB:`), and a link
with no round trip after 5 s moves to it (`p2p_link.c`, `relay_fallback`;
`BLOCK=both|host|join RELAY=0 RECORD=1 scripts/relay_test.sh`). `--netem <profile|cycle>` puts the game's own emulator (`src/netem_core.c`, shared) in
the middle, where it disturbs the lobby's link too, which the client-side one never does.
`scripts/relay_test.sh` (`NETEM=wifi|awful|...`) plays two local players through it via the real
menus; run it after touching `p2p_link`, the lobby's start, or the relay. `--record` makes the
same server a recorder any live match can stream to (`SWOS_RECORD`/`[netplay] record`, ON by default
since 2026-09-30 for the matches the matchmaker arranges -- `MatchRecord_SetArrangedByServer` keeps
direct and test matches off the live server; relayed
or peer to peer): it compares both players' confirmed-frame hashes and logs a desync with its
exact frame, and keeps the match as a `.SRP` that must be the host's `LATEST.SRP` byte for byte
— both are laid out by `src/srp_writer.c` (§7 "The recorder";
`RECORD=1 [RELAY=0] [DESYNC_FRAME=N] scripts/relay_test.sh`).
After every online match the two games swap their `SWOS.log` over the lobby's link and each
writes one zip of both logs and both HOW WAS THE MATCH? answers (`src/match_report.c`,
docs/NETPLAY_REPORT.md): kept in `save/netplay/` and sent to the server's `--reports DIR` —
unless SEND MATCH REPORTS (`[netplay] reports`, on in tester builds) is off. Run
`REPORTS=1 RELAY=0 scripts/relay_test.sh` after touching it, the rating screen, or the lobby's end.
**Pacing and the prediction balance** (docs/NETPLAY_BALANCE.md). GGPO-X is built with
`GGPO_SEND_FRAME_ADVANTAGE=0` (`GGPO_CXXFLAGS`): its time sync works from each peer's own
estimate alone, which is what the pacing in `src/netplay_match.c` was tuned against -- until
2026-10-06 by accident (the fork's `min()` returned a dangling reference, so the figure a peer
sent was noise; that noise tipped matches into one player predicting 2+ frames deeper). Do not
switch the true figure on without retuning the pacing: the peers swing five frames apart. The
prediction balance (and the CATCH UP switch) were REMOVED from the game 2026-10-30 -- they
caused netplay problems (Playa); the balance's code stays in the fork, unused. The input delay
is Playa's ping table (`propose_delay`, docs/NETPLAY.md: DOS 1-5, Amiga 1-4, never 0). A change
to the pacing: several runs a link of NETPLAY_BALANCE.md's matrix, never one.
"Ghosting" (players running through each other online) has its own doc, docs/NETPLAY_GHOSTING.md:
every match records each frame as drawn and as played (`src/netplay_ghost.c`), logs `[ghost]`
lines, keeps windows around the worst detected events for the report zip, and
`scripts/ghost_view.py` draws them; `SWOS_LIVE_CHASE_INPUT` + `SWOS_NETEM_LINK=1` reproduce it.
The **web build plays online too** (`make WASM=1 NETPLAY=1`, docs/NETPLAY.md §8): a browser
has no UDP, so Emscripten sends each datagram as a WebSocket message to the server, whose
`--websocket` gate (`Server/WebGate.cpp`) gives each browser socket a UDP address of its own;
a match with a browser in it is always relayed. `scripts/wasm_netplay_test.sh` plays it in
headless Chrome against a browser, the native build, the Mac's, or a browser on the Mac
(`PAIR=<host>-<join>` of `web|native|mac|macweb`); run it after touching the gate, the link's
sockets or the web build's pacing or clock (its `clock_gettime` is ours: Emscripten's refuses
the clock SDL asks for, and SDL's wall-clock fallback steps -- NETPLAY.md §8). On by default for WASM since us.swos.eu got its certificate (2026-09-24): an https page can only
open `wss://`, which the gate speaks with `--tls-cert`/`--tls-key` (TLS on the same port;
NETPLAY_SETUP.md §10, `TLS=1 scripts/wasm_netplay_test.sh`).

**UNIFORM and MY CUSTOM TEAM** (SDL only; docs/UNIFORM.md, docs/CUSTOM_TEAMS.md). The PLAY MATCH
screen's UNIFORM button (entry 68, hidden on the Amiga) opens SWOS 2020's uniform selector
(`src/uniform_menu.c`): a kit per side for ONE match, kept beside the team and applied at kick-off
on copies of the team headers (`Uniform_Kits` in `InitializeInGameTeamsAndStartGame`) -- never
written into a team, and `SetInGameTeamsPrimaryColors` still decides a clash. Online the choice
travels inside the team (both kits become it), so no wire format changed. Its drawings are
`src/kit_art.c`: the Amiga's EDIT TEAM sprites or the Jaguar's (`[graphics] kit_art`, default
amiga; `bin/grafs_jag/JAGKITS.BIN` from `scripts/make_jag_graphics.py`). MY CUSTOM TEAM
(`src/custom_team.c`) is three slots of `CUSTOMn.TEAM` (JSON) in the save folder, edited on the
game's EDIT TEAM screen (IMPORT takes a database team whole, UNDO is LINEUP). **A custom team is a
database team with only names, faces, nationalities, shirt numbers, kits and line-up changed --
no position, skill or value can differ**: the file has NO player data -- it is a `CustomTeam`,
the UUID and checksum of the database team it was made from plus the changes, resolved against
the database on load -- and the lobby refuses any team from the other player that is not that
(`CustomTeam_Allowed`, every team, custom or not). **Every database team and player has a `uuid`,
every team a `checksum` of its positions, skills and values** (`bin/data_json/teams`,
`src/team_db.c`, docs/TEAM_IDS.md): an id names fixed data, the 96/97 ids are derived (uuid5) so
the binary database has them too, `scripts/assign_team_uuids.py [--check] [--registry]` writes and
verifies them. **The matchmaking server is the authority**: its list of id + checksum is compiled
in (`Server/TeamDesk.cpp`, `TeamRegistry.inc` -- rerun `--registry` and rebuild the server when the
database's teams change), and in a server-arranged match each game asks it about BOTH teams before
START (`include/team_proto.h`, one `SWTC` datagram on the server's UDP port; `team_check` in
`src/netplay_lobby.c`) -- a refused team stops the start, no answer in ~3 s plays unverified.
`scripts/relay_test.sh` asserts it; `TEAMCHECK_REFUSE=1` (with `HOST_ENV=SWOS_TEAMCHECK_CORRUPT=1`)
proves a refusal. A raw
684-byte `.TEAM` (SWOS 2020, editors) in a slot is converted to the JSON, its base team found BY ITS
PLAYERS -- SWOS 2020 renumbers custom teams. `--custom-team-selftest` runs all 1,631 database
teams through the rule and the file; run it after touching either.

**`SWOS_WORLDS`** (default 1; `make SWOS_WORLDS=0`, the same on `Makefile.amiga`): the football worlds
beyond 96/97 -- World 2026-27 and all it brought (docs/WORLD_2026_27.md: its countries, leagues,
splits, play-offs, cups, pools, the Coppa D, Europe's league phases). At 0 the game is 96/97 alone, as
before them: `World_Current()` is the constant 96/97, `World_Set` takes no other, the generated
world data (`src/world_data.c`) compiles out, so every world hook finds nothing and does nothing,
and the global team numbers are 96/97's 2000. New world code must keep that true: gate on the world's
data or `World_Current()`, never act for 96/97. Proved: the 96/97 golden careers on a
`SWOS_WORLDS=0` build equal the record made with the worlds on (a save writes the league offsets at
2000 when nothing past them is used, so it is the same from either build).
**World 2026-27's data is NEVER in OpenSWOS** (its squads are its authors'; the owner, 2026-10-30):
`bin/data_json/worlds/` stays .gitignored. It is a package hosted apart (`scripts/make_world_package.sh`
-> `dist/OpenSWOS-World-2627.zip`, made after every import whose output changed: its WORLD.ini
fingerprint must be the one `world_import.py` baked into `src/world_data.c`), which OpenSWOS Setup
imports like the original game's data (source "World 2026-27", part `world`). 96/97 is the default
world: a run plays `--world`, else `SWOS_WORLD`, else SWOS.ini's `[general] world`, else 96/97 (a
world without its WORLD.ini installed falls back to 96/97, said in `[world]` lines); the Amiga plays
its drawer's `data/WORLD`, written only when the player ticks the world on the Amiga tab. The
matchmaking server's team list carries the world's teams (`TeamRegistry.inc`, refreshed by the package
script: commit it and redeploy the server with each package). Test rigs
whose corpus or careers are 96/97's export `SWOS_WORLD=9697` -- a new one must too.

**The golden career test** (`scripts/career_golden_test.sh`, docs/WORLD_2026_27.md phase 0): the
game's career autopilot (`--career-golden=new:<continent>/<country>/<division>/<club>` or a save;
`src/career_autopilot.c`) plays seven new careers -- England's two top divisions, Italy, Germany,
Spain, Scotland, Brazil (`tests/career_golden_careers.txt`) -- and any local saves in `tests/career`
for five seasons, deterministically, and compares every season-end JSON save with
`tests/career_golden.txt`. **Run it after touching the season engine, competitions, finances,
transfers or saves**; `--update` records a deliberate change, `--twice` re-proves the runs repeat,
`scripts/career_golden_diff.py A B` names what changed. A save must hold no address -- the test
found two (`namePtr` in the competition blobs, now written as a set/unset marker).

**Whole-match replays** (`.SRP`, `src/replay_v2.c`, docs/REPLAY_FORMAT_V2.md) are SDL-only and
recorded from every match that is not training, the arcade cabinet or the rig: `LATEST.SRP` in the
save folder, plus a dated copy under `[replays] auto_save`. They play through the highlights viewer
(LOAD/VIEW), which still reads `.HIL` and `.RPL` — the attract reels are `.RPL`, so those readers
stay. A recording is **resolution independent**: sprites are stored in world coordinates and the
camera in full 16.16, so a 4:3 match plays on a 16:9 screen exactly as it would have been played
there. `scripts/replay_v2_test.sh --all` proves it — the corpus recorded and played back at
336/484/672, requiring the sprites, the pitch origin, the sub-pixel scroll and every sound to
equal the live match's at the playback width. Two traps it was written to catch: the frame head
must be taken WITH the sprites (the camera moves inside the end-of-match frame, and
`DrawAnimatedPatterns` sets the crowd state for the frame it is about to draw), and META must be
taken at KICK-OFF (by the end of a match `SetPitchTypeAndNumber` has run again, so an end-of-match
`pitchNumberAndType` plays the match on the wrong pitch). Netplay records by GGPO frame number
(`ReplayRec_SetFrame`), so a rollback re-records what was finally played.
A live match's `.SRP` also carries what it takes to PLAY it again — `SIMV SETP STAT INPT HASH`:
the setup, the state at frame 0, every confirmed frame's input and SIM hash (REPLAY_FORMAT_V2.md
§4). `--srp-resim=<file>` (`src/srp_resim.c`) replays it headless through the netplay step and
names the first frame that parts from the recording: a cross-machine desync reproduced offline from
the recorder's copy. `RECORD=1 scripts/relay_test.sh` resims every copy of what it records.
**The recordings player** (docs/PLAYBACK.md): `make WASM=1 PLAYER=1` builds `dist/playback`, which the
server publishes at `https://online.openswos.net/playback/` (behind the admin login: recordings, reports and
the player are internal, and `scripts/fetch_*.py` delete what they fetch) -- every recording, playable under a scrub bar with
frame numbers and `&f=<frame>` links, so a tester's "the short pass after the first corner" becomes a
frame. `--replay-player=<file>` is the same natively; `scripts\serve_playback.bat` serves it locally
over `build/recordings`. It found three viewer bugs that every replay had: sprites drawn
(W-336)/2 right of the pitch on wide screens, the pitch in left-over colours, the animated ad
unrolled -- `scripts/replay_v2_test.sh` now traces where sprites actually land on screen.
A live match's `.SRP` also keeps what each PLAYER saw (`PRED`: per frame per player the lag
-- how many of the other's inputs were guesses -- and rollbacks; `SEEN`: where each one's
screen drew players somewhere they never were), both halves in one file: each streams its
own to the recorder and they swap them in the lobby (`src/match_prediction.c`), so the
host's file still equals the recorder's. Optional (`[netplay] prediction`); the page draws
it as a timeline and an overlay. GGPO-X's guess is "repeat the last input", checked on every
guessed frame (`[pred]` log line) -- so PRED + INPT rebuild each player's predictions.
It also keeps what each player's game READ from its controls each frame and when (`READ`,
beside `INPT`, what was applied): `scripts/srp_inputs.py <file.SRP>` checks that every input
was applied the agreed delay after it was read, and how evenly each game's frames were paced.
Carried like PRED (recorder stream `RECORD_REPLAY_READ`, lobby swap), so the server must be
rebuilt with the game for its copy to have it.
A replay is shown in the VIEW its match was played in (`ReplayV2_UseRecordedView`: META flags
8/16, else SIMV, else the camera) -- a DOS-view match drawn 272 tall showed rows past the
stadium. The page names who played whom from the recorder's per-match card (`<rec>.json`,
`index.json`; `scripts/recordings_index.py` backfills from the ledger, never addresses), and
the admin page's "Update recordings player" runs `scripts/update_playback.sh` (no root/restart).

### Release builds — `scripts/build_dist.sh`

Builds every target this machine can produce and writes the archives below (WASM
is built but excluded from all of them — it's a web deployment, and its `.data`
preload alone is ~21 MB).

**Everything a tester is handed lands in `dist/`.** `bin/` stays what it has
always been: the shared runtime tree the game reads and every target links its
binary into. `dist/` is gitignored wholesale, which is why release artifacts no
longer show up as untracked files in `git status`. `build_switch.sh` already
wrote `dist/SWOS.nsp`; the zips, the installer and the macOS archives now sit
beside it.

| artifact | ~size | contents |
|---|---|---|
| `dist/SWOS.zip` | 10 MB | the 6 binaries only — for someone who already has a `bin/` |
| `dist/SWOS-portable.zip` | 45 MB | every PC binary + the project's own data + the data setup (`openswos-setup.exe`, `swosdata`) + the Cygwin runtime for `SWOS.exe`; NO original game data (was `SWOS-full.zip`) |
| `dist/SWOS-Amiga.zip` | 2 MB | a SWOS drawer + icon: `SWOS2`, its Workbench script and icon, only the trees the m68k binary opens, `c`/`s`, an empty `save/` |
| `dist/SWOS-Setup.exe` | 14 MB | Inno Setup installer — the Windows half of the full zip, for a tester who'd rather double-click |
| `dist/SWOS.nsp` | 84 MB | the Switch package (`scripts/build_switch.sh`) |
| `dist/SWOS-macOS*.zip` | 19 / 1.4 MB | from `scripts/build_macos.sh` — see below |
| `dist/OpenSWOS-Setup.zip` | ~30 MB | the data setup ALONE for Windows, macOS and Linux (`scripts/build_setup_zip.sh`, after `build_macos.sh`, which brings back the Mac's part): how an Amiga player gets it -- the setup cannot run on the Amiga, it fills the drawer on a PC or Mac. Its programs find `setup/` and `data/` in the folder ABOVE them. The Mac window is UNIVERSAL like the game: a static Slint built for both slices by `tools/swosdata/build_slint_macos.sh` (once per Mac; the published Slint SDK is arm64-only) |

The full dist keeps everything at the zip root because the SDL binary chdir's to
its own directory. Its data list mirrors what `make macos-app` bundles (the
project's tested "what the game needs at runtime" set). `save/` is deliberately
excluded — saves go to a per-user dir (`swos_io.c`), and `bin/save` here is 24 MB
of test careers.

**Windows 10 or later** (`MinVersion=10.0` in `swos.iss`): Cygwin 3.5+ dropped 7 and 8, and
OpenSWOS Setup's Slint is Rust, whose programs need 10 -- on 7 the setup died at start with
0xc0000005 (tester report, 2026-10-21).

The Windows runtime is **four** DLLs, and only two of them are visible to
`cygcheck bin/SWOS.exe`:

| DLL | why |
|---|---|
| `cygwin1.dll` | the Cygwin runtime |
| `cygSDL2-2-0-0.dll` | **not SDL2** — it is `sdl2-compat`, a ~350 KB shim |
| `cygSDL3-0.dll` | the actual SDL, `dlopen`'d by that shim at runtime |
| `cygiconv-2.dll` | a link-time import of `cygSDL3-0.dll` |

Cygwin now ships SDL2 as **sdl2-compat**, which implements the SDL2 API on top
of SDL3 and loads `cygSDL3-0.dll` with `dlopen`. A `dlopen` is invisible to
`cygcheck` and to the PE import table, so a drop built from `cygcheck` on the
exe alone ships two DLLs and dies on any machine without Cygwin with the shim's
own message — **`Failed loading SDL3 library.`** That is a real tester report
(2026-09-07); removing `cygiconv-2.dll` produces the *same* message, because
SDL3 then fails to load and the shim reports its own failure rather than the
cause.

So `cygcheck` is still the right tool — but run it on **`cygSDL3-0.dll` too**,
not only on the exe: it cannot see a library loaded at runtime. It is still
wrong to derive the list with `strings`, which picks up every DLL name mentioned
anywhere, including backends SDL only ever `dlopen`s, and lands you at ~14 MB of
X11/pulse/codec libraries that never load. And **not** the `SDL2.dll` from a
normal SDL release: that is a native MSVC/MinGW build with no `cygwin1.dll`
import, which a Cygwin binary cannot load.

To check a change here, run the exe from a directory holding **only** the
shipped DLLs, with Cygwin off PATH — that is what a tester's machine looks like:

```bash
cmd.exe /c "cd /d C:\Temp\dlltest && set PATH=C:\Windows\system32;C:\Windows && SWOS.exe --help"
```

**Linux gets the same treatment**, at `lib/<arch>/` in `SWOS-portable.zip`:
`libSDL2-2.0.so.0` **and `libdecor-0.so.0`**, for each of the four ELFs that
was built. Two things make this work, and both are easy to undo by accident:

- The binaries carry `-Wl,-rpath,$ORIGIN/lib/$(BUILD_ARCH)` with
  **`--disable-new-dtags`**, i.e. **DT_RPATH, not DT_RUNPATH**. RUNPATH applies
  only to the executable's *direct* dependencies; RPATH is inherited down the
  chain. Ubuntu links SDL2's backends instead of dlopening them (20 `DT_NEEDED`
  entries), so under RUNPATH the bundled libSDL2 loads but *its* dependency on
  libdecor still comes from `ld.so.cache` — measured, not assumed. The cost is
  that `LD_LIBRARY_PATH` no longer overrides; delete `lib/` to use a system SDL2.
- **libdecor is bundled because `libsdl2-2.0-0` is essentially its only
  consumer** (`apt-cache rdepends libdecor-0-0`), so a machine without SDL2 has
  no libdecor either, and shipping libSDL2 alone merely trades
  `libSDL2-2.0.so.0 not found` for `libdecor-0.so.0 not found`.

The other 18 `DT_NEEDED` are deliberately **not** bundled: they are the
X11/Wayland/Mesa/audio stack, they drag in ~50 transitive libraries including
libsystemd and libgcrypt, and any machine with a display has them. Our binaries
need glibc 2.34 anyway (Ubuntu 21.10+ / Debian 12 / Fedora 35+), so a host old
enough to be missing them cannot run SWOS at all. If one ever *is* missing, the
fix is not to bundle more of the distro — it is to build SDL2 ourselves with
`--enable-{x11,wayland,alsa,pulseaudio}-shared`, the way upstream builds its own
release binaries, which leaves libSDL2 with almost no `DT_NEEDED`. All four
cross dev stacks are installed here, so that is a script away.

**macOS needs nothing** — and that is by design, not an oversight. The tiers are
consistent: the *binaries-only* archives (`SWOS.zip`, `SWOS-macOS.zip`) ship no
libraries on any platform, and the *runnable* ones each carry their own
(`SWOS-Setup.exe`/`SWOS-portable.zip` the Cygwin DLLs and now `lib/<arch>`,
`SWOS-macOS-app.zip` its bundled `SDL2.framework`). `--no-full` skips the big one.

The **Amiga** drop carries `data grafs grafs_aga sound` plus `c s`, in a drawer (below). The data list
was derived from the built binary, not guessed —
`strings bin/SWOS2 | grep -oE '^(data|grafs|...)/'` shows it opens exactly those
four plus `save/`, and never touches `grafs_dos`, `grafs_2020`, `grafs_arcade` or
`data_json` (SDL-only asset tiers, ~50 MB the Amiga can't load). `save/` ships
EMPTY: the binary writes there but won't create the directory.

The zip is a **SWOS drawer** to drop onto a hard drive as it is: `SWOS.info` (the drawer's
icon) and `SWOS/` at the top, everything else inside the drawer. Workbench starts it from
`SWOS/SWOS`, a one-line **script** (`SWOS2`) whose Project icon runs `C:IconX` -- IconX
EXECUTEs the file the icon belongs to, so that file must never be the binary ("EXECUTE:
Invalid Directive"). Those files, the icons and their `.uaem` sidecars (the script flag a zip
cannot carry) live in `packaging/amiga/`, which mirrors the layout. `bin/c` and `bin/s` (the
**AmigaDOS boot files**: `s/startup-sequence` runs `SYS:SWOS2` then `c/UAEquit`) ship INSIDE
the drawer, so mounting the drawer as an emulator's boot volume still runs the game -- never at
the top, where a dropped-in `s/` would replace the drive's own startup-sequence. (The harness is
different: it mounts `bin/` as **DH1:** and `cd`s in, so the same relative paths resolve without
booting from it.)

**WHDLoad: our own Slave, no Kickstart** (`asm/whdload/OpenSWOS.slave.s`, built by
`Makefile.amiga` beside SWOS2 as `bin/OpenSWOS.slave`, shipped inside SWOS-Amiga.zip's drawer).
It does what dos.LoadSeg does from Workbench -- `resload_Relocate` SWOS2 into ExpMem, its chip
hunk into BaseMem at $1000 (8-aligned) -- turns the caches on for ExpMem (WHDLoad's default leaves
them off on a 68020 without an MMU), fills in `whd_hook` ("OSWSWHDL", in SWOS2's data: resload
base + the ExpMem left over) and jumps in. `src/resload.c` checks `whd_hook.resload`: set, its
five file calls go to resload (names relative, no `PROGDIR:`), AllocMemAny comes from a heap over
that ExpMem, vectors are at 0, quitting is `resload_Abort`; zero (Workbench/Shell), nothing
changes. Its BaseMem/ExpMem come from SWOS2's hunk header at build time
(`scripts/amiga/whdload_sizes.py`), so a slave only goes with the SWOS2 it was built with.
`asm/whdload/whdload_api.i` is our own: WHDLoad's include files are All Rights Reserved, so only
the interface numbers are written there, never copied. The setup's WHDLoad package
(`SwosData_MakeWhdload`) is `OpenSWOSAGA-NE/` with the Slave, its icon and `data/` (SWOS2 + the
trees); the icons are ours (`scripts/amiga/make_whdload_icons.py` -> `bin/setup/whdload/`).
Test it in the harness with `TRIGGER=":WHDLoad"` and a boot volume whose startup-sequence runs
`WHDLoad SLAVE=OpenSWOS.slave` -- the harness starts PAUSED at the trigger: send `print 1`,
then `continue`.

The **Windows installer** is the fourth artifact, built last because it is the
only one that needs a tool on the Windows side. `scripts/build_installer.sh`
stages the runtime tree into `dist/windows` and runs Inno Setup's `ISCC.exe`
over `packaging/windows/swos.iss`; the result is `dist/SWOS-Setup.exe`. It is a
**packaging step, not a compile** — it needs `bin/SWOS.exe` to exist already, so
`--only installer` will not build one for you.

- **It stages rather than sweeping `bin/`.** `bin/` is the shared data directory
  for every target: the Linux ELFs, `SWOS2` + its `.map`, the WASM bundle,
  `.uaem` sidecars, `SWOS.log` and 24 MB of test careers in `save/`. A wildcard
  over it would ship all of that. The stage list mirrors
  `FULL_DATA_DIRS` in `build_dist.sh`, which mirrors `make macos-app` — **keep
  the three in step**, or the installer and the zip ship different games and a
  bug reproduces on one and not the other.
- **`save/` is never installed and never uninstalled** — saves live in
  `%USERPROFILE%\Documents\SWOS` (`swos_io.c`), so a reinstall never costs a
  player their career.
- **Missing Inno Setup is a SKIP, not a failure.** `build_installer.sh` exits
  **3** for "not installed" and 1 for a real failure, and `build_dist.sh` maps
  those onto SKIP / FAIL, so a drop built on a machine without it still succeeds
  for everything else. Override the search with `ISCC=/path/to/ISCC.exe`.
- **The `AppId` GUID in `swos.iss` is what makes an install an upgrade** rather
  than a second copy. Never change it, and never reuse it for another product.
- The two wizard graphics are `packaging/windows/WizardImage.bmp` (164×314) and
  `WizardSmallImage.bmp` (55×58), 24-bit BMP. Inno will not take PNG.

```bash
scripts/build_dist.sh              # TESTER drop: all targets -> dist/
scripts/build_dist.sh --release    # PUBLIC build: stock injuries + cards
scripts/build_dist.sh --only windows --no-clean
scripts/build_dist.sh --no-installer
scripts/build_dist.sh --help

# The installer on its own, against whatever bin/SWOS.exe is already built:
scripts/build_installer.sh
scripts/build_installer.sh --stage-only    # inspect dist/windows first
```

**No injuries, no cards is a SETTING, off by default, offline only**
(`[testing] no_injuries_cards = true` in `SWOS.ini`, or `SWOS_TEST_NO_INJURIES_CARDS=1` in
the environment for one run; `include/test_rules.h`). With it a tester can hit VIEW RESULT
through a season: no substitution menus, no booking prompts. An online match plays the
real rules whatever either file says -- unless the HOST switches CARDS/INJURIES off in the
lobby (the host's switches, below) -- and so do the rigs (replay corpus, rollback, netplay
tests); `--srp-resim` plays a recording by the rule it was recorded under.
**The host's switches** (`include/netplay_match.h`, report 0042, docs/NETPLAY.md "The lobby"):
SHORT PRESSES, KICK-OFF SYNC and CARDS/INJURIES, ON/OFF on the left of
the lobby -- each a netplay change since 7d9d6d9 -- decided by the host ALONE and played by
both (they travel in the host's rules; `NetplayLobby_ApplyMatch` puts them in force,
`NetplayLobby_RestoreTactics` takes them back). The rows are entries OpenSWOS adds to SWOS
2020's menu through `EXTRA_ENTRIES` in `scripts/import_swos2020_menus.py`, never by hand.
`SWOS_LOBBY_SWITCHES=<hex>` sets them for a test (`HOST_ENV=` of the lobby/relay tests).
A RELEASE build (no-cards rule compiled out) plays with cards as host, and as joiner will
not ready up for a host with CARDS/INJURIES off ("HOST PLAYS WITHOUT CARDS: NOT IN YOUR
BUILD") -- it used to force the switch on silently, which would have desynced a tester
host's no-cards match at the first booking.
Until 2026-10-11 it was a compile-time flag of the same name, on in every tester drop: a
tester's binary was a different game, took the rule online, and a build with it could not
play one without. Those old tester builds are refused at the handshake ("YOUR BUILDS PLAY
BY DIFFERENT RULES").
**`SWOS_TEST_RULES`** is what the compiler still decides: on by default and in every
tester drop (the setting exists; match reports and the menu-nav log default on; the
version line says "tester build"), off with `make SWOS_TEST_RULES=0` /
`build_dist.sh --release`, which compiles the rule OUT for a public release. Keep it
identical across every platform in one drop. With the rule on, subbing an injured player
scores as a DOWNGRADE — see the Makefile's "KNOWN DISPLAY SIDE-EFFECTS" block.

Three traps it exists to handle — **don't hand-roll a release without them**:

- **`SWOS_SAVE_FORMAT_JSON_VERIFY` defaults to ON for every target except
  WASM.** The `?= 0` line sits *inside* the Makefile's `ifdef WASM` branch, so a
  plain `make` builds in the save round-trip verifier, which makes every SAVE
  zero the saved fields, reload the file and deliberately not restore. The
  script passes `=0` (use `--verify` to opt back in).
- **The Amiga makefile keeps objects in its own `obj/` tree**, so `rm -rf build`
  leaves it stale and the build reports "Nothing to be done for 'all'". The
  script also runs `make -f Makefile.amiga clean`.
- **`Makefile.amiga` has no `SWOS_*` knobs**, so the m68k target would silently
  miss `SWOS_TEST_RULES`; the script passes it through that makefile's
  `EXTRA_CFLAGS` escape hatch. (The Amiga reads no SWOS.ini, so the testers' rule
  cannot be switched on there: it always plays the real rules.)

`bin/SWOS.exe` is a **Cygwin** binary (imports `cygwin1.dll`; testers need that
plus `SDL2.dll`). The Makefile selects the Windows target from the *host shell*
(`$(OS)` / `uname -s`), never from a flag, so from WSL the script reaches Cygwin
through Windows interop (`/mnt/c/cygwin64/bin/bash.exe -lc`). Machine paths are
overridable: `EMSDK_DIR`, `AMIGA_TOOLCHAIN`, `CYGWIN_BASH`.

**Icons** come from one drawing, `SWOS.svg`. `python3 scripts/generate_icons.py` (cairosvg +
Pillow) renders every size from the vector and writes `SWOS.ico` (linked into `SWOS.exe` by
`SWOS.rc`, and the installer's icon), `SWOS.icns` (copied into the `.app`),
`packaging/macos/SWOS.iconset/`, `packaging/linux/hicolor/`, `packaging/switch/swos_icon.bmp`
(the 1024x1024 24-bit BMP the Switch tools demand), `include/swos_window_icon.h` (the Linux
window icon, set in `platform_sdl.c`) and the favicon inlined in `wasm/shell.html`. All of it is
committed, so no build needs the script; re-run it after changing the SVG. A hand-tuned bitmap
for a small size goes in `packaging/icons/overrides/<size>.png` and wins over the vector.

**Build stamp.** Every binary says which commit it is, because a tester report that does not
is expensive to triage (docs/TRIAGE.md §5, "Never from a `-dirty` tree"). `scripts/gen_build_info.sh` writes
`include/swos_build.h` (gitignored) with the short hash, a `-dirty` marker when tracked source
differs from it, and the commit date; `src/build_info.c` exposes it. BOTH makefiles run the
script at parse time, so the Amiga binary carries it too, and the header is rewritten only when
it changes, so an unchanged commit does not trigger a rebuild. It shows up in four places: the
bottom line of the main menu, the SDL window title, the first line of `SWOS.log`, and
`--version`. **A drop must never be cut from a `-dirty` tree** — that is the stamp telling you
the binaries are not the commit you think they are.

### macOS — `scripts/build_macos.sh`

macOS cannot be built here (`make macos-universal` / `make macos-app` hard-fail
unless `uname -s` is Darwin, and there is no cross-compile path), so this script
drives the Mac mini over ssh and brings the bundle back:

```bash
scripts/build_macos.sh             # tester build -> both archives below
scripts/build_macos.sh --release   # public build
scripts/build_macos.sh --no-pull   # build the Mac's tree as-is
```

Two archives come back, mirroring `build_dist.sh`'s split:

| archive | ~size | contents |
|---|---|---|
| `dist/SWOS-macOS-app.zip` | 19 MB | the drag-installable `.app`, `SDL2.framework` bundled |
| `dist/SWOS-macOS.zip` | 1.4 MB | the universal binary alone — **needs SDL2 installed system-wide**, since the framework only ships inside the `.app` |

They are packed into the **Mac's** `dist/` too, not its `bin/` — the Mac is a
checkout of this same repo and follows the same split.

Defaults match `build_dist.sh` so a drop is uniform. Overrides: `MAC_HOST`
(default `bbaker@192.168.1.200`), `MAC_PATH` (`~/Programming/OpenSWOS`),
`MAC_ZIP`.

- It `git pull --ff-only`s and **prints the commit it built**, because a stale
  remote checkout is invisible otherwise — a tester once reported a fixed bug as
  still present on macOS purely because that Mac was two commits behind.
- The bundle is packed with `ditto -c -k --sequesterRsrc --keepParent`, never
  `zip -r`: `SDL2.framework` is built out of symlinks, and plain zip follows
  them, producing a bloated archive whose framework won't load. The summary
  prints the preserved-symlink count — 0 means the bundle is broken.
- **`ssh` does not preserve argv boundaries** — it joins its arguments into one
  string that the remote shell re-splits, so multi-word arguments must be
  pre-quoted (`printf %q`). Getting this wrong silently shifted a make variable
  onto `-j`.

## Long-term roadmap

1. ~~**Stabilize the SDL port.**~~ / ~~**Back-port to the Amiga C port.**~~
   Superseded: the trees are merged, so a fix lands on both targets at once.
   `SWOS2.s` is retained as a **reference only** (`data/SWOS2.s`) — the Amiga
   build runs entirely from C.
2. **Keep both targets building.** Any change to shared code must compile for
   m68k as well as SDL. The Amiga build is the stricter of the two (32-bit
   pointers, big-endian, `-nostdlib`), so it catches bitness/endian slips early.
3. **Untangle the data overlays.** The current code mirrors the Amiga's BSS
   layout faithfully — adjacent-globals reuse, careerFileBuffer-style
   overlays, `selTeamsPtr` aliased onto its own data, etc. (see
   "Adjacent-globals BSS pattern" below). Once both ports run on C, those
   overlays become **proper structs** with named fields. No more `.equ`
   aliases, no more "this label is the next label's label", no more
   adjacent-reads-into-the-following-global. Memory is structured.
4. **Replace the save format.** The current `.CAR` format is a raw memory
   dump of the careerFileBuffer overlay region — fragile against any layout
   change, and contains absolute pointers (see "Pointer-to-disk problem"
   below). Eventually replace with a structured format (likely JSON or a
   tagged binary) that serializes named fields, not memory addresses. This
   change happens *together* on both ports.

What this means for current decisions:

- **"Compatible with Amiga" means our own m68k target, not original SWOS2.s.**
  Save-file format changes we make are fine — the original Amiga binary is not
  a compatibility target. SWOS2.s is a *reference*, not a *spec*.
- **Adjacent-globals workarounds and `.equ` aliases are a transitional
  scaffold.** They exist so both targets can run today against the same C
  code. When step 3 happens, we delete every alias and replace every
  "label-into-next-label" read with a struct field access. Don't write *new*
  code that depends on adjacency — fix the call site to use a proper struct
  field instead.
- **Don't preserve format details that exist only because m68k did them.**
  Big-endian byte order in the on-disk save, raw pointer fields, the precise
  98476-byte career-header size — these are all artifacts of the Amiga
  memory dump, not deliberate format design. They're fair game to change in
  step 4. Until then we use the BE_*/PACK_BE helpers to keep the SDL build
  reading the Amiga byte order.
- **Don't introduce an SDL-only divergence without checking the m68k build.**
  There is no back-port lag any more — a shared file that only compiles on SDL
  breaks the Amiga target in the same commit. Build both before you call it done.

## Pointer-to-disk problem

The careerFileBuffer overlay (see `swos_globals_career_overlay.c`) contains
several BSS labels typed as pointers — `currentCompetitionPtr`,
`secondaryCompetitionPtr`, `careerCompetitionSlotPtrTable[5]`,
`*CompStringOffset`, `diyCompetitionTablePtr`, plus runtime-only pointers
inside `tmdFileBuffer` (`inGameTeamPtr`, `spritesTable`, etc.). Because
`_SaveCareerFile` is a raw memory dump of the overlay region, those pointer
*values* end up in the .CAR file. After a rebuild (or load on a different
machine), the linker places the destination buffers at different addresses
and the saved pointers become stale.

**Original SWOS2.s ignored this problem** — the Amiga binary loaded at a
fixed address with a stable BSS layout, so saved pointer values stayed
valid across runs of the same binary. `LoadCareerFile` does no pointer
fixups at all (`LoadCompetitionFile` is just a magic-bytes verifier). This
is one of the bugs we *don't* inherit: our Amiga C port no longer places
data at fixed addresses either, so the same fixup is needed on both ports.

**The JSON save format is live** (`src/save_format_json.c`, ~3270 lines).
Build with `make SWOS_SAVE_FORMAT_JSON=1` to write new saves in JSON via
mjson — pointer fields are emitted as semantic identities (competition id,
string-pool key, etc.) rather than raw address bytes, so saves round-trip
across rebuilds and across 32/64-bit. Load auto-detects format from the
file header so legacy `.CAR` dumps still open. `SWOS_SAVE_FORMAT_JSON_VERIFY=1`
adds a load-time diff that compares the JSON-derived byte image against the
opaque-data fallback — a zero-mismatch result means every meaningful byte
is covered by typed fields/blobs and opaque_data can be removed. Step 4
of the long-term roadmap is now mostly "delete the legacy dump path once
all save types are proven."

## Data

> **The original game's data is NOT in this repo** (since 2026-10-19). The data
> setup, `tools/swosdata` (C library + `swosdata` CLI + the Slint window
> `openswos-setup`), builds it from the player's own copy -- Amiga ADFs, the PC
> (GOG) folder, the Jaguar cartridge, a SWOS 2020 install for its pitches
> (`grafs_2020/` + `grafs_aga/P2020_n.MAP`, from its `swos/` folder: the set its
> launcher picked; untracked 2026-10-19 until their authors agree) --
> byte-for-byte what used to be committed,
> into the **assets folder**: `$SWOS_ASSETS`, default `assets` in the per-user
> game folder (`swos_assets_dir()`, `src/swos_io.c`). The SDL game looks there
> first and beside the binary second (`swos_data_path()`); `make WASM=1` preloads
> `bin/` plus `$(SWOS_ASSETS)`. Without the Amiga disks the game plays the PC
> sound/music and skips the Amiga loading screens (`_gNoAmigaSound`,
> `_gNoAmigaScreens`, main.c). What stays tracked in the trees below is the
> project's own (LOADER76, credits photos, 1UP.WAV, `bin/setup/`);
> the rest is `.gitignore`d, so a developer's `bin/` can still hold it (the
> FS-UAE harness needs it there). **Every drop is staged from git-tracked files
> only** (`scripts/stage_runtime.sh`) -- never sweep `bin/` into a release.
> Check a change to the converters with `swosdata ... --out X` then
> `swosdata --compare X <a bin/ that has the data>`.
> **ASSETS.ini** (`include/assets_ini.h`), written by the setup in the assets folder: a DATA
> VERSION per part (the setup's steps) and the sources it used, which it fills back in next
> time. A part's version goes up ONLY when the setup makes that part differently -- then the
> setup shows "update needed" / an Update button, and the game says so once at start-up
> (`src/assets_check.c`, "Don't remind me" = `[general] assets_reminder_off`). An assets folder
> without ASSETS.ini is an older setup's. `scripts/check_data_versions.py` runs the setup on the
> reference copies and FAILS when a part's files change and its version did not (record:
> `tools/swosdata/data_versions.txt`, `--update` after raising one): run it after touching
> tools/swosdata, third_party/swosgfx or bin/setup. The AGA ball (setup/*_AGA.bmp) is the Amiga
> drawer's only: `assets/amiga/grafs_aga/`, copied over the drawer's by "Add the game data".

`bin/` is the **single** data directory, shared by both targets. The SDL binary
runs with `bin/` as its CWD; FS-UAE mounts `bin/` as **DH1:** and `cd`s into it,
so the same relative paths (`data/`, `grafs/`, `sound/`, `save/`) resolve on both.
Nothing is duplicated under `uae/` any more.

- bin/data - Amiga game data
- bin/grafs - Amiga graphics in 4 bitplane format
- bin/grafs_aga - Amiga graphics in 8 bitplane format (AGA pre-processor switches between AGA and non-AGA)
- bin/grafs_dos, bin/grafs_2020, bin/grafs_arcade - alternate asset tiers (SDL only)
- bin/grafs_jag - the Atari Jaguar's gameplay sprites, 8bpp (SDL only, `--jag-graphics`,
  docs/JAGUAR.md). Built from SwosGfx's `-jag2amiga` output by `scripts/make_jag_graphics.py`.
  The Jaguar redrew each flat colour as a ramp, so a kit slot is FOUR shades (240-255) and
  only those follow the team; the art's own colours are installed in free palette entries
  (82-175), the ramps in 176-239, so the pitch, HUD and menus are untouched.
- bin/sound - Amiga music modules and sound effects in RJP1 format
- bin/sound_dos - the PC game's sampled effects, crowd, chants and commentary: 156 raw files,
  unsigned 8-bit mono at 22,050 Hz (SDL only, `[audio] style = pc`, docs/DOS_SOUND.md). Copied and
  indexed by `scripts/gen_dos_sound_tables.py` from the PC game's own tables. The match code sends
  EVENTS (`DOS_SOUND(event, arg)`, `include/dos_sound.h`) through the sound gate -- recorded, and
  held back on a re-simulated frame, whatever the style -- and `src/dos_sound.c` owns everything
  after that; none of it is match state. `--dos-sound-selftest` checks the files and tables.
  A player's own commentary is a folder per category under `sound_dos/commentary/` (SWOS 2020's
  names; `.wav`/`.mp3`/`.raw`, any length; `[audio] commentary_dir`), per category in place of the
  PC game's lines.
  `[audio] music = pc` plays the PC menu music, `menu.xmi` with `sample.ad`, through our own
  player (`src/dos_music.c`: XMI reader, Miles timbre reader, 18-voice driver) on the
  public-domain Opal chip (`third_party/opal`) with the MIT Miles arithmetic
  (`third_party/opl_models`). **Never add libADLMIDI: its player is GPL v3.**
  `--dos-music-selftest` plays the song through. The same setting plays the PC CD's title song
  in the intro: `sound_dos/title.mp3`, streamed by `src/music_stream.c` with dr_mp3
  (`third_party/dr_mp3`, public domain).
- bin/save - save games, shared by both targets
- bin/data_json - editable JSON that bakes into competition/menu/tactics C (SDL only)
- If a file starts with `RNC` then it's RNC compressed; `src/rnc.c` decompresses it on both targets.

## Sources

- **SWOS-RE — `C:\Projects\Amiga\SWOS-RE` (github.com/benbaker76/SWOS-RE).**
  The upstream reverse-engineering project this port grew out of: the
  documented understanding of the original Amiga 68k binary. **This is the
  authority on original intent** — when the C looks wrong, arbitrary, or you
  need to know what a magic constant/struct field/BSS label actually meant,
  check here BEFORE guessing or "fixing" it. Contains:
  - `README.md` (~77KB) — the narrative: version info, DOS-vs-Amiga history,
    formats, and how the RE was done (IDA Pro for symbols → Ghidra for m68k
    decompilation via a patched exporter plugin → Python cleanup passes).
  - `Docs/` — `Structs.txt`, `Variables.md`, `Prototypes.txt`, `Menus.txt`,
    `Sprites.txt`, plus the reloc dumps.
  - `IDAPro/`, `Ghidra/` — the actual databases.
  Its DOS-side naming derives from Zlatko Karakaš's DOS IDA database, which is
  why our symbol names often match the DOS port's.

  ⚠️ **The COMMENTS in `data/SWOS2.s` come from that DOS database too, so they
  are evidence of intent and none at all about the instructions beside them.**
  They can be stale, DOS-specific, or simply wrong for both releases — a
  four-shift sequence annotated "sprite speed /= 32" cost real accuracy until
  the instructions were counted (docs/TESTS.md, "The comment is not the code").
  When a comment and the code disagree, the code wins, and
  `swos-port/swos/swos.asm` is a second witness that costs one grep.
- `data/` (in this repo) - IDA/Ghidra reference artifacts exported from
  SWOS-RE: `Data.c`, `SWOS2.s`, `SWOS2_IDA.h`, `SWOS2.h`. **Reference only —
  not compiled** (`Makefile.amiga` scans only `src/`). These are a *snapshot*;
  SWOS-RE is the live source.

  `data/SWOS2.s` (~5.4 MB, matching SWOS-RE's `Amiga/SWOS/SWOS2.s`) is **the**
  disassembly to read — every doc and audit cites it. It is not assembled.

  > A second, smaller (~1.2 MB) *sparse* `SWOS2.s` used to sit at the repo root,
  > paired with `asm/swos2_unit.s` which `include`d it. That pair was the original
  > bootstrap scaffolding — assemble the whole 68k program as one vasm unit and
  > progressively move routines out into C. Both were **deleted 2026-08-12**: the
  > port runs entirely from C, neither was in the build (`swos_asm_sources` is
  > only `asm/resload_unit.s`), and `startShim` comes from
  > `src/platform_amiga_stubs.c`. Deleting them left the Amiga binary
  > byte-for-byte identical (same sections, same 5721 symbols). Recover from git
  > history if ever needed.

### Writing new m68k assembly

There IS still a live asm path — don't resurrect the old monolith to use it:

- `asm/resload_unit.s` (vasm) `include`s `../Resload.s`. **`Resload.s` at the
  repo root is a real build input** — don't mistake it for the deleted
  `SWOS2.s` and remove it.
- `support/gcc8_a_support.s` (gas) is the toolchain support unit.

To move a routine back to assembly (e.g. an interrupt handler where C codegen
isn't tight enough): add a **new** `asm/<name>.s`, append it to
`swos_asm_sources` in `Makefile.amiga`, `xdef` the entry point, and delete or
`#ifdef` out the C version. Copy the original body from `data/SWOS2.s`. Do not
re-introduce a single all-encompassing asm unit — the per-routine split is what
lets the two targets share `src/`.
- C:\Projects\AI\SWOS\swos-port-c - Partial C++ port with the Amiga game loop working under SDL. Used as the verified game-loop reference.
- **C:\Projects\GitHub\swos-2020 — SWOS 2020, which OpenSWOS continues.** Its online
  menus (`src/menus/*.mnu`, compiled by its `mnu2h/`) are imported by
  `scripts/import_swos2020_menus.py` into `bin/data_json/menus/swos2020/` and run from
  JSON, with handlers in `src/online_menus.c` (docs/MENUS.md §10). Its netplay
  (`src/mpgame.cpp`, on GGPO-X) is the reference for `src/netplay.c` (docs/NETPLAY.md).
  `ggpo-x` and `cereal` are git submodules there — `git submodule update --init` first.
- **C:\Projects\AI\SWOS\swosgfx — swosgfx (github.com/benbaker76/swosgfx)**, the C99 library + C++
  CLI rewrite of SwosGfx, byte-identical to the C# for every mode; `tools/swosdata` is built on it
  (submodule `third_party/swosgfx`). A converter change goes there, then the submodule is bumped —
  rerun `swosdata --compare` after.
- C:\Projects\GitHub\SwosGfx - SWOS graphics conversion app (C#; on GitHub as SwosGfx-CSharp). **`SwosGfx\AmigaTools.cs` is the authoritative reference for parsing Amiga SPRITE, PITCH (tile-map mapping + bitplane tile stream), and PALETTE data.** Consult it whenever implementing or debugging an Amiga asset decoder.
- C:\msys64\home\headk\rjp1player - RJP1 module player for the original Amiga music modules / SFX extraction.
- github.com/benbaker76/AmigaScope - the gdb/profiling harness, a submodule at `scripts/amiga/amigascope`.

## Files

- `data/Data.c` - C data structures exported from IDA Amiga SWOS
- `data/SWOS2.s` - Original Amiga SWOS 68k asm
- C:\Projects\GitHub\SwosGfx\SwosGfx\AmigaTools.cs - Routines to parse and manage Amiga SWOS sprites, pitch and palettes
- C:\Projects\GitHub\SwosGfx\SwosGfx\bin\Debug\net8.0\Amiga_Images_16 - Extracted graphics from Amiga SWOS in 8bpp BMP format

## Architecture conventions

### Graphics pipeline is 8bpp chunky end-to-end

- **Frontbuffer**: 8bpp chunky. One byte per pixel = palette index. RGBA conversion happens only at the SDL texture upload boundary (platform_sdl.c).
- **Sprites**: 8bpp chunky **after load**. The Amiga bitplane decode happens once in `_ProcessImageCommandStream` (src/image_cmd.c) — every render-time consumer (blitter, `_ConvertSpriteColors`, glyph blit, `_Text2Sprite`, etc.) reads chunky bytes, not planes.
- **Why**: eliminates per-pixel bitplane-gather at blit time, reduces `_ConvertSpriteColors` to a 1-byte LUT, and unifies the render path with the DOS-data variant so either asset set works with the same pipeline. Conversion is the responsibility of the loader, not every downstream function.
- When adding new rendering code, assume 8bpp chunky sprite data. If you need the bitplane layout, you are doing something wrong — fix the load site instead.

### Endian-swap macros are explicit for back-portability

SWOS data files (team files, RNC headers, save games, audio modules) were authored on Amiga m68k (big-endian). Fixes for the x86 LE host MUST go through `src/endian_helpers.h` rather than inline byte arithmetic or `__builtin_bswap*` calls, so the SDL changes can be cherry-picked cleanly back to the Amiga tree (where every macro is a no-op).

Use:
- `BE16_TO_HOST(x)` / `HOST_TO_BE16(x)` — 16-bit byteswap (no-op on BE host)
- `BE32_TO_HOST(x)` / `HOST_TO_BE32(x)` — 32-bit byteswap
- `BE_READ_U16(p)` / `BE_READ_U32(p)` — unaligned big-endian read from a byte pointer (use for file-format headers like RNC)
- `BE_WRITE_U16` / `BE_WRITE_U32` — unaligned BE write
- `PACK_BE_U16(hi, lo)` — combine two separate single-byte struct fields into the uint16 an m68k `move.w` would produce. Use this wherever the original asm/C did `*(uint16_t *)&struct.byteField` and the next byte is a *separate* logical field (e.g. `PACK_BE_U16(team.countryNumber, team.teamOrdinal)` for team id).

Do **not** write `*(uint16_t *)&foo.byteField`, `((uint16_t)hi << 8) | lo`, or hand-rolled byte arithmetic for endian-sensitive reads — use the macros so the intent is greppable and the Amiga build stays compatible.

### Save/load is typed JSON of NAMED fields — no addresses, no ranges, no raw dumps

The save/load format (`src/save_format_json.c`) serializes **named fields** to
JSON. This is the same cleanup as untangling the Amiga's overlapped /
adjacency-packed data (roadmap step 3): the on-disk format must be **decoupled
from the runtime memory layout** so a save survives rebuilds, 32↔64-bit, and
LE↔BE. The Amiga's memory-saving tricks (overlaid globals, "this label is the
next label's label", region dumps) are what made SWOS hard to reverse-engineer
— they must NOT leak into the save format.

Save/load must **never**:

- **Decide what to save/restore by a runtime ADDRESS RANGE.** Testing a field's
  runtime address against a `[&symA, &symB)` pointer span — e.g.
  `field_in_range(f, &competitionFileBuffer, &numSelectedTeams)` — is
  forbidden. Symbol addresses are build/target-specific: a range that is valid
  on the 64-bit SDL build is **empty or inverted** on the 32-bit Amiga when the
  two symbols' relative order flips. (Real bug, 2026-06-09: on the Amiga
  `&competitionFileBuffer` (0x565a94) sorts *above* `&numSelectedTeams`
  (0x33cb00), so the `.DIY` comp-scalars range was empty, `numSelectedTeams`
  never restored → 0 → cup `_VerifyTeamLoaded` panic.) Select fields to
  serialize by **explicit name/key**, never by address span — and never do
  "fuzzy"/approximate pointer comparisons either.
- **Emit raw binary memory dumps** of a struct/overlay region (a `FLD_BYTES` of
  a whole buffer). Promote each region to typed, named fields.
- **Emit or compare pointers / pseudo-pointer (address-derived) values.** Save a
  semantic identity (competition id, team index, string-pool key) and
  re-derive the pointer on load.

If you find yourself comparing `&globalA` to `&globalB`, gating on whether an
address falls in a span, or copying a region by address span — stop. That is
the exact fragility this format exists to remove.

**KEEP binary READ support for `.TAC`, `.HIL`, `.EDT`, and `.CFG`.** The JSON
migration applies to the big overlay-dump saves (`.CAR`/`.SEA`/`.PRE`/`.DIY`) —
there the legacy raw-dump *reader* is dead post-untangle and is being retired.
But the small fixed-layout binary records (`.TAC` tactics, `.HIL` highlights,
`.EDT` edited teams / `CUSTOMS.EDT`, `.CFG` options) are still authored/consumed
as binary (`.EDT` is a SWOS-community interop format too); their
`_Read*`/`_Load*`/`_LoadFileEx` paths are NOT legacy and must be maintained.
Don't fold them into a "retire legacy reads" cleanup. See `docs/SAVE_DATA.md`
"Format migration / read-support policy".

### NULL-deref crashes are a symptom, not a bug

When ported asm code SIGSEGVs on a pointer field (`team->inGameTeamPtr`, `team->spritesTable`, a SpriteGraphics slot, etc.), **do not add a `if (!ptr) return;` guard and move on**. The guard masks a real problem:

- On Amiga the BSS was one flat `ds.b` block. Every symbol landed somewhere mapped and adjacent globals were contiguous, so reads past the "end" of a variable silently picked up the next declared global (often intentional — see the `stadiumMenuLittlePlayer`/`littlePlayerSpriteGeneric`/`littlePlayersColorTable` triple that `MENU_CMD_SPRITE_COPY` reads as one 36-byte struct).
- On our SDL host each global is its own `.o` symbol at a linker-determined address. Reads beyond a variable's declared size hit an unrelated global — or unmapped memory — and nothing is adjacent unless we make it so.

When you see a NULL-deref crash, root-cause it:

1. **Look at the Amiga source.** If the crashing code is reading `*(type *)&sym + offset`, grep `SWOS2.s` (or the Amiga-port `Data.c`) for the label — whatever `dc.b`/`dc.w`/`dc.l` follows it is the "next global" the code expects adjacent. Merge the run into one shared backing buffer with `.equ` symbol aliases (see `stadiumMenuLittlePlayerBackingBuf` / `currentMenu` in `src/swos_globals.c` for the pattern).
2. **Look for undersized stubs.** If the crashing pointer should have been written during init, trace back — is it a `char foo[4096]` auto-stub in `swos_amiga_data_stubs.c` masking a typed definition the code expects? Replace the stub with the real typed data from `Data.c` / `SWOS2.s` (see `src/swos_data_bulk_imports.c` for the bulk-imported set).
3. **Look for int8_t-vs-int16_t traps.** A `int8_t[2N]` definition that the game reads as `int16_t[N]` decodes to different values on LE vs BE — crashes and weird behaviour both come from this. Re-encode the table with the correct type.
4. **Only after ruling the above out**, consider whether the pointer is genuinely optional in the intended design (rare — most of these fields were guaranteed non-NULL by the original Amiga layout).

Adding a NULL guard "because it crashes" hides whichever of 1/2/3 is actually wrong and lets unrelated data stay broken for longer.

### Panics are signals — never patch them out

The same rule applies to every original-asm panic, not just NULL-deref SIGSEGVs. The Amiga code panics (`_PanicHaltLoop`, `panicReasonCode = 0x...`) when an invariant has been violated — a team that was supposed to be in the cache isn't, a slot count that should be 16 is 6073, a magic value got stomped, a `findInList` walked off the end. These panics are the original game telling us the *previous* setup step left state in a shape it shouldn't be. **Trust them.**

Do not:
- Replace `_PanicHaltLoop()` with a `return` / fallback / "load it now" path to make the menu draw work.
- Add a `if (X != expected) X = sane_default;` clamp before a panic-checking site.
- Wrap a `_VerifyTeamLoaded` / `_FindTeamInDIY` / `_GetCurrentSeasonPointer` call in a fallback that loads or fabricates the data the original would have expected to already be there.
- Bump a sentinel/epsilon to silence a `magicValue != 0x12345678` check.

Do:
- **Trace upward.** What was meant to populate the missing data? Add prints there to verify it ran and wrote the expected values.
- **Compare to the asm.** Find the analogous call site in `SWOS2.s` and check it matches the C ordering, parameters, and side effects (especially registers d0-d7/a0-a6 the asm relied on as scratch state).
- **Suspect adjacency / endian / sizing first.** 90% of the panics we hit on bring-up are one of: undersized backing buffer (`teamSelectionKeyCache` was 1 byte), `int8_t[2N]` stored where the game reads `int16_t[N]`, `*(uint16_t *)p` reading a `{country, ordinal}` byte pair without `BE_READ_U16`, or a `.equ`-aliased global the SDL build forgot to alias.
- **Leave the panic in.** Once the *real* setup bug is fixed, the panic stops firing on its own. If you can't find the cause yet, leave the crash and tell the user — a hard panic on an unknown invariant is far more useful than silent corruption.

Patching out a panic to "make the screen render" is the worst kind of bring-up debt: the symptom disappears but the corrupted state propagates into save files, wage calculations, scorer tables, etc., where it surfaces hours later as something completely unrelated.

### Amiga target: CHIP RAM placement is load-bearing

Data the original kept in **Chip RAM** must stay there — anything the custom
chips DMA from (copper lists, bitplanes, the frontbuffer, audio samples) is
unreachable from Fast RAM and fails silently or corrupts the display.

- Globals that lived in `BSSInChip` in `SWOS2.s` must land in Chip RAM in C.
  Use the `SWOS_CHIP_BSS` section attribute (`include/swos_sections.h`), not a
  plain `.bss` definition.
- **`SWOS2.ld` is authoritative** for section placement and the memory map.
  Read it before adding a new module or migrating a global out of asm.
- **Validate every Amiga build against the map file** (`bin/SWOS2.map`):
  - code/data land in the expected region (Chip vs Fast)
  - `BSSInChip`-equivalent data is still in Chip RAM
  - no large unintended shifts in segment sizes — those expose hard-coded
    offset bugs elsewhere

On SDL these attributes compile to no-ops, so a Chip-RAM mistake is **invisible
on the SDL target** and only shows up on Amiga. Check the map, don't assume.

### Amiga target: code-as-data and boundary spill

`SWOS2.s` sometimes reuses a **code block as a data buffer** once it's no longer
executed (e.g. `ShowLoaderIntroSequence` is really `team1Scorers[8]`), and
sometimes indexes **past the declared end** of a label, relying on the next
label being contiguous.

Porting rule: do **not** preserve the fake code-label aliases. Move the storage
into real, correctly-typed globals in the correct section, and update the call
sites. For genuine adjacency, merge the run into one backing buffer with `.equ`
aliases — see the `stadiumMenuLittlePlayer` triple in `src/swos_globals.c`, and
"NULL-deref crashes are a symptom" above. Use `bin/SWOS2.map` to confirm whether
two objects really were contiguous.

### Required C types

Use only the fixed-width integer types: `int8_t`/`int16_t`/`int32_t` and their
`uint` counterparts. Avoid `long`, `short`, `char` (except raw byte buffers and
`char *` strings), `size_t` and `bool` unless there's a strong reason — the
widths differ between the 32-bit m68k target and the 64-bit SDL hosts, and
obscuring the 68k semantics is how endian/size bugs get in.

### Struct access — typed structs, not byte offsets

Game struct definitions live in **`include/game_types.h`**. Access fields via
`->`/`.`, declare externs with real types (`extern Sprite *lastPlayerPlayed;`,
not `extern uint32_t`). For `FixedPoint` (16.16): `.raw`, `.s.i`, `.s.frac`.

Do **not** reintroduce byte-offset macros (`RD16(ptr, SPRITE_X)`,
`*(int16_t *)(ptr + 30)`, …) — they were fully removed. When `SWOS2.s` uses a
capitalised EQU offset like `TEAMFILE_TACTICS`, look the struct up in
`data/SWOS2_IDA.h` and use the matching `game_types.h` struct. Prototypes in the
asm comments (`; prototype: void __usercall Foo(PlayerFile *player@<a0>)`) tell
you the parameter types.
