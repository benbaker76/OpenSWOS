# OpenSWOS agent entry points

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
