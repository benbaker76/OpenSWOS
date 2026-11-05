#!/bin/bash
# Install/restart/remove the swos-matchmaker systemd service.
# Usage: ./swos-matchmaker-service.sh {install|restart|remove|status}
set -euo pipefail

SERVICE_NAME="swos-matchmaker"
SERVICE_FILE="/etc/systemd/system/${SERVICE_NAME}.service"
# The checkout this script is in, run as the user who owns it (us.swos.eu:
# /home/redhair/git/OpenSWOS, redhair; online.openswos.net: /home/swos/OpenSWOS, swos).
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN_USER="${RUN_USER:-$(stat -c %U "$REPO_DIR")}"
BUILT_BINARY="${REPO_DIR}/build/matchmaker/swos-matchmaker"
DEPLOY_DIR="/srv/swos-matchmaker"
BINARY="${DEPLOY_DIR}/bin/swos-matchmaker"
# The group nginx runs as: it serves recordings/, reports/ and playback/, and nothing else.
WEB_GROUP="${WEB_GROUP:-www-data}"
PORT=19602

usage() {
    echo "Usage: $0 {install|restart|remove|status}" >&2
    exit 1
}

ensure_srv_layout() {
    echo "Ensuring ${DEPLOY_DIR} directories/files exist (ReadWritePaths requires them to already be there, systemd won't create them) ..."
    sudo mkdir -p "${DEPLOY_DIR}/tls" "${DEPLOY_DIR}/bin" "${DEPLOY_DIR}/recordings" "${DEPLOY_DIR}/reports" \
                  "${DEPLOY_DIR}/playback"
    sudo touch "${DEPLOY_DIR}/matches.jsonl"
    sudo chown -R "${RUN_USER}:${RUN_USER}" "${DEPLOY_DIR}"
    srv_permissions
}

# Who may read what (after any chown -R, which resets the groups). The folders are
# the gate, the files keep their modes: the top one can be passed through but not
# listed; the TLS key, the admin queue, the binary and the ledger (players'
# addresses) are the service user's alone; nginx gets the three folders it serves
# through its group, setgid so what the server writes there later gets it too.
srv_permissions() {
    sudo chmod 711 "${DEPLOY_DIR}"
    for d in tls bin admin; do
        [ -d "${DEPLOY_DIR}/$d" ] && sudo chmod 700 "${DEPLOY_DIR}/$d"
    done
    for f in matches.jsonl prune.log tls/privkey.pem; do
        [ -f "${DEPLOY_DIR}/$f" ] && sudo chmod 600 "${DEPLOY_DIR}/$f"
    done
    for d in recordings reports playback; do
        [ -d "${DEPLOY_DIR}/$d" ] || continue
        sudo chgrp -R "$WEB_GROUP" "${DEPLOY_DIR}/$d"
        sudo find "${DEPLOY_DIR}/$d" -type d -exec chmod 2750 {} +
    done
}

deploy_binary() {
    if [ ! -x "$BUILT_BINARY" ]; then
        echo "Error: built binary not found or not executable: $BUILT_BINARY" >&2
        echo "Build it first, then re-run." >&2
        exit 1
    fi
    echo "Copying ${BUILT_BINARY} -> ${BINARY} ..."
    # Copy beside it, then rename over it. Copying straight onto the binary the running
    # server is executing fails with "Text file busy"; a rename is allowed, and the
    # running server keeps the old file until the restart below replaces the process.
    sudo cp "$BUILT_BINARY" "${BINARY}.new"
    sudo chown "${RUN_USER}:${RUN_USER}" "${BINARY}.new"
    sudo chmod 755 "${BINARY}.new"
    sudo mv -f "${BINARY}.new" "$BINARY"
}

# The recordings player (docs/PLAYBACK.md): static files nginx serves at /playback/,
# built beforehand as the unprivileged user (scripts/build_playback.sh, or the admin
# page's Rebuild) -- never here, which can run as root.
deploy_playback() {
    local built="${REPO_DIR}/dist/playback"
    if [ ! -s "${built}/SWOS.wasm" ] || [ ! -s "${built}/index.html" ]; then
        echo "Recordings player not built (${built}); skipping it. Build it with"
        echo "  ${REPO_DIR}/scripts/build_playback.sh   (needs emsdk)"
        return 0
    fi
    echo "Copying the recordings player -> ${DEPLOY_DIR}/playback ..."
    sudo mkdir -p "${DEPLOY_DIR}/playback"
    sudo cp -f "${built}"/index.html "${built}"/SWOS.html "${built}"/SWOS.js "${built}"/SWOS.wasm \
               "${built}"/SWOS.data "${DEPLOY_DIR}/playback/"
    sudo chown -R "${RUN_USER}:${RUN_USER}" "${DEPLOY_DIR}/playback"
    srv_permissions
    # Who played whom, for the page's list: cards for recordings made before the
    # recorder wrote them (scripts/recordings_index.py), as the service's own user.
    sudo -u "${RUN_USER}" python3 "${REPO_DIR}/scripts/recordings_index.py" \
        --recordings "${DEPLOY_DIR}/recordings" --ledger "${DEPLOY_DIR}/matches.jsonl" || true
    nginx_playback
}

