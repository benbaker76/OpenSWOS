# Contributing to OpenSWOS

Thank you for wanting to help. OpenSWOS is a small project with one maintainer, so the
rules below exist to make sure what you send can actually be reviewed and merged.

## Reporting bugs

Open an issue at [github.com/benbaker76/OpenSWOS/issues](https://github.com/benbaker76/OpenSWOS/issues).
Say which version you have (the bottom line of the main menu), what you did and what
happened, and attach your `SWOS.log` ([where it is](INSTALL.md#if-something-goes-wrong)). A
screenshot, your save or a match recording (`LATEST.SRP`) helps too.

## Pull Requests

Keep pull requests small and focused. A bug fix or one self-contained change is easy to
review; a large code drop isn't, and may wait a long time or be closed unreviewed. If you
plan a big feature, discuss it first in an issue before writing the code. The same goes for
anything that changes how a match plays, what two games say to each other online, or the
save format: those touch every player, so agree on the design first. That way your
contribution is much more likely to be accepted.

What makes a pull request easy to accept:

- **One change.** A fix, or one feature. Not a fix plus a refactor plus a reformat. If you
  find a second problem on the way, send it separately.
- **A description that says why**, what you changed, and how you tested it -- which target,
  which test scripts, which menus or matches you played.
- **Code that reads like the code around it**: the same naming, comment style and idioms.
  No mass renames, reformatting or reorganising of code you did not otherwise need to touch.
- **Both targets build.** `src/` and `include/` are shared by the SDL build and the Amiga
  (m68k) build; a change that only compiles on one breaks the other
  ([docs/BUILDING.md](docs/BUILDING.md)).
- **The tests for what you touched pass.** The scripts in `scripts/` that cover your area --
  for example `career_golden_test.sh` for the season engine, competitions or saves,
  `replay_v2_test.sh` and `rollback_test.sh` for the match, `netplay_test.sh` and
  `relay_test.sh` for netplay. [docs/INDEX.md](docs/INDEX.md) says which doc covers which
  area, and each says how it is tested. Say in the PR which ones you ran.

## AI-assisted contributions

AI tools are welcome, but you are the author and you are responsible for every line. Before
you open a pull request:

- **Read and understand all of it.** You should be able to explain any line if asked. A
  change you cannot explain will not be merged.
- **Keep it small.** An AI can write a thousand lines in a minute; nobody can review them in
  one. Large generated pull requests will be closed without a detailed review -- split the
  work, and discuss it in an issue first.
- **Check it against the original.** OpenSWOS follows the original game closely, and code
  that "looks wrong" is often deliberate. Do not "fix" behaviour, remove a panic or add a
  guard without finding the real cause ([docs/DEVELOPERS.md](docs/DEVELOPERS.md)).
- **Test it yourself**, on a real build, and say how.
- **Say that you used AI** in the description, and for what.

## What never goes in

- **No data from the original game** -- graphics, sound, music, team files, the game's code
  or disk images. Players build their own copy with OpenSWOS Setup; nothing of the game is in
  this repository.
- **No data you do not have the right to share**, such as another project's squads or
  databases.
- **No secrets** -- passwords, keys, server credentials.

## License

OpenSWOS is GPL-3.0-or-later ([LICENSE](LICENSE)). By sending a pull request you agree that
your contribution is licensed the same way, and that you have the right to contribute it.
