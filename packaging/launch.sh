#!/bin/bash
# Banana Player's launcher, in its own folder (BananaPlayer/launch.sh). It's started by:
#   - the Store's tile, as the package's menu.launch: BananaOS's run-store-app gives it its own session,
#     HOME=/data/home, SDL_KMSDRM_FIRST (the top panel is SDL's first display) and both panels on;
#   - "Banana Player.sh" on the Ports shelf: run-port does the same, but turns the bottom panel's backlight off.
D="$(cd "$(dirname "$0")" && pwd)"

# run-port turns the bottom panel's backlight off, for ports that draw on the top one only. Banana Player has its
# controls there: back on (run-port puts it as it was when the player quits)
if [ -f /run/banana/panel-off ]; then
	BL=$(cat /run/banana/panel-off)
	rm -f /run/banana/panel-off
	case "$BL" in /sys/class/backlight/*) [ -w "$BL/bl_power" ] && echo 0 > "$BL/bl_power" ;; esac
fi

LOG_DIR="${XDG_CONFIG_HOME:-/data/home/.config}/banana-player"
mkdir -p "$LOG_DIR"
export LD_LIBRARY_PATH="$D/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# the player's fonts for subtitles and titles in any script (BananaOS has none of its own)
export FONTCONFIG_FILE="$D/share/banana-player/fonts.conf"
# the last run's log, for a report: home/.config/banana-player/log.txt on the card, and the run before it beside it
# (log.prev.txt: a relaunch doesn't lose what happened)
[ -f "$LOG_DIR/log.txt" ] && mv -f "$LOG_DIR/log.txt" "$LOG_DIR/log.prev.txt"
exec "$D/bin/banana-player" "$@" > "$LOG_DIR/log.txt" 2>&1
