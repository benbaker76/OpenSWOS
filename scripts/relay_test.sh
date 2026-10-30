#!/usr/bin/env bash
#
# relay_test.sh — two players meet through the matchmaker and play THROUGH IT.
#
#   scripts/relay_test.sh                     # 2 matches, 300 frames each, a clean link
#   NETEM=wifi scripts/relay_test.sh          # the server puts wifi in the middle
#   NETEM=cycle MATCHES=1 ...                 # a different profile per session
#   MATCHES=3 FRAMES=600 scripts/relay_test.sh
#   RECORD=1 scripts/relay_test.sh            # both players also stream to the recorder
#   RECORD=1 RELAY=0 scripts/relay_test.sh    # ... while playing peer to peer
#   RECORD=1 DESYNC_FRAME=150 ...             # the joiner goes out of step at frame 150
#                                             # ON PURPOSE; the recorder must name 151
#   HOST_RES=1024x768 JOIN_RES=320x200 ...    # a [video] resolution each: two display
#                                             # widths (362 and 336), as a Mac and a PC had
#   GAMEPLAY=dos RECORD=1 ...                 # DOS gameplay on both (a resim must take it
#                                             # from the file, not from its own SWOS.ini)
#   MM_HOST=us.swos.eu scripts/relay_test.sh  # through an EXISTING server, not one of ours
#   REPORTS=1 RELAY=0 scripts/relay_test.sh   # the match reports (include/match_report.h):
#                                             # each player zips both logs and sends it here
#   RECORD=1 RELAY=0 BLOCK=both ...           # the RELAY FALLBACK: neither player lets the
#                                             # other's packets in (SWOS_P2P_TEST_BLOCK_DIRECT),
#                                             # so the direct punch fails and both must move to
#                                             # the server's fallback session (RELAYFB:,
#                                             # p2p_link.c relay_fallback); BLOCK=join or host:
#                                             # one side only, a link that works one way
#
# MM_HOST=<name> [MM_PORT=19602] plays through a server someone else runs, so it tests a
# DEPLOYMENT: the round trip to it and its relay. No server is started. What cannot be
# checked from here is skipped and said so -- the server's own log and ledger are on its
# machine -- so there RELAY decides nothing: the server does, and the summary says which it
# was. The two players get names of their own and SWOS_MM_AUTO_PEER, so on a public server
# they meet only each other: never a real player's game, never a real player in ours.
# DESYNC_FRAME needs the recorder's ledger, so it needs a server of our own.
#
# The relay (include/relay_proto.h, third_party/matchmaker/Server/Relay.cpp): the
# server makes a session at START, tells each OpenSWOS player RELAY:<token>, and
# both send everything to the server, which forwards it. This goes through what a
# player goes through -- login, create, join, START, the lobby, the match, a rematch
# -- with the matchmaker run here with --relay, and checks three things:
#
#   * both peers played MATCHES matches and each ended on the same SIM-tier hash;
#   * each peer's match was set up with the SERVER as its peer (so the packets did go
#     through it -- a direct punch would name the other process's port);
#   * the server's ledger has the session, linked, with packets forwarded each way.
#
# With RECORD=1 the server is also the recorder (include/record_proto.h) and each
# match must be in its ledger with both players' streams, every frame compared, and
# the same final hash -- or, with DESYNC_FRAME=N, a desync found at exactly frame N+1:
# the frame is simulated differently, so the first START-of-frame hash that differs
# is the next one. RELAY=0 plays peer to peer (the relay checks are skipped).
#
# A recording is FULL unless RECORD_MODE=hashes: the players also stream the .SRP
# their games are writing, and the recorder keeps it. Each player runs in a HOME of its
# own, so each writes its own LATEST.SRP -- and the recorder's file of the last match
# must be the HOST's LATEST.SRP byte for byte (include/srp_writer.h lays out both).
# And every copy must play again, frame for frame, from its own setup, kick-off state
# and inputs (--srp-resim, src/srp_resim.c).
#
# With REPORTS=1 the server keeps match reports (--reports) and each player sends one
# after every match: each side's zip -- its own copy in save/netplay and the server's --
# must hold BOTH players' SWOS.log and a rating.txt, and the ledger must have a "report"
# line from each player for each match. The players then log to SWOS.log in their HOMEs
# (a report is made of it), which the checks below read in place of their output.
#
# SERVER=<path> runs another copy of the matchmaker: netplay_desync_hunt.sh stops every
# build/matchmaker/swos-matchmaker between its rounds, a relay test's among them.
#
# Each side's input comes from its own generator (SWOS_LIVE_RANDOM_INPUT), so both
# predict wrong and roll back, and the relay carries real rollback traffic.
set -uo pipefail
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's, whatever SWOS.ini says)
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
BIN="${BIN:-SWOS.x86_64}"
PORT="${PORT:-19612}"
MATCHES="${MATCHES:-2}"
FRAMES="${FRAMES:-300}"
WAIT="${WAIT:-$((120 + MATCHES * 60))}"
OUT="${OUT:-${TMPDIR:-/tmp}/swos-relay.$$}"
RELAY="${RELAY:-1}"
BLOCK="${BLOCK:-}"                      # both|host|join: the relay fallback (below)
RECORD="${RECORD:-0}"
REPORTS="${REPORTS:-0}"
RECORD_MODE="${RECORD_MODE:-full}"
DESYNC_FRAME="${DESYNC_FRAME:-}"
SERVER="${SERVER:-build/matchmaker/swos-matchmaker}"
GAMEPLAY="${GAMEPLAY:-}"                # dos | amiga: both players, whatever SWOS.ini says
MM_HOST="${MM_HOST:-}"                  # an existing server to play through instead of our own
MM_PORT="${MM_PORT:-19602}"
REMOTE=0; [ -n "$MM_HOST" ] && REMOTE=1
case "$GAMEPLAY" in
    ''|amiga|dos) ;;
    *) echo "relay-test: GAMEPLAY must be amiga or dos" >&2; exit 1 ;;
