# NETPLAY_SETUP.md — running a server and playing online

> Day-to-day updates of a running server go through the admin panel (`/cc/`):
> [NETPLAY_ADMIN.md](NETPLAY_ADMIN.md). This runbook is setting one up, and what the panel cannot do.

How to deploy the matchmaking/relay server, point games at it, and get two players into
a match — natively and in a web browser. This is the operator's runbook.
**docs/NETPLAY.md is the design**: why each piece works the way it does, and what every
test proves. Where this file says *why* in one line, NETPLAY.md has the section.

## 1. What talks to what

The server is SimpleMatchmaker — the same project SWOS 2020 ships — vendored at
`third_party/matchmaker`, with OpenSWOS's additions recorded in its `OPENSWOS.md`.
It does up to five jobs, each switched on by a flag:

| job | flag | what it does |
|---|---|---|
| **introduce** | *(always)* | players log in, see each other, host and join. At `START:` it hands each side the other's addresses and **drops out** — the match is peer to peer |
| **relay** | `--relay` | plays the match *through* the server instead. For players whose NATs will not punch, and for testing |
| **browser gate** | `--websocket` | lets the web build connect. A browser has no UDP, so every browser match is relayed, `--relay` or not. With `--tls-cert`/`--tls-key` it also takes `wss://`, which a page served over `https://` needs (§10) |
| **recorder** | `--record` | players stream their matches to it; it compares both sides frame by frame, names the first desync, and keeps the match as a `.SRP` |
| **report desk** | `--reports DIR` | after an online match each player's game sends one zip — both players' `SWOS.log` and their HOW WAS THE MATCH? answers — filed by match id (§12, docs/NETPLAY_REPORT.md) |

```
  native player A ──UDP 19602──┐                     ┌──UDP 19602── native player B
                               ├── swos-matchmaker ──┤
  browser player ──TCP 19602───┘  (introduce/relay)  └── ...
     (WebSocket)

  introduce only : A ◄──────────── peer to peer ────────────► B   (server not involved)
  --relay        : A ◄──► server ◄──► B                          (every packet via server)
```

**Killing the server does not end a peer-to-peer match** — it only stops new ones being
made. A relayed match does end with it.

## 2. Ports

Everything is on **one port number, 19602**, split by protocol:

| port | protocol | needed when | carries |
|---|---|---|---|
| 19602 | **UDP** | **always** | matchmaking (ENet), the relay, relayed native matches |
| 19602 | **TCP** | only with `--websocket`, `--record` or `--reports` | browsers' WebSockets (`ws://` and `wss://`), players' recorder streams and match reports |

The relay shares UDP 19602 with the matchmaker through ENet's `intercept` hook, so relaying
needs no second port. The TCP side is sniffed per connection: one opening with `GET ` is a
browser's WebSocket, one opening with a TLS handshake (`0x16 0x03`) is a browser's `wss://`,
one opening with `SWMR` is a match report, one opening with `SWRC` is a recorder stream.

**TCP and UDP cannot be given different numbers.** The server takes one `port` argument and
binds both to it, because the web build sends its "datagrams" (WebSocket messages) to the
same port number a native build would send UDP to. That is also why `wss://` is served by
the server itself on the same port rather than by a proxy in front of it — §10.

## 3. Build the server

```bash
scripts/run_matchmaker.sh --build-only      # -> build/matchmaker/swos-matchmaker
scripts/run_matchmaker.sh --rebuild         # wipe build/matchmaker and reconfigure first
```

Needs `cmake`, a C++ compiler and **OpenSSL's headers** for `wss://` (§10):

```bash
sudo apt install cmake g++ libssl-dev          # Debian / Ubuntu
sudo dnf install cmake gcc-c++ openssl-devel   # Fedora / RHEL
```

ENet is vendored (`third_party/enet`), shared with the client. OpenSSL comes from the
system on purpose: the server faces the internet, and a TLS fix then arrives with the
distro's own updates. Configure prints which it got:

```
-- swos-matchmaker: wss:// with OpenSSL 3.0.2
```

Without `libssl-dev` it still builds, warns, and serves `ws://` only. **Installing it later
needs `--rebuild`**, since CMake remembers that it was missing. It builds and runs on Linux,
including WSL2.

`run_matchmaker.sh` without `--build-only` builds if needed and runs the server on UDP
19602 — fine for local testing. Anything after `--` goes to the server
(`scripts/run_matchmaker.sh -- --relay --websocket`). A public server runs under systemd
with its full argument list (§4).

## 4. Run it

The configuration for a public server that serves everyone, browsers on `https://` pages
included:

```bash
cd /srv/swos-matchmaker        # the working directory matters: see --recordings
build/matchmaker/swos-matchmaker 19602 \
    --websocket \
    --tls-cert /srv/swos-matchmaker/tls/fullchain.pem \
    --tls-key  /srv/swos-matchmaker/tls/privkey.pem \
    --record \
    --recordings /srv/swos-matchmaker/recordings \
    --reports /srv/swos-matchmaker/reports \
    --ledger /srv/swos-matchmaker/matches.jsonl
```

That is every option a production server uses. Left out on purpose: `--relay` (below) and
`--netem`/`--netem-seed` (testing). `swos-matchmaker --help` prints the list.

**Native players play peer to peer; only browser games go through the server.** That is
what the command above does: `--websocket` relays any match with a browser in it (a
browser has no other way to reach anyone), and every native pair connects directly, as in
SWOS 2020. Do NOT add `--relay` to a public server: it sends every native match through
the server too, so both players' packets make the round trip to wherever it is hosted.
On us.swos.eu (Virginia) that put two European testers' ping -- and so the input delay
the game picks from it -- far above what the same two get directly (2026-09-25).

