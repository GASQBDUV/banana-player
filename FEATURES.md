# Banana Player: all the features

Everything Banana Player does, button by button. For installing it, see the [README](README.md).

- [What it plays](#what-it-plays)
- [Finding something to play](#finding-something-to-play)
- [Controls](#controls): [while playing](#while-playing), [photos](#looking-at-a-photo), [the file list](#in-the-file-list), [the menu](#the-menu-start)
- [Photos](#photos)
- [Picture size](#picture-size)
- [Music](#music)
- [Chapters](#chapters)
- [Settings](#settings)
- [Quick settings](#quick-settings)
- [Updates](#updates)
- [More](#more)

## What it plays

| | |
|---|---|
| **Video** | MP4, MKV, WebM, AVI, MOV, TS, M4V, MPG, WMV, FLV and more: H.264, HEVC, VP9, AV1, MPEG-2/4… |
| **Music** | MP3, FLAC, OGG, WAV, M4A, M4B (audiobooks), AAC, Opus, WMA, AIFF, APE and more |
| **Photos** | JPG, PNG, WebP, GIF (animated too), BMP, TIFF, AVIF |
| **Subtitles** | Inside the video (SRT, ASS/SSA, PGS, VobSub, VTT), or files next to it (`movie.srt`, `movie.en.ass`) |
| **Languages** | Subtitles, titles and file names in Latin scripts (Vietnamese too), Cyrillic, Greek, Chinese, Japanese, Korean, Thai, Arabic, Hebrew and Hindi |

Videos are decoded by the handheld's CPU. Up to 720p plays smoothly; 1080p, and especially HEVC (x265), may drop
frames. **Info** shows how it's keeping up.

## Finding something to play

Open Banana Player and pick something on the bottom screen:

- **Recent:** the last 30 things you played.
- **Videos:** the `videos` folder in `roms` on your card.
- **This card** and **Second card:** anything on your SD cards.

Folders come first, then files in natural order (episode 2 before episode 10). Each row has an icon (folder, video,
song, photo), and each file shows its size. Anything you stopped part-way through shows where you left off. The line
under the tabs shows where you are (`share / Movies`), and the bottom line shows what each button does there: the
button's name in yellow, then what it does.

The **Local** and **Network** tabs at the top switch between your cards and your network folders (**L/R**, or tap a
tab).

Everything in a folder plays as one playlist: **L/R** go to the previous or next file, and the next one starts when
the current one ends. Folders can mix videos and music. Photos are a list of their own: from a photo you step through
the folder's photos, and from a video or a song through its videos and songs. When you close something, you're back in
its folder on that file.

## Controls

### While playing

| Button | What it does |
|---|---|
| **A** | Play / pause |
| **LEFT / RIGHT** | Back 5 seconds / forward 10 seconds (hold to keep going) |
| **L / R** | Previous / next file in the folder |
| **L2 / R2** | Previous / next chapter (**F + L2** is still the screenshot) |
| **UP / DOWN** | Faster / slower (0.5x, 1x, 1.5x, 2x, 2.5x) |
| **X** | Video: audio and subtitles (one page, two tabs: **Y** / **X** or **L / R** switch them) · Music: No loop → Loop all → Loop one |
| **Y** | Video: chapters · Music: Bars → Waves → Spectrum → Circle, or chapters for a song or audiobook that has them (the visualizer is still in Settings › Music) |
| **SELECT** | Video: Fit → Fill → Stretch · Music: Album art → Art + visualizer → Visualizer → None |
| **START** | Menu: Recent, Settings, About, Quit |
| **B** (or **Back**) **twice** | Close the video (back to the list) |
| **F + L2** | Screenshot of both screens (saved in the card's `screenshots` folder) |
| **F + START** | Quit Banana Player at once (anywhere; your place is kept) |
| **Menu/Function** (F) | Quick settings: volume, brightness, Same brightness for both screens, Quit |

**By touch:**
- **Progress bar:** touch it to jump there. Hold and drag to scrub, and the picture follows your finger.
- **Buttons:** they act when you lift your finger, so slide off one to cancel.
- **Audio and subtitles:** on a video, tap the audio or subtitle line under the buttons to change that track.
- **Chapters:** tap the chapter line above the progress bar to see the chapters.
- **Lists:** tap to choose, drag to scroll.
- **Close:** tap **<** (top left) once, or Close (bottom right, marked **B×2**) twice.
- **Lock**, **Info** and **Screen off** are along the bottom, beside Close.
- **<** is at the top left of every page and does what **B** does: in the file list it goes up a folder (at the top:
  tap twice to quit), on a page or in the menu it goes back.

Under the progress bar you'll see the time played, the time left (at the current speed) and the total length.

### Looking at a photo

| Button | What it does |
|---|---|
| **LEFT / RIGHT** or **L / R** | Previous / next photo (hold to keep going) |
| **UP / DOWN** | Zoom in / out: 100%, 150%, 200%, 300%, 400%, 600%, 800% |
| **A** | Zoom to 200%, or back to the whole photo |
| **L3** (press the stick) | Rotate a quarter turn |
| **SELECT** | Fit (the whole photo) or Fill (the screen filled) |
| **START** | Menu |
| **B** (or **Back**) **twice** | Close the photo |

See [Photos](#photos) for the touch screen.

### In the file list

| Button | What it does |
|---|---|
| **A** (or tap it) | Open the folder or play the file |
| **B** or **Back** | Up one folder (twice at the top to quit) |
| **UP / DOWN** | Move |
| **LEFT / RIGHT** | Page up / down |
| **L / R** | Local / Network tab |
| **X** | Options: **Refresh** (read the folder again), and on a folder **Hide folder** / **Unhide folder** (or hold the folder on the touch screen). On the Network tab: **Add connection**, **Find on the network**, **Refresh** |
| **Y** | On a network connection: **Edit connection**, **Test connection**, **Delete connection** (press twice) (or hold it on the touch screen) |
| **START** | Menu |

### The menu (START)

Four tabs. Switch with **L/R** or a tap; **B**, **START** or **<** closes it.

- **Recent:** what you played last. Pick one to play it.
- **Settings:** see [Settings](#settings).
- **About:** version, licence and the libraries it uses.
- **Quit:** quits Banana Player, keeping your place.

## Network folders

Play videos, music and photos straight from a computer or a NAS on your Wi-Fi, over **SMB** (Windows file sharing,
most NAS boxes, Samba) or **FTP** (and FTPS). Nothing is copied to the card.

- With no connections yet, the Network tab offers **Add connection** (tap it, or **A**).
- **Find on the network** (X on the Network tab) lists the computers sharing files nearby (Windows, Mac, Linux, NAS):
  pick one, type your user name and password, and Save. No address to type: a computer found by its name is found
  again by that name, even when its address changes.
- **Add connection** (X) for one it can't find, or for FTP: the type, the server's address or name, and optionally the
  port, the share or folder to start in, and how to sign in (an account, or as a guest / anonymous for FTP). **Name**
  is optional: left empty, it's named after the server ("Media on MY-PC"). **Test connection** tries it before you save.
- Passwords are saved encrypted with a key that only this handheld has.
- **Y** on a connection: edit it, test it, or delete it.

Network files play like the ones on your card: the folder plays as a playlist, subtitles next to a video (`movie.srt`,
`movie.en.srt`...) and a folder's `cover.jpg` are found, Recent lists them, and they resume where you left
off. The player reads ahead (up to 64 MB), so a short Wi-Fi drop doesn't stop it; while it waits for more, the top
screen says **Buffering**. If the connection is lost for good (20 seconds), it stops and says so.

## Photos

Pick a photo in the file list. The bottom screen shows the photo small, in the **pan area** (on the left; Settings ›
Photos can put it on the right), with a box around the part you see on top:

- **Drag** the box over the small photo to look around: the top screen follows it. Or, with Settings › Photos ›
  **Drag moves: The photo**, drag the photo itself, as on a phone. Let go while moving and it glides on.
- **Double-tap** to zoom to 200% on that spot. Double-tap again to see all of it.
- **Tap** somewhere on the small photo to look there.

The buttons beside it do the same as the handheld's: Prev, Next, Zoom −, Zoom +, Rotate, Fit/Fill, Info and Menu.

The next photo opens at the same zoom, in the same corner, which suits scanned comics page by page (turn this off with
**Keep zoom between photos**). Camera photos (JPEG) taken sideways are shown upright, and animated GIFs play. Photos don't go into
Recent. **Info** shows the photo's size, format, and when and with which camera it was taken.

## Picture size

| | |
|---|---|
| **Fit** | The whole picture, with black bars where its shape doesn't match the screen |
| **Fill** | Fills the screen; the edges of the picture that don't fit are cropped |
| **Stretch** | Fills the screen by stretching the picture |

## Music

Songs show their title, artist and album on the top screen. While a song plays, three buttons change how:

- **SELECT: what's shown.** **Album art** (default), **Art + visualizer** (the visualizer over the bottom of the art),
  **Visualizer**, or **None** (just the title: the lightest on the battery). A song without art shows its title alone
  on Album art, and the visualizer on Art + visualizer. Album art can be inside the song file or a `cover.jpg` in the
  same folder.
- **Y: the visualizer.** **Bars**, **Waves**, **Spectrum** or **Circle** (bars round a glowing ring, cyan to pink;
  with Art + visualizer, the album art sits inside the ring). A song or audiobook with chapters has **Y** for its
  chapters instead; the visualizer is still in Settings › Music.
- **X: looping.** **No loop**, **Loop all** (the folder starts again after its last song) or **Loop one** (the same
  song again). Videos never loop.

## Chapters

Videos (MKV, MP4, M4V) and audiobooks (M4B, which play as music) have chapters when the file
marks them:

- **L2** goes back: to the start of the current chapter if you're more than 5 seconds into it, otherwise to the
  chapter before. **R2** goes to the next chapter. On the last one it stops there and doesn't run into the next file
  (**R** does that).
- The top screen shows a card on each jump: **Chapter 3 of 12** and the chapter's title.
- The line above the progress bar on the Main page shows the chapter you're in (its title, or "Chapter 3 of 12" if it
  has none). Tap it for the list.
- **Y** (for a video, or a song with chapters) or that line opens the **Chapters** page: every chapter with its start
  time, and the one playing marked. **A** (or a tap) plays from the chapter you pick, and **L2** / **R2** still work
  there.
- Marks on the progress bar show where each chapter starts.
- Audiobooks and songs with chapters also show the chapter under the title on the top screen.
- Nothing pops up over a film by itself: the card appears only when you jump with **L2** or **R2**.

All three are remembered.

## Settings

Settings opens on seven groups, as tiles two to a row: move with the D-pad (**UP/DOWN** a row, **LEFT/RIGHT** a tile),
open one with **A** or a tap, and **B** goes back to them. In a group, **Y** goes to the group before it and **X** to
the one after (or tap the names either side of its title), and the line above the bottom says what the setting under the
cursor does. **Subtitle style...** at the end of the Subtitles list opens the Subtitles group; **B** there
goes back to the list.

| Group | Setting | Default | |
|---|---|---|---|
| **Subtitles** | Font, size, colour, border, border colour | Inter, Medium, White, Normal, Black | How plain subtitles (SRT, VTT…) look, with a live preview. Styled subtitles (ASS/SSA) keep their own look. |
| **Music** | Music display, Visualizer | Album art, Bars | What the top screen shows for music (also on SELECT and Y while a song plays) |
| **Photos** | Keep zoom between photos | On | The next photo opens at the same zoom and place. Off: each opens whole |
| | Pan area | Left | Which side of the bottom screen the photo's pan area is on |
| | Drag moves | The box | A drag in the pan area moves the box over the small photo, or the photo itself (like a phone) |
| **Remember** | Remember position | On | Reopening a video continues where you stopped (songs always start from the beginning) |
| | Remember audio track | On | Keeps each file's audio track; a new file starts in the language you chose last |
| | Remember subtitles | On | The same, for subtitles |
| **Files** | Remember last folder | Off | The file list opens where you last were |
| | Show hidden folders | Off | Shows folders you've hidden (they contain a `.hidden` file), so you can unhide them |
| | Keep play history | On | Keeps Recent. Turning it off also deletes the history |
| | Network files in Recent | On | Files played from a network folder go in Recent too. Off: they're left out, and the ones already there are removed |
| | Clear play history | | Press **A** (or tap) twice. Positions you stopped at are kept |
| **Controls & screen** | Brightness (or Top and Bottom brightness) | as BananaOS has them | The screens' backlight, 10% to 100%: the same levels as BananaOS's own Settings and the brightness keys, kept after a restart |
| | Same for both screens | On | One brightness for both screens; off, each its own |
| | Button hotkeys | On | Off: buttons can't pause, skip or close while something plays (touch still can) |
| | Battery percentage | Off | Shows the battery's percent beside its icon (the icon, with a bolt while charging, is always there) |
| **Updates** | Check automatically | On | Looks for a new version a few seconds after the start, in the background (Wi-Fi needed) |
| | Check for updates | | Looks now, and shows what it found |
| | Update to … | | Downloads the new version (shown when there's one), then **Restart now** |

## Updates

Banana Player updates itself when it's installed on the Ports shelf (from 0.2.3 on; Wi-Fi needed):

- A few seconds after it starts, it looks for a new version in the background (turn this off in Settings › Updates ›
  **Check automatically**).
- When there's one, the file list asks: **Update now**, **Later** or **Not until tomorrow**.
- **Update now** downloads it and checks it against its published checksum (a damaged download changes nothing), then
  offers **Restart now**. Otherwise it's put in place the next time you start Banana Player.
- The version before is kept on the card as `roms/ports/BananaPlayer.old`.
- Installed from the BananaOS Store, the Store updates it instead: open the Store to see an update.

Your settings, resume positions and history are kept through every update: they live in `home/.config/banana-player`
on the card, not in the player's folder.

## Quick settings

Press the **Menu/Function** button (F), anywhere, for a panel on the bottom screen:

- **Volume** and **Brightness**: drag along the bar, tap **−** / **+**, or use **LEFT / RIGHT**. They change the same
  levels as BananaOS's own settings and its volume and brightness keys, and they're kept after a restart.
- **Same brightness for both screens** (a tick box): on, one Brightness bar sets both screens; off, **Top** and
  **Bottom** each get their own.
- **Quit Banana Player.**

**F** again, **B** or a tap outside the panel closes it. (F with another button, like F + L2 or F + volume, does that
combination instead.)

## More

- **Info** (while playing): file, size, length, resolution, codecs, frame rate, bitrate, audio format, every track and
  the file's tags.
- **Battery:** an icon on the bottom screen, red when it's low, with a bolt while charging (and its percent, if you
  turn that on in Settings).
- **Volume and brightness:** the keys show a level card on the top screen.
- **Loading:** a file that takes a moment to open (a big photo, a slow card) shows a spinner on the top screen.
- **Lock:** locks the touch screen while playing; Screen off still works. Hold the lock for a second to unlock.
- **Screen off:** turns the bottom screen off to save battery. Touch it to turn it back on.
- **Lid:** closing it pauses; opening it keeps it paused until you press play.