esac
mkdir -p "$OUT" && OUT="$(cd "$OUT" && pwd)"   # the game chdirs to bin/
[ -n "$DESYNC_FRAME" ] && [ "$RECORD" != 1 ] && { echo "relay-test: DESYNC_FRAME needs RECORD=1" >&2; exit 1; }
[ "$REPORTS" = 1 ] && [ "$REMOTE" = 1 ] && { echo "relay-test: REPORTS needs a server of our own" >&2; exit 1; }
[ -n "$DESYNC_FRAME" ] && [ "$REMOTE" = 1 ] && { echo "relay-test: DESYNC_FRAME needs a server of our own (its ledger names the frame)" >&2; exit 1; }
[ -n "$DESYNC_FRAME" ] && MATCHES=1     # the match ends at the desync; one is the test
[ -x "bin/$BIN" ] || { echo "relay-test: bin/$BIN not built" >&2; exit 1; }
HNAME=RELAYHOST; JNAME=RELAYJOIN
if [ "$REMOTE" = 1 ]; then
    # Theirs: resolve it, because a relayed match's peer is the server by ADDRESS.
    MM_IP=$(getent ahostsv4 "$MM_HOST" 2>/dev/null | awk 'NR == 1 { print $1 }')
    [ -n "$MM_IP" ] || { echo "relay-test: cannot resolve $MM_HOST" >&2; exit 1; }
    PLAY_HOST=$MM_HOST; PLAY_PORT=$MM_PORT; RELAY_PEER="$MM_IP:$MM_PORT"
    n=$(( $$ % 10000 )); HNAME="UHOST$n"; JNAME="UJOIN$n"   # never two runs' players mixed
    [ -n "${NETEM:-}" ] && echo "relay-test: NETEM is the server's to choose; ignored for $MM_HOST" >&2
    echo "relay-test: through $MM_HOST ($RELAY_PEER) as $HNAME and $JNAME"
    SERVER_PID=
