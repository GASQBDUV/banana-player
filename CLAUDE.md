# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Banana Player: a video and music player for BananaOS (github.com/JorreFog/BananaLinux) on the Anbernic RG DS / RG DS
Plus (RK3566, aarch64, two 4:3 panels: 640×480 or 1024×768; the bottom one is a touchscreen). C++17, libmpv, SDL2,
SDL2_ttf, zlib. `README.md` is for end users; `DEVELOPMENT.md` has the technical detail and the roadmap.

## Commands

```sh
cmake -S . -B build && cmake --build build -j       # desktop build (macOS: brew mpv sdl2 sdl2_ttf cmake pkg-config)
ctest --test-dir build                               # or ./build/core-test (all unit tests, one binary, no filter)
./build/banana-player [FILE|FOLDER]                  # two windows = the two panels; no file: the file browser
BP_BROWSE=dir1:dir2 ./build/banana-player            # the browser's top-level places
packaging/build-bundle.sh                            # aarch64 bundle for the handheld (Docker, arm64 Debian 12)
docs/make-screenshots.sh <library>                   # the README's docs/img/screen-*.png
```

- **Scripted runs** are the end-to-end test harness: `--press STEPS` runs comma-separated steps 0.8 s apart and prints
  the state after each, then quits; real keyboard/mouse/touch input is ignored meanwhile. Steps are button names
  (`A,B,X,Y,L,R,Up,Down,Left,Right,Start,Select`), `tap:X:Y`, `drag:X1:Y1:X2:Y2`, `hold:X:Y` (in the bottom panel's
  640×480 layout units), `info`, `page`, `prop:NAME` (any mpv property), `shot:NAME` (both panels to
  `$BP_SHOTS/NAME-{top,bottom}.png`), `screenshot`. Use `--state-dir` with a scratch dir so runs don't share
  settings/resume/history. The state line can lag one step behind (mpv is asynchronous).
- **Linux-only code** (evdev touch, lid switch, inotify, backlight) only compiles on Linux. Check it with the arm64 Docker
  command in DEVELOPMENT.md ("Linux check") after touching `touch.*`, `sleepwatch.*`, `levelwatch.*` or `panel.*`.
- `build-bundle.sh` caches FFmpeg/libplacebo/libass/mpv source builds in `.cache/bundle`; reruns rebuild only the
  player. It outputs `dist/ports/` (`Banana Player.sh`, `BananaPlayer/`, `media/box/Banana Player.png`), which goes into
  `roms/ports/` on the card. When copying to a card mounted on macOS, use `COPYFILE_DISABLE=1 cp -X` and `dot_clean`:
  a stray `._Banana Player.sh` would show up as a broken port.

## Architecture