# nginx must serve ${DEPLOY_DIR}/playback at /playback/, BESIDE /recordings/ (the page reads
# ../recordings/). Without it every /playback/ URL falls through to the site's catch-all
# ("Nothing to see here") and the page never loads -- which is what a copy-only install
# did (2026-09-29). So: a snippet of our own, included once into the server block that
# already serves /recordings/, checked with nginx -t and put back if it fails.
NGINX_SNIPPET="/etc/nginx/snippets/swos-playback.conf"
PLAYBACK_WARNING=""

# nginx almost always lives in /usr/sbin, which a plain (non-root) login shell's
# PATH often excludes - command -v alone false-negatived here even with nginx
# installed and running (confirmed live: "nginx is not installed here" on a
# server that already had /reports/ and /cc/ configured in it). The later sudo
# calls (nginx -t, systemctl reload) already work regardless, via sudo's own
# secure_path - only this detection needed the fallback.
nginx_installed() {
    command -v nginx >/dev/null 2>&1 && return 0
    for p in /usr/sbin/nginx /usr/local/sbin/nginx /sbin/nginx; do
        [ -x "$p" ] && return 0
    done
    return 1
}

nginx_playback() {
    local conf n
    if ! nginx_installed; then
        PLAYBACK_WARNING="nginx is not installed here: serve ${DEPLOY_DIR}/playback at /playback/ yourself (docs/PLAYBACK.md)."
        return 0
    fi
    sudo mkdir -p "$(dirname "$NGINX_SNIPPET")"
    sudo tee "$NGINX_SNIPPET" > /dev/null <<EOF
# OpenSWOS recordings player (docs/PLAYBACK.md) -- written by swos-matchmaker-service.sh.
location = /playback { return 301 /playback/; }
location /playback/ {
    alias ${DEPLOY_DIR}/playback/;
    index index.html;
    types { text/html html; application/javascript js; application/wasm wasm; application/octet-stream data; }
}
EOF
    if sudo grep -Rqs "include ${NGINX_SNIPPET}\|location /playback/" /etc/nginx/sites-enabled/ /etc/nginx/conf.d/ /etc/nginx/nginx.conf; then
        sudo nginx -t >/dev/null 2>&1 && sudo systemctl reload nginx && echo "nginx: /playback/ already configured; reloaded."
        check_playback
        return 0
    fi
    # The one enabled config that serves /recordings/ is where the player goes.
    n=$(sudo grep -Rls "location /recordings/" /etc/nginx/sites-enabled/ /etc/nginx/conf.d/ 2>/dev/null | wc -l)
    conf=$(sudo grep -Rls "location /recordings/" /etc/nginx/sites-enabled/ /etc/nginx/conf.d/ 2>/dev/null | head -1)
    if [ "$n" != 1 ]; then
        PLAYBACK_WARNING="nginx: found ${n} configs with 'location /recordings/', so /playback/ was NOT added. Add
    include ${NGINX_SNIPPET};
beside 'location /recordings/' in the us.swos.eu HTTPS server block, then
    sudo nginx -t && sudo systemctl reload nginx"
        return 0
    fi
    conf=$(readlink -f "$conf")
    echo "nginx: adding /playback/ to ${conf} (backup: ${conf}.swos-bak) ..."
    sudo cp -p "$conf" "${conf}.swos-bak"
    sudo sed -i "0,/^\([[:space:]]*\)location \/recordings\//s||\1include ${NGINX_SNIPPET};\n&|" "$conf"
    if sudo nginx -t >/dev/null 2>&1; then
        sudo systemctl reload nginx
        echo "nginx: /playback/ added and reloaded."
        check_playback
    else
        sudo cp -p "${conf}.swos-bak" "$conf"
        PLAYBACK_WARNING="nginx rejected the change, so ${conf} was put back as it was. Run 'sudo nginx -t' after adding
    include ${NGINX_SNIPPET};
beside 'location /recordings/' yourself to see why."
    fi
}

