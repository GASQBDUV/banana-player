# CLAUDE.md

Banana Player: a video and music player for BananaOS (github.com/JorreFog/BananaLinux) on the Anbernic RG DS / RG DS
Plus (RK3568, aarch64, two 4:3 panels: 640×480 or 1024×768; the bottom one is a touchscreen). C++17, libmpv, SDL2,
SDL2_ttf, zlib. `README.md` is the end-user page; every feature, control and setting is in `FEATURES.md` (a new one goes
there); `DEVELOPMENT.md` has the technical detail and roadmap; **`docs/architecture.md` is the long form of the rules
below** (read the part you touch); investigations are in `docs/notes/` (an agent finishing one adds a note there).

## Commands

```sh
cmake -S . -B build && cmake --build build -j       # desktop build (macOS: brew mpv sdl2 sdl2_ttf cmake pkg-config)
./build/core-test                                    # all unit tests, one binary, no filter (also ctest --test-dir build)
./build/banana-player [FILE|FOLDER]                  # two windows = the two panels; no file: the file browser
BP_BROWSE=dir1:dir2 ./build/banana-player            # the browser's top-level places
packaging/build-bundle.sh [--quick|--clean]          # aarch64 bundle (Docker arm64 Debian 12; libs kept in .cache/bundle: ~2 s when nothing changed; --quick: no zip)
docs/make-screenshots.sh <library>                   # the README's docs/img/screen-*.png (library layout: the script's header)
python3 tools/web-mock.py [port]                     # the web app without the player (http://127.0.0.1:8099/)
tools/web-smoke.sh                                   # the web dashboard end to end with curl (needs a player with BP_WEB_FORCE=1)
tools/make-sprite.py                                 # regenerates web/sprite.svg from the G_* glyphs in src/ui_glyphs.h
```

