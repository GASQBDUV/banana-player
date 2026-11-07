#!/bin/bash
# The README's pictures: scripted scenes of the desktop build on a demo library, both panels captured (--press's
# shot:NAME) and stacked like the handheld's, in docs/img/.
#   docs/make-screenshots.sh DEMO_DIR      (a library: Movies/, Shows/, Music/<album>/ with cover.jpg,
#                                           Photos/Sintel/ with stills of the trailer)
set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
DEMO="$(cd "$1" && pwd)"
BIN="$R/build/banana-player"
OUT="$R/docs/img"
WORK="$(mktemp -d)"
STATE="$WORK/state"
export BP_SHOTS="$WORK" BP_BROWSE="$DEMO"
MOVIE="$DEMO/Movies/Sintel (trailer).mkv"	# Sintel: © Blender Foundation, CC BY 3.0 (durian.blender.org)
SONG="$(ls "$DEMO"/Music/*/01*.mp3 | head -1)"

run() { "$BIN" --state-dir "$STATE" --press "$@" > "$WORK/log.txt" 2>&1 || true; }

# some history first: places to come back to, Recent filled
mkdir -p "$STATE"
printf 'music-show = both\nvisualizer = circle\n' > "$STATE/settings.conf"
run Right,Right,B,B "$DEMO/Movies/Night Garden.mp4"
run Right,B,B "$SONG"
run Right,Right,B,B "$MOVIE"

# the scenes (each step 0.8 s apart: the picture has moved on by the shot)
run prop:pause,shot:browser "$DEMO/Movies"    # the browser, opened in Movies
run prop:pause,prop:pause,shot:playing "$MOVIE"
run Y,shot:subs "$MOVIE"
run Right,shot:music "$SONG"
run Start,R,A,Down,Down,shot:settings "$MOVIE"	# Settings › Subtitles
run tap:322:452,Down,Down,Down,Down,Down,Down,Down,Down,shot:info "$MOVIE"
run Up,Up,shot:photo "$DEMO/Photos/Sintel/Sintel 02.jpg"	# a still of the trailer, at 2x

# each pair stacked: top over bottom with a hinge between, on a dark frame
for s in browser playing subs music settings info photo; do
	ffmpeg -loglevel error -y -i "$WORK/$s-top.png" -i "$WORK/$s-bottom.png" -filter_complex \
		"[0]scale=640:480:flags=lanczos[t];[1]scale=640:480:flags=lanczos[b];color=c=0x2A2931:s=640x20[h];[t][h][b]vstack=3,pad=iw+40:ih+40:20:20:color=0x2A2931" \
		-frames:v 1 "$OUT/screen-$s.png"
	echo "docs/img/screen-$s.png"
done
rm -rf "$WORK"
