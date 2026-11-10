#!/bin/bash
# Puts dist/ports/BananaPlayer (packaging/build-bundle.sh --quick) on the handheld over SSH, sending only the files that
# differ: the installed folder is copied on the handheld (fast, local), the changed files are laid over the copy, then
# the player is quit cleanly (SIGTERM, by its exact process name) and the folders swapped (the old one kept as
# BananaPlayer.old). The owner reopens it from the shelf.
#
#   tools/push-handheld.sh [user@]host        (or BP_HANDHELD=root@192.168.1.34)
set -euo pipefail
HOST="${1:-${BP_HANDHELD:-}}"
[ -n "$HOST" ] || { echo "usage: tools/push-handheld.sh root@<handheld address>" >&2; exit 2; }
case "$HOST" in *@*) ;; *) HOST="root@$HOST" ;; esac
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$HERE/dist/ports/BananaPlayer"
[ -x "$SRC/bin/banana-player" ] || { echo "no bundle: run packaging/build-bundle.sh --quick first" >&2; exit 1; }
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")
D=/data/roms/ports

# what's installed: path and md5 of every file (BusyBox md5sum)
remote=$("${SSH[@]}" "cd $D/BananaPlayer 2>/dev/null && find . -type f -exec md5sum {} + || true")
# what's here (macOS md5 -r prints the same "md5 path")
local_list=$(cd "$SRC" && find . -type f -exec md5 -r {} + | sort -k2)
changed=$(join -1 2 -2 2 -a 1 -o 1.2,1.1,2.1 \
	<(printf '%s\n' "$local_list") \
	<(printf '%s\n' "$remote" | sort -k2) | awk '$2 != $3 {print $1}')
gone=$(join -1 2 -2 2 -v 2 <(printf '%s\n' "$local_list") <(printf '%s\n' "$remote" | sort -k2) | awk '{print $1}')
n=$(printf '%s' "$changed" | grep -c . || true)
echo "push: $n changed file(s) of $(printf '%s\n' "$local_list" | grep -c .)"
if [ "$n" -eq 0 ] && [ -z "$gone" ]; then echo "push: the handheld already has this build; nothing done"; exit 0; fi

# the copy, the changed files over it, files no longer in the bundle removed; then the clean quit and the swap
{ cd "$SRC"; if [ "$n" -gt 0 ]; then printf '%s\n' "$changed" | COPYFILE_DISABLE=1 tar -cf - -T -; else tar -cf - -T /dev/null; fi; } |
"${SSH[@]}" "set -e; cd $D
	rm -rf BananaPlayer.new && cp -a BananaPlayer BananaPlayer.new
	tar -xf - -C BananaPlayer.new
	for f in $(printf '%s ' $gone); do rm -f \"BananaPlayer.new/\$f\"; done
	pid=\$(ps -o pid,comm | awk '\$2==\"banana-player\"{print \$1}')
	if [ -n \"\$pid\" ]; then kill -TERM \$pid; for i in 1 2 3 4 5 6 7 8 9 10; do ps -o comm | grep -qx banana-player || break; sleep 0.5; done; fi
	if ps -o comm | grep -qx banana-player; then echo 'push: the player is still running, left as it was'; rm -rf BananaPlayer.new; exit 1; fi
	rm -rf BananaPlayer.old && mv BananaPlayer BananaPlayer.old && mv BananaPlayer.new BananaPlayer
	echo \"push: installed \$(cat BananaPlayer/VERSION) \$(md5sum BananaPlayer/bin/banana-player | cut -c1-8)\${pid:+, the player quit (reopen it)}\""
