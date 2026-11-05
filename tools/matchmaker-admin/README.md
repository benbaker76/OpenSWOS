# matchmaker-admin

> Using it -- which buttons to press after which change: `docs/NETPLAY_ADMIN.md`. This
> README is how it works and how it is set up.

A tiny password-protected web page with ten buttons (`git pull`, rebuild, update the
recordings player, the four `swos-matchmaker-service.sh` actions, restarting the page itself,
and clearing the reports and the recordings) plus a live output pane, for maintaining the matchmaker server without
an SSH session.

## Restart admin page

How a change to this page goes live -- a new endpoint in `main.py`, or its unit's
`ReadWritePaths` -- without an ssh session. Queued for the root cron worker like
install/restart/remove, which runs `matchmaker-admin-service.sh install`: the unit is
rewritten and the gateway restarted. The page drops for a moment while it restarts; reload
it. (The very first time, the gateway running the older `main.py` has no such button, so
that one restart is by hand: `tools/matchmaker-admin/matchmaker-admin-service.sh install`.)

## Deleting one recording

`POST /delete` (`/cc/delete` through nginx), body `name=<recording>.SRP`: the playback page's
per-recording **delete** button (docs/PLAYBACK.md). Removes that `.SRP` and its card and
rewrites `index.json`. It needs the `X-SWOS-Admin: 1` header, which a cross-site form cannot
set, and only a name matching `<date>-<time>-<id>.SRP` -- nothing outside the recordings
folder. Same user, no root, like the Clear buttons.

## Update recordings player

Builds the player (`scripts/build_playback.sh`, needs emsdk) and copies it into
`/srv/swos-matchmaker/playback`, then brings the recordings' match cards and `index.json`
up to date -- `scripts/update_playback.sh`, as the gateway's own user. No root and no
restart, so a match in progress is untouched; reload `/playback/` to get it. The gateway's
unit needs write access to that folder: after pulling this, run
`tools/matchmaker-admin/matchmaker-admin-service.sh install` once so the new unit is
written. (nginx's `/playback/` itself is set up once by `swos-matchmaker-service.sh
install`.) "Rebuild" builds the player too, but only this button puts it live.

## Clearing reports and recordings

The matchmaker keeps match reports in `/srv/swos-matchmaker/reports` and recordings
in `/srv/swos-matchmaker/recordings`, which nginx publishes for
`scripts/fetch_reports.py` and `scripts/fetch_recordings.py` to download. Once they
are downloaded they can go:

- **From the fetch scripts** (the usual way, and their default since 2026-10-20; `--keep`
  skips it): after a fetch they send `POST /cc/clear` with
  exactly the files that run downloaded, each at the size it downloaded it; the
  gateway deletes only those, and only if they are still that size -- a report that
  arrived, or a recording that grew, since the fetch stays. Names are checked against
  `<match id>/player<N>.zip` and `<date>-<time>-<id>.SRP`; anything else is refused.
  The scripts take the page's login from `SWOS_ADMIN_USER` / `SWOS_ADMIN_PASSWORD`.
- **From the page**: "Clear all reports" / "Clear all recordings" delete everything
  in the folder (after a confirmation).

Both run as the gateway's own unprivileged user -- the matchmaker writes those files
as the same account -- so no root and no queue. The gateway's unit needs write access
to the two folders (`ReadWritePaths` in `matchmaker-admin-service.sh`): after pulling
this, run `tools/matchmaker-admin/matchmaker-admin-service.sh install` once, not just
`restart`, so the new unit is written.

## Two tiers, by whether the action actually needs root

`pull`, `build` and `status` need no elevated privilege at all - a plain git
pull, a plain build, and reading a systemd unit's status are all fine as the
same unprivileged user the gateway already runs as. Those three run
**immediately**, right inside the already-running gateway process (a
background thread), no queue, no cron, no delay.

`install`, `remove` and `restart` genuinely need root (`systemctl enable/
disable/start/stop --now`, and writing the unit file into `/etc/systemd`).
The gateway (`main.py`) must never have that, so for these three it only ever
writes a small, named, empty file into a queue directory - never runs
anything itself. A **root** cron job (`admin-cron-worker.sh`) is the only
thing that actually executes those, polling the queue once a minute. This way
a bug or a leaked password in the web part can, at worst, trigger one of six
fixed actions - never arbitrary code, and never with more privilege than the
action actually needs.

```
                                     pull / build / status (no root needed)
                                                     |
browser --(auth_basic, HTTPS)--> nginx --(127.0.0.1:8899)--> matchmaker-admin (unprivileged)
                                                                     |
                                                     install / remove / restart (needs root)
                                                                     |
                                       writes /srv/swos-matchmaker/admin/requests/<ts>-<action>.req
                                                                     |
                                                     root cron, once a minute -> admin-cron-worker.sh
                                                                     |
                                                              systemctl (as root)
```

Either way, output lands in `/srv/swos-matchmaker/admin/processed/<ts>-<action>.log`
and `latest.log` - the page polls `latest.log` regardless of which tier ran.

## One-time setup on the server

1. **Install the gateway service:**
   ```
   tools/matchmaker-admin/matchmaker-admin-service.sh install
   ```
   (installs+starts it as a systemd service, bound to
   127.0.0.1:8899 only - see that script for the unit file it writes.)

2. **Root crontab** (`sudo crontab -e`, as root):
   ```
   * * * * * /home/redhair/git/OpenSWOS/tools/matchmaker-admin/admin-cron-worker.sh >/dev/null 2>&1
   ```
   (the path of the checkout on that server: the scripts take their checkout from where
   they are, and the user they run as from who owns it -- online.openswos.net's is
   `/home/swos/OpenSWOS`, user `swos`.)

3. **nginx: password + reverse proxy.** Create the password file once:
   ```
   sudo apt-get install -y apache2-utils   # if htpasswd isn't already there
   sudo htpasswd -c /etc/nginx/.htpasswd-matchmaker-admin redhair
   ```
   Then add this location block to the `us.swos.eu` HTTPS server block
   (alongside the `/reports/` one, if that's already there):
   ```nginx
   location /cc/ {
       auth_basic "matchmaker admin";
       auth_basic_user_file /etc/nginx/.htpasswd-matchmaker-admin;

       proxy_pass http://127.0.0.1:8899/;
       proxy_set_header Host $host;
   }
   ```
   Test and reload: `sudo nginx -t && sudo systemctl reload nginx`.

Afterwards, `https://us.swos.eu/cc/` asks for the password and shows the
button page.

**A team login** (online.openswos.net, since 2026-10-26): the team watches the
recordings (`/recordings/`, `/playback/`) with their own logins, kept in
`/etc/nginx/.htpasswd-openswos-team`; the reports and this page stay on the admin's
file. The whole site is `packaging/server/nginx/online.openswos.net.conf`, which says
how to add (`htpasswd -B`) and remove (`htpasswd -D`) a member. nginx reads one file per
location, so the admin's line is in the team file too: a new admin password goes in both.

## Files

- `main.py` - the gateway (stdlib only, no dependencies to install - run
  directly with `python3 main.py`, or via `matchmaker-admin-service.sh`).
- `admin-cron-worker.sh` - the root worker for `install`/`remove`/`restart`
  only; also documents the request-file naming scheme (must match `main.py`'s
  own `QUEUED_ACTIONS`).
- `matchmaker-admin-service.sh` - installs/restarts/removes/status's the
  gateway's own systemd service (same `install|restart|remove|status`
  convention as `scripts/swos-matchmaker-service.sh`).
