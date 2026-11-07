# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Banana Player: a video and music player for BananaOS (github.com/JorreFog/BananaLinux) on the Anbernic RG DS / RG DS
Plus (RK3568, aarch64, two 4:3 panels: 640×480 or 1024×768; the bottom one is a touchscreen). C++17, libmpv, SDL2,
SDL2_ttf, zlib. `README.md` is for end users; `DEVELOPMENT.md` has the technical detail and the roadmap.

## Commands

```sh
cmake -S . -B build && cmake --build build -j       # desktop build (macOS: brew mpv sdl2 sdl2_ttf cmake pkg-config)
ctest --test-dir build                               # or ./build/core-test (all unit tests, one binary, no filter)
./build/banana-player [FILE|FOLDER]                  # two windows = the two panels; no file: the file browser
BP_BROWSE=dir1:dir2 ./build/banana-player            # the browser's top-level places
packaging/build-bundle.sh                            # aarch64 bundle for the handheld (Docker, arm64 Debian 12)
docs/make-screenshots.sh <library>                   # the README's docs/img/screen-*.png (library layout: the script's header)
```

- **Scripted runs** are the end-to-end test harness: `--press STEPS` runs comma-separated steps 0.8 s apart and prints
  the state after each, then quits; real keyboard/mouse/touch input is ignored meanwhile. Steps are button names
  (`A,B,X,Y,L,R,Up,Down,Left,Right,Start,Select,L3`), `tap:X:Y`, `dtap:X:Y`, `drag:X1:Y1:X2:Y2`, `hold:X:Y` (in the
  bottom panel's 640×480 layout units), `info`, `page`, `rows` (the browser's rows, in view or not), `prop:NAME` (any mpv property),
  `shot:NAME` (both panels to `$BP_SHOTS/NAME-{top,bottom}.png`), `screenshot`. Use `--state-dir` with a scratch dir
  so runs don't share settings/resume/history. The state line can lag a step behind (mpv is asynchronous; the
  harness doesn't wait for it, as a loop that stops drawing holds mpv up). Steps wait for the file to open, 3 s at
  most. The log's `timing: <file> started in N ms` lines give how long each file took to start, on the handheld too.
