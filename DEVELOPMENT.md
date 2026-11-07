# Developing Banana Player

For what the player does and how to use it, see [README.md](README.md). This file covers how it's built, run,
tested and laid out.

## How it works

```
             ┌─────────────── banana-player ───────────────┐
 file ──▶ libmpv (FFmpeg demux/decode, A/V sync, libass)  │
             │   render API (OpenGL ES) ─▶ top panel        │  SDL2 KMSDRM: one window per panel
             │   ALSA "default" (bananad picks the output)  │
             │ Ui (SDL_Renderer + SDL_ttf) ─▶ bottom panel  │
             │ buttons (SDL GameController), touch (evdev)  │
             └──────────────────────────────────────────────┘
```

- **libmpv** does the demuxing, decoding, A/V sync, track switching and subtitles (libass). Decoding is software only
  for now (`hwdec=no`, a thread per core).
- **Top panel:** mpv's render API draws into an OpenGL ES context on an SDL window. For audio files, `force-window`
  keeps the window. A visualizer is an mpv `lavfi-complex` graph (`showfreqs`, `showwaves`, `showspectrum`; Cover +
  bars overlays the bars on `[vid1]`, the cover). The title, artist and album are an `osd-overlay`. Auto and Cover +
  bars choose per file at `FILE_LOADED`, once mpv knows whether the file has a cover. `lavfi-complex` is cleared
  before every load, because mpv refuses to switch tracks while a graph holds them.
- **Bottom panel:** `Ui` draws with SDL_Renderer and SDL_ttf. The layout is in 640×480 units (the RG DS panel), and
  text is rendered at the panel's real size, so Pixelify Sans stays sharp on the RG DS Plus's 1024×768 (the same 4:3
  shape, scaled ×1.6).
- **Fonts:** the bottom panel draws with SDL_ttf: Pixelify Sans, else (per text, or per run of characters when no
  one font has them all) Fusion Pixel and Noto Sans Thai/Arabic/Hebrew/Devanagari. SDL_ttf on BananaOS doesn't shape,
  so Arabic and Hebrew names read left to right there. libass (subtitles, titles) is built with fontconfig, and the
  bundled `share/banana-player/fonts.conf` (`FONTCONFIG_FILE`, set by the launcher) lists only the player's fonts:
  that's how it falls back per character with no system fonts. `BP_FONT_PROVIDER=none` shows libass without it.
- **Sound:** ALSA `default`. There's no sound server: bananad writes `/run/banana/asound.conf` (speaker, headphones or
  Bluetooth through bluez-alsa).

### Source

| File | What |
|---|---|
| `src/main.cpp` | Arguments, SDL windows and GL, the event loop, browser/playing modes, scripted runs |
| `src/player.*` | libmpv: options, properties, playlist, resume, history, audio display, photo view and thumbnail, loading spinner, media info |
| `src/ui.*` | The bottom panel: pages (main, photo, tracks, settings groups, browser, info, about), buttons and touch |
| `src/input.*` | SDL keys and pads to the handheld's buttons (Nintendo layout: A is east) |
| `src/touch.*` | The bottom touchscreen from evdev, one finger |
| `src/sleepwatch.*` | Pause on the lid's `SW_LID` and on `SIGCONT` |
| `src/panel.*` | The bottom backlight on/off, restored on quit and on a crash |
| `src/aspect.*`, `tracks.*`, `format.*`, `substyle.*`, `settings.*`, `resume.*`, `history.*`, `playlist.*`, `browser.*`, `photo.*` | No window code; covered by `tests/core_test.cpp` |

### BananaOS integration

- **Store apps** run through BananaOS's `run-store-app`. It gives the app its own session, leaves both panels on,
  sets `HOME=/data/home` and `XDG_CONFIG_HOME=/data/home/.config`, and sets `SDL_KMSDRM_FIRST` to the top connector so
  the top panel is SDL's first display.
- **State:** `$XDG_CONFIG_HOME/banana-player/` (on the handheld `/data/home/.config/banana-player/`):
  `settings.conf`, `resume.tsv` (position and tracks per file, the last languages, the last folder) and
  `history.tsv` (30 paths). `--state-dir` overrides it.
- **Panels:** the top panel is the `DSI-2` connector (`BANANA_TOP`, or `BP_TOP_CONNECTOR`; `BP_TOP_DISPLAY` /
  `BP_BOTTOM_DISPLAY` take indexes). The bottom touchscreen is found by its sysfs path, `fe5e0000.i2c` (`BANANA_TOUCH`
  or `BP_TOUCH`), because SDL can't tell the two touchscreens apart. This is how banana-ui does it.