- **Scripted runs** are the end-to-end harness: `--press STEPS` runs comma-separated steps 0.8 s apart, prints the state
  after each, then quits; real input is ignored meanwhile. Steps: button names (`A,B,X,Y,L,R,L2,R2,Up,Down,Left,Right,
  Start,Select,L3`; `R*15`: that button N times, 80 ms apart; `A~700`: held 700 ms, then let go), `tap:X:Y`, `dtap:X:Y`, `drag:X1:Y1:X2:Y2`, `hold:X:Y` (bottom panel's 640×480 units), `info`, `page`,
  `rows` (the browser's rows; on Home the Recent cards, `card …`, then tiles, `tile …`, `>` on the focus), `prop:NAME`
  (any mpv property), `shot:NAME` (both panels to `$BP_SHOTS/NAME-{top,bottom}.png`), `screenshot`, `web` (URL, PIN,
  clients). Use `--state-dir` with a scratch dir. The state line can lag a step behind (mpv is asynchronous; the harness
  doesn't wait for it). Steps wait for the file to open, 3 s at most. `timing: <file> started in N ms` log lines give
  start times, on the handheld too.
- **How much to test** (the owner finds the real bugs on the handheld and in Safari, which headless runs miss): every
  change: build, `core-test`, and one check aimed at the change; before a card copy: `tools/web-smoke.sh`, one
  phone-size browser run (WebKit for the browser player) and one scripted handheld run, then copy the card first and let
  the owner test while anything longer runs; the full multi-size browser suite only before a release, in the background.
  Subagents don't run long suites unless asked. When a bug can't be pinned from `log.txt`, add logging that would.
- **Test variables:** `BP_POWER_SUPPLY` (fake `/sys/class/power_supply`), `BP_OSD_FILE` (bananad's level file),
  `BP_FONT_PROVIDER=none` (libass with no system fonts, as on BananaOS), `BP_SCREENSHOTS`, `BP_BOTTOM_SIZE=1024x768`,
  `BP_AO_PCM=FILE` (sound out as raw 44.1 kHz s16 stereo, untimed), `BP_CRASH_TEST=segv|abort|throw|overflow` (crashes
  1.5 s after start), `BP_BACKLIGHT_DIR`/`BP_BACKLIGHT_STORE`, `BP_VOLUME_SCRIPT`, `BP_PACKAGE_DIR`/`BP_UPDATE_URL`;
  web: `BP_WEB_FORCE=1`, `BP_WEB_PIN`, `BP_WEB_PORT`, `BP_WEB_DIR` (serve `web/` from disk, not the embedded copy),
  `BP_WEB_IP` (`none` = no Wi-Fi), `BP_WEB_FAIL`, `BP_WEB_KEEPALIVE`. Crash log: `<state>/crash.log` (DEVELOPMENT.md).
- **Linux-only code** (evdev touch, lid, inotify, backlight) compiles only on Linux: after touching `touch.*`,
  `sleepwatch.*`, `levelwatch.*`, `panel.*` (or Ui's includes) run the arm64 Docker check in DEVELOPMENT.md ("Linux check").
- **Bundle:** `build-bundle.sh` caches source builds in `.cache/bundle` and outputs `dist/ports/` and
  `dist/BananaPlayer-<version>-aarch64.zip` (`BananaPlayer/`, `Banana Player.sh`, `media/box/Banana Player.png`). The card
  is `/Volumes/BANANA`; copy into `roms/ports/` with `COPYFILE_DISABLE=1 cp -X` and `dot_clean` (a stray
  `._Banana Player.sh` shows up as a broken port).
- **Releasing:** (1) bump `project(... VERSION x.y.z)` in `CMakeLists.txt`, the README's install links
  (`releases/tag/vx.y.z`, the zip's name) and the `?v=x.y.z` on its screenshot links (images are cached by URL);
  regenerate the pictures first if a pictured screen changed (Sintel library; the Network shot needs the Docker Samba and
  FTP servers). (2) Commit, push, `build-bundle.sh`, `shasum -a 256` the zip into `<zip>.sha256`. (3)
  `gh release create vx.y.z --target main --latest <zip> <zip>.sha256`, then prepend "Superseded by…" to the previous
  release's notes. (4) The BananaOS Store follows the releases (asset `BananaPlayer-{version}-aarch64.zip`, tag prefix
  `v`; entry and PR text in `packaging/store/`; the PR to JorreFog/BananaLinux isn't opened).

## Where things live

- `src/main.cpp`: SDL, both windows, the one event loop (browser vs playing, scripted steps, web calls). `src/player.cpp`:
  libmpv, playlist, resume/history/settings, graphs, ASS overlays. Library code with no window is `bpcore`
  (tests in `tests/core_test.cpp`): keep window/mpv-handle code out of it.
- **Ui** (class in `src/ui.h`; shared names in `src/ui_internal.h`, glyphs in `src/ui_glyphs.h`): `ui.cpp` core (init,
  `press`, `pointer`, `act`, `tick`, `signature`), `ui_draw.cpp` primitives and page pieces, `ui_main.cpp` playing screen
  and photos, `ui_home.cpp` Home, `ui_browse.cpp` the file list and sections, `ui_forms.cpp` connection form and keyboard,
  `ui_playlists.cpp` playlists/select/picker, `ui_settings.cpp`, `ui_menus.cpp` (START, quick settings, popups, Info,
  Like), `ui_sound.cpp` (Display, Sound), `ui_web.cpp`. A table of what is in each: `docs/architecture.md`.
- Web: `src/web/` (server, API), `web/app.js` (entry) + `web/js/*.js` (ES modules, no build step), `web/style.css`,
  `web/vendor/`; `cmake/EmbedWeb.cmake` embeds every file under `web/` into the binary.
- Network: `net*.cpp`, `netstore`, `networker`, `netstream`, `discovery`. Updater: `updater.*`, `update.cpp`.

## Hard rules (the reasons are in docs/architecture.md)

- **Never wait for mpv on the main thread while a file opens** (no `mpv_set_property`/`mpv_command`/`mpv_get_property`
  there: mpv waits for the top panel's draw and gives up after 200 ms, several times a file). `prop()` and commands are
  async; the track list, tags and chapters come by `observe_property`; `fileReady()` waits for the track list; read
  (`get`) only once a file has started or on the Info page. mpv's own Lua scripts stay off.
- **A file's own choices (`start`, `aid`, `sid`) go as `loadfile`'s per-file options**, never as properties set before it
  (an `aid` the next file lacks silences it for good). `alang`/`slang` stay global. `checkAudio()` re-picks a lost track.
  `loading()` is true from `load()` till `PLAYBACK_RESTART`; stale `video-params` can't tell.
- **Music graphs** (`lavfi-complex`): clear before setting `aid`/`sid`; taking a graph off leaves no audio (restore
  `aid`); set `vid` by id. A graph's picture never ends, so `checkSongEnd()` ends songs. A graph's still inputs are single
  frames, never `loop=-1` (floods mpv; memory grows, the handheld freezes); test with a memory watchdog. Every visual
  branch starts with `aformat=sample_rates=44100|48000`, makes ≤30 frames/s, and converts no frame (no per-frame
  `format=` but `rgb0`, no `colorkey`). Keep them cheap: time a new graph on the handheld if you can, and against
  Art+viz Bars (~0.85 ms/frame on the Mac, ~4 on the handheld). Circle reads `remap` tables written by `graphFor()`.
- **Equalizer and effect** are one `af` after the graph: `@bpin` (to 48 kHz, always), `@bpeq`, `@bpfx`, built by
  `syncEq`/`fxPart`. Band edits go by `af-command`; anything that rebuilds the chain needs `syncEq(true)` first.
- **Ui redraws only when `signature()` changes:** any new state that affects drawing goes into it. Layout is fixed
  640×480 units scaled by `sx`/`sy`. Colours and radii come from `src/theme.h`; a new box goes through `fillRound`/
  `drawBox`/`drawLevel`/`fillRoundBand`, not `fill`. A new path check goes through `pseudoDir()` (PLROOT, PLDIR, FORMDIR,
  FINDNET are pseudo-dirs). `dropTouch()` swallows only a finger still down.
- **Settings:** a new switch goes in `Settings` plus the `REMEMBER` table in `settings.cpp`; in `src/ui_internal.h` keep
  `REMEMBER`/`REMEMBER_LABELS`, `TOGGLES` and the row ids (`TOGGLE_ROW`, `PAN_ROW`, … in `ui.h`) in step, and place the row
  in a group in `settingRows()` (+ `settingHelp()`).
- **Threads:** background threads (`SleepWatch`, `BottomTouch`, `LevelWatch`, mpv callbacks, web server, `NetWorker`)
  never touch `Player`/`Ui`: they push SDL user events (`evMpv`, `evRender`, `evTouch`, `evLevel`, `evNet`, `evWeb`).
  Never block on a server in a destructor. The Updater and `WebServer` are never destroyed.
- **Playlists:** Liked (`.liked.m3u`) is built in: only the heart adds to it; every other edit path refuses it
  (`isBuiltinFile`). Edits are debounced 1 s and flushed on leaving and quit. Photos have their own playlist, no
  history, no resume; they play through mpv (scaled to 4096 px at most).
- **Network:** a place is `bpnet://<server-id>/<path>`, never with credentials; passwords sealed by `secret.*`; libsmb2
  async calls only; names resolved at each connect (no DNS/mDNS on BananaOS).
- **Web security gate** (`server.cpp`, before routing): private remote address, Host an IP literal or localhost, `X-BP: 1`
  and matching Origin on non-GET, `bp_s` session cookie on every `/api/*` but login. Paths are virtual
  `<rootIndex>/<rel>` via `resolveVirtual`; the deny list holds the state dir (`servers.key`), `/data/home`,
  `/data/banana`, `/data/store`. Server threads reach the player only through `WebServer::call` (`evWeb`; async calls).
  The web app is plain ES modules: export a name where it is declared, assign a `let` only in its own module, and keep
  `store.js` free of app imports. `?debug` exposes `window.__bp` (tests use it). The glyph sprite is generated
  (`tools/make-sprite.py`); web-only glyphs are inline in `index.html`. `web/style.css` copies `theme.h` by hand.
- **Playing in the browser:** the paused copy on the handheld is never given the page's position; its first play there
  does `ao-reload`; iPhone/iPad play in the page is off (`NO_HERE`).
- **Tracks in the browser** (docs/notes/2026-11-11-browser-tracks.md): `/api/subs` (WebVTT), `/api/ass` (the whole ASS
  script for JASSUB, vendored in `web/vendor/jassub/`, loaded only when a styled track is chosen) and `/api/fonts` +
  `/api/font` (Matroska font attachments) in `subs.*`; a non-default audio track is a Direct Stream (`remux.*`:
  `/api/remux-start` → `{start}`, `/api/remux` fMP4 from that keyframe, one per tab). The page's clock is then
  `B.remux.base + currentTime`: use `bPos()`/`seekTo()`, never `el.currentTime`. The bundle's FFmpeg needs the mp4 muxer.
- **Leaving is not stopping** (docs/notes/2026-11-10-background-playback.md): `press`/`pointer` returning true is
  `main`'s `leave()`, a screen change (what plays goes on behind the browser, `Ui::bgPlay`); stopping is
  `stopPlaying()` (`takeStopPressed`, `takeStopRequest`, the end of the list), which changes screens only when
  `Ui::playerShown()`. "Back to Main" goes through `home()`/`showPlayer()`, never `p.playing()` alone. Any Stop clears
  the browser hand-off (`Ui::stopped`) without telling the page; the page announces only to a snapshot with a path.
  A photo opened over a song parks it (`Ui::park`); the mini bar (`drawMiniBar`, `miniBarShown`) offers it back.
- **Fonts:** SDL_ttf has no fallback (`Ui::runs()` picks per run); libass uses the bundle's `fonts.conf` (player's fonts
  only). The icon's 32×32 grid lives in both `packaging/icon/make-icon.py` and `src/logo.h`: change both.
- **Code style:** tabs; comments say why, not what. Screenshots/README use Blender's *Sintel* (CC BY 3.0); no
  copyrighted material.

## BananaOS integration (constraints, not discoverable from this repo)

- Runs as a Port (`run-port` runs `Banana Player.sh`) or Store app (`run-store-app` runs `launch.sh`), in its own
  session, `HOME=/data/home`, `SDL_KMSDRM_FIRST=DSI-2` (the top panel is SDL display 0). `run-port` darkens the bottom
  backlight; `launch.sh` turns it on. State: `$XDG_CONFIG_HOME/banana-player`; log: `home/.config/banana-player/log.txt`
  on the card (`log.prev.txt` the run before).
- **Lid / sleep:** bananad SIGSTOPs the process group and SIGCONTs on wake; pause on `SW_LID` and `SIGCONT`.
  **Volume/brightness keys:** bananad writes the level to `/run/banana/osd`.
  **Bottom backlight:** `bl_power` at the path in `/run/banana/backlight-bottom`, plus `/run/banana/panel-off`.
  **Bottom touchscreen:** found by sysfs path `fe5e0000.i2c` (SDL can't tell them apart).
  **Brightness:** `backlight1` top, `backlight0` bottom, level = thousandths of `max_brightness` into sysfs, both levels to
  `/data/banana/backlight` ("<top> <bottom>"); no other interface.
  **Volume:** only `/usr/libexec/banana/volume get | set N`, then SIGHUP to bananad; never amixer. `volume.cpp` sets it
  on a thread, latest value only.
  **F** is shared with bananad (F + volume = brightness): quick settings open as F lets go only if nothing came between.
  **Battery:** `/sys/class/power_supply`; an online charger counts as charging (the rk817 gauge says "Discharging").
  **Sound:** no sound server; ALSA's plug maps channels, hence `audio-channels=stereo`.
- SDL's KMSDRM drops the first presented frames: both panels are redrawn repeatedly after start and wake.
- Bundling: libmpv, FFmpeg, libplacebo, libass, fontconfig, expat, harfbuzz, fribidi are bundled. glibc, libstdc++, SDL2,
  SDL2_ttf, Mesa, libdrm, ALSA, FreeType, zlib come from BananaOS and must not be (patched SDL; Mesa needs its libstdc++).
  No fonts on BananaOS: `FONTCONFIG_FILE` points at the bundle's `fonts.conf` (only `assets/fonts`, also
  `sub-fonts-dir`/`osd-fonts-dir`, `osd-font=Inter`).
- Decoding is software only (kernel has no `CONFIG_MEDIA_SUPPORT`; JorreFog/BananaLinux#208, text in
  `docs/bananalinux-hw-decoding-request.md`). The SoC is an RK3568, not RK3566.
- Nintendo layout: A is the east button, `SDL_CONTROLLER_BUTTON_B`. The RG DS Plus has one stick, the left, which clicks
  (L3, `SDL_CONTROLLER_BUTTON_LEFTSTICK`). F is SDL's `GUIDE`; `main.cpp` turns both trigger axes into `L2`/`R2` unless F
  is held (F + L2 screenshot, F + START quits). Back key = B as `SDLK_AC_BACK` (unconfirmed); each press logs `input: ...`.
- The RK3568's Mali-G52 may take no texture over 8192 (unverified), hence photos ≤ 4096 px.
