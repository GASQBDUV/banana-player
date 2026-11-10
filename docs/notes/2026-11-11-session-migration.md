# 2026-11-11 The Session migration (steps b–f)

**Problem.** The handheld and a browser tab shared playback through ~35 flags on both sides, each added for one race
(fast-skip, background playback, silent hand-off notes). The owner's bug that started this step: with the browser
playing, Stop on the handheld, then a video opened on the handheld played on the handheld instead of the browser.

**Root cause of that bug.** After the Stop the page was idle; hidden and paused, it closed its event stream, so
`toPage()` saw the tab gone and played the pick on the handheld, and the take-over detector then cleared `pageOut`. The
fix by construction: the selected output is the Session's `pref`, which only an explicit choice moves (SetOutput, an
Open that names where, a tab gone 10 s), and a page that is `pref` keeps its stream while hidden.

**What changed.** `docs/design/session.md` §10–11 has the detail. In short: every pick is an `Open`, Next/Previous a
`Step`, a file's end `Ended`, the hand-offs `SetOutput`, track choices `SetAudio`/`SetSub`, Stop/deletion
`Stop`/`FileGone`, a photo `PhotoPark`/`PhotoResume`; pages send theirs to `POST /api/intent` and render from
`state.session`; `SessionHost` runs the effects on the handheld once per loop pass. While the handheld itself is the
output, mpv stays the clock and reports back.

**Behaviour the owner will notice.** Where it plays is one choice for the whole system (Stop, the end of a list, a
photo, a file the browser can't play don't move it). Tracks carry across a switch both ways. A paused file stays paused
where it goes. A remote tab's Play/Pause while a browser plays now pauses that browser (hold A takes it over). A file
the browser can't play, picked in the browser, plays on the handheld with the note instead of being skipped. A photo
over the browser's song leaves the song playing; the handheld holds it again when the photo closes. A chosen tab gone
10 s: the handheld, paused at the browser's place.

**Verified** (Mac only): core-test with the fuzz test and the shared match vectors (C++ and JS); fake-page flows per
step; web-smoke (170/0); v1/v2 at 390 px; headless Chrome: ep 02 browser→handheld→browser with jpn + English
Subtitles, Next×10/Prev×4 at once while the browser plays (both on episode 7), Stop then a handheld-side pick (the
browser plays it), a photo over the browser's song, hold A. **Not verified:** the handheld itself, Safari, real
Wi-Fi latency (a page's Next is now a round trip), a duplicated browser tab (it copies sessionStorage, so both tabs
share one TAB_ID: one Chrome run on a reused profile behaved as two tabs would; a fresh profile passed).
