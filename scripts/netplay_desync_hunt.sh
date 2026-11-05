#!/usr/bin/env bash
# Unattended live desync hunt, with two players actually PLAYING.
#
# v1 ran AI against AI with no input at all, which never shoots, never tackles under
# pressure and never touches half the match code. SWOS_LIVE_RANDOM_INPUT gives each peer a
# stick and a fire button (its own generator, never the game's), seeded differently per
# round, so every round is a different match and the prediction is wrong often enough to
# roll back -- which is also the code path a desync would hide in.
#
# The desync ring is armed on both sides: nothing is written unless the two part.
#
# HUNT_INPUT=random (default) both peers play from SWOS_LIVE_RANDOM_INPUT
#            ai               no input at all -- how the 2026-09-17 desync was running
#            mix              alternate, so both input modes meet both team orders
# GAMEPLAY=amiga (default) | dos | mix   passed to BOTH peers as --dos-gameplay. It has to
#   be: each machine otherwise takes it from its own SWOS.ini, and when those differ the
#   handshake refuses the pair (P2P_FAIL_GAMEPLAY) without a line in SWOS_P2P_LOG -- both
#   peers just sit on the waiting screen until the round ends. Lost two rounds on
#   2026-09-18 when one machine's INI had been switched to DOS gameplay.
# RELAY=1                     play THROUGH the matchmaker (--relay, include/relay_proto.h)
# NETEM=<profile>|cycle       ... with the server's network emulator in the middle; cycle
#                             is a new profile each round: none lan adsl wifi mobile
#                             satellite awful. Each round's ledger lines go to hunt/ledger.jsonl.
# MAX_ROUNDS=N                stop after N rounds, whatever the hours (NETEM=cycle: 7 is
#                             one of each profile)
# MAC_LEDGER=<path on the Mac>  a recorder's ledger to watch too: a "desync" line there is a
#                             catch. Run the recorder on the Mac (its firewall is off, and
#                             only UDP 19602 is open on this machine) and point both players
#                             at it with HOST_ENV/JOIN_ENV SWOS_RECORD=... (docs/NETPLAY.md §7)
# HOST_ENV / JOIN_ENV         extra K=V pairs for one side only. The 2026-09-17 joiner ran
#   JOIN_ENV="SWOS_PRESENT_CAPTURE=200 SWOS_PRESENT_CAPTURE_FROM=200", a PNG every 200
#   presented frames, which stalls that peer. It stops after 60 files a process, so keep
#   ROUND short (a restart per round) if the stall is what you are sampling.
set -u
export SWOS_WORLD="${SWOS_WORLD:-9697}"   # (its corpus / careers are 96/97's, whatever SWOS.ini says)
SCR="${SWOS_HUNT_OUT:-$(cd "$(dirname "$0")/.." && pwd)/tests/hunt}"; mkdir -p "$SCR"
ROOT=/mnt/c/Projects/AI/SWOS/OpenSWOS
MINUTES_PER_ROUND="${ROUND:-25}"
HOURS="${1:-3}"
HUNT_INPUT="${HUNT_INPUT:-random}"
GAMEPLAY="${GAMEPLAY:-amiga}"
HOST_ENV="${HOST_ENV:-}"
JOIN_ENV="${JOIN_ENV:-}"
RELAY="${RELAY:-0}"
NETEM="${NETEM:-}"
MAC_LEDGER="${MAC_LEDGER:-}"
MAX_ROUNDS="${MAX_ROUNDS:-0}"          # stop after this many rounds (0 = only the hours)
OUT="$SCR/hunt"; mkdir -p "$OUT"
END=$(( $(date +%s) + HOURS * 3600 ))
round=0

caught() {
    [ -n "$(ls /mnt/c/Temp/swhunt/ 2>/dev/null)" ] && return 0
    [ -n "$(ssh bbaker@192.168.1.200 'ls /tmp/swhunt/ 2>/dev/null' 2>/dev/null)" ] && return 0
    [ -n "$MAC_LEDGER" ] && ssh bbaker@192.168.1.200 "grep -q '\"kind\":\"desync\"' $MAC_LEDGER" 2>/dev/null && return 0
    return 1
}

collect() {   # round
    ssh bbaker@192.168.1.200 'cat /tmp/mac_hunt.log' > "$OUT/round$1-mac.log" 2>/dev/null
    tr -d '\r' < /mnt/c/Temp/win_hunt.log > "$OUT/round$1-win.log" 2>/dev/null
}

ssh bbaker@192.168.1.200 'rm -rf /tmp/swhunt; mkdir -p /tmp/swhunt' >/dev/null 2>&1
rm -rf /mnt/c/Temp/swhunt; mkdir -p /mnt/c/Temp/swhunt