- **Sleep:** bananad sends the process group `SIGSTOP` when the handheld sleeps and `SIGCONT` when it wakes. The player
  pauses on the lid's `SW_LID` event and again on `SIGCONT`, so opening the lid never resumes playback.
- **Bottom screen off:** `bl_power` 4/0 on the backlight that bananad names in `/run/banana/backlight-bottom`, with
  `/run/banana/panel-off` naming it so bananad keeps it dark after a wake. This is the same mechanism as BananaOS's
  `one-panel` launchers. A crash handler turns it back on.

## Build and run on a computer

macOS (Homebrew): `brew install cmake pkg-config mpv sdl2 sdl2_ttf`. Debian/Ubuntu:
`apt install cmake g++ pkg-config libmpv-dev libsdl2-dev libsdl2-ttf-dev`.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build                        # unit tests
./build/banana-player some-movie.mkv          # two windows: the top panel and the bottom panel
./build/banana-player                         # the file browser (your home folder)
BP_BROWSE=~/Videos:~/Music ./build/banana-player   # the browser's top list
./build/banana-player ~/Videos/Show           # the browser, in that folder
```

On a computer the mouse stands in for a finger. Keys: Enter (A), Esc/Backspace (B), the arrows, PgUp/PgDn or Q/E (L/R),
X, Y, S (START), Tab (SELECT).

Options: `--windowed`, `--aspect fit|fill|stretch`, `--lang eng,en`, `--state-dir DIR`, `--font TTF`, `-v` (mpv's
log), `--press LIST` (below).

### Scripted runs

`--press` runs steps 0.8 s apart, prints the state after each, then quits. Real keyboard, mouse and touch input is
ignored meanwhile. It works on the handheld over SSH too. The steps are:

- buttons: `A,B,X,Y,L,R,Up,Down,Left,Right,Start,Select,L3`;
- taps and drags on the bottom panel's 640×480: `tap:X:Y`, `dtap:X:Y` (a double-tap), `drag:X1:Y1:X2:Y2`;
- `info`, which prints the Media info rows;
- `page`, which prints the Info or About page as drawn.

```sh
./build/banana-player --press X,Down,A,tap:440:330,drag:24:162:320:162,info,B,B movie.mkv
# press 3: file=1/3 t=3.2 paused=0 speed=1x aspect=Fit audio=Japanese · AAC 5.1 · Surround sub=English · SRT screen=on
```

The state line can lag one step behind, because mpv applies changes asynchronously. A photo's line is
`photo=1/5 4000x3000 zoom=2.00 pan=-0.250,0.000 turn=90 fit thumb=560x420`. Steps wait for the file to open, 3 s at
most, so a file that never opens (a named pipe called `x.mkv`) can test the loading spinner.

### The README's pictures

`docs/make-screenshots.sh <library>` runs scripted scenes (`shot:NAME` captures both panels) on a demo library
(`Movies/`, `Music/<album>/` with `cover.jpg`) and stacks each pair into `docs/img/screen-*.png`. The screenshots use
Blender's *Sintel* trailer (CC BY 3.0). `packaging/icon/make-icon.py` draws the icon from its 32×32 grid, and
`src/logo.h` holds the same grid for the top panel's logo.

### Linux check

The evdev touch, lid and backlight code only builds on Linux. A quick arm64 check (the handheld's architecture) in
Docker:

```sh
docker run --rm -v "$PWD":/src:ro debian:trixie bash -c "apt-get update -qq && \
  apt-get install -y -qq g++ cmake make pkg-config libmpv-dev libsdl2-dev libsdl2-ttf-dev >/dev/null && \
  cmake -S /src -B /tmp/b -DCMAKE_CXX_FLAGS='-Wall -Wextra' && cmake --build /tmp/b -j && /tmp/b/core-test"