else
    PLAY_HOST=127.0.0.1; PLAY_PORT=$PORT; RELAY_PEER="127.0.0.1:$PORT"
    [ -x "$SERVER" ] || scripts/run_matchmaker.sh --build-only >/dev/null 2>&1
    [ -x "$SERVER" ] || { echo "relay-test: $SERVER not built" >&2; exit 1; }
    if command -v ss >/dev/null && ss -ulnp 2>/dev/null | grep -q ":$PORT "; then
        echo "relay-test: UDP $PORT is taken; set PORT=" >&2
        exit 1
    fi

    SERVER_ARGS=()
    [ "$RELAY" = 1 ] && SERVER_ARGS+=(--relay ${NETEM:+--netem "$NETEM"} --netem-seed 7)
    [ -n "$BLOCK" ] && SERVER_ARGS+=(--relay-fallback)     # off by default on a server
    [ "$RECORD" = 1 ] && SERVER_ARGS+=(--record --recordings "$OUT/recordings")
    [ "$REPORTS" = 1 ] && SERVER_ARGS+=(--reports "$OUT/reports")
    "$SERVER" "$PORT" "${SERVER_ARGS[@]}" --ledger "$OUT/ledger.jsonl" > "$OUT/server.log" 2>&1 &
    SERVER_PID=$!
    trap 'kill -INT $SERVER_PID 2>/dev/null' EXIT
    for _ in $(seq 1 50); do
        grep -q "running on Port" "$OUT/server.log" 2>/dev/null && break
        sleep 0.1
    done
fi

RECORD_ENV="SWOS_RECORD=off"     # recording is on by default in the game; only RECORD=1 tests it
[ "$RECORD" = 1 ] && RECORD_ENV="SWOS_RECORD=server SWOS_RECORD_MODE=$RECORD_MODE"
# A report is made of SWOS.log, so REPORTS=1 lets each player write one (in its own HOME).
NO_LOG="SWOS_NO_LOG=1"
[ "$REPORTS" = 1 ] && { NO_LOG=""; RECORD_ENV="$RECORD_ENV SWOS_REPORTS=server"; }
# A HOME each: its own SWOS.ini (the defaults, the same on both) and its own save folder,
# so the two LATEST.SRP files are two files.
mkdir -p "$OUT/home-host/.swos" "$OUT/home-join/.swos"
[ -n "${HOST_RES:-}" ] && printf '[video]\nresolution=%s\n' "$HOST_RES" > "$OUT/home-host/.swos/SWOS.ini"
[ -n "${JOIN_RES:-}" ] && printf '[video]\nresolution=%s\n' "$JOIN_RES" > "$OUT/home-join/.swos/SWOS.ini"
[ -n "$DESYNC_FRAME" ] && JOIN_ENV="${JOIN_ENV:-} SWOS_LIVE_DESYNC_FRAME=$DESYNC_FRAME"
# BLOCK=both|host|join: those players let nothing in from the other, only from the server,
# so the match must end up relayed through the fallback session however RELAY is set.
case "$BLOCK" in
    both) HOST_ENV="${HOST_ENV:-} SWOS_P2P_TEST_BLOCK_DIRECT=1"; JOIN_ENV="${JOIN_ENV:-} SWOS_P2P_TEST_BLOCK_DIRECT=1" ;;
    host) HOST_ENV="${HOST_ENV:-} SWOS_P2P_TEST_BLOCK_DIRECT=1" ;;
    join) JOIN_ENV="${JOIN_ENV:-} SWOS_P2P_TEST_BLOCK_DIRECT=1" ;;
    "")   ;;
    *)    echo "relay-test: BLOCK is both, host or join" >&2; exit 2 ;;
esac
RELAYED="$RELAY"; [ -n "$BLOCK" ] && RELAYED=1     # what the matches must have been

# HOST_START=0: the host does not press START by itself, so a key script can use the
# lobby first (the team screen, docs/NETPLAY.md "The lobby"); the run then ends at WAIT.
HOST_START_ENV="SWOS_LOBBY_START=1"
[ "${HOST_START:-1}" = 0 ] && HOST_START_ENV=""

