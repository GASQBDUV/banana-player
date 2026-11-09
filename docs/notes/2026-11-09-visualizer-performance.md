# Visualizer performance (2026-11-09)

## Problem

The owner saw the music visualizer lag on hi-res FLAC, and badly above 1x speed, while the audio stayed smooth.

## Method

Measured on the handheld (RK3568, Cortex-A55), not the Mac: a libavfilter benchmark cross-compiled in the arm64 Docker
image against the bundle's FFmpeg 7.1.1 `.so` files, run over SSH, single-threaded. It is in `tools/viz-bench/`
(README there; results `before.txt`, `after.txt`).

## Root causes

1. **Per-frame colour conversion dominated.** `format=yuv420p` (RGBA to YUV through swscale) cost about 6.5 to 7 ms
   per 640x480 frame, and `colorkey` about 5 ms on the art strip. The Mac hid it: Bars took 0.7 ms/frame there and
   18.4 ms on the handheld; Art+viz Bars about 0.85 ms on the Mac and about 4 ms on the handheld after the fix.
2. **mpv applies `speed` in the audio output chain after lavfi-complex**, so the graph makes 30 frames per second of
   song time: 60 per wall second at 2x, 75 at 2.5x. The old Bars at 2x needed about 110% of a core on mpv's core
   thread, so mpv dropped video frames while the audio stayed smooth.
3. **Hi-res input.** Spectrum at 192 kHz cost 1.9x (8.7 to 16.5 ms): one column per FFT hop, each a full-frame copy,
   with the old separate `fps` filter discarding most of them. It also drew mostly ultrasound and scrolled about 4x
   faster. Waves cost 1.2x. Bars and Circle barely depend on the sample rate.
4. **Circle's `nullsrc d=1` still inputs ran at 25 fps**, so the first second of each song (and after a seek) made 55
   frames (`estimated-vf-fps` 51).

## Fix (`src/settings.cpp`, `src/settings.h`)

- Every visual branch starts `aformat=sample_rates=44100|48000` (plus mono where the channels share a colour: Bars,
  Circle).
- At most 30 frames per second of song time, by the filter's own `rate`/`fps`.
- No per-frame full-frame conversion and no `colorkey`. Only `format=rgb0` at the end in Visualizer mode (plain RGBA
  showed mpv's alpha checkerboard).
- Art + visualizer: the art slice is `overlay`'s main input, held as one frame with `settb=AVTB` (otherwise timestamps
  round to 1/25 s); the drawing is overlaid with `alpha=premultiplied`.
- Spectrum is composited in yuv444 (`opacity=4`, the art converted once to full-range yuv444p).
- Circle's remap tables cover only its 400x400 square (`CIRCLE_BOX`, `CIRCLE_X`, `CIRCLE_Y`), then `pad`; its still
  inputs are `r=1:d=1`.
- Deliberately not varying the fps with `speed`: changing the graph mid-song resets mpv's audio chain and reopens the
  sound output (a second or more on the handheld).

## Numbers (handheld, ms per frame, 44.1 kHz / 192 kHz, before to after; all 30.0 fps)

| Graph | Before | After |
|---|---|---|
| Bars | 18.4 / 15.8 | 7.3 / 8.0 |
| Bars art+viz | 12.5 / 12.1 | 4.0 / 5.0 |
| Waves | 8.7 / 10.4 | 2.2 / 3.4 |
| Waves art+viz | 8.6 / 9.7 | 2.3 / 3.4 |
| Spectrum | 8.7 / 16.5 | 1.5 / 2.8 |
| Spectrum art+viz | 17.5 / 23.2 | 6.1 / 7.6 |
| Circle | 13.7 / 13.8 | 8.4 / 9.4 |

At 2x, after (share of a core): Bars 44% (was 110%), Spectrum art+viz 37% (was 105%), Circle 51% (was 82%).

## Ruled out

`video-sync=audio`, `interpolation`, `display-resample`.

## Rule

Time any graph change on the handheld with `tools/viz-bench/`; the Mac underestimates conversions about 10x. The
Timeline visualizer is being brought in line separately and must follow the same rules (no per-frame conversion, at
most 30 frames per second of song time, a low sample rate in).
