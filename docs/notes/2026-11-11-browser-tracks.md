# Audio and subtitle tracks in the browser player

The dashboard's player (playing in the browser) switches a video's audio and subtitle tracks the way Plex does: the
video stays the original file (Direct Play) whenever it can, and only what the browser can't do is helped by the
handheld, without ever converting anything (the RK3568 decodes in software; transcoding is out).

## Three pipelines

1. **Subtitles, no interruption.**
   - Text tracks (SRT, mov_text, WebVTT, and the files beside the video) go through `/api/subs` as WebVTT into a
     `<track>`; an embedded one is read out of the file once on a worker thread (202 with the progress meanwhile) and
     cached in `<state>/webcache/subs` (LRU, 50 MB).
   - ASS/SSA (embedded or `.ass`/`.ssa` beside the video) are drawn by **JASSUB 1.8.8** (libass in WebAssembly,
     `web/vendor/jassub/`, about 2 MB in the binary). `/api/ass` gives the whole script: the track's header from the
     container (Script Info, styles) plus a `Dialogue:` line per packet, copied, not decoded (Matroska keeps
     `ReadOrder,Layer,Style,...`; the lines go out in ReadOrder). Cached the same way as `.ass`. `/api/fonts` lists the
     file's font attachments and `/api/font` serves one; JASSUB loads them all, with the dashboard's Inter as the
     fallback. JASSUB is imported only when a styled track is chosen; its canvas sits in `assBox`, which moves with the
     `<video>`. If it can't start (no WebAssembly or Worker, or an error), the same track shows as plain cues.
   - Picture subtitles (PGS, VobSub, DVB) are decoded on the handheld a window at a time into PNGs (`/api/picsubs`,
     kind `picture`) and placed over the picture by the page: [2026-11-10 picture subtitles](2026-11-10-picture-subs.md).
     Other picture formats (XSUB, a `.sub` without its `.idx`) keep kind `image`: **Shows on the handheld only**.
2. **Audio tracks.**
   - Safari exposes `video.audioTracks`: switched in place, true Direct Play.
   - Elsewhere the default track is the file itself. Another track is a **Direct Stream**: `/api/remux-start` finds the
     keyframe at or before the position (`{start}`), and `/api/remux` copies the video and that one audio stream into
     fragmented MP4 (`frag_keyframe+empty_moov+default_base_moof`, also cut every 2 s) from there, streamed chunked.
     Timestamps start at 0, so the page counts `B.remux.base + currentTime` (`bPos()`); a seek starts a new stream
     (debounced 250 ms); the `<track>` cues are moved back by the base and JASSUB gets it as `timeOffset`. The picture
     blinks about a second on a switch. Choosing the default track again goes back to the file.
   - **Timestamps and tags:** the HEVC stream goes out tagged `hvc1` (Safari and Chrome's MSE want it; `hev1` is
     refused). Matroska keeps presentation times only, so the remuxer always runs `avformat_find_stream_info` (for the
     B-frame delay) and makes the video's decode times itself (`decodeTs`: missing ones from the delay and the frame
     rate, always rising, never after the frame shows). Before that the muxer dropped the B-frames and Chrome's HEVC
     decoder (VideoToolbox) failed at once with -12909 ("Couldn't switch the audio track here" on 10-bit x265 anime).
     The log line counts any packet still dropped; a browser that can't play the stream posts `/api/remux-failed`,
     logged as `remux: the browser couldn't play …` with its MediaError and user agent.
   - **The stream starts exactly at `{start}`** (the keyframe's decode time): `remuxRun` drops every keyframe before
     it. Matroska seeks by presentation time, later than the decode time with B-frames, so seeking to `{start}` landed
     on the keyframe before; the stream began a GOP early (~6 s on x265 anime) while the page counted from `{start}`:
     picture and sound ran behind the subtitles, the bar and the handheld's position. The page keeps one clock,
     `bPos()` = `base + currentTime`, for everything (cues, JASSUB's `timeOffset`, bar, chapters, browser-pos, Media
     Session); keeping the file's timestamps in the MP4 instead would rely on Chrome and Safari agreeing on a
     non-zero start in progressive fMP4 (duration, seekable, `currentTime` from 0 or not), which the offset avoids.
   - Offered only when the browser says it plays the pair in MP4 (`canPlayType`/`MediaSource.isTypeSupported`): video
     H.264, HEVC, VP9, AV1; audio AAC, MP3, Opus, FLAC, AC-3, E-AC-3. DTS, TrueHD, PCM and the rest are **Plays on the
     handheld only**, with **Play this one on the handheld**.
   - Server side: one stream per page tab (a newer request ends the older), it ends when the client goes (a failed
     write), behind the usual gate (session, private network, `resolveVirtual`, no state dir), on its server thread,
     never touching Player or Ui. It reads only as fast as the browser takes it. Each one logs
     `remux: <file> audio stream <n> from <t>: to <t>, <cpu> s CPU in <wall> s (<why it ended>)` to log.txt.
   - A stream that stops early (the write timeout, 15 s, while the browser stopped reading) ends the video early: the
     page sees `ended` short of the duration and starts a new stream from there.
3. **The Tracks tab** (`web/js/playing.js`) lists all of it with the reason for anything that won't play here, and
   **Details** says **Direct Play** or **Direct Stream**. The size/position controls apply to plain subtitles only.

## Not done

- Network files: `/api/*` paths are the card's only; the browser doesn't play network files at all yet.
- The bundle's FFmpeg had every muxer off; `packaging/build-bundle.sh` now adds `--enable-muxer=mp4` (the stamp changed,
  so the next bundle build rebuilds the libraries once).