```

## Roadmap

- [x] Video on the top panel, controls on the bottom, by buttons and touch
- [x] Audio and subtitle tracks, subtitle style, Fit / Fill / Stretch, speed, live scrubbing
- [x] Playlist per folder, resume, remember options, file browser, play history, media info, about
- [x] Music: audio files, album art, visualizers
- [x] Pause on lid/sleep, bottom screen off, button hotkeys off
- [x] **Settings in groups:** Subtitles, Music, Photos, Remember, Files, Controls & screen.
- [x] **Photos:** JPG, PNG, WebP, GIF, BMP, TIFF, AVIF, shown by mpv (`image-display-duration=inf`, `video-zoom`,
      `video-pan-*`, `video-rotate`, a `scale` filter to at most 4096 pixels across), upright by their rotation
      metadata or, where FFmpeg leaves it (7.1), their EXIF orientation (`photo.cpp`). The pan area's thumbnail comes
      from `screenshot-raw video`, asked asynchronously. Photos step through photos only, stay out of Recent and keep
      no resume position.
  - [ ] Pinch to zoom: `touch.cpp` reads the first finger only. Does the RG DS's touchscreen report two?
  - [ ] Animated WebP: FFmpeg 7.1 may not decode its animation.
  - [ ] On the handheld: L3 as SDL's `LEFTSTICK`; decode time of a 12 MP JPEG; `dscale=hermite` with
        `correct-downscaling` on Panfrost.
- [x] **Loading spinner** on the top panel (from 0.3 s, till mpv's `playback-restart`), for any file.
- [ ] **On the handheld (needs the hardware):**
  - [ ] Does SDL2's KMSDRM give both panels as two displays, with a GLES window on one and a renderer on the other?
        If not, the bottom panel falls back to raw KMS, as banana-ui uses.
  - [ ] Touch: is the bottom touchscreen found by `fe5e0000.i2c`, and do its axes match the panel?
  - [ ] Backlight off/on through `/run/banana/backlight-bottom`, and dark again after a wake.
  - [ ] Lid: does the `SW_LID` pause land before bananad's `SIGSTOP`? If not, does a little audio play out at the wake?
  - [ ] Buttons: does SDL map the ADC joypad as a GameController, with A east?
  - [ ] CPU and dropped frames with software decoding: 480p/720p/1080p H.264, HEVC 8/10-bit, AV1.
  - [ ] Visualizer cost: on an M1 the player used 15–25% of a core with a visualizer and ~5% with album art. If it's
        too much on the RK3568, draw at 320×240 or 20 fps.
  - [ ] Sound on the speaker, headphones and Bluetooth.
- [ ] **App Store package:** bundle libmpv, FFmpeg and libass with their dependencies (SDL2, SDL2_ttf, ALSA and
      FreeType come with BananaOS), cross-built with BananaOS's Buildroot toolchain to match its glibc, plus a Store
      entry script for `run-store-app`. The catalog format (`store/catalog.json`) still needs reading.
- [x] **Software decoding, fast:** mpv's `fast` profile, `vd-lavc-fast`, `vd-lavc-skiploopfilter=nonref`,
      `framedrop=decoder+vo`.
- [ ] **Hardware decoding: blocked on BananaOS.** Its kernel fragment disables `CONFIG_MEDIA_SUPPORT`, so the
      RK3566's Hantro and rkvdec decoders aren't built and there's no `/dev/video*`. A draft request to build them as
      modules is in [docs/bananalinux-hw-decoding-request.md](docs/bananalinux-hw-decoding-request.md). Once they
      exist: FFmpeg with the v4l2-request hwaccel (Kwiboo's patches), mpv `hwdec=drm` (zero-copy through EGL
      dma-buf import) with `drm-copy` and software as fallbacks, and `modprobe hantro-vpu rockchip-vdec` in the
      launcher.
- [ ] **Sound that sometimes doesn't open:** when the sound device isn't free as a file starts, mpv plays silently;
      the player now reopens it (`ao-reload`, three tries, logged). Find what holds the device then.
- [ ] A `banana-perf` clock profile while playing.
- [ ] **Later:** an audio delay setting for Bluetooth, holding off idle sleep while playing, music with the lid closed
      (bananad would need to hold the lid's sleep), translations, CJK file names on the bottom panel.

## Licences

The code is [MIT](LICENSE) © tonywei92. The fonts are under the SIL OFL 1.1 (`assets/fonts/licenses/`): Pixelify Sans
and Fusion Pixel come from BananaLinux's `ui/assets/rnds/fonts`, and Inter from its `ui/assets/banana/fonts`. libmpv
is GPL-2.0+ by default and LGPL-2.1+ when built with `-Dgpl=false`. MIT is compatible with either, but the Store
package must ship the licence of each bundled library and the source offer the GPL requires.
