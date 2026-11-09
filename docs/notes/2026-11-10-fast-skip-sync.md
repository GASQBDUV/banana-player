# Fast Prev/Next from the handheld while a browser plays

## As seen

With the web dashboard playing in the browser, the handheld's L/R moved the page's list on, and the handheld opened
the page's new file paused. Pressing Next many times quickly broke this: the page and the handheld ended on different
files. Sometimes the handheld started playing aloud and the page stopped.

## Causes

There were four races, and any one of them was enough.

1. **Presses lost on the handheld.** `Ui::browserSkip` was a single ±1, set by a press and sent by main's loop as
   `handheld-skip {dir}`. Two presses handled in the same pass of the loop sent one event.
2. **The handheld stepping its own copy mid-load.** L/R went to the page only while `predicting()` was true.
   `predicting()` needs the handheld's copy loaded, and needs `browserPos.path` to be its file. Right after the page's
   `browser-play` for the next file, the copy is still opening and `browserPos` still names the previous file. A press
   in that window ran `Player::next()`, which unpauses. The copy played aloud, main saw it play and sent
   `handheld-play`, and the page stopped.
3. **Overlapping `browserNext` calls on the page.** Each call computed `B.index + dir` and then awaited
   `/api/media` (the probe, up to 4 s) before it set `B.index`. Presses during that wait all counted from the same
   index, so presses were lost. The loads also finished in any order, so the page could settle on an older one.
4. **Stale posts on the handheld.** One `browser-play` and one `browser-pos` were posted per file. These go out on
   separate connections and can arrive out of order, so the handheld could open an older file after a newer one.

## Fix

- **Handheld** (`ui.cpp`, `ui_main.cpp`, `main.cpp`):
  - Presses are summed (`browserSkip += dir`), and main sends `handheld-skip {dir, n}`.
  - L/R go to the page whenever `pageDrives()` is true: a page plays (`browserPlaying`), a client is connected, and
    the copy is paused or still loading. The copy no longer has to be predictable.
- **Page** (`browser-player.js`):
  - `browserNext(auto, dir, steps, fromHandheld)` counts from `B.navTarget`, the place a still-pending navigation is
    going, and takes a new `B.navTok`.
  - `browserPlay` checks the token after each await and gives up with `null` once a newer navigation has started.
    Any other play (a tap, Continue here) also takes a new token. `browserStop` and `dropBrowser` cancel a pending
    navigation.
  - The handheld's Previous never restarts the song, as on the handheld. The restart after 3 s stays for the
    page's own Previous button when no navigation is pending.
- **Ordering** (`api_player.cpp`):
  - Every `browser-play` and `browser-pos` carries the tab id and a sequence number, `Date.now()*1000` plus a
    counter, so it still grows after a reload.
  - The handheld drops a post from the same tab whose sequence number is older than the last one it took, and logs
    `web: browser-play dropped (late …)`.
  - A `browser-pos` for a file other than the page's last `browser-play` is dropped too.
  - A page from before this change sends no sequence number, and its posts are always taken.
- **Page** (`events.js`): the takeover check in `applySnap` also ignores a snapshot taken while the handheld is
  still loading.

## How it was tested

- **Fake page with forced reordering.** A `BUTTON*N` step was added to the scripted runs: N presses 80 ms apart,
  between passes of the loop and not held up by a file opening. The handheld started on 04 of a 25-song folder and
  ran `R*15`, then `L*7`. A fake page (node, login plus SSE) answered every `handheld-skip` with `browser-play` and
  `browser-pos` for the new index. It did not coalesce, and it delayed each post by a random 0 to 500 ms, so posts
  overtook each other.
  - The handheld logged all 22 skips.
  - It dropped 8 late `browser-play` posts.
  - The fake page and the handheld both ended on index 11 (12/25), which is 3 + 15 − 7.
- **Headless Chrome.** The real page, muted and playing the same folder from 04, with the same burst on the
  handheld. The page ended on `12 Track.mp3` with no navigation pending, and the handheld on 12/25.
- **Not verified:** the handheld itself, Safari or WebKit, and a slow network where the probe takes seconds. The old
  build was not run against the same burst; the causes above come from reading the code.
