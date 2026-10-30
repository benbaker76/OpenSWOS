# FORKING.md — OpenSWOS downstream (SWOS United)

SWOS United (github.com/SWOS-United/SWOS-United) is a fork of OpenSWOS that keeps OpenSWOS's
history and takes every change OpenSWOS makes. OpenSWOS stays the upstream: it never takes
changes back by merge. This file is how the two are kept together without conflicts.

## The rule: one direction, by merge

- Work that both need goes into **OpenSWOS first**: the engine, fixes, tester reports, netplay
  and GGPO-X, World 2026-27, the data setup, the tests. SWOS United gets it at its next sync.
- Only what makes SWOS United different is committed there.
- A fix made in SWOS United first that OpenSWOS needs too is **cherry-picked** into OpenSWOS;
  the next sync then sees the same change on both sides and goes through cleanly.
- Never merge SWOS United into OpenSWOS.

In SWOS United, once:

```bash
git remote add upstream git@github.com:benbaker76/OpenSWOS.git
git config rerere.enabled true      # a conflict resolved once is resolved the next time too
```

Then, every sync (often: small merges rarely conflict):

```bash
git fetch upstream
git merge upstream/main             # never rebase, never squash: the shared history is the point
# build both targets, run what the merge touched, then
git push
```

## What a fork changes: `include/project_identity.h`

Everything that names the project, and every identifier another build or a server compares,
comes from that one header, so a fork changes it and nothing else for them. OpenSWOS does not
edit its values, so the file never conflicts:

| Macro | OpenSWOS | What it is |
|---|---|---|
| `PROJECT_NAME`, `PROJECT_NAME_UPPER` | OpenSWOS | window title, build line, messages, the setup's window |
| `PROJECT_URL` | github.com/benbaker76/OpenSWOS | the setup's About window |
| `PROJECT_SETUP_NAME`, `PROJECT_SETUP_PROGRAM` | OpenSWOS Setup, openswos-setup | what messages tell the player to run; its temp folder |
| `PROJECT_NETPLAY_TAG` | OPENSWOS | the matchmaking VERSION: tag (`<tag>/<NETPLAY_COMPAT>`, the web build's `<tag>-WEB/`); the server compares it too |
| `PROJECT_GGPO_GAME` | openswos | GGPO-X's session name |
| `PROJECT_TEAM_FORMAT` | openswos-team | a custom team file's `format` |
| `PROJECT_MM_HOST` | online.openswos.net | the public matchmaking server, recorder and team check |
| `PROJECT_USER_DIR`, `PROJECT_USER_DOT_DIR` | SWOS, .swos | the player's folder: saves, SWOS.ini, the assets (game and setup agree) |

**Online.** Two builds meet only when `PROJECT_NETPLAY_TAG`, `PROJECT_GGPO_GAME` and
`NETPLAY_COMPAT` (include/netplay_compat.h) agree. A fork that keeps OpenSWOS's tags plays
OpenSWOS players -- and then every change to how a match plays has to be in both, at the same
`NETPLAY_COMPAT`; a fork with its own tag is a community of its own. The matchmaking server
(third_party/matchmaker) reads the same header, so a fork's server answers to the fork's tag.

**Not in the header** (a fork replaces the files themselves): the icons (`SWOS.svg` and what
`scripts/generate_icons.py` makes from it), the credits text (`include/credits_text.h`), the
installer's ids (`packaging/windows/swos.iss`: give a fork its own `AppId`), the release
scripts' archive names, and the docs. Each of those is a file OpenSWOS rarely touches; a
fork's copy can still conflict when it does, and is resolved by keeping the fork's.

## Keeping the merge quiet

- **Docs.** Leave `.claude/CLAUDE.md` as upstream's; put the fork's own notes in a file of its
  own (SWOS United: `.claude/UNITED.md`) that CLAUDE.md does not need to mention -- a fork that
  edits CLAUDE.md conflicts on every upstream change to it.
- **Generated files** -- `src/world_data.c`, `src/menu_data.c`, `src/menu_symbols.c`,
  `include/swos2020_menus.h`, `third_party/matchmaker/Server/include/TeamRegistry.inc`,
  `tests/career_golden.txt`, `tests/netplay_compat.txt`: never merge them by hand. Take either
  side, rerun the script that writes them (world_import.py, emit_menus.py +
  gen_menu_symbols.py, import_swos2020_menus.py, assign_team_uuids.py --registry --worlds, the
  golden `--update`, check_netplay_compat.sh `--update`) and commit what it writes.
- **Submodules.** The fork uses OpenSWOS's (swosgfx, GGPO-X's `openswos` branch, AmigaScope).
  A fork that needs its own GGPO-X changes gives it a branch of its own, synced from
  `openswos` the same merge-only way. Private submodules of the fork's own (SWOS United's API)
  are added in the fork only.
