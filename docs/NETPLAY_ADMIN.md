# NETPLAY_ADMIN.md — updating the server from the admin panel

`https://us.swos.eu/cc/` is a password-protected page of buttons that updates the matchmaking
server — the matchmaker, the relay and recorder, and the recordings player at `/playback/` —
without an ssh session. This is how the server is kept up to date day to day. How the panel
itself is built and set up the first time is `tools/matchmaker-admin/README.md`; the server
from the ground up (ports, certificates, nginx, the service) is [NETPLAY_SETUP.md](NETPLAY_SETUP.md).

## The buttons

| button | what it does | runs as | takes |
|---|---|---|---|
| **git pull** | `git pull --ff-only` in the server's checkout | the service account | seconds |
| **Rebuild (server + recordings player)** | `scripts/run_matchmaker.sh --rebuild --build-only --with-playback`: builds the matchmaker and the player (emsdk at `~/git/emsdk`). Builds only — nothing goes live | the service account | minutes |
| **Update recordings player** | `scripts/update_playback.sh`: builds the player, copies it to `/srv/swos-matchmaker/playback`, and refreshes the match cards (`scripts/recordings_index.py`). Live at once, no restart | the service account | minutes |
| **Refresh match list** | `scripts/recordings_index.py`: every recording's card (players' names, score) and the list's `index.json`, from the ledger and the match reports. No build, live at once | the service account | seconds |
| **Service: restart** | `swos-matchmaker-service.sh restart`: puts the built matchmaker live (and the built player, and refreshes the cards), then restarts it. **Ends any match being relayed or recorded** | root, queued | up to a minute to start |
| **Service: install** | `swos-matchmaker-service.sh install`: the above, plus the `/srv` folders, the systemd unit and nginx's `/playback/`. Use it when a change touches the unit or nginx | root, queued | up to a minute to start |
| **Service: status** | the service's `systemctl status` | the service account | seconds |
| **Service: remove** | stops and removes the service | root, queued | — |
| **Restart admin page** | `matchmaker-admin-service.sh install`: rewrites this page's own unit and restarts it. The page drops for a moment; reload it | root, queued | up to a minute to start |
| **Clear all reports / recordings** | deletes every match report or every recording (and its card). One recording at a time: **manage** on `/playback/` ([PLAYBACK.md](PLAYBACK.md)) | the service account | seconds |

"Queued" means the page drops a request file and a root cron job picks it up within a minute
— the page itself never has root. The output pane shows each action's log when it finishes.

## After a change: what to press

Always **git pull** first, then:

| what changed | press |
|---|---|
| the matchmaker, relay or recorder (`third_party/matchmaker/`, `include/record_proto.h`, `src/srp_writer.c`) | **Rebuild**, then **Service: restart** |
| the recordings player or its pages (`wasm/player.html`, `wasm/playback-index.html`, anything the web build compiles) | **Update recordings player** |
| the match cards (`scripts/recordings_index.py`) | **Update recordings player** (it reruns the cards) |
| this admin page (`tools/matchmaker-admin/main.py`, its unit) | **Restart admin page** |
| the service script, its unit, or nginx's `/playback/` (`scripts/swos-matchmaker-service.sh`) | **Service: install** |
| the game only (a new drop) | nothing — players download it ([TRIAGE.md](TRIAGE.md)) |

Not sure? **Rebuild**, **Service: restart**, **Update recordings player** covers everything
but the admin page itself. Restart when nobody is playing: a restart ends a match in progress
through the relay, and cuts off any recording in progress there.

## What the panel cannot do

These still need someone with ssh on the server (Redhair):

- **The first restart onto a new admin page version that adds a button.** The running page is
  the old `main.py` until it restarts, so a button added in the new version is not there yet —
  `tools/matchmaker-admin/matchmaker-admin-service.sh install` once, by hand. (That is how
  **Restart admin page** itself arrived, 2026-09-30.)
- **The root crontab, nginx's `/cc/` block and its password file** — one-time setup,
  `tools/matchmaker-admin/README.md`.
- **The TLS certificate's renewal hook** and anything else outside the repo
  ([NETPLAY_SETUP.md](NETPLAY_SETUP.md) §10).

## When something looks wrong

| symptom | why, and what to do |
|---|---|
| `/playback/` still shows the old page | **Update recordings player**, then reload the page (Ctrl+F5); **Rebuild** alone does not put it live |
| a **delete** on `/playback/` fails with "the server said 403/404" | the admin page is older than the delete endpoint: **Restart admin page** (or, the first time, the ssh restart above) |
| a card shows `?` for a player, or a recording shows only its id | **Refresh match list**. A match still open when the server restarted gets its ledger line at the restart itself; `Service: restart` now refreshes the cards after restarting too. Names come from the ledger and the match reports — one without a report in `reports/` stays `?` |
| Rebuild's log says the player was skipped ("no emsdk") | emsdk not found: the panel sets `EMSDK_DIR=~/git/emsdk`; `build_playback.sh` falls back to `~/emsdk`, `~/git/emsdk` and `emsdk` beside the checkout |
| a queued button does nothing for more than a minute | the root cron job is not running — ssh, `sudo crontab -l` ([README](../tools/matchmaker-admin/README.md), setup step 2) |
| after **Service: install**, the log ends with a **WARNING** about `/playback/` | nginx could not be set up by the script; the warning says what to add by hand ([PLAYBACK.md](PLAYBACK.md), "On the server") |

## Where things are on the server

The checkout is `/home/redhair/git/OpenSWOS`; what runs is in `/srv/swos-matchmaker/`:
`bin/` (the matchmaker), `recordings/` (with the cards and `index.json`), `reports/`,
`playback/` (the player), `matches.jsonl` (the ledger), `admin/` (this page's queue and logs).
The full list of paths is in [INDEX.md](INDEX.md), "Paths".