# SWOS_NO_LOG=1: two processes would otherwise share one SWOS.log.
run() {   # name, auto, team, extra env...
    local name=$1 auto=$2 team=$3; shift 3
    cd bin || exit 1
    env $NO_LOG SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        SWOS_MM_HOST="$PLAY_HOST" SWOS_MM_PORT="$PLAY_PORT" SWOS_MM_AUTO="$auto" SWOS_P2P_LOG=1 \
        SWOS_LOBBY_TEAM="$team" SWOS_LOBBY_READY=1 SWOS_LOBBY_MATCHES="$MATCHES" \
        SWOS_LIVE_FRAMES="$FRAMES" $RECORD_ENV "$@" \
        timeout -s KILL "$WAIT" ./"$BIN" --matchmaker-menu="$name" \
        ${GAMEPLAY:+--dos-gameplay=$([ "$GAMEPLAY" = dos ] && echo on || echo off)}
}
# <side>.log is the raw output; <side>.ts the same lines with the time each arrived,
# to line up against the server's timed [relay] and [record] lines.
stamp() { gawk '{ print strftime("%H:%M:%S"), $0; fflush() }'; }
( run "$HNAME" host "${HOST_TEAM:-50:0B}" SWOS_MM_AUTO_PEER="$JNAME" ${HOST_START_ENV} SWOS_LIVE_RANDOM_INPUT=0xA5A5F00D HOME="$OUT/home-host" ${HOST_ENV:-} \
    2>&1 | tee "$OUT/host.log" | stamp > "$OUT/host.ts"
  echo "[relay-test] exit ${PIPESTATUS[0]}" >> "$OUT/host.log" ) &
HOST_PID=$!
sleep 3
( run "$JNAME" join "${JOIN_TEAM:-52:02}" SWOS_MM_AUTO_PEER="$HNAME" SWOS_LIVE_RANDOM_INPUT=0x5A5A0FF0 HOME="$OUT/home-join" ${JOIN_ENV:-} \
    2>&1 | tee "$OUT/join.log" | stamp > "$OUT/join.ts"
  echo "[relay-test] exit ${PIPESTATUS[0]}" >> "$OUT/join.log" ) &
JOIN_PID=$!
wait "$HOST_PID" "$JOIN_PID"
# REPORTS=1: what each player printed went to its SWOS.log -- read that instead.
if [ "$REPORTS" = 1 ]; then
    for side in host join; do
        { cat "$OUT/home-$side/.swos/SWOS.log" 2>/dev/null; cat "$OUT/$side.log"; } > "$OUT/$side.all" &&
            mv "$OUT/$side.all" "$OUT/$side.log"
    done
fi
if [ -n "$SERVER_PID" ]; then
    kill -INT "$SERVER_PID" 2>/dev/null
    wait "$SERVER_PID" 2>/dev/null
    trap - EXIT
fi

# TEAMCHECK_REFUSE=1: the host's game asks the server about a team it has "doctored"
# (SWOS_TEAMCHECK_CORRUPT, netplay_lobby.c: the joiner's team with a checksum that is
# not its own). The server must refuse it, the host must not start, and no match may be
# played -- so this run is expected to time out in the lobby, and that is its pass.
if [ "${TEAMCHECK_REFUSE:-0}" = 1 ]; then
    echo "================ team check, refused ================"
    grep -h "team check" "$OUT/host.log" "$OUT/join.log"
    grep "^\[teams\]" "$OUT/server.log"
    if grep -q "team check: player 2's .* is REFUSED" "$OUT/host.log" &&
       grep -q "^\[teams\] .*REFUSED" "$OUT/server.log" &&
       ! grep -q "p2p match:" "$OUT/host.log" && ! grep -q "p2p match:" "$OUT/join.log"; then
        echo "relay-test: PASSED — the server refused the doctored team and no match was played (logs in $OUT)"
        exit 0
    fi
    echo "relay-test: FAILED — a refused team did not stop the match (logs in $OUT)"
    exit 1
fi

fail=0
for side in host join; do
    echo "================ $side ================"
    grep -E "^\[p2p\] (relay|direct connection|link (failed|closed))|^\[netplay\] (p2p match|live match|desync|the peer|network)|^\[live\] final sim|^\[record\]|^\[relay-test\]" \
        "$OUT/$side.log"
    grep -q '^\[relay-test\] exit 0$' "$OUT/$side.log" || { echo "!! $side did not exit 0"; fail=1; }
    # The team check (include/team_proto.h): each game asked the server about both
    # teams, and both are on its list. Only where the server is ours to know.
    if [ "$REMOTE" != 1 ]; then
        for n in 1 2; do
            grep -q "team check: player $n's .* is canonical" "$OUT/$side.log" ||
                { echo "!! $side: the server did not vouch for player $n's team"; fail=1; }
        done
    fi
    # Relayed: the match's peer must be the server itself.
    if [ -n "$BLOCK" ] && ! grep -q "^\[p2p\] direct connection failed: playing through the server" "$OUT/$side.log"; then
        echo "!! $side: blocked direct, but never fell back to the server"; fail=1
    fi
    if [ "$RELAYED" = 1 ]; then
        if grep -q "^\[netplay\] p2p match:" "$OUT/$side.log" &&
           ! grep "^\[netplay\] p2p match:" "$OUT/$side.log" | grep -qv "peer $RELAY_PEER,"; then
            :
        else
            echo "!! $side: a match whose peer was not the relay ($RELAY_PEER)"; fail=1
        fi
    fi
