#!/bin/bash
# PORTMASTER: swos.zip, Sensible World of Soccer.sh

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source "$controlfolder/control.txt"
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR="/$directory/ports/swos"
cd "$GAMEDIR" || exit 1
exec > "$GAMEDIR/log.txt" 2>&1

# No game data ships with the port: see assets/README.txt.
if [ ! -f "$GAMEDIR/assets/ASSETS.ini" ]; then
  pm_message "No game data. Copy the assets folder made by OpenSWOS Setup to ports/swos/assets (see assets/README.txt there)."
  sleep 8
  pm_finish
  exit 1
fi

BIN="$GAMEDIR/SWOS.${DEVICE_ARCH}"
GLIBC_DIR="$GAMEDIR/libs.${DEVICE_ARCH}/glibc"
$ESUDO chmod +x "$BIN" "$GLIBC_DIR/ld-linux-aarch64.so.1"

# The game keeps SWOS.ini, saves and its log in $HOME/.swos.
export HOME="$GAMEDIR/conf"
export SWOS_ASSETS="$GAMEDIR/assets"
# The panels hold presents to 60 Hz; without this, DOS gameplay's 70 Hz runs 14% slow.
export SWOS_FRAME_SKIP=1
mkdir -p "$HOME/.swos/save"
[ -f "$HOME/.swos/SWOS.ini" ] || cp SWOS.ini.default "$HOME/.swos/SWOS.ini"

# Trimui's get_controls does not fill sdl_controllerconfig.
[ -z "$sdl_controllerconfig" ] && [ -f "$SDL_GAMECONTROLLERCONFIG_FILE" ] &&
  sdl_controllerconfig="$(< "$SDL_GAMECONTROLLERCONFIG_FILE")"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# The binary needs glibc 2.34; stock Trimui firmware has 2.33. The bundled ld.so
# and libc must be used as a matched pair, or an old loader fails on the new libc
# with _dl_audit_preinit. Prefer the system's when it can run the binary.
SYS_LIBS="/usr/trimui/lib:/usr/lib64:/lib64:/usr/lib:/lib"
RUN=("$BIN")
if ! env LD_BIND_NOW=1 "$BIN" --version >/dev/null 2>&1; then
  RUN=("$GLIBC_DIR/ld-linux-aarch64.so.1" --library-path "$GLIBC_DIR:$SYS_LIBS" "$BIN")
fi
echo "Launching: ${RUN[*]}"

$GPTOKEYB "SWOS.${DEVICE_ARCH}" -c "./swos.gptk" &
# (an older PortMaster -- ArkOS on an R36S -- has no pm_platform_helper)
command -v pm_platform_helper >/dev/null 2>&1 && pm_platform_helper "$BIN"

"${RUN[@]}"

pm_finish