# The page and the game as nginx serves them, through the site's own name.
check_playback() {
    local host code
    # The site's name: a server_name DIRECTIVE, from the config that serves /recordings/
    # when there is one. Any line mentioning "server_name" used to do -- a comment's
    # second word is "server_name" itself, and the check then asked for
    # https://server_name/..., got nginx's 404 and reported a player that was live as
    # not live (2026-10-12).
    local conf
    conf=$(sudo grep -Rls "location /recordings/" /etc/nginx/sites-enabled/ /etc/nginx/conf.d/ 2>/dev/null | head -1)
    host=$(sudo grep -hs -E '^[[:space:]]*server_name[[:space:]]' ${conf:+"$conf"} /etc/nginx/sites-enabled/* 2>/dev/null |
           awk '{ for (i = 2; i <= NF; i++) { gsub(";", "", $i); if ($i != "" && $i != "_" && $i != "localhost") { print $i; exit } } }')
    host=${host:-localhost}
    code=$(curl -sk -o /dev/null -w "%{http_code} %{content_type}" --resolve "${host}:443:127.0.0.1" \
           "https://${host}/playback/SWOS.wasm" 2>/dev/null || true)
    case "$code" in
        "200 application/wasm"*) echo "nginx: https://${host}/playback/ serves the player." ;;
        # Behind a login (online.openswos.net: the recordings are internal): nginx found the
        # location and asks for the password, which this check does not have.
        "401 "*) echo "nginx: https://${host}/playback/ serves the player, behind a login." ;;
        *) PLAYBACK_WARNING="https://${host}/playback/SWOS.wasm answered '${code}', not 200 application/wasm: check nginx (docs/PLAYBACK.md)." ;;
    esac
}

playback_warning() {
    [ -n "$PLAYBACK_WARNING" ] || return 0
    echo
    echo "=================================================================="
    echo "WARNING -- the recordings player at /playback/ is NOT live:"
    echo "$PLAYBACK_WARNING"
    echo "=================================================================="
}

install_service() {
    ensure_srv_layout
    deploy_binary
    deploy_playback

    echo "Writing ${SERVICE_FILE} ..."
    # One argument per line. In this unquoted heredoc "\\" writes the single backslash
    # systemd reads as a line continuation (a lone "\" would join the lines here).
    sudo tee "$SERVICE_FILE" > /dev/null <<EOF
[Unit]
Description=OpenSWOS matchmaking / relay server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${RUN_USER}
Group=${RUN_USER}
WorkingDirectory=${DEPLOY_DIR}
ExecStart=${BINARY} ${PORT} \\
    --websocket \\
    --tls-cert ${DEPLOY_DIR}/tls/fullchain.pem \\
    --tls-key ${DEPLOY_DIR}/tls/privkey.pem \\
    --record \\
    --recordings ${DEPLOY_DIR}/recordings \\
    --reports ${DEPLOY_DIR}/reports \\
    --ledger ${DEPLOY_DIR}/matches.jsonl
Restart=always
RestartSec=2
# SIGTERM is a clean stop: relay sessions close and write their ledger lines.
KillSignal=SIGTERM
TimeoutStopSec=10

# Hardening. It reads its certificate and writes only these three paths, which
# must exist before it starts (ensure_srv_layout): a missing one is 226/NAMESPACE.
NoNewPrivileges=yes
PrivateTmp=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=${DEPLOY_DIR}/recordings
ReadWritePaths=${DEPLOY_DIR}/reports
ReadWritePaths=${DEPLOY_DIR}/matches.jsonl

[Install]
WantedBy=multi-user.target
EOF

    echo "Reloading systemd and enabling service ..."
    sudo systemctl daemon-reload
    sudo systemctl enable "$SERVICE_NAME"
    # restart, not start: install is also how a changed unit or a new binary goes live,
    # and "enable --now" leaves an already-running server as it was. restart starts a
    # stopped one too, so install works whether or not the service exists yet.
    sudo systemctl restart "$SERVICE_NAME"
    refresh_match_cards
    echo "Installed and (re)started."
    systemctl status "$SERVICE_NAME" --no-pager || true
    playback_warning
}

# The match cards again, AFTER a restart: stopping the server closes any match still open
# and writes its ledger line then, which the cards read -- the run in deploy_playback, before
# the restart, could not see it (a match left open until a restart got no card, 2026-09-30).
refresh_match_cards() {
    sudo -u "${RUN_USER}" python3 "${REPO_DIR}/scripts/recordings_index.py" \
        --recordings "${DEPLOY_DIR}/recordings" --ledger "${DEPLOY_DIR}/matches.jsonl" || true
}

restart_service() {
    deploy_binary
    deploy_playback
    echo "Restarting ${SERVICE_NAME} ..."
    sudo systemctl restart "$SERVICE_NAME"
    refresh_match_cards
    systemctl status "$SERVICE_NAME" --no-pager || true
    playback_warning
}

remove_service() {
    echo "Stopping and removing ${SERVICE_NAME} ..."
    sudo systemctl stop "$SERVICE_NAME" 2>/dev/null || true
    sudo systemctl disable "$SERVICE_NAME" 2>/dev/null || true
    sudo rm -f "$SERVICE_FILE"
    sudo systemctl daemon-reload
    echo "Removed."
}

status_service() {
    systemctl status "$SERVICE_NAME" --no-pager
}

case "${1:-}" in
    install) install_service ;;
    restart) restart_service ;;
    remove)  remove_service ;;
    status)  status_service ;;
    *) usage ;;
esac