| flag | use it for |
|---|---|
| `19602` | the port, UDP and TCP. First bare argument; default 19602 |
| `--relay` | relay EVERY OpenSWOS pair, natives too. **Not for a public server** (above): for testing, or a private server for players whose NATs will not punch. Without it only browser matches are relayed |
| `--websocket` | accept browsers. Opens TCP on the port |
| `--tls-cert FILE` | with `--tls-key`: also accept `wss://` on the same port, for a web build served over `https://`. The certificate chain, PEM — certbot's `fullchain.pem` (§10). Needs `--websocket` |
| `--tls-key FILE` | the certificate's private key, PEM — certbot's `privkey.pem`. Both files are re-read within a minute of changing, so a renewal needs no restart |
| `--record` | accept match streams from players who set `[netplay] record = server`. Opens TCP on the port |
| `--reports DIR` | keep the match reports players send after each online match: `DIR/<match id>/player1.zip` and `player2.zip`, each holding both players' `SWOS.log` and ratings, and a `"report"` ledger line each. Opens TCP on the port. A report is a few KB; nothing prunes them (§12) |
| `--ledger FILE` | append one JSON line per relay session, recorded match, desync and match report |
| `--recordings DIR` | where recorded `.SRP`s go. **Default `recordings/`, relative to the working directory** |
| `--netem P` | **testing only.** Puts a misbehaving network (`wifi`, `awful`, `cycle`...) in the middle of every relayed match. Never on a server real players use |
| `--netem-seed N` | **testing only.** The seed for `--netem`, to replay one run |

Without `--relay` a native match costs the server nothing, adds no latency, and survives
the server restarting. The price: two players whose NATs will not punch (some home
routers, most mobile connections) cannot play each other -- there is no automatic fallback
to the relay yet -- and see THE HOST DID NOT ANSWER (§11).

### Run it as a service (systemd)

This is how the public server runs. Once, as root:

```bash
# 1. A user of its own: it needs no privileges (19602 is not a privileged port).
sudo useradd --system --home-dir /srv/swos-matchmaker --shell /usr/sbin/nologin swos

# 2. The checkout, built (§3). Its directory is also where the ledger and recordings go.
sudo git clone https://github.com/benbaker76/OpenSWOS.git /srv/swos-matchmaker
cd /srv/swos-matchmaker && sudo scripts/run_matchmaker.sh --build-only
sudo mkdir -p /srv/swos-matchmaker/recordings /srv/swos-matchmaker/reports /srv/swos-matchmaker/tls
sudo chown -R swos:swos /srv/swos-matchmaker

# 3. The certificate, copied where the swos user can read it: §10.
```

**Or let the script do it.** `scripts/swos-matchmaker-service.sh {install|restart|remove|status}`
is what us.swos.eu actually uses: it builds nothing itself, but copies the binary built in a
checkout (`REPO_DIR`, `RUN_USER` at the top of the script -- edit them for your machine) to
`/srv/swos-matchmaker/bin/swos-matchmaker`, creates the `/srv/swos-matchmaker` folders and
ledger, writes the unit below (with `ExecStart` pointing at that copy), copies the recordings
player and sets up nginx for it (§12), and restarts the service. The password-protected admin
page (`tools/matchmaker-admin/README.md`) runs pull, build, install and restart from a browser.
The manual steps below are the same thing by hand, with the checkout itself in
`/srv/swos-matchmaker`.

The unit — every argument spelled out, one per line, so a change is one line:

```ini
# /etc/systemd/system/swos-matchmaker.service
[Unit]
Description=OpenSWOS matchmaking / relay server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=swos
Group=swos
WorkingDirectory=/srv/swos-matchmaker
ExecStart=/srv/swos-matchmaker/build/matchmaker/swos-matchmaker 19602 \
    --websocket \
    --tls-cert /srv/swos-matchmaker/tls/fullchain.pem \
    --tls-key /srv/swos-matchmaker/tls/privkey.pem \
    --record \
    --recordings /srv/swos-matchmaker/recordings \
    --reports /srv/swos-matchmaker/reports \
    --ledger /srv/swos-matchmaker/matches.jsonl
Restart=always
RestartSec=2
# SIGTERM is a clean stop: relay sessions close and write their ledger lines.
KillSignal=SIGTERM
TimeoutStopSec=10

# Hardening. It reads its certificate and writes only the ledger, recordings and reports.
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/srv/swos-matchmaker/recordings /srv/swos-matchmaker/reports /srv/swos-matchmaker/matches.jsonl

[Install]
WantedBy=multi-user.target
```

