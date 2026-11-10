# 2026-11-10 File durations in the lists

**What:** lengths in the handheld's file rows (`3:34 · 3.5 MB`), the dashboard's Files rows and Up next (folder queues
and the browser's own queue had none; playlists carry theirs from `#EXTINF`).

## How

- `src/durprobe.cpp` (`bpprobe`, linked into the player and core-test): `avformat_open_input` only, 1 MB probesize,
  2 s interrupt deadline. **Never `avformat_find_stream_info`**: it decodes, and for a file with no length in its
  header it guesses from the bitrate or reads on (MPEG-TS: to the end). No length in the header → -1.
- Formats that keep no length in a header are never opened (`durationInHeader()` in `src/durations.cpp`, one list,
  also mirrored in `web/js/durations.js`): `.ts .m2ts .mts .m2t .tp .trp .mpg .mpeg .vob .m2v .aac .ac3 .eac3 .dts
  .h264 .264 .hevc .h265 .265`.
- MP3 without a Xing/Info/VBRI frame: the header has nothing (`ctx->duration` and `st->duration` are both NOPTS
  without `find_stream_info`; measured). The first ~16 KB of packets are walked frame by frame (`mp3CbrBitrate`): if
  8+ whole frames share one bitrate, length = bytes from the first frame / bitrate (CBR: exact to ~0.03 s). Frames that
  differ = VBR with no index → -1 (a first-frame guess gave 1.7 s for a 10 s VBR file).
- `saneDuration`: NaN, ≤ 0, over a week, or under 64 bits/s over the file's size → -1 (a cut WAV claiming 4 GB).
- `DurationCache::shared()` (bpcore, 5000 entries, LRU, key = real path + size + mtime, -1 kept) is shared by the
  web pool threads, the handheld worker and `probeMedia` (which feeds it). A probe cut by the deadline isn't cached.
- Handheld: `DurationWorker` (one detached thread, heap, never destroyed), nice 15 + I/O best-effort 7 on Linux, a pause
  of max(2 ms, half the probe) after each, none while `Player::loading()`. Rows on screen first, then outward;
  `enterFolder` replaces the list (serial). Results batched behind one `evDur` until `take()`; only a change to a row on
  screen bumps `browseVersion` (signature).
- Web: `POST /api/durations` with `p[]` (≤ 32; through `resolve`, media extensions only), stops probing after 6 s
  (the rest left out, asked again on the next sighting). Page: IntersectionObserver (200 px ahead), 80 ms batching,
  one request in flight, a session `Map` (including -1).

## Numbers (Mac, warm cache)

Per probe: MKV/MP4/FLAC/Opus/AVI with a header 0.1–0.6 ms; MP3s with a big ID3 cover ~3 ms; 256 KB of random bytes
named `.mkv`/`.mp3` ~25 ms (libavformat's format detection reading up to the probesize); empty file 0 ms (never
opened). A 150-file folder probed in the background in well under a second. Handheld: unmeasured; `BP_DURATION_TIMING=1`
logs `timing: duration N ms` per probe.

## Not covered

A crash inside libavformat on a hostile file would still take the player down (same as `probeMedia` and mpv itself);
nothing here runs out of process. libavformat's complaints about broken files go to mpv's log (mpv owns `av_log`'s
callback), so a folder of broken files adds lines to `log.txt`.

## Verify

`./build/core-test` (testDurations: cache, notes, the list, frame walk, and probes of empty/noise/ID3-only/broken
EBML/cut WAV/`.ts`/CBR-no-Xing files). End to end: `BP_WEB_FORCE=1 BP_WEB_PIN=ACDE BP_WEB_PORT=8093 BP_BROWSE=<lib>
./build/banana-player --press info,...` and `curl -X POST -b jar -H 'X-BP: 1' -H 'Origin: …' --data-urlencode
'p[]=0/x.mp3' …/api/durations`.