while [ "$(date +%s)" -lt "$END" ] && { [ "$MAX_ROUNDS" = 0 ] || [ "$round" -lt "$MAX_ROUNDS" ]; }; do
    round=$((round + 1))
    hseed=$(printf '0x%08X' $(( (round * 2654435761 + 12345) & 0x7FFFFFFF )))
    jseed=$(printf '0x%08X' $(( (round * 40503 + 98765) & 0x7FFFFFFF )))
    if [ $((round % 2)) -eq 1 ]; then hteam=50:0B; jteam=52:02; else hteam=52:02; jteam=50:0B; fi
    input=$HUNT_INPUT
    if [ "$input" = mix ]; then
        if [ $((round % 4)) -lt 2 ]; then input=ai; else input=random; fi
    fi
    gp=$GAMEPLAY
    if [ "$gp" = mix ]; then
        if [ $((round % 8)) -lt 4 ]; then gp=amiga; else gp=dos; fi
    fi
    if [ "$gp" = dos ]; then dosflag=--dos-gameplay=on; else dosflag=--dos-gameplay=off; fi
    hextra="$HOST_ENV"; jextra=""
    if [ "$input" = random ]; then
        hextra="SWOS_LIVE_RANDOM_INPUT=$hseed $hextra"
        jextra="set SWOS_LIVE_RANDOM_INPUT=$jseed"$'\n'
    fi
    for kv in $JOIN_ENV; do jextra+="set $kv"$'\n'; done
    mmargs=(--ledger "$OUT/ledger.jsonl")
    np=""
    if [ "$RELAY" = 1 ]; then
        mmargs+=(--relay)
        np=$NETEM
        if [ "$NETEM" = cycle ]; then
            profiles=(none lan adsl wifi mobile satellite awful)
            np=${profiles[$(( (round - 1) % ${#profiles[@]} ))]}
        fi
        [ -n "$np" ] && [ "$np" != none ] && mmargs+=(--netem "$np")
    fi
    echo "== round $round  $(date +%T)  $gp $input$([ "$RELAY" = 1 ] && echo "  relay ${np:-none}")  host $hteam/$hseed  join $jteam/$jseed${JOIN_ENV:+  join+ $JOIN_ENV}${HOST_ENV:+  host+ $HOST_ENV}"

    pkill -INT -f 'build/matchmaker/swos-matchmaker' 2>/dev/null; sleep 1
    ( cd "$ROOT" && nohup build/matchmaker/swos-matchmaker 19602 "${mmargs[@]}" > "$OUT/mm-round$round.log" 2>&1 & ); sleep 2
    cmd.exe /c "taskkill /F /IM SWOS.exe" >/dev/null 2>&1
    ssh bbaker@192.168.1.200 'pkill -9 -f SWOS.macos' >/dev/null 2>&1
    sleep 2

    ssh bbaker@192.168.1.200 "cd ~/Programming/OpenSWOS/bin && rm -f /tmp/mac_hunt.log && nohup env \
      SWOS_MM_HOST=172.249.109.62 SWOS_MM_AUTO=host SWOS_P2P_LOG=1 \
      SWOS_LOBBY_TEAM=$hteam SWOS_LOBBY_READY=1 SWOS_LOBBY_START=1 \
      SWOS_LIVE_FRAMES=2000 SWOS_LIVE_HASH_EVERY=1 SWOS_LIVE_DESYNC_DUMP=/tmp/swhunt \
      $hextra \
      SWOS_NO_LOG=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      ./SWOS.macos $dosflag --matchmaker-menu=MACHOST > /tmp/mac_hunt.log 2>&1 &" >/dev/null 2>&1

    cat > /mnt/c/Temp/swos_join_hunt.bat <<BAT
@echo off
cd /d C:\\Projects\\AI\\SWOS\\OpenSWOS\\bin
set SWOS_MM_HOST=172.249.109.62
set SWOS_MM_AUTO=join
set SWOS_LOBBY_TEAM=$jteam
set SWOS_LOBBY_READY=1
set SWOS_LIVE_FRAMES=2000
set SWOS_LIVE_HASH_EVERY=1
set SWOS_LIVE_DESYNC_DUMP=C:\\Temp\\swhunt
${jextra}set SWOS_NO_LOG=1
set SWOS_P2P_LOG=1
set SDL_VIDEODRIVER=dummy
set SDL_AUDIODRIVER=dummy
SWOS.exe $dosflag --matchmaker-menu=PCJOIN > C:\\Temp\\win_hunt.log 2>&1
BAT
    sed -i 's/$/\r/' /mnt/c/Temp/swos_join_hunt.bat
    sleep 24
    rm -f /mnt/c/Temp/win_hunt.log
    cmd.exe /c "C:\\Temp\\swos_join_hunt.bat" >/dev/null 2>&1 &

    for _ in $(seq 1 $((MINUTES_PER_ROUND * 2))); do
        sleep 30
        if caught; then
            echo "!! DESYNC CAPTURED in round $round at $(date +%T)"
            sleep 5
            cmd.exe /c "taskkill /F /IM SWOS.exe" >/dev/null 2>&1
            ssh bbaker@192.168.1.200 'pkill -9 -f SWOS.macos' >/dev/null 2>&1
            collect "$round"
            scp -q 'bbaker@192.168.1.200:/tmp/swhunt/*' "$OUT/" 2>/dev/null
            cp /mnt/c/Temp/swhunt/* "$OUT/" 2>/dev/null
            ls -la "$OUT"/desync-* 2>/dev/null
            pkill -f 'build/matchmaker/swos-matchmaker' 2>/dev/null
            exit 0
        fi
    done

    cmd.exe /c "taskkill /F /IM SWOS.exe" >/dev/null 2>&1
    ssh bbaker@192.168.1.200 'pkill -9 -f SWOS.macos' >/dev/null 2>&1
    collect "$round"
    n=$(grep -ac 'p2p match:' "$OUT/round$round-win.log" 2>/dev/null)
    if [ "${n:-0}" -eq 0 ]; then
        echo "!! round $round: NO MATCH PLAYED -- the pair never got going; see the [p2p] lines in round$round-*.log"
    else
        echo "   round $round: $n matches, no desync"
    fi
done
pkill -f 'build/matchmaker/swos-matchmaker' 2>/dev/null
echo "== hunt finished, nothing caught in $HOURS h ($round rounds)"