`ReadWritePaths` names a file, so create the ledger before the first start
(`sudo -u swos touch /srv/swos-matchmaker/matches.jsonl`) — under `ProtectSystem=strict`
the server may not create it. Leave out lines you do not want: without the two `--tls-*`
lines it serves `ws://` only; `--relay` would put native matches through the server too
(don't, above); without
`--record`/`--recordings` there is no recorder; without `--reports` players' match reports
are turned away (their games log it and carry on); without `--ledger` no ledger.

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now swos-matchmaker
systemctl status swos-matchmaker
journalctl -u swos-matchmaker -f       # every [relay] line is timed to the millisecond
```

Its first lines say what it is running — check them after every change to the unit (§6):

```
[relay] on for browsers: a match with one in it plays through this server
[record] on: players stream their matches to TCP 19602 ...
[web] on: browsers connect over WebSocket on TCP 19602 and play through the relay
[web] wss:// on the same port, certificate /srv/swos-matchmaker/tls/fullchain.pem
SimpleMatchmaking Server running on Port 19602
```

**Updating it** to a new commit:

```bash
cd /srv/swos-matchmaker
sudo -u swos git pull --ff-only
sudo -u swos scripts/run_matchmaker.sh --rebuild --build-only
sudo systemctl restart swos-matchmaker
```

The server keeps no state worth losing: it introduces players and forgets them, so a
restart interrupts only relayed matches and matches being introduced at that moment. A
certificate renewal needs **no** restart (§10). For a quick session without systemd,
`tmux new -s mm '<the command at the top of §4>'`.

## 5. Open the ports

A player has to reach the server's UDP 19602 (and TCP 19602 for browsers and recording)
from the internet. There are up to three layers, and **each one has to be opened**.

**The machine's own firewall.**

```bash
# Linux, ufw
sudo ufw allow 19602/udp
sudo ufw allow 19602/tcp                        # only for --websocket / --record

# Linux, firewalld
sudo firewall-cmd --permanent --add-port=19602/udp
sudo firewall-cmd --permanent --add-port=19602/tcp
sudo firewall-cmd --reload
```

**Windows running the server in WSL2.** With WSL2 in **mirrored** networking mode
(`networkingMode=mirrored` in `%USERPROFILE%\.wslconfig`) a WSL listener shares the Windows
host's addresses — no `netsh portproxy` (which is TCP-only and could not carry UDP anyway).
Mirrored mode sits behind the **Hyper-V firewall**, so the rules go there, from an elevated
PowerShell, once:

```powershell
New-NetFirewallHyperVRule -Name SWOS-MM -DisplayName "SWOS matchmaker (UDP)" `
    -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' `
    -Protocol UDP -LocalPorts 19602 -Action Allow

# Only for --websocket / --record. docs/NETPLAY.md's rule is UDP-only; browsers need this too.
New-NetFirewallHyperVRule -Name SWOS-MM-TCP -DisplayName "SWOS matchmaker (TCP)" `
    -Direction Inbound -VMCreatorId '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' `
    -Protocol TCP -LocalPorts 19602 -Action Allow
```

**The router**, for a server at home: forward **UDP 19602** — and **TCP 19602** for
browsers and recording — to the server machine's LAN address.

**A cloud VPS**: the provider's security group / network firewall, same two rules.

### The address to hand out

Players need the server's **public** address or a DNS name for it:

- A VPS has a fixed public IP; give it a DNS name if you can (and you need one for §10).
- A home connection has a public IP that can change. Use a **dynamic-DNS** name pointed at
  it, or hand out the current IP and expect to update it.
- **`baker76.com` is the wrong name.** It resolves to a GoDaddy shared host, not to whatever machine runs the server.

## 6. Prove it before handing it out

From **another machine**, against the **public** address:

```bash
SWOS_MM_HOST=<public address> bin/SWOS.x86_64 --matchmaker-selftest
```

That is a real protocol handshake — it logs in, lists the lobby and leaves — not a ping.
Two things that look like failures and are not:

- **Test the public address even from inside the same house.** Measured on this setup: a
  machine on the same LAN reached the server through the public address and the port
  forward, but **not** through the server's LAN address — same host, same port, same rule.
  So a failure against the LAN address does not mean it is broken, and a success against it
  would not have proved the forward. The public address is what a player uses.
- **There is nothing TCP to test on the UDP side.** A TCP tool reporting "connection
  refused" against UDP 19602 means nothing. (TCP 19602 is only there with `--websocket` or
  `--record`, and speaks WebSocket or `SWRC`, not anything a port scanner understands.)

**The same check from the game**, which is what a player will see: on another machine,
**MULTIPLAYER**, type the public address in **SERVER**, **LOGIN**. The matchmaking screen
should go from **CONNECTING TO SERVER** to showing who is online. **SERVER NOT CONNECTED**
means it was not reached.

Then the full rehearsal — one machine hosts, another joins, both through the public address:

```bash
SWOS_MM_HOST=<public> bin/SWOS.x86_64 --matchmaker-pair=host    # machine 1
SWOS_MM_HOST=<public> bin/SWOS.x86_64 --matchmaker-pair=join    # machine 2
```

Each side should end with `START:`, the right player number, the other's name, and a list
of candidate addresses — then one line saying whether the server will play the match
itself: `RELAYED through <server>, token <n>` (the **same** token on both sides) if it runs
`--relay`, or `not relayed` if not. That is the only way to confirm `--relay` from outside.

The TCP side can be proved from outside too, if the server runs `--websocket`: a real
WebSocket handshake must come back `101 Switching Protocols`. ("Connection refused" means
nothing is listening on TCP — the flag is off, or the firewall has not been opened.)

```bash
curl -si --max-time 5 --http1.1 -H "Connection: Upgrade" -H "Upgrade: websocket" \
     -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
     http://<address>:19602/ | head -1
```

With `--tls-cert`, the same request over **`https://`** proves `wss://` — and, because curl
checks the certificate as a browser does, that the certificate is right for the name.
Use the **name**, not the IP: a certificate is for a name.

```bash
curl -si --max-time 5 --http1.1 -H "Connection: Upgrade" -H "Upgrade: websocket" \
     -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" \
     https://your.server.name:19602/ | head -1                 # HTTP/1.1 101 ...
echo | openssl s_client -connect your.server.name:19602 -servername your.server.name 2>/dev/null |
     openssl x509 -noout -subject -issuer -enddate            # which certificate, and until when
```

A curl error such as `SSL certificate problem` is exactly what a browser will refuse; §10
has the causes.

`--record` shares that TCP port but not the WebSocket protocol, so confirm it from the
server's own startup lines, which list what it is running before `running on Port`:

```
[relay] on for browsers: a match with one in it plays through this server
[record] on: players stream their matches to TCP 19602 ...
[web] on: browsers connect over WebSocket on TCP 19602 ...
[web] wss:// on the same port, certificate ...
```

A server started with no flags prints none of them — which is what `run_matchmaker.sh`
does unless it is given them after `--` (§3). Two machines behind the **same** router still reach the server by
hairpin, so this proves the forward and the server — not yet that the internet side accepts
traffic. The first real outside player proves that.

## 7. Point the games at the server

**Out of the box a game uses the public server, `us.swos.eu`** (`MM_DEFAULT_HOST`,
`include/mm_client.h`), so a player on it has nothing to set: MULTIPLAYER, LOGIN. To use
another server — your own — players do this in the game, once:

1. Main menu → **MULTIPLAYER**. This opens the **PLAY ONLINE** screen.
2. **NAME** — the name other players will see.
3. **SERVER** — type the server's address: `your.server.name`, or `your.server.name:port`
   if it is not on 19602. Until something is typed it shows the server LOGIN will use:
   **US.SWOS.EU**.
4. **LOGIN**.

The SERVER row is **saved to `SWOS.ini` as soon as it is typed**, so a player enters it once
and never again — the next time they open MULTIPLAYER it is already filled in. Editing it
starts from the address already in force, so changing one digit is one digit.

That is the only thing a player has to be told: *"MULTIPLAYER, then type
`your.server.name` in the SERVER row."*

After LOGIN the matchmaking screen shows how the connection is going — **CONNECTING TO
SERVER**, then the players and games online, or **SERVER NOT CONNECTED** if it cannot reach
it (§11).

**Recording matches** is in the menu too. After LOGIN, open **SETTINGS** on the matchmaking
screen: **MULTIPLAYER SETTINGS → RECORD MATCHES**, OFF or ON — **ON by default** since
2026-09-30 (an empty `record=` in `SWOS.ini` means ON; OFF writes `off`). ON streams every
match the matchmaker arranges to the server's recorder (§4's `--record`), which keeps it and compares both players frame by frame
to catch a desync. Like the SERVER row it is saved as soon as it is changed, and it applies
from the very next match — no restart. What it keeps and how to use it: §12.

