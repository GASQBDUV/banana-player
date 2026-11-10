# Photos over what plays, the way back from the player, Tracks tabs, Up next

## Photos: a second mpv, nothing parked

Before: one mpv, so a photo opened while a song or video played stopped it (`Ui::park`, the Session's `PhotoPark`),
and the mini bar offered it back at its place. Now photos always play in a second `Player` (`main.cpp`'s `photos`),
and what plays goes on under the photo: a song's sound and its next song, a video's sound. Closing the photo shows the
video again where it has got to; it never stopped.

Chosen over a GL image renderer of our own (FFmpeg decode + PhotoView's math): the second mpv keeps everything photos
already do exactly as it was (zoom/pan/rotate as mpv properties, EXIF turns, the 4096 px scale, the thumbnail by
`screenshot-raw`, animated GIFs, network photos through `bpnet://`), with no new GL code paths to get right on the
Mali's GLES and the Mac's GL. The manga reader can still use it (or take a renderer later).

- `PlayerOptions::photosOnly`: `ao=null` and `aid=no` (no second ALSA open), no state dir (nothing kept), its own
  `NetStreams` (a photo's open would otherwise cancel the video's network opens: `cancelOpens`).
- The bottom panel's `Ui` gets the photo viewer as its `Player&` while a photo is up (`shown()` in main: press,
  pointer, release, repeats, tick, draw, capture, quick settings). The web, the Session, the mini bar and Stop keep the
  player. Settings: copied into `photos` as a photo opens, and each pass whichever side changed is copied to the other
  (`photoSettingsSeen`): a change from START's menu over a photo is the player's `setSettings` (written, applied).
- A video's picture is off under the photo (`Player::hideVideo`): `vid=no` mid-file, and `vid=no` as each video's own
  `loadfile` option while hidden (so the next video in the folder opens hidden too, and the global `vid` never leaks
  into later files: every video's `loadfile` now says `vid=auto` or `vid=no`). Back with `vid=auto`: mpv's track
  switch seeks the new decoder to the clock. A song after a hidden video gets `vid=auto` back before its `loadfile`.
- A song's graph keeps making frames (it carries the sound), and mpv waits for each frame to be taken (200 ms stalls,
  "mpv_render_context_render() not being called or stuck"). `drainUnder()` renders them into a 16×16 FBO of its own
  (made once with `glGenFramebuffers`, through `SDL_GL_GetProcAddress`), so the photo isn't drawn again 30 times a
  second. With `-v`: no stall messages while the photo was up.
- Closing (`closePhoto`): B, Close, any pick that isn't a photo (a song or video picked over a photo takes the screen).
  Back to the player when the photo was opened over it (a web pick), else `Ui::backFromPlayer`.
- `stopPlaying` doesn't change screens while a photo is up (the end of a list under a photo: "Finished" over the photo).
- A page playing the item: the handheld's held copy is the player's, untouched by the photo (the uncommitted
  `session_host.cpp` line, a held `HhLoad` skipped while a photo is up, can no longer trigger: the player never plays
  a photo now; kept).
- Gone: `Ui::park`/`resumeParked`/`Parked`, the "Stopped" chip, the web snapshot's parked branch and its Play-resumes
  command, `SessionHost::photoPark/photoResume`. The reducer keeps `PhotoPark`/`PhotoResume` (tested; unused).

Measured on the Mac (`/usr/bin/time -l`, ~14 s each, 640×360 H.264 + AAC): the video alone 23% CPU, max RSS 152 MB; the
same video, 4 s of browsing then under a photo, 14% / 178 MB; a photo alone 8% / 160 MB. Not measured on the handheld.

## Back from the player: where it was opened from

`Ui::showPlayer` keeps `playerBack` (page, menu and its tab, settings group, cursor, scroll, and the menu's own way back,
which START over the player overwrites) when the player wasn't already up (`onPlayer`). Leaving (B, "<", Back), Stop
and a list's end with the player up go through `Ui::backFromPlayer`, which restores it (the browser's folder and list
were never touched meanwhile). Fallback to `showBrowser` on the file: started with a file, Show in folder, or a
Playing tab that no longer applies.

## Tracks: Chapters, Subtitles, Audio

One page, three tabs (`tracksTabs`: Chapters when the file has some, then Subtitles and Audio for a video; a song has
the Chapters tab only, reached from the chapter line). `Page::Chapters` stays the Chapters tab's page. X opens the last
tab shown, first Chapters if there are chapters, else Subtitles. L/R and Y/X step tabs (no wrap); the neighbours carry
their key where it fits.

## Up next (Y, the "List" key)

`upNextList`: the Session's queue when its item is the player's file (the list's own order; Shuffle's is hidden), else
the player's list. A row is a `PlayRequest` of that list from that index (main's pick path: `routed()`, the Session's
Open, so the output and Shuffle/Loop behave as for any pick); the playing row just goes back. Durations: mpv's for the
one playing, the durations cache for local files.

The second row of the player's keys is now List (Y), then Tracks (video) or Shuffle/Loop (song) on X, SELECT, speed,
Menu. A song's **FX** key (Y's old place on songs without chapters) is gone: the Sound page is the line under the keys.

## Verified (desktop scripted runs)

- v1.mp4 playing → browser → Pics → a photo: `under=v1.mp4 t=6.6 … 11.3` advancing, `vid = no`; B → `vid = 1`,
  `time-pos = 14.7`, the top panel the test pattern at 0:13, back in Pics on the photo's row.
- Three 4 s songs with the visualizer: Song 2 → Song 3 under the photo; no render stalls logged while it was up.
- Back: Mix folder, Home (focus kept), START › Playing › Now playing → back to the menu, `lib` with the cursor on "..".
- Tracks: X → Chapters (the file has three), R → Subtitles, R → Audio, R (nothing), L, L → Chapters; B then X → Chapters.
- Up next: rows 1–4 of Mix, Now on the playing one; Down, Down, A → v2.mp4 plays (`file=3/4`).

Not verified: on the handheld (GLES FBO, two mpv instances' memory, CPU), with a browser page as the output, a network
photo over a network video.

## Later the same day: Picture tab, one key row for both screens

The video's picture size (Fit / Fill / Stretch, SELECT before) is the Tracks page's fourth tab, **Picture** (A chooses,
the one in use ticked). SELECT on a video opens Tracks; X is the play mode on a video as on a song (In order → Loop
all → Loop one → Shuffle). The second row is the same on both: List (Y) | mode (X) | Tracks or the music display
(SELECT) | speed | Menu. With four tabs the names drop a size where one wouldn't fit (`drawTab`'s size). Checked at
640×480 and 1024×768: A on Fill sets `aspect=Fill` (`panscan = 1`), X on a video goes Off → All, a song's SELECT
opens the Display popup and Y its Up next.
