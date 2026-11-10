# Picture subtitles in the browser player

Blu-ray PGS, DVD VobSub and DVB subtitles are pictures, not text: the browser can't show them with a `<track>` or
JASSUB. The handheld decodes them with libavcodec and the page lays the pictures over the video. Text and ASS paths are
unchanged (see [browser tracks](2026-11-11-browser-tracks.md)).

## Server: `/api/picsubs?p=&i=&from=&to=`

- `/api/media` gives a decodable picture track `kind: "picture"` (`pictureCodec()`: `dvd_subtitle`,
  `hdmv_pgs_subtitle`, `dvb_subtitle`; beside the video a `.idx` or `.sup`). Anything else stays `image` (handheld only).
  `subDefault` may now pick a picture track (the handheld's last language, or a forced one).
- One window, at most 120 s (`pictureWindow`); the page asks 60 s. `src/web/picsubs.cpp` opens the file, seeks to
  `from - 10 s` (a cue still showing as the window starts), discards every stream but the subtitle and one "clock"
  stream (the first audio, else the video: subtitle packets are sparse, so without it a gap of minutes would be read
  through to the next cue), decodes each subtitle packet (`avcodec_decode_subtitle2`), and stops at the first event
  after `to`, or 10 s past it.
- **Timing** (`pictureCues` in `src/picsubs.cpp`, bpcore, tested): one picture shows at a time; each ends at its own end
  time, the next event's start (PGS has no end times: an empty event clears; a new picture replaces), or, with
  neither, where reading stopped (a later window extends it; the page keeps the later end) or 5 s at the end of the
  file; never over 60 s (ffmpeg's own PGS→VobSub conversions write an end of 4294967 s).
- **Answer** (JSON): `{from, to, w, h, eof, cues: [{s, e, r: [{x, y, w, h, png}]}]}`; times in file seconds, rects in
  the track's canvas (`w`×`h`: PGS's presentation size, VobSub's `size:` from the .idx/CodecPrivate, else the video's,
  DVB 720×576), each picture a paletted PNG (`pngIndexed` in `src/png.cpp`: PLTE + tRNS, zlib level 1) in base64. A
  picture is ~1–6 KB; a minute of film ~5–25 KB. `to` is where it got to: short of the ask when the 8 s deadline hit
  (interrupt callback) or the base64 passed 8 MB; the page asks on from there.
- Two windows decode at once at most (a gate); answers are cached in memory by file + mtime + size + stream + window,
  16 MB LRU. Same gate as every `/api/*` route (session, `ws.resolve`); `i`, `from`, `to` checked. Each request logs
  `picsubs: <file> stream <n> <from>-<to> s: <cues> cues, <KB> KB in <ms> ms (read <MB> MB)` to log.txt.

## Page (`web/js/browser-player.js`)

- `chooseSub` sends a `picture` track to `choosePic`. A `requestAnimationFrame` loop reads `bPos()` (so a Direct
  Stream's `B.remux.base` and a parked position count), asks the window holding the clock (from `floor(t/10)*10`: cache
  hits after a seek back) when it isn't covered, and the next one once within 25 s of the end. The cue showing is the
  latest to start with `s <= t < e`; nothing shows while the clock is outside what's loaded (after a seek, until it
  arrives).
- `psubBox` (`.psub-box`) sits inside `assBox`, so it moves with the video (and `:root.vid-mini` hides it); it's placed
  each frame at the picture's shown rect inside the `<video>` (object-fit: contain: `videoWidth/videoHeight` against
  the element's box), and the images go in % of the canvas. Only a layout change touches its style; only a cue change
  touches the DOM. A failing window is retried 3 times (1.5, 3, 4.5 s), then **Couldn't load these subtitles**.

## Measured (Mac, M-series, SSD)

Test media (scratch only): the Sintel trailer with a PGS track made by a small `.sup` writer (blocky text, PCS/WDS/PDS/
ODS/END), converted by ffmpeg to VobSub and DVB (`-copyts -fix_sub_duration`, else the durations are 4294967 s), and a
25-minute loop of it (2.2 Mbit/s, 424 MB) with a cue every 20 s and a 3-minute gap. A window costs 6–110 ms and reads
~bitrate × 80 s (16–24 MB here): the demuxer still reads the video's packets it then drops. A 4K film at ~15 Mbit/s
would read ~150 MB per minute of subtitles; on the handheld's SD card that is likely 2–4 s a window, which the 25 s
lead covers in playback, but a seek waits that long for its first cue. Not measured on the handheld.

## Not verified

- On the handheld (the 4K VobSub film there), in Safari/iPhone (an `<img>` over the video; iPhone fullscreen is
  the system player, which shows no picture subtitles), and with a Direct Stream (the clock is `bPos()`, untested end
  to end). Real PGS from a Blu-ray (cropping, several objects, palette-only updates) relies on libavcodec's decoder;
  only generated PGS was tried. External `.idx`/`.sup` weren't tried (no muxer for VobSub in ffmpeg).
- The canvas stretches over the whole picture as mpv does; a VobSub made for a 4:3 frame over a 16:9 encode will look
  as it does on the handheld.