done
echo "================ server ================"
HOW=
if [ "$REMOTE" = 1 ]; then
    echo "(the server's log and ledger are on $MM_HOST; not checked from here)"
    # What the players saw instead: relayed if every match's peer was the server.
    np=$(grep -c "^\[netplay\] p2p match:" "$OUT/host.log")
    nr=$(grep "^\[netplay\] p2p match:" "$OUT/host.log" | grep -c "peer $RELAY_PEER,")
    if [ "$np" -gt 0 ] && [ "$np" = "$nr" ]; then HOW="relayed by $MM_HOST"
    elif [ "$nr" = 0 ]; then HOW="peer to peer (introduced by $MM_HOST)"
    else HOW="mixed: $nr of $np relayed"; fi
    echo "played: $HOW"
else
    grep -E "^\[relay\]|^\[record\]" "$OUT/server.log"
fi

hh=$(grep -oE "final sim hash [0-9a-f]+" "$OUT/host.log" | awk '{print $4}' | paste -sd' ')
jh=$(grep -oE "final sim hash [0-9a-f]+" "$OUT/join.log" | awk '{print $4}' | paste -sd' ')
hn=$(grep -c "live match finished" "$OUT/host.log")
jn=$(grep -c "live match finished" "$OUT/join.log")
echo "host: $hn finished, hashes $hh"
echo "join: $jn finished, hashes $jh"

# One clock tick a frame, as on the Amiga: every online match until 2026-09-29 counted
# two (NetplayMatch_OwnsClock), and no test could see it -- they all run under the rig's
# clock. The tick at the end must be the frame, give or take the one being finished.
clock_ok=1
while read -r tick frame; do
    d=$((tick - frame)); [ "${d#-}" -le 2 ] || { echo "!! the match clock ran $tick ticks in $frame frames"; clock_ok=0; }
done < <(grep -hoE "clock: tick [0-9]+ at frame [0-9]+" "$OUT/host.log" "$OUT/join.log" | awk '{print $3, $6}')

# The ledger. Relayed: one session, linked, packets forwarded each way. Recorded: a
# "match" line per match with both players' streams -- and either every frame agreeing
# and the same final hash, or (DESYNC_FRAME) a desync found at exactly the next frame.
ledger_ok=0
if [ "$REMOTE" = 1 ]; then
    ledger_ok=1     # it is on the server's machine
elif [ -s "$OUT/ledger.jsonl" ] && RELAY=$RELAYED RECORD=$RECORD MATCHES=$MATCHES \
   DESYNC_FRAME=$DESYNC_FRAME python3 - "$OUT/ledger.jsonl" <<'PYEOF'
import json, os, sys
rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
ok = True
if os.environ["RELAY"] == "1":
    relay = [r for r in rows if r.get("kind") == "relay"]
    ok &= len(relay) == 1 and relay[0]["linked"] and all(n > 0 for n in relay[0]["packets"])
    for r in relay:
        print("ledger: relay %s %s vs %s, linked %s, packets %s, netem %s %s" % (
            r["token"], r["players"][0], r["players"][1], r["linked"], r["packets"],
            r["netem"], [s for s in r["netemSummary"] if s]))