- **`main.cpp`** owns SDL: the top window (GL/GLES context; mpv draws into it through its render API), the bottom
  window (an SDL_Renderer that `Ui` draws on), and the single event loop. The loop also handles browser vs playing
  mode (`leave()`), scripted steps, screenshots, periodic redraws, and the hand-off of events from background threads.
  Background threads (`SleepWatch`, `BottomTouch`, `LevelWatch`, mpv's callbacks) never touch UI state: they push SDL
  user events (`evMpv`, `evRender`, `evTouch`, `evLevel`), or for pause call libmpv directly, which is thread-safe.
- **`Player`** wraps libmpv and owns everything about playing: the playlist (the file's folder, `playlist.*`),
  per-file resume (`resume.tsv`, which also keeps the last audio/subtitle languages and the browser's last folder),
  play history (`history.tsv`, 30 entries), `settings.conf`, music display (`lavfi-complex` graphs: visualizer, art +
  visualizer, a tiny black picture for None; the art choices are decided at `FILE_LOADED`, from whether there's an
  album-art track) and Loop (one: `loop-file`; all: `advance()`), and the top-panel overlays drawn as
  ASS through `osd-overlay` (1: song title, 2: volume/brightness card, 3: idle logo). Track choices are applied by
  setting mpv's global options (`start`, `aid`, `sid`, `alang`, `slang`) before each `loadfile`. `lavfi-complex` must
  be cleared before setting `aid`/`sid`.
- **`Ui`** is the whole bottom panel: pages (Main, Audio, Subs, Settings, Browse, Info, About, Exit), START's tabbed
  menu (`inMenu`: Recent, Settings, About, Exit), the folder context menu, Lock, Screen off. Layout is in fixed
  640×480 logical units, scaled by `sx`/`sy`; text is rendered at the real panel size. It redraws only when
  `signature()` changes (or when forced), so any new state that affects drawing must be added to `signature()`.
  `press(Button)` and `pointer(Pointer)` return true for "leave", which `main` turns into closing the video (browser
  mode) or quitting.
- **Library code with no window** (`aspect`, `tracks`, `format`, `substyle`, `settings`, `resume`, `history`,
  `playlist`, `browser`, `png`) builds as `bpcore` and is covered by `tests/core_test.cpp` (a hand-rolled `CHECK_EQ`).
  Keep window/mpv-handle code out of these.
- **Settings** are an INI-ish `key = value` file. New switches go in `Settings` plus the `REMEMBER` table in
  `settings.cpp`. In `ui.cpp`, the `REMEMBER`/`REMEMBER_LABELS` arrays and the row constants (`SET_ROWS`,
  `TOGGLE_ROW`, `CLEAR_ROW`…) in `ui.h` must be kept in step with them.

## BananaOS integration (constraints, not discoverable from this repo)

- The player runs as a Port: BananaOS's `run-port` starts `Banana Player.sh` in its own session with
  `HOME=/data/home` and `SDL_KMSDRM_FIRST=DSI-2` (the top panel is SDL display 0), and darkens the bottom backlight.
  The launcher turns that back on. State lives in `$XDG_CONFIG_HOME/banana-player`; the log is
  `home/.config/banana-player/log.txt` on the card (overwritten every launch).
- Behaviour on BananaOS:
  - **Lid / sleep:** bananad SIGSTOPs the process group to sleep and SIGCONTs it on wake; the player pauses on
    `SW_LID` and on `SIGCONT`.
  - **Volume / brightness:** bananad handles the keys and writes the new level to `/run/banana/osd`.
  - **Bottom backlight:** `bl_power` on the path in `/run/banana/backlight-bottom`, with `/run/banana/panel-off` so
    bananad keeps it dark after a wake.
  - **Bottom touchscreen:** found by its sysfs path (`fe5e0000.i2c`), because SDL can't tell the two touchscreens
    apart.
- SDL's KMSDRM drops the first presented frames, so both panels are redrawn repeatedly after start and wake.
- Bundling: libmpv, FFmpeg, libplacebo, libass, harfbuzz and fribidi are bundled. glibc, libstdc++, SDL2,
  SDL2_ttf, Mesa, libdrm, ALSA, FreeType and zlib come from BananaOS and must not be bundled: BananaOS's SDL is
  patched, and its Mesa needs its own libstdc++. BananaOS has no fontconfig, so subtitle and OSD fonts come from
  `assets/fonts` (`sub-fonts-dir`/`osd-fonts-dir`).
- Decoding is software only: BananaOS's kernel disables `CONFIG_MEDIA_SUPPORT` (see
  `docs/bananalinux-hw-decoding-request.md`).
- The panels use the Nintendo button layout: A is the east button, which is `SDL_CONTROLLER_BUTTON_B`.

## Conventions

- Code style follows BananaLinux's: tabs, and comments that say why rather than what.
- Fonts: SDL_ttf (bottom panel) has no fallback, so `Ui::runs()` picks a font per text or per run of characters.
  libass (subtitles, titles) is built with fontconfig, and the bundle's `fonts.conf` lists only the player's own fonts.
  `BP_FONT_PROVIDER=none` shows libass the way it would be without that.
- The icon's 32×32 grid lives in both `packaging/icon/make-icon.py` and `src/logo.h`; change them together.
- Screenshots and the README use Blender's *Sintel* (CC BY 3.0, credited in the README). Don't use copyrighted
  material.