ON means the server the player logged in to, and only for the matches it arranged: a
direct-IP match (`--menu=p2p/p2pMenu`) or a test's loopback pair is never streamed to it
(`MatchRecord_SetArrangedByServer`). The browser build does not record yet. A player who names
a recorder somewhere else in `SWOS.ini` (`[netplay] record = host[:port]`) records every match
there, and sees that address in the row instead of ON,
and switching the row off and back on restores it rather than losing it.

The other rows on MULTIPLAYER SETTINGS — SOUND FX, COMMENTARY, the chat options and the
buttons under them — are SWOS 2020's and do nothing in OpenSWOS yet.

> **For operators and scripts only** — never something to ask a player to do. The SERVER
> row *is* `[netplay] matchmaker` in `SWOS.ini`: typing one writes the other. The only thing
> that overrides it is the environment, `SWOS_MM_HOST` / `SWOS_MM_PORT`, which the repo's
> tests use. **That override is invisible in the menu**: the SERVER row still shows the
> `SWOS.ini` address while LOGIN connects to the environment's. So a player who "typed the
> right server and still can't connect" may simply have one of those left set.

## 8. Two players, one match

1. Both players open **MULTIPLAYER**, set the same **SERVER** (§7) and **LOGIN**.
2. One **hosts** a game. It appears in the other player's lobby.
3. The other **joins**. The host is asked to **accept**.
4. The server sends `START:`. With `--relay` the players both connect through the server;
   otherwise they punch straight to each other.
5. The two games **handshake**, agree an input delay from the ping they both measure, and
   go to the pre-match lobby. From there it is a normal match, and after it a rematch goes
   back through the same lobby without logging in again.

**The handshake refuses to start a match the two games would play differently** — and
this is the most common reason a match will not start. Both players need:

- **a compatible build** — the same netplay number (`NETPLAY_COMPAT`, since 2026-10-25;
  before that the same commit): a different one is refused with THE OTHER PLAYER HAS
  ANOTHER VERSION, and `SWOS.log` names both builds and numbers. The server only lists games
  of the same number. The version is on the main menu's bottom line, the window title and
  `--version`;
- **the same rules** — since 2026-10-11 every build plays the real rules online (no
  injuries / no cards is an offline setting, `[testing] no_injuries_cards`). An OLDER tester
  build ("test build: no injuries or cards" on its version line) cannot play a newer one;
- **the same arcade mode and fire-hold lockout** (`[gameplay] fire_hold_lockout`).