- **Test variables** that stand in for the handheld: `BP_POWER_SUPPLY` (a fake `/sys/class/power_supply`),
  `BP_OSD_FILE` (bananad's level file), `BP_FONT_PROVIDER=none` (libass with no system fonts, as on BananaOS),
  `BP_SCREENSHOTS`.
- **Linux-only code** (evdev touch, lid switch, inotify, backlight) only compiles on Linux. Check it with the arm64 Docker
  command in DEVELOPMENT.md ("Linux check") after touching `touch.*`, `sleepwatch.*`, `levelwatch.*` or `panel.*`.
- `build-bundle.sh` caches FFmpeg/libplacebo/libass/mpv source builds in `.cache/bundle`; reruns rebuild only the
  player. It outputs `dist/ports/` and `dist/BananaPlayer-<version>-aarch64.zip`, holding `BananaPlayer/` (the whole
  package: `launch.sh`, `VERSION`, `bin/`, `lib/`, `share/`), `Banana Player.sh` (the Ports shelf's two-line wrapper
  for `BananaPlayer/launch.sh`) and `media/box/Banana Player.png`. The card is mounted at `/Volumes/BANANA`. Copy them
  into `roms/ports/` with `COPYFILE_DISABLE=1 cp -X` and `dot_clean`: a stray `._Banana Player.sh` would show up as a
  broken port.
- **Releasing:**
  1. Bump `project(... VERSION x.y.z)` in `CMakeLists.txt` (the bundle's `VERSION` comes from it), and the README's
     install links (`releases/tag/vx.y.z`, the zip's name).
  2. Commit and push, then run `build-bundle.sh` and `shasum -a 256` the zip into `<zip>.sha256`.
  3. `gh release create vx.y.z --target main --latest <zip> <zip>.sha256`, then prepend "Superseded by…" to the
     previous release's notes.
  4. The BananaOS Store follows these releases: asset `BananaPlayer-{version}-aarch64.zip`, tag prefix `v`. Its entry,
     32×32 icon, 320×240 screenshot and PR text are in `packaging/store/`; the PR to JorreFog/BananaLinux isn't
     opened yet.

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
  ASS through `osd-overlay` (1: song title, 2: volume/brightness card, 3: idle logo). A file's own choices (`start`, `aid`,
  `sid`) go as `loadfile`'s per-file options (`loadfile <path> replace -1 start=…,aid=…,sid=…`), never as properties
  set before it: a property reaches the file still playing, and an `aid` it doesn't have turns its sound off for good,
  so the next file opened silent (a remembered 3rd audio track, Next from a file with 2). `alang`/`slang` stay
  global. `checkAudio()` picks the track again when a file has audio and none is chosen (mpv drops it when the sound
  device won't open). `lavfi-complex` must
  be cleared before setting `aid`/`sid`; taking a graph off leaves no audio track chosen (restore `aid`), and `vid`
  has to be set by id. A graph's picture never ends, so mpv never ends a song that has one (every song without art):
  `checkSongEnd()` ends it, or restarts it for Loop one, once the clock passes the duration. Circle's graph reads
  two `remap` tables with `movie=` from the state folder (`graphFor()` writes them first; without them it's Bars).
  Keep visualizers cheap: Circle was shaped by timing `ffmpeg -filter_complex_threads 1` against Bars per frame.
  A graph's still inputs must be single frames, never `loop=-1`: an endless source floods mpv past the song's end
  (its clock stops, so the song never ends, and memory grows by gigabytes; on the handheld, a freeze). Test such a
  graph in mpv with a memory watchdog: the ffmpeg CLI keeps running endless sources even after the output ends.
- **Photos** play through mpv too (`isPhoto`): `PhotoView` (zoom over Fit or Fill, pan in mpv's `video-pan` units,
  a user turn) is applied as `video-zoom`/`video-pan-*`/`video-rotate`; the geometry is in `photo.cpp`. Fill is done
  by zoom, not `panscan`. Everything set for a photo at `load()` (vf, dscale, zoom/pan/rotate) is reset for a video.
  `video-params` is before the `vf` scale and before rotation. The thumbnail is `screenshot-raw video`, unrotated.
  Photos have their own playlist (`siblingVideos` splits photos from videos/songs), no history and no resume.
- **Never wait for mpv on the main thread while a file opens.** As it switches files mpv waits for the main thread
  to draw the top panel (`mpv_render_context_render`); a blocking call meanwhile (`mpv_set_property`,
  `mpv_command`, `mpv_get_property`) holds both until mpv gives up after 200 ms ("not being called or stuck" in
  `-v`), several times a file: Next took 2-3 s on the handheld. So `prop()` and commands are async, the track list
  and tags come by `observe_property` (`MPV_FORMAT_NODE`), `fileReady()` waits for the track list rather than read
  it, and `graphSet` remembers `lavfi-complex`. Reads (`get`) only once a file has started (`checkPhoto`), or on
  the Info page. mpv's own Lua scripts are off: their per-file hooks stalled every open too. A song keeps the last
  one's graph when it's the same (taking it off and on reopens the sound output).
- **Loading:** `loading()` is true from `load()` till mpv's `PLAYBACK_RESTART`; the stale `video-params` and
  `video-out-params` of the last file (or the idle window) can't tell.
- **`Ui`** is the whole bottom panel: pages (Main, Audio, Subs, Settings, Browse, Info, About, Exit), START's tabbed
  menu (`inMenu`: Recent, Settings, About, Exit), the folder context menu, Lock, Screen off. Main over a photo is
  `drawPhoto` (the pan area and `photoBtn`s, ids from `PHOTO_BTN`; `photoPointer`, a flick's glide in `tick()`; a
  drag moves the photo or, `dragBox`, the box). `BACK_ICON` ("<", top left of the browser and of Main) is B by touch, except
  over what plays, where one tap closes it;
  the browser has no other Back. `dropTouch()` (on `showPlayer`/`showBrowser`) swallows only a finger still down:
  swallowing the next touch regardless ate the first tap after opening a file. Settings opens on groups (`setGroup`
  -1); `settingRows()` lists a group's row ids. Layout is in fixed 640×480 logical units, scaled by `sx`/`sy`; text
  is rendered at the real panel size. It redraws only when
  `signature()` changes (or when forced), so any new state that affects drawing must be added to `signature()`.
  `press(Button)` and `pointer(Pointer)` return true for "leave", which `main` turns into closing the video (browser
  mode) or quitting.
- **Updater** (`updater.*`, Ports installs; a Store install, under `/data/store`, is the Store's): a thread runs
  BananaOS's `curl` against GitHub's `/releases/latest` (`update.cpp` parses it), downloads the zip and its
  `.sha256`, checks the hash (SHA-256 in `update.cpp`), unpacks it (`unzip`, else `python3 -m zipfile`) and renames it
  to `BananaPlayer.update` beside the package once whole. `launch.sh` swaps it in before the player starts (the old
  folder kept as `BananaPlayer.old`) and starts again when the player exits with 75 ("Restart now"). The pop-up is
  `Ui::popupShown` over the browser; "Not until tomorrow" is `updateRemindAfter` in settings.conf. Tests stand in
  with `BP_PACKAGE_DIR` (the installed folder) and `BP_UPDATE_URL` (a `file://` answer whose asset URLs are
  `file://` too); the `update` step prints its state. The Updater is never destroyed, so a download doesn't hold up
  quitting.
- **Library code with no window** (`aspect`, `tracks`, `format`, `substyle`, `settings`, `resume`, `history`,
  `playlist`, `browser`, `png`, `battery`, `photo`, `update`) builds as `bpcore` and is covered by `tests/core_test.cpp` (a hand-rolled `CHECK_EQ`).
  Keep window/mpv-handle code out of these.
- **Settings** are an INI-ish `key = value` file. New switches go in `Settings` plus the `REMEMBER` table in
  `settings.cpp`. In `ui.cpp`, the `REMEMBER`/`REMEMBER_LABELS` arrays, `TOGGLES` and the row ids (`TOGGLE_ROW`,
  `PAN_ROW`, `DRAG_ROW`, `CLEAR_ROW`, `CHECK_ROW`, `INSTALL_ROW`…) in `ui.h` must be kept in step with them, and the row placed in a group in `settingRows()`.

## BananaOS integration (constraints, not discoverable from this repo)

- The player runs as a Port (`run-port` runs `Banana Player.sh`) or a Store app (`run-store-app` runs `launch.sh`).
  Both give it its own session, `HOME=/data/home` and `SDL_KMSDRM_FIRST=DSI-2` (the top panel is SDL display 0).
  `run-port` also darkens the bottom backlight, and `launch.sh` turns it back on. State lives in `$XDG_CONFIG_HOME/banana-player`; the log is
  `home/.config/banana-player/log.txt` on the card (the run before it: `log.prev.txt`).
- Behaviour on BananaOS:
  - **Lid / sleep:** bananad SIGSTOPs the process group to sleep and SIGCONTs it on wake; the player pauses on
    `SW_LID` and on `SIGCONT`.
  - **Volume / brightness:** bananad handles the keys and writes the new level to `/run/banana/osd`.
  - **Bottom backlight:** `bl_power` on the path in `/run/banana/backlight-bottom`, with `/run/banana/panel-off` so
    bananad keeps it dark after a wake.
  - **Bottom touchscreen:** found by its sysfs path (`fe5e0000.i2c`), because SDL can't tell the two touchscreens
    apart.
  - **Battery:** `/sys/class/power_supply`, read as BananaOS's menu does. A charger that's online counts as charging,
    because the rk817 gauge says "Discharging" on the cable.
  - **Sound:** there's no sound server, so ALSA's plug does the channel mapping. Mono went to the left speaker only,
    and 5.1 lost its centre channel, hence `audio-channels=stereo`.
- SDL's KMSDRM drops the first presented frames, so both panels are redrawn repeatedly after start and wake.
- Bundling: libmpv, FFmpeg, libplacebo, libass, fontconfig, expat, harfbuzz and fribidi are bundled. glibc,
  libstdc++, SDL2, SDL2_ttf, Mesa, libdrm, ALSA, FreeType and zlib come from BananaOS and must not be bundled: BananaOS's
  SDL is patched, and its Mesa needs its own libstdc++. BananaOS has no fonts of its own. The launcher's
  `FONTCONFIG_FILE` points at the bundle's `fonts.conf`, which lists only `assets/fonts` (also set as
  `sub-fonts-dir`/`osd-fonts-dir`, with `osd-font=Inter`).
- Decoding is software only: BananaOS's kernel disables `CONFIG_MEDIA_SUPPORT`. Requested as modules in
  JorreFog/BananaLinux#208 (text: `docs/bananalinux-hw-decoding-request.md`). The SoC is an RK3568 (BananaOS's
  `rk3568-anbernic-rg-ds*.dts`), not an RK3566.
- The panels use the Nintendo button layout: A is the east button, which is `SDL_CONTROLLER_BUTTON_B`. The RG DS Plus
  has one stick, the left, and it clicks (L3, `SDL_CONTROLLER_BUTTON_LEFTSTICK`).
- F is SDL's `GUIDE` button and L2 a trigger axis (handled in `main.cpp`, not `Button`): F + L2 screenshot, F + START
  quits. The Back key is mapped to B as `SDLK_AC_BACK` (unconfirmed on the
  handheld); every button press is logged as `input: ...` to find what a key sends.
- Photos are scaled to 4096 pixels at most: the RK3568's GPU (Mali-G52, Panfrost) may take no texture over 8192
  (unverified), and a 50 MP photo's would be 200 MB.

## Conventions

- Code style follows BananaLinux's: tabs, and comments that say why rather than what.
- Fonts: SDL_ttf (bottom panel) has no fallback, so `Ui::runs()` picks a font per text or per run of characters.
  libass (subtitles, titles) is built with fontconfig, and the bundle's `fonts.conf` lists only the player's own fonts.
  `BP_FONT_PROVIDER=none` shows libass the way it would be without that.
- The icon's 32×32 grid lives in both `packaging/icon/make-icon.py` and `src/logo.h`; change them together.
- Screenshots and the README use Blender's *Sintel* (CC BY 3.0, credited in the README); the photo screenshot is a
  still cropped from its trailer. Don't use copyrighted material.
