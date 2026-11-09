# viz-bench

Times the music visualizers' lavfi-complex graphs on the handheld's own CPU (RK3568, Cortex-A55), with the FFmpeg the
bundle ships. The Mac hides the cost of colour conversion by about 10x, so any change to a graph in `src/settings.cpp`
is timed here before it is believed. Background and numbers: `docs/notes/2026-11-09-visualizer-performance.md`.

## What it measures

`gbench.c` builds one graph (`amovie` and, for art graphs, `movie` feeding `[aid1]`/`[vid1]`, your graph, then
`[vo] buffersink` and `[ao] abuffersink`), single-threaded (`nb_threads = 1`), pulls both ends until the audio has
passed SECONDS of song time, and prints `CPU-seconds frames audio-seconds`. `hbench.sh` runs it for each graph in a TSV
(`NAME<TAB>ART 0|1<TAB>GRAPH`; `@MAPS@` is the folder of Circle's remap tables) at each sample rate, subtracts a
decode-only run (`[aid1] anull`) and prints one line per graph.

`graphs-before.tsv` and `graphs-after.tsv` are the graphs as they were before and after the 2026-11-09 fix.
`before.txt` and `after.txt` are the handheld's output for them (44.1 and 192 kHz, 40 s of song). `before.txt` has no
Circle lines: its Circle graphs failed on a wrong map path in that run (the figures in the note come from a later run).

## Build (arm64 Docker, against the bundle's FFmpeg)

The bundle's FFmpeg is built by `packaging/build-bundle.sh` inside `debian:bookworm` (linux/arm64). Its install
(`/opt/bp`) lives only inside that container, but two things survive on the host and are all the bench needs:
the configured source tree in `.cache/bundle/src/ffmpeg-7.1.1/` (headers, including the generated
`libavutil/avconfig.h`) and the built libraries in `dist/ports/BananaPlayer/lib/` (run `packaging/build-bundle.sh`
first if `dist/` is empty). Link against those, so the bench uses exactly the libraries the handheld runs:

```sh
docker run --rm --platform linux/arm64 -v "$PWD":/src debian:bookworm bash -c "
  apt-get update -qq && apt-get install -y -qq gcc >/dev/null &&
  cd /src/tools/viz-bench &&
  gcc -O2 -o gbench-arm gbench.c -I/src/.cache/bundle/src/ffmpeg-7.1.1 \
    /src/dist/ports/BananaPlayer/lib/libavfilter.so.10 /src/dist/ports/BananaPlayer/lib/libavutil.so.59 \
    -Wl,-rpath,'\$ORIGIN/lib' -Wl,--allow-shlib-undefined"
```

(The soname versions match FFmpeg 7.1; adjust them if the bundle's FFmpeg is upgraded.)

(`gbench-arm` is a build product, ignored by git.)

## Run on the handheld over SSH

Set up a scratch folder, which a run never touches the player's own files in:

```sh
export BP_HANDHELD=root@<handheld ip>            # or pass it as hbench.sh's 4th argument
W=/tmp/vb
ssh $BP_HANDHELD "mkdir -p $W/lib $W/m44 $W/m192 $W/maps"
scp tools/viz-bench/gbench-arm $BP_HANDHELD:$W/
scp <the bundle's lib>/libav*.so* <libsw*.so*, libpostproc if present> $BP_HANDHELD:$W/lib/   # or set LD_LIBRARY_PATH
scp song-44k.flac $BP_HANDHELD:$W/m44/song.flac ; scp song-192k.flac $BP_HANDHELD:$W/m192/song.flac
scp cover.jpg $BP_HANDHELD:$W/
scp <Circle's remap tables (circle2-x.pgm, circle2-y.pgm)> $BP_HANDHELD:$W/maps/
tools/viz-bench/hbench.sh tools/viz-bench/graphs-after.tsv "44 192" 40
```

The binary finds the bundle's libraries through the rpath above (a copy of them in `$W/lib`), so the player's own
`BananaPlayer/lib` is neither used nor changed. Run it when the player is idle or closed: a playing player takes a core
and the figures are then too high. Afterwards: `ssh $BP_HANDHELD "rm -rf /tmp/vb"`.

Not verified when this was written: the exact copy list above (the run it comes from used a `lib/` and `maps/` folder
beside the binary), and that the Docker command compiles as written.

## Reading the output

```
Bars      viz      44k: frames  1201 (30.0/s) graph   8.79s   7.32 ms/frame  22.0% of a core at 1x
```

`frames (N/s)` is the picture rate per second of song time (it should be 30). `graph` is the CPU time beyond the
decode-only run. `ms/frame` is that over the frames. The last figure is the share of one core at 1x speed. mpv applies
`speed` after the graph, so at 2x the graph runs twice as fast per wall second: double the share (and the graph must
stay well under one core, as mpv's core thread also does the rest). `viz` is Visualizer mode, `art+viz` is art plus
visualizer. Compare with `before.txt` and `after.txt`; a graph worth shipping has to be near or under them.