Not needed any more: **the same gameplay style** — since 2026-09-29 a game plays its
HOST's style (AMIGA or DOS, picked when the host OPENs it) and the joiner borrows it until
they leave, without touching their `SWOS.ini`; the view goes with the style. And **not the
same screen width** — the two agree one simulation width (docs/NETPLAY.md §5, "One
simulation width"). Only a view pinned with `--view` on the command line can still be
refused (ONE OF YOU PINNED THE VIEW).

What each refusal means is in §11.

## 9. Playing in a web browser

The web build can play online — against another browser, or against a native Linux,
Windows or macOS player. It is proven bit for bit: the same match ends on the same state
hash in a browser as natively.

**1. Build the web build.** Netplay is on by default for WASM. It bakes in the game's data
from the assets folder (`$SWOS_ASSETS`, default `~/.swos/assets`), which the data setup
makes from a copy of the game -- see "The recordings player" in §12 for the command:

```bash
make WASM=1                                             # -> dist/web/SWOS.html (+ .js .wasm .data)
```

Match the native builds' rules, or the handshake refuses them (§8).

**2. Run the server with `--websocket`.** A browser can only play through the server, and a
server with `--websocket` relays any match with a browser in it, whether or not it has
`--relay`. Open **TCP** 19602 as well as UDP (§5).

**3. Serve the web build — `http://` or `https://`.** The page decides how the browser
reaches the server (`wasm/pre.js`): from an `http://` page it opens `ws://`, and from an
**`https://` page it can only open `wss://`**, which needs the server's certificate (§10).

```bash
cd dist/web && python3 -m http.server 8080      # then http://<host>:8080/SWOS.html
```

Any static web server will do; it does not have to be the machine the matchmaker runs on,
because the browser dials the matchmaker directly.

**4. Point it at the server** — exactly as natively: **MULTIPLAYER**, type the server in
the **SERVER** row, **LOGIN** (§7). The browser keeps it in its own storage, so it is typed
once per browser.

**5. Keep the tab in front.** A browser throttles a background tab's timers to about once a
second, and a live match does not survive that.

**A public web build** (itch.io's HTML5 player included) is served over `https://`, so it reaches the server
with `wss://`, which us.swos.eu serves with its certificate (§10). The web build has
netplay ON by default since that went in (`make WASM=1`; `NETPLAY=0` for one without).

## 10. Certificates and `wss://`

A page served over `https://` may only open `wss://` (TLS) WebSockets — browsers block
`ws://` from it as mixed content. So for the public web build the server needs a
**certificate for the name players connect to** (`us.swos.eu`). A certificate is issued
for a name, not an IP, so the server needs a DNS name. Let's Encrypt's are free, and
`certbot` renews them by itself.

**TLS is done by the server, on the same port** (`--tls-cert`/`--tls-key`,
`third_party/matchmaker/Server/include/WebGate.h`). A connection that opens with a TLS
handshake is a `wss://` browser; after the handshake it is exactly a plain WebSocket. One
port, one process, `ws://` still works beside it, and the server still sees each browser's
own address. A TLS proxy in front (nginx, Caddy) is **not** the way: the browser dials
`wss://<server>:19602` — the port each datagram is for — which the server already owns,
and behind a proxy every browser would arrive from the proxy's address.

### Getting the certificate (Let's Encrypt)

On the server, as root. The name must already resolve to this machine
(`dig +short us.swos.eu` prints its public IP).

**1. Install certbot.**

```bash
sudo apt install certbot           # Debian / Ubuntu  (Fedora: sudo dnf install certbot)
```

**2. Get the certificate.** Let's Encrypt proves you control the name by connecting to it on
**TCP port 80** — not 19602 — so port 80 must be open to the internet (§5's firewall, router
or security group, for TCP 80) for the moment it runs:

```bash
sudo certbot certonly --standalone -d us.swos.eu \
     --agree-tos -m you@example.com --no-eff-email
```

`--standalone` runs its own brief web server on port 80, so it only works when nothing
else is on 80.

**If nginx already runs on port 80** (us.swos.eu's case), let certbot go through it:

```bash
sudo apt install certbot python3-certbot-nginx
# nginx needs a server block for the name (server_name us.swos.eu;) -- the default one will do
sudo certbot certonly --nginx -d us.swos.eu --agree-tos -m you@example.com --no-eff-email
```

`certonly` gets the certificate without touching nginx's configuration. Plain
`certbot --nginx -d us.swos.eu` also works -- it additionally switches nginx's own site to
https, which is harmless here: nginx serves web pages on 80/443, the matchmaker does its
own TLS on 19602, and the two never meet. Either way renewals go through nginx too, with
nothing stopped. (Apache: `--apache`; any other web server: `--webroot -w <its root>`.)

No port 80 at all? Prove it with DNS instead: `--manual --preferred-challenges dns`, which
asks you to add a TXT record (renewal then needs your DNS provider's certbot plugin).

The result is in `/etc/letsencrypt/live/us.swos.eu/`: `fullchain.pem` (the certificate and
its chain — use this, **not** `cert.pem`, or some browsers cannot build the chain) and
`privkey.pem`.

**3. Let the server read it.** Which way depends on the user the service runs as — the
`User=` line in its unit (§4):

- **As root** (no `User=` line): skip this step and point the server straight at
  certbot's files, `--tls-cert /etc/letsencrypt/live/us.swos.eu/fullchain.pem --tls-key
  /etc/letsencrypt/live/us.swos.eu/privkey.pem`. Those names are symlinks certbot
  repoints on each renewal, and the server follows them.
- **As its own user** (the recommended unit, and us.swos.eu): `/etc/letsencrypt` is
  readable by root only, so a certbot **deploy hook** copies the pair into a folder that
  user owns, every time the certificate is issued or renewed. Replace `swos` with the
  unit's `User=`, and `/srv/swos-matchmaker` with the server's checkout:

```bash
sudo tee /etc/letsencrypt/renewal-hooks/deploy/swos-matchmaker.sh >/dev/null <<'EOF'
#!/bin/sh
# Copy the renewed certificate where swos-matchmaker (user swos) reads it. The server
# notices the new files within a minute: no restart.
set -e
[ "$RENEWED_LINEAGE" = /etc/letsencrypt/live/us.swos.eu ] || exit 0
install -o swos -g swos -m 0644 "$RENEWED_LINEAGE/fullchain.pem" /srv/swos-matchmaker/tls/fullchain.pem
install -o swos -g swos -m 0600 "$RENEWED_LINEAGE/privkey.pem"   /srv/swos-matchmaker/tls/privkey.pem
EOF
sudo chmod 755 /etc/letsencrypt/renewal-hooks/deploy/swos-matchmaker.sh

# certbot runs it on RENEWAL; run it once now for the certificate just issued:
sudo RENEWED_LINEAGE=/etc/letsencrypt/live/us.swos.eu \
     /etc/letsencrypt/renewal-hooks/deploy/swos-matchmaker.sh
```

**4. Start (or restart) the server with the two `--tls-*` lines** (the unit in §4):

```bash
sudo systemctl daemon-reload && sudo systemctl restart swos-matchmaker
journalctl -u swos-matchmaker -b | head -20
```

It should print:

```
[web] wss:// on the same port, certificate /srv/swos-matchmaker/tls/fullchain.pem
```

and not `[web] TLS: ...` (§11). Then prove it from another machine: §6's `https://` curl.

**5. Renewal is automatic.** The certbot package installs a timer that renews a
certificate when it has under 30 days left (they last 90). The hook copies the new pair,
and **the server re-reads the files within a minute** of them changing — connections
already open carry on with the old one, new ones get the new one. Nothing to restart.

```bash
systemctl list-timers | grep certbot          # the renewal timer is there
sudo certbot renew --dry-run                  # renewal would succeed (port 80 again)
```

Renewal needs port 80 again each time, so **leave it open** (only certbot listens on it,
and only for a few seconds every couple of months), or use the DNS route above. A renewal
that fails leaves the old certificate serving until it expires — `certbot` emails the
address given in step 2 well before that.

### Without Let's Encrypt

Any certificate and key in PEM work. From another authority, `--tls-cert` takes the
certificate **followed by its intermediate(s)** in one file (what certbot calls
`fullchain.pem`). For a LAN party or testing, a **self-signed** one:

```bash
openssl req -x509 -newkey rsa:2048 -nodes -days 365 -subj "/CN=my.server.name" \
    -addext "subjectAltName=DNS:my.server.name" \
    -keyout tls/privkey.pem -out tls/fullchain.pem
```

A browser refuses a self-signed certificate for a WebSocket **without asking**: each player
first opens `https://my.server.name:19602/` in the same browser and accepts the warning,
and then the game's `wss://` is let through. (That page itself shows an error: it is a
WebSocket port, not a web site.) `TLS=1 scripts/wasm_netplay_test.sh` uses one of these,
accepted for that key only (§13).

## 11. Troubleshooting

| what the player sees | what it means | fix |
|---|---|---|
| **SERVER NOT CONNECTED** (matchmaking screen) | the server was not reached | check the **SERVER** row (§7); the server's firewall and router forward (§5) |
| stuck on **CONNECTING TO SERVER** | nothing answering at that address | wrong address or port in **SERVER**, or UDP 19602 blocked (§5) |
| **THE HOST DID NOT ANSWER** / CHECK THE ADDRESS, PORT AND FIREWALL | the other player was never reached | usually NATs that will not punch -- one player forwarding UDP 19602 on their router often fixes it; a private server with `--relay` always does, at the cost of latency (§4). Otherwise the firewall and forward (§5) |
| **THE OTHER PLAYER HAS ANOTHER VERSION** / ONE OF YOU NEEDS TO UPDATE | different netplay numbers (`SWOS.log` names them), or a build with uncommitted changes | both download the latest build |
| **YOU PLAY DIFFERENT WORLDS** / PICK THE SAME WORLD IN SWOS.INI | `[general] world` differs (the server lists only same-world games, so this is a direct-IP match) | both set the same `world` |
| **YOUR BUILDS PLAY BY DIFFERENT RULES** / ONE OF YOU HAS A TEST BUILD | one build is an old tester build (before 2026-10-11) | both update (§8) |
| **YOU PLAY BY DIFFERENT RULES** / CHECK FIRE_HOLD_LOCKOUT IN SWOS.INI | one has the original's fire-hold lockout on | `[gameplay] fire_hold_lockout`, the same on both. (AMIGA v DOS is NOT refused: the joiner plays the host's style) |
| **ONE OF YOU PINNED THE VIEW** / RUN WITHOUT --VIEW | the same style, but one side started with `--view=` pinning the other view | run without `--view` (SWOS.ini has no view setting any more: `[gameplay] style` sets it) |
| **YOUR SCREEN WIDTHS ARE DIFFERENT** / BOTH OF YOU NEED THE SAME ONE | from an old build only: widths are no longer refused (§8) | both update to the same build |
| **THE HOST ALREADY HAS A PLAYER** | someone else joined first | host another game |
| **THE SERVER'S RELAY HAS CLOSED** / FIND EACH OTHER AGAIN | a relay session closes after about a minute with no traffic | go back to the lobby and join again |
| **THIS SERVER CANNOT RELAY A BROWSER GAME** | a browser is in the match and the server lacks `--websocket` | restart the server with `--websocket` (§4) |
| **THE CONNECTION WENT QUIET** | the link stopped. In a browser, most often the tab went to the background | keep the tab in front (§9); otherwise the network dropped |
| two natives never connect, but the server is fine | their NATs will not punch | as above: a port forward on one side, or a private `--relay` server (§4) |
| a browser on an `https://` page cannot connect at all | the server has no `wss://`: no `--tls-cert`/`--tls-key`, or they did not load | §10; the server's first lines say which |
| server log: `[web] TLS: cannot use the certificate ...` / `the key ... does not match` | the files are missing, unreadable by the `swos` user, or not a pair | rerun the deploy hook (§10 step 3); `fullchain.pem` and `privkey.pem` from the same issue |
| server log: `[web] wss:// OFF: this server was built without OpenSSL` | built before `libssl-dev` was installed | install it and `scripts/run_matchmaker.sh --rebuild --build-only` (§3) |
| server log: `TLS handshake failed: ... alert unknown ca` / `bad certificate` | a browser did not trust the certificate | wrong name, `cert.pem` instead of `fullchain.pem`, expired, or self-signed (§10). Scanners cause these too: one now and then is noise |
| selftest fails against the LAN address, works against the public one | expected — see §6 | use the public address |
| a TCP tool says "connection refused" on 19602 | meaningless — the service is UDP | use `--matchmaker-selftest` (§6) |

On the server, the console (or `journalctl`) shows every login, game, `START:` and relay
session, each `[relay]` line timed to the millisecond. With `--ledger`, each relay session,
recorded match and **desync** is a JSON line: a desync line names the exact frame, both
builds and both platforms. A recorded match can be played again anywhere with
`bin/SWOS.x86_64 --srp-resim=recordings/<match>.SRP`, which names the first frame that parts.

## 12. Recording matches, and finding out why a match felt laggy

Two different questions, answered by two different things:

| the question | where the answer is |
|---|---|
| **Did the two games go out of step** (a desync: a goal on one screen and not the other)? At which frame, and can we replay it? | the **recorder** -- this section's first half |
| **Why did it feel laggy** -- delay, stutter, freezing? | each player's **`SWOS.log`** -- the second half. A recording does not measure lag |

### Recording

**On the server:** `--record`, with `--recordings DIR` and `--ledger FILE` (§4). It takes
TCP on the same port as everything else.

**On each player's game:** nothing, by default: **RECORD MATCHES** (MULTIPLAYER, LOGIN,
SETTINGS → MULTIPLAYER SETTINGS) is ON unless a player turned it off (§7). That is
`[netplay] record = server` in `SWOS.ini`, or an empty `record=`; a recorder somewhere else is
`host` or `host:port`, and `off` turns it off. Both players should have it on: the recorder's
whole method is comparing the two, and a match only one of them streams is kept with the
other's name but not compared.

**What it keeps:**

- **A `.SRP` of the match** in `recordings/`, the same file the host's own game writes as
  `LATEST.SRP` in its save folder, byte for byte. It plays in the game: HIGHLIGHTS, LOAD,
  then VIEW.
- **One JSON line per match in the ledger** (`"kind":"match"`): the teams, the gameplay
  style, the **agreed input delay** (`"delay"`), whether it was relayed (`"relay"`, `null`
  for peer to peer), both players' names, builds, platforms and addresses, how each side
  ended and why, and whether their final states agreed (`"finalAgree"`).
- **A desync line the moment one happens** (`"kind":"desync"`), naming the exact frame and
  both hashes, and the match line repeats it (`"desync": {"frame": …}`).

**Using it:** a desync is reproduced offline from the recorder's copy, on any machine:

```bash
bin/SWOS.x86_64 --srp-resim=recordings/<match>.SRP
```

That replays the match from its recorded inputs and names the first frame that parts from
what was recorded (docs/REPLAY_FORMAT_V2.md, docs/NETPLAY.md §7 "The recorder").

### Keeping the recordings in bounds

Every match the matchmaker arranges is recorded by default (release builds too), at about
1 MB a match, and nothing deletes them but a fetch. `scripts/prune_recordings.sh` deletes the
ones unchanged for 30 days (`--days N`, `--dry-run`), with their cards, and rewrites the
list `/playback/` shows; each run is a line in `/srv/swos-matchmaker/prune.log`.
`sudo scripts/prune_recordings.sh --install` sets up a daily systemd timer
(`swos-prune-recordings.timer`, 04:30, as the checkout's owner); `--remove` takes it away.
A recording is checked for a desync while it is being recorded, so deleting old ones loses
nothing but the replay.

### The recordings player: /playback/

`https://<server>/playback/` plays any recording in a browser under a scrub bar, so a
moment a tester describes can be named by its frame (docs/PLAYBACK.md). Once:

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk     # as the service user
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
```

**The game's data.** The player is the web build, which bakes the game's data into
`SWOS.data` -- and OpenSWOS does not ship that data (README, "You need your own copy of the
game"). Make it once on the server from a copy of the game, AS THE SERVICE USER (the build
reads `~/.swos/assets` of whoever runs it; set `SWOS_ASSETS` in the service's environment to
put it elsewhere):

```bash
make -C tools/swosdata                                   # -> tools/swosdata/build/swosdata
tools/swosdata/build/swosdata --dos "/path/to/Sensible World of Soccer 96-97" \
    --adf /path/to/disk1.adf --adf /path/to/disk2.adf \
    --res bin --out ~/.swos/assets
```

The PC game's folder alone is enough (the Amiga disks add the Amiga graphics and music);
copy it from a GOG install. `--swos2020 "/path/to/SWOS 2020"` adds SWOS 2020's pitches
(`[graphics] style = 2020`) from a SWOS 2020 install, the set picked in its launcher.
Without the data the player still builds, but has no graphics: `make WASM=1` preloads
`bin/` plus `$(SWOS_ASSETS)`, and `bin/` holds only the project's own files. Rerun it after a change to `tools/swosdata` -- it rewrites the same files.

Then the admin page's Rebuild builds it with the server (`scripts/run_matchmaker.sh
--build-only --with-playback`), and `scripts/swos-matchmaker-service.sh install` or `restart`
copies it into place AND sets up
nginx: a `location /playback/` snippet, included beside the `/recordings/` block, tested,
reloaded and checked (docs/PLAYBACK.md, "On the server"). If it cannot, the run ends with a
WARNING saying what to add by hand. Without emsdk the build says so and skips the player.

### Lag, delay and stutter: read both players' SWOS.log

`SWOS.log` is in the save folder (`Documents\SWOS` on Windows). Every online match writes
two lines to it, and a lag report is answerable only with **both** players' logs.

At kick-off (the numbers here are an example):

```
[netplay] p2p match: …; player 1 on UDP port 51234, peer 81.2.3.4:51240, input delay 3, pauses up to 0 ms
```

During the match, a line every 30 seconds, and one for the whole match at the end —
GGPO's own ping, its range and jitter, the rollbacks and holds since the last line, and
how far this player's game runs ahead of the other's:

```
[netplay] kick-off: ping 118 ms, jitter 9 ms (measured by the link), input delay 2
[netplay] 0:30, frame 1500: ping 121 ms (110..140), jitter 4 ms; 43 rollbacks, 0 holds since the last line; 0.3 frames ahead
[netplay] whole match: ping 124 ms (108..190), jitter 5 ms
```

At the end:

```
[netplay] live match finished at frame 18000: 18000 frames played, 2210 re-simulated after
640 rollbacks; held at the prediction limit 3 times; time sync asked for 12 waits, 180 ms in
all, ending 0 us a frame slower
```

What each number means, and what it points at:

| in the log | what it is | high means |
|---|---|---|
| `input delay N` | frames between pressing and the player moving, agreed by both (SWOS 2020's AUTO rule: 3 at most on AMIGA, 4 on DOS) | a high ping when the match started. Compare it with the ping on the connecting screen (`CONNECTED TO … - 120 MS`) |
| `peer` | who the game sent its packets to | if it is **the server's address**, the match was relayed (a browser was in it, or the server runs `--relay` -- §4): the round trip went through the server |
| `rollbacks`, `re-simulated` | how often the other player's input arrived late and the game corrected itself | a jittery or slow link. Corrections show as players jumping a step. Normal: a few percent of the frames |
| `held at the prediction limit` | how often the game **stopped** because it was too far ahead of the other player's input | freezes. The link dropped out for a moment, or the other machine stalled |
| `time sync … waits, … ms` | the game slowing itself to stay level with the other one | one machine running ahead -- usually a slower machine, or a browser tab in the background |

A match that **felt** laggy with a low delay, few rollbacks and no holds was probably the
display, not the network: compare the `[render]` lines at the top of both logs
(refresh rate, vsync).

### Match reports: both logs, without asking anyone for them

After every online match, once both players have answered HOW WAS THE MATCH? (or skipped
it), each game sends its `SWOS.log` to the other over the lobby's connection and writes one
zip — `SWOS_player1.log` (the host's), `SWOS_player2.log`, and `rating.txt` (the teams, the
input delay, whether it was relayed, and each player's build, rating and comment):

- **on each player's machine** in `save/netplay/`, named by the kick-off time and both
  players (`2026-09-27_211405_PREDICADOR_vs_ELSHEIMY.zip`), the last 20 kept;
- **on the server** with `--reports DIR` (§4): `DIR/<match id>/player1.zip` and
  `player2.zip` — both players send one, so a match is covered if either gets through —
  and a `"kind":"report"` line in the ledger with the names, build, rating and comment.

Grep the ledger for a low `"rating"` or a comment, and the match id is the folder.

The player's side is **MULTIPLAYER SETTINGS → SEND MATCH REPORTS** (`[netplay] reports` in
`SWOS.ini`): ON by default in tester builds, OFF in a public release. OFF means nothing
leaves that player's machine — the other player's zip then says only that they had it
off. The game folder's path in a log (which names the player's account) is replaced by
`<SWOS folder>`; the logs still hold both players' IP addresses, which is why it asks.
The web build exchanges logs but never uploads (a browser has no TCP to give it).
docs/NETPLAY_REPORT.md has the design and the wire format.

## 13. Testing a deployment with the repo's own tests

These drive the real menus with real binaries; run them after changing anything in the
path. NETPLAY.md says what each one proves.

**Against a deployed server.** Both match tests take `MM_HOST=<server>`: they then start no
server of their own and play through that one, so they test the DEPLOYMENT — the round
trip to it, its relay and its browser gate — with two CPU players in real matches:

```bash
MM_HOST=your.server.name scripts/relay_test.sh                       # two natives, 2 matches
make WASM=1 NETPLAY=1 OUTDIR=dist/web-netplay                        # a web build for the test,
                                                                     # kept apart from dist/web (netplay
                                                                     # is on by default for WASM now)
WEBDIR=dist/web-netplay MM_HOST=your.server.name scripts/wasm_netplay_test.sh   # browser v browser
PAIR=web-native WEBDIR=dist/web-netplay MM_HOST=... scripts/wasm_netplay_test.sh # browser v native
TLS=1 WEBDIR=dist/web-netplay MM_HOST=your.server.name scripts/wasm_netplay_test.sh # ... over wss://
```

`TLS=1` serves the test page over `https://`, so the browsers use `wss://` exactly as the
public page would. Against a deployed server it is **the** test of the certificate: Chrome
accepts the test page's own throwaway certificate and nothing else, so the server's must
pass Chrome's ordinary checks.

The checks that read the server's own log and ledger are skipped, and say so — those are
on the server's machine. What the players can see replaces them: each match's peer was
the server (relayed), and each browser registered with the relay. The two players get
names of their own and `SWOS_MM_AUTO_PEER`, so **on a public server they meet only each
other** — never joining a real player's game, never letting a real player into theirs. The
web build has to be built by the same rules as the native binary for `web-native` (a test
build and a release build refuse each other, which is the handshake working).

Locally, against a server the test starts itself:

```bash
scripts/netplay_lobby_test.sh                     # two instances, lobby, match, rematch
scripts/relay_test.sh                             # two players through the relay
NETEM=awful scripts/relay_test.sh                 #   ... under the worst emulated link
RECORD=1 scripts/relay_test.sh                    #   ... recorded; the .SRP must match byte for byte
REPORTS=1 RELAY=0 scripts/relay_test.sh           #   ... peer to peer, each side zips both logs to --reports
scripts/wasm_netplay_test.sh                      # browser v browser, headless Chrome
TLS=1 scripts/wasm_netplay_test.sh                #   ... over wss://, a self-signed certificate
PAIR=web-native scripts/wasm_netplay_test.sh      # browser v the native build
PAIR=mac-web scripts/wasm_netplay_test.sh         # the Mac's native build v a browser
```

## 14. Related

- **docs/NETPLAY.md** — the design: §6 the direct-IP menu, the handshake and the lobby, §7
  matchmaking, the relay and the recorder, §8 the browser.
- **docs/REPLAY_FORMAT_V2.md** — the `.SRP` a recorder keeps, and `--srp-resim`.
- **`third_party/matchmaker/OPENSWOS.md`** — what OpenSWOS changed in the server.
- **`include/relay_proto.h`, `include/record_proto.h`** — the relay and recorder wire formats.
