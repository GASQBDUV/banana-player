# Background playback: leaving the player no longer stops it

## What changed

B, Back or "<" on the playing screen used to close what played (B twice, a pocket's press being one). Now they only
leave the screen: the song or video goes on behind the browser, and **Stop** is explicit (Main's bottom-right button,
START › Playing › Stop, the mini bar's hold menu). A photo still closes on one B (it has no place to lose), and a file
that failed to open is stopped as it's left (an error isn't kept in the background).

## The split in `main.cpp`

`leave()` was the one way out: flush the playlist, `player.stop()`, `showBrowser(last, from, index)`. It is now two:

- `leave()` (what `press()` / `pointer()` returning true means): the screen only. `flushPlaylist`, then
  `showBrowser(currentPath, source, index)` with no stop, so the browser opens on the playing file's folder (or
  playlist) with the cursor on it, whatever was browsed meanwhile, and follows auto-advance (it reads what plays now).
  With nothing playing (B twice at Home) it quits, as before. Started with a file, it turns browser mode on first.
- `stopPlaying(why)`: the place saved, `ui.stopped()` (the hand-off flags cleared), `player.stop()`, and
  `showBrowser` **only if the player was what the bottom panel showed** (`Ui::playerShown`: something plays and
  `bgPlay` is false). Used by the player's Stop (`Ui::takeStopPressed`), the web's (`takeStopRequest`: Stop, a file
  deleted or renamed from the web, the page's own Stop) and the end of the list. Browsing meanwhile, the end of the
  list says "Finished" on the top panel and the list stays put. Started with a file, its end still quits.

`Ui::bgPlay` is set by `showBrowser` (true while something plays) and cleared by `showPlayer`. `home()` and
`closeMenu()` go to Main only when `playerShown()`, so pages opened from the browser (Settings, Info, the menu) come
back to the browser while something plays behind it. The snapshot's `screen` ("player" / "browser") is the same test.

## The browser-play bug it found

With a page playing the file ("Play in this browser"), the handheld holds a paused copy of it (`browserPlaying`,
`browserFile`). Closing the handheld's player used to `stop()` that copy but leave `browserPlaying` true and
`browserFile` set: the snapshot's `path` went empty, `pageDrives()` / `pagePlaying()` (which need `p.playing()`) went
false, so the handheld lost its control of the page, and the page's next `play` event re-announced
(`!s.browserPlaying || s.path !== B.meta.p` in `browser-player.js`) and reopened the file on the handheld, held.

Now:

- Leaving the player keeps the held copy, so nothing changes for the page (its mini bar on the handheld reads
  "Playing in your browser": its mark toggles the page, a hold takes it over, as on the player).
- The handheld's Stop clears `browserPlaying` / `browserFile` (`Ui::stopped`) and doesn't tell the page: it plays on.
- The page announces only when the snapshot has a path (the handheld has something to silence). The first "Play in
  this browser" opens the held copy through its own `/api/browser-play`, not this listener.
- `events.js`' take-over check ("the handheld plays again: this page pauses") needed `s.path` too: after a Stop the
  snapshot's `paused` is false (nothing plays) and the previous one's was true (the held copy), which read as the
  handheld playing again and stopped the page. Found by the scripted test below, not by reading.
- `/api/browser-play stop=1` (the page stopped) also unloads the held copy when it's the page's file and paused
  (`stopRequest`); the screens change only if the handheld's player showed.

## Verified

Desktop scripted runs: a song and a video left with B keep playing (`prop:pause` no, `time-pos` advancing);
SELECT, the bar and B at Home go back; the next song started by itself is where B lands; Stop from Main and from the
bar's menu; a photo opened over a song parks it and the bar's mark resumes it at its place. A CDP test (phone size)
plays a file in the page, presses B then the bar's Stop on the handheld, and checks the page plays on with the
handheld empty, also after the page pauses and plays again. Not verified on the handheld or in Safari.
