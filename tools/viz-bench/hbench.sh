#!/bin/bash
# hbench.sh GRAPHS.tsv "RATES" SECONDS [HOST]: each graph in GRAPHS.tsv on the handheld's FFmpeg, one thread.
# RATES are the folders m<RATE>/ in the work dir (m44, m192: song.flac at that sample rate, in kHz).
# HOST defaults to $BP_HANDHELD (root@<ip>); the work dir on it is $BP_VB_DIR (default /tmp/vb).
# A TSV line is NAME<TAB>ART(0|1)<TAB>GRAPH; @MAPS@ in a graph is the folder of Circle's remap tables.
H=${4:-${BP_HANDHELD:?give the handheld as the 4th argument or BP_HANDHELD=root@IP}}
W=${BP_VB_DIR:-/tmp/vb}
for r in $2; do
	base=$(ssh -n "$H" "cd $W && ./gbench-arm m$r/song.flac cover.jpg '[aid1] anull [ao]; nullsrc=s=16x16:d=1 [vo]' $3" | cut -d' ' -f1)
	echo "decode-only ${r}k: $base s"
	while IFS=$'\t' read -r name art graph; do
		g=${graph//@MAPS@/$W/maps}
		out=$(ssh -n "$H" "cd $W && ./gbench-arm m$r/song.flac cover.jpg \"$g\" $3")
		echo "$name $art $r $base $out" | awk '{vis=$5-$4; printf "%-9s %s %3sk: frames %5d (%.1f/s) graph %6.2fs %6.2f ms/frame %5.1f%% of a core at 1x\n", $1, ($2=="1"?"art+viz":"viz    "), $3, $6, $6/$7, vis, 1000*vis/$6, 100*vis/$7}'
	done < "$1"
done