if os.environ["RECORD"] == "1":
    matches = [r for r in rows if r.get("kind") == "match"]
    desyncs = [r for r in rows if r.get("kind") == "desync"]
    want = os.environ.get("DESYNC_FRAME")
    for m in matches:
        p = m["players"]
        both = p[0] is not None and p[1] is not None
        print("ledger: match %s %s, %s frames compared, desync %s, final hashes agree %s, relay %s" % (
            m["match"], " v ".join(m["teams"]), m["compared"],
            m["desync"]["frame"] if m["desync"] else "none", m["finalAgree"], m["relay"]))
        ok &= both and m["compared"] > 0
        if want:
            ok &= m["desync"] is not None and m["desync"]["frame"] == int(want) + 1
        else:
            ok &= m["desync"] is None and m["finalAgree"] is True
        ok &= (m["relay"] is not None) == (os.environ["RELAY"] == "1")
    ok &= len(matches) == (1 if want else int(os.environ["MATCHES"]))
    for d in desyncs:
        print("ledger: DESYNC match %s at frame %d (%s): %s vs %s" % (
            d["match"], d["frame"], d["at"], d["hashes"][0], d["hashes"][1]))
    ok &= len(desyncs) == (1 if want else 0)
sys.exit(0 if ok else 1)
PYEOF
then ledger_ok=1; fi

