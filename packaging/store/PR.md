Store: add Banana Player

Hi! This adds [Banana Player](https://github.com/tonywei92/banana-player) to the Store: a video and music player for
the RG DS and RG DS Plus. The picture plays on the top screen, and the controls are on the bottom touch screen.

**What's added:**
- the entry in `store/catalog.json`;
- `store/apps/bananaplayer/icon.png` (32×32) and `screen.png` (320×240).

**How the entry maps to the package:** it follows the releases of tonywei92/banana-player, asset
`BananaPlayer-{version}-aarch64.zip` with tag prefix `v`. The zip's `BananaPlayer/` folder is the package:
- `launch.sh` is `menu.launch`;
- `VERSION` is `package.version_file`;
- `bin/banana-player` is `check`.

The package also carries `Banana Player.sh` and `media/box/` beside that folder, so the same zip installs on the
Ports shelf by hand.

**How it fits BananaOS:**
- It runs under `run-store-app` with both panels on, and puts the top panel first through `SDL_KMSDRM_FIRST`.
- It finds the bottom touchscreen by its device path (`fe5e0000.i2c`), the way banana-ui does.
- It pauses on `SW_LID` and on bananad's `SIGCONT`.
- It shows bananad's `/run/banana/osd` levels.
- It switches the bottom backlight off and on the way the one-panel launchers do (`/run/banana/panel-off`).
- Settings and resume positions live in `$XDG_CONFIG_HOME/banana-player`, so nothing needs to be kept across
  updates.

**Bundled with it:** libmpv, FFmpeg, libplacebo, libass, fontconfig, expat, harfbuzz and fribidi, built in arm64
Debian 12 (glibc 2.36). It uses BananaOS's own SDL2, SDL2_ttf, Mesa, ALSA and FreeType.

**Tested on:** an RG DS Plus, starting from the Ports shelf. The Store install itself (`run-store-app`) is untested on
the handheld, but on arm64 Linux `launch.sh` starts the player from the installed folder. I haven't run
`store/test-banana.py`, because I don't have the patched `rocknixds-store`.

Happy to change anything, such as the genre, accent, wording or package layout.
