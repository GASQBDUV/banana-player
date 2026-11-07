<p align="center">
  <img src="docs/img/banner.png" width="100%" alt="Banana Player: video and music for BananaOS, on the Anbernic RG DS and RG DS Plus">
</p>

<p align="center">
  <b>Watch videos, play music and look at photos on your RG DS: the picture on top, the controls on the touch screen below.</b>
</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#the-basics">The basics</a> ·
  <a href="FEATURES.md">All features</a> ·
  <a href="#something-wrong">Help</a>
</p>

<table>
  <tr>
    <td align="center" width="33%"><img src="docs/img/screen-browser.png" alt="The file list on the bottom screen, the Banana Player logo on the top"><br><sub><b>Your files.</b> Folders, videos, music and photos, with where you left off</sub></td>
    <td align="center" width="33%"><img src="docs/img/screen-playing.png" alt="A film playing on the top screen, the controls on the bottom"><br><sub><b>Playing.</b> Seek, skip, speed and size, by button or touch</sub></td>
    <td align="center" width="33%"><img src="docs/img/screen-photo.png" alt="A photo zoomed in on the top screen, the pan area and its buttons on the bottom"><br><sub><b>Photos.</b> Zoom in and look around with the pan area</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/img/screen-music.png" alt="A song with its album art inside the Circle visualizer"><br><sub><b>Music,</b> with album art, a visualizer, or both (here: Circle)</sub></td>
    <td align="center"><img src="docs/img/screen-subs.png" alt="The subtitle list"><br><sub><b>Subtitles and audio tracks,</b> remembered per file</sub></td>
    <td align="center"><img src="docs/img/screen-settings.png" alt="Settings › Subtitles, with a subtitle preview"><br><sub><b>Settings</b> in groups, with a live subtitle preview</sub></td>
  </tr>
</table>

## Highlights

- 🎬 **Videos:** MP4, MKV, AVI and more, with subtitles and audio tracks you can switch.
- 🎵 **Music:** folders play as albums, with album art or a visualizer.
- 🖼️ **Photos:** zoom in and look around with your finger.
- 👆 **Made for two screens:** the picture on top, big touch controls below.
- 💾 **Picks up where you left off**, and lists what you played recently.
- 🔄 **Updates itself** when a new version is out.

## Install

> [!TIP]
> **Latest: [Banana Player 0.2.4](https://github.com/tonywei92/banana-player/releases/tag/v0.2.4)**, for BananaOS on the
> Anbernic RG DS and RG DS Plus.

1. **Download** `BananaPlayer-0.2.4-aarch64.zip` from [the release](https://github.com/tonywei92/banana-player/releases/tag/v0.2.4).
2. **Unzip it into `roms/ports`** on your SD card. You should end up with `Banana Player.sh`, a `BananaPlayer` folder
   and a `media` folder there. (On a Mac, if the zip opens into a folder of its own, move those three out of it.)
3. **Play:** put the card back in, open the **Ports** shelf and choose **Banana Player**.

Put your videos, music and photos anywhere on the card.

**Updates** come by themselves: with Wi-Fi on, Banana Player tells you when a new version is out and installs it for
you. Your settings and places are always kept.

## The basics

- **Open something:** tap it, or press **A**. Folders play as playlists.
- **Play / pause:** **A**. **L / R** go to the previous or next file.
- **Close it:** tap **<** at the top left, or press **B** twice.
- **Menu** (recent files, settings, quit): **START**.
- **Quick settings** (volume, brightness, quit): press **F** on its own.
- **Quit straight away:** **F + START**.

Every button has a touch equivalent on the bottom screen. All the controls, photos, music and settings are in
**[FEATURES.md](FEATURES.md)**.

## Something wrong?

Plug the SD card into a computer and attach `home/.config/banana-player/log.txt` to an
[issue](https://github.com/tonywei92/banana-player/issues). To uninstall, delete `Banana Player.sh`, the
`BananaPlayer` folder and `media/box/Banana Player.png` from `roms/ports`.

## Credits

For developers: see [DEVELOPMENT.md](DEVELOPMENT.md).


- Made by [tonywei92](https://github.com/tonywei92) for [BananaOS](https://github.com/JorreFog/BananaLinux) by
  JorreFog and the Banana contributors.
- Plays with [mpv](https://mpv.io) and [FFmpeg](https://ffmpeg.org). Drawn with [SDL](https://libsdl.org).
- Fonts: [Pixelify Sans](https://github.com/eifetx/Pixelify-Sans), [Inter](https://github.com/rsms/inter),
  [Fusion Pixel](https://github.com/TakWolf/fusion-pixel-font) and [Noto Sans](https://notofonts.github.io) Thai,
  Arabic, Hebrew and Devanagari, under the SIL Open Font License 1.1.
- The film in the screenshots is [Sintel](https://durian.blender.org), © copyright Blender Foundation |
  durian.blender.org, under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).

## Licence

[MIT](LICENSE) © tonywei92. You're free to use, change and share Banana Player; copies and forks must keep the
copyright notice and the licence.
