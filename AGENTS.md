# OpenSWOS agent entry points

## Driving or testing the Amiga UI in FS-UAE

Read `docs/AMIGA_UI_TESTING.md` and `docs/LOCAL_WORKFLOW.md` first. Use the
tested debugger workflow in `scripts/testing_support/amiga_session.py` and
`amiga_challenge_benchmark.py` for fast menu flows. It observes game memory and
does not require Windows focus or Amiga `SWOS.log` output.

Start with `amiga_session.py status` (the proxy's `health` cannot tell a dead
emulator from a running one). Navigate with `amiga_goto.py <labels|preset>`, hand
the game over with `release` and take it back with `pause`. Never signal gdb:
SIGINT kills it, and the debugger port is one-shot.

Reuse a healthy, paused test session for the same build. A changed executable
requires a fresh boot; never claim that a running emulator picked up a rebuild.
Budget about 30 seconds for boot, then measure menu actions and match loading
separately. Do not rediscover or promise warp acceleration: the tested settings
did not reduce this machine's boot time.

Never kill FS-UAE or GDB by process name. Only stop an owned, disposable test
session; after handing a running match to the user, leave it alone. Use the
private proxy/socket specified in the guide, not the legacy shared debugger.
For real keyboard/focus or visual checks, use the explicitly targeted fallback
in that guide and distinguish those checks from debugger-driven menu tests.

## Creating challenges

When the user asks to **create a challenge**, **generate a challenge**, **make an
Amiga challenge**, or **make an SDL challenge** (including similar wording), use
the existing challenge generator. The user does not need to remember document
paths or invoke a special command.

Read these files before doing the work:

1. `docs/CHALLENGE_REQUESTS.md` — request format, supported choices, limits and
   working examples.
2. `docs/LOCAL_WORKFLOW.md` — this machine's build and run paths.
3. `docs/CHALLENGE.md`, section “Generating challenges” — target-specific commands
   and compatibility requirements.

For implementation changes or debugging, also read `docs/CHALLENGE_GEN.md` and
`docs/CHALLENGE_GEN_AGENT.md`.

The usual request is: teams, minute, situation (corner or kickoff), ball side,
score, human-controlled team, and platform. Teams may be from different leagues
or countries. Use the current conversation to fill in known choices; ask only
for missing information that matters. Do not promise unsupported starting states.

For Amiga generation, use `scripts/challenge_gen_amiga.ps1` with a JSON spec.
For SDL, use `SWOS --challenge-gen=<spec.json>`. Generate with the requested
target's engine; `.CHL` files are target-specific.

Explain before generation that the machine first prepares and saves the position,
then closes its generation session. The user subsequently loads the finished
`.CHL` from CHALLENGES to play directly at the requested minute. The visible
Amiga preparation sequence and planned closure are not the loaded challenge.
Do not stop or overwrite the user's running game to generate another file.

Verify the resulting file's teams, score, minute and human side with
`scripts/chl_dump.py`, report its location, and distinguish file verification from
actual play testing. Keep the request spec so the challenge can be regenerated.
