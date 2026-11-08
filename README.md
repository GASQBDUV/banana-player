<p align="center">
  <img src="docs/img/banner.png" width="100%" alt="Banana Player: video and music for BananaOS, on the Anbernic RG DS and RG DS Plus">
</p>

<p align="center">
  <b>Your RG DS is a little cinema. Films, music and photos on the top screen, big easy controls on the touch screen below.</b>
</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#the-basics">The basics</a> ·
  <a href="FEATURES.md">All features</a> ·
  <a href="#something-wrong">Help</a>
</p>

Banana Player is a free video, music and photo player for [BananaOS](https://github.com/JorreFog/BananaLinux) on the
Anbernic RG DS and RG DS Plus. Drop your files on the SD card, pick one, and watch. It remembers where you stopped a film,
switches subtitles and audio tracks in a tap, turns a folder of songs into an album, and lets you zoom around your
photos with a finger. When a new version is out, it installs in one tap.

<table>
  <tr>
    <td align="center" width="33%"><img src="docs/img/screen-browser.png?v=0.4.0" alt="The file list on the bottom screen, the Banana Player logo on the top"><br><sub><b>Your files.</b> Icons, sizes, and where you left off in each film</sub></td>
    <td align="center" width="33%"><img src="docs/img/screen-network.png?v=0.4.0" alt="The Network tab on the bottom screen, with saved connections"><br><sub><b>Network folders.</b> Your computer or NAS, right from the Wi-Fi</sub></td>
    <td align="center" width="33%"><img src="docs/img/screen-playing.png?v=0.4.0" alt="A film playing on the top screen, the controls on the bottom"><br><sub><b>Watching.</b> Seek, skip, chapters, speed, subtitles and audio tracks, by button or by touch</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="docs/img/screen-music.png?v=0.4.0" alt="A song with its album art inside the Circle visualizer"><br><sub><b>Music.</b> Album art, a visualizer, or both (here: Circle)</sub></td>
    <td align="center"><img src="docs/img/screen-photo.png?v=0.4.0" alt="A photo zoomed in on the top screen, the pan area and its buttons on the bottom"><br><sub><b>Photos.</b> Zoom in and drag the box to look around</sub></td>
    <td align="center"><img src="docs/img/screen-settings.png?v=0.4.0" alt="Settings › Subtitles, with a subtitle preview"><br><sub><b>Settings</b> as tiles, with a help line for each and a live subtitle preview</sub></td>
  </tr>
</table>

## Why you'll like it

- 🎬 **Videos:** MP4, MKV, AVI and more, with subtitles and audio tracks you can switch mid-film, and chapters you can jump between with L2 and R2, and playlists that mix your card and network.
- 🎵 **Music:** a folder plays as an album, with the cover art, a visualizer, or both, and an equalizer with presets and room effects like Concert hall. Audiobooks (M4B) keep their chapters.
- 🖼️ **Photos:** zoom in, look around, step through a folder; scanned comics keep your zoom from page to page.
- 📡 **From your computer or NAS:** play straight from shared folders on your Wi-Fi (SMB or FTP), with nothing copied to the card.
- 👆 **Built for two screens:** the picture on top, every control a big button under your thumb below. Every page has the same header and a footer that names the buttons, and an on-screen keyboard takes care of typing.
- 💾 **Remembers for you:** where you stopped a film, which subtitles you chose, what you played recently.
- 🔄 **One-tap updates** when a new version is out, with no re-copying files to the SD card. Your settings stay put.

Videos play on the handheld's CPU. The various 1080p videos we tested play well; very high bitrates, and HEVC (x265) especially, may drop frames.

## Install

> [!TIP]
> **Latest: [Banana Player 0.4.0](https://github.com/tonywei92/banana-player/releases/tag/v0.4.0)**, for BananaOS on the
> Anbernic RG DS and RG DS Plus.

1. **Download** `BananaPlayer-0.4.0-aarch64.zip` from [the release](https://github.com/tonywei92/banana-player/releases/tag/v0.4.0).
2. **Unzip it into `roms/ports`** on your SD card. You should end up with `Banana Player.sh`, a `BananaPlayer` folder
   and a `media` folder there. (On a Mac, if the zip opens into a folder of its own, move those three out of it.)
3. **Play:** put the card back in, open the **Ports** shelf and choose **Banana Player**.

Your videos, music and photos can live anywhere on the card. From then on, Banana Player tells you when a new
version is out and installs it for you.

## The basics

The buttons follow the Nintendo layout: **A** confirms, **B** goes back.

- **Open something:** tap it, or press **A**. A folder plays as a playlist.
- **Play / pause:** **A**. **L / R** go to the previous or next file.
- **Close it:** tap **<** at the top left, or press **B** twice.
- **Local or Network:** **L / R** in the file list, or tap the tab. On the Network tab, **X** then **Find on the network** finds your computer.
- **Options** (refresh, hide a folder; on the Network tab, add a connection): **X**.
- **Menu** (Recent, Settings, About, Quit): **START**. **L / R** switch its tabs.
- **Quick settings** (volume, brightness, quit): press the **Menu/Function** button.
- **Quit straight away:** **F + START**.

The bottom line of every page shows what the buttons do there.

Every button has a touch equivalent on the bottom screen. Every control, setting and format is in
**[FEATURES.md](FEATURES.md)**.

## Something wrong?

Plug the SD card into a computer and attach `home/.config/banana-player/log.txt` to an
[issue](https://github.com/tonywei92/banana-player/issues). To uninstall, delete `Banana Player.sh`, the
`BananaPlayer` folder and `media/box/Banana Player.png` from `roms/ports`.

Want to build it or help out? See [DEVELOPMENT.md](DEVELOPMENT.md).

## Credits

- Made by [tonywei92](https://github.com/tonywei92) for [BananaOS](https://github.com/JorreFog/BananaLinux) by
  JorreFog and the Banana contributors.
- Plays with [mpv](https://mpv.io) and [FFmpeg](https://ffmpeg.org). Drawn with [SDL](https://libsdl.org).
- Network folders with [libsmb2](https://github.com/sahlberg/libsmb2) (LGPL 2.1) and [libcurl](https://curl.se).
- Fonts: [Pixelify Sans](https://github.com/eifetx/Pixelify-Sans), [Inter](https://github.com/rsms/inter),
  [Fusion Pixel](https://github.com/TakWolf/fusion-pixel-font) and [Noto Sans](https://notofonts.github.io) Thai,
  Arabic, Hebrew and Devanagari, under the SIL Open Font License 1.1.
- The film in the screenshots is [Sintel](https://durian.blender.org), © copyright Blender Foundation |
  durian.blender.org, under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).

## Licence

[MIT](LICENSE) © tonywei92. You're free to use, change and share Banana Player; copies and forks must keep the
copyright notice and the licence.