# The recorder's .SRP of the last match against the host's own LATEST.SRP.
srp_ok=1
if [ "$RECORD" = 1 ] && [ "$RECORD_MODE" = full ] && [ -z "$DESYNC_FRAME" ] && [ "$REMOTE" = 0 ]; then
    srp_ok=0
    server_srp=$(ls "$OUT"/recordings/*.SRP 2>/dev/null | sort | tail -1)
    host_srp=$(find "$OUT/home-host" -name LATEST.SRP 2>/dev/null | head -1)
    if [ -n "$server_srp" ] && [ -n "$host_srp" ] && python3 - "$server_srp" "$host_srp" <<'EOF'
import struct, sys, zlib
a, b = (open(p, "rb").read() for p in sys.argv[1:3])
def chunks(f):
    n = struct.unpack(">H", f[20:22])[0]
    out = {}
    for i in range(n):
        cc, off, stored, raw = struct.unpack(">4sIII", f[32 + 16 * i:48 + 16 * i])
        data = f[off:off + stored]
        out[cc.decode()] = data if stored == raw else zlib.decompress(data, -15)
    return out
if a == b:
    print("srp: the recorder's %s is the host's LATEST.SRP byte for byte (%d bytes, %d frames)"
          % (sys.argv[1].split("/")[-1], len(a), struct.unpack(">I", a[16:20])[0]))
    sys.exit(0)
ca, cb = chunks(a), chunks(b)
print("srp: DIFFERENT -- recorder %d bytes / %d frames, host %d bytes / %d frames"
      % (len(a), struct.unpack(">I", a[16:20])[0], len(b), struct.unpack(">I", b[16:20])[0]))
for k in sorted(set(ca) | set(cb)):
    print("  %s %s" % (k, "same" if ca.get(k) == cb.get(k) else
                           "differs (%d vs %d bytes)" % (len(ca.get(k, b"")), len(cb.get(k, b"")))))
sys.exit(1)
EOF
    then srp_ok=1; else echo "!! the recorder's .SRP ($server_srp) is not the host's ($host_srp)"; fi
fi

# And the last match plays again from each copy of its recording (--srp-resim,
# src/srp_resim.c): the recorder's, the host's and the joiner's, every frame as recorded
# -- except, with DESYNC_FRAME=N, the joiner's, which must part at frame N+1: the frame
# its own match went out of step, played again as the host played it.
resim_ok=1
if [ "$RECORD" = 1 ] && [ "$RECORD_MODE" = full ]; then
    resim_ok=0
    mkdir -p "$OUT/home-resim"
    server_srp=$(ls "$OUT"/recordings/*.SRP 2>/dev/null | sort | tail -1)
    host_srp=$(find "$OUT/home-host" -name LATEST.SRP 2>/dev/null | head -1)
    join_srp=$(find "$OUT/home-join" -name LATEST.SRP 2>/dev/null | head -1)
    bad=0
    whos="recorder host join"
    [ "$REMOTE" = 1 ] && whos="host join"     # the recorder's copy is on the server's machine
    for who in $whos; do
        case $who in recorder) f=$server_srp ;; host) f=$host_srp ;; join) f=$join_srp ;; esac
        [ -n "$f" ] || { echo "!! resim: no $who recording"; bad=1; continue; }
        line=$(HOME="$OUT/home-resim" timeout -s KILL 300 "bin/$BIN" --srp-resim="$f" 2>"$OUT/resim-$who.log" |
               grep '^\[resim\]' | tail -1)
        rc=${PIPESTATUS[0]}
        echo "resim $who: ${line#\[resim\] } (exit $rc)"
        if [ -n "$DESYNC_FRAME" ] && [ $who = join ]; then
            [ "$rc" = 2 ] && [[ "$line" == *"parts at frame $((DESYNC_FRAME + 1)):"* ]] || bad=1
        else
            [ "$rc" = 0 ] || bad=1
        fi
    done
    [ "$bad" = 0 ] && resim_ok=1
fi

# The match reports: every copy -- each player's own, and the server's of each -- holds
# both players' logs and the ratings, and the ledger has a report from each side.
reports_ok=1
if [ "$REPORTS" = 1 ]; then
    reports_ok=0
    if MATCHES=$MATCHES python3 - "$OUT" <<'PYEOF'
import glob, json, os, sys, zipfile
out, matches = sys.argv[1], int(os.environ["MATCHES"])
ok = True
want = {"SWOS_player1.log", "SWOS_player2.log", "rating.txt"}
def check(path):
    z = zipfile.ZipFile(path)
    names = set(z.namelist())
    logs = {n: z.read(n).decode("latin-1") for n in names if n.endswith(".log")}
    good = want <= names and all("[build]" in t or "live match" in t for t in logs.values())
    print("report: %s %s" % (os.path.relpath(path, out), "ok" if good else "MISSING %s" % sorted(want - names)))
    return good
for side in ("host", "join"):
    mine = sorted(glob.glob(os.path.join(out, "home-" + side, ".swos", "**", "netplay", "*.zip"), recursive=True))
    ok &= len(mine) == matches
    for p in mine:
        ok &= check(p)
kept = sorted(glob.glob(os.path.join(out, "reports", "*", "player*.zip")))
ok &= len(kept) == 2 * matches
for p in kept:
    ok &= check(p)
rows = [json.loads(l) for l in open(os.path.join(out, "ledger.jsonl")) if l.strip()]
rep = [r for r in rows if r.get("kind") == "report"]
for r in rep:
    print("ledger: report %s player %d (%s v %s), rating %s, %d bytes" % (
        r["match"], r["player"], r["name"], r["peer"], r["rating"], r["bytes"]))
ok &= len(rep) == 2 * matches and len({r["match"] for r in rep}) == matches
sys.exit(0 if ok else 1)
PYEOF
    then reports_ok=1; else echo "!! the match reports are not all there"; fi
fi

what="$MATCHES matches $([ "$RELAY" = 1 ] && echo "through the relay${NETEM:+ ($NETEM)}" || { [ -n "$BLOCK" ] && echo "through the fallback relay (direct blocked: $BLOCK)"; } || echo "peer to peer")$([ "$RECORD" = 1 ] && echo ", recorded")$([ "$REPORTS" = 1 ] && echo ", reported")"
[ "$REMOTE" = 1 ] && what="$MATCHES matches through $MM_HOST, $HOW$([ "$RECORD" = 1 ] && echo ", recorded")"
if [ -n "$DESYNC_FRAME" ]; then
    # A desync on purpose: the match ends early on both sides, and the recorder named the frame.
    if [ "$ledger_ok" = 1 ] && [ "$resim_ok" = 1 ] && grep -q "live match ended early" "$OUT/host.log" &&
       grep -q "live match ended early" "$OUT/join.log"; then
        echo "relay-test: PASSED — the desync at frame $DESYNC_FRAME was caught by the recorder at frame $((DESYNC_FRAME + 1)) (logs in $OUT)"
        exit 0
    fi
elif [ "$fail" = 0 ] && [ "$clock_ok" = 1 ] && [ "$ledger_ok" = 1 ] && [ "$srp_ok" = 1 ] && [ "$resim_ok" = 1 ] && [ "$reports_ok" = 1 ] &&
     [ "$hn" = "$MATCHES" ] && [ "$jn" = "$MATCHES" ] &&
   [ -n "$hh" ] && [ "$hh" = "$jh" ]; then
    echo "relay-test: PASSED — $what, both peers identical (logs in $OUT)"
    exit 0
fi
echo "relay-test: FAILED (logs in $OUT)"
exit 1
