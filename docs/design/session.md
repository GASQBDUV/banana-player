# One playback Session (design, 2026-11-10)

**Status: step (a) done** (2026-11-11): `src/session.{h,cpp}` (bpcore) with its tests in `core-test`, and a shadow
`SessionHost` (`src/session_host.{h,cpp}`) that logs `session: …` and `session: disagree <what>` lines and acts on
nothing. Where this text left a choice open, §9 says what was chosen.

Goal: every way the handheld and a browser tab share playback goes through one value,
the **Session**, changed only by **intents** through one pure reducer, and followed by two **renderers** (the
handheld's mpv, the page's `<video>`), Spotify Connect / Chromecast style. Today the same facts live in ~35 flags on
both sides, each added for one race; this replaces them with invariants checked by tests, including a fuzz test.

## 1. Today

Handheld (`Ui`, `ui.h`; set in `ui*.cpp`, `main.cpp`, `api_player.cpp`):

| Flag / token | Means | Fixed (note / commit) |
|---|---|---|
| `browserPlaying` | a page plays; the handheld holds a paused copy | the base of "Play in this browser" |
| `pageOut` | a page is the sound, survives the handheld's Stop | picks go to the page (a808b7d, background-playback "browser stays the sound") |
| `browserFile` | the file the page last announced | `browser-pos` for a file the page left is dropped (fast-skip #4) |
| `browserTab`, `browserSeq`, `staleBrowserWord()` | last tab + growing seq of `browser-play`/`browser-pos` | posts overtaking each other opened an older file (fast-skip #4) |
| `browserPos{path,t,at,playing}` | the page's last position report | Play here resumes where the page is, not the stale copy |
| `predicting()`, `shownPosition()`, `shownChapter()`, `noteSeek()` | extrapolated page clock for the bar | no seek of the held copy per report (decodes a frame) |
| `browserSeenPos`, `browserSyncUntil` | detect a seek made here → `handheld-seek` | seeks on the handheld reach the page |
| `browserSkip` (a sum) | L/R presses to send as `handheld-skip {dir,n}` | presses lost in one loop pass (fast-skip #1) |
| `pageDrives()` | L/R/A go to the page even while the copy loads | stepping the copy mid-load made it sound (fast-skip #2) |
| `browserWant`, `browserWantAt`, `browserToggleSend`, `WANT_MS` | the play state wished by A, flipped from the pending wish | A×3 or an event seen twice ending the wrong way (fast-skip "Later") |
| `playHoldAt`, `playHoldTouch`, `playHoldMini`, `PLAY_HOLD_MS`, `takeOver()` | hold A 600 ms = play here | take-over without a separate button |
| `browserPlayAt`, `browserSawPause` | the take-over detector's grace | the held copy "looked playing" while opening |
| `pageOpenTok/Path/At/Sent/Refused` | a pick sent to the page (`handheld-open`), answer or 6 s timeout | older answers dropped; unanswered picks play here (a808b7d) |
| `bgPlay`, `playerShown()` | playing behind the browser | background playback (e07179c) |
| `parked`, `park()`, `resumeParked()`, `parkedShown()` | a song stopped by a photo (one mpv) | photo over music; the page sees it paused (b699559) |
| `seekPath/seekTo/seekUntil` | a position to apply once a file knows its length | hand-off and parked resume positions |
| `stopRequest`, `stopPressed`, `stopped()` | Stop from web / player; clears `browserPlaying`/`browserFile` | Stop left `browserPlaying` true, page re-announced (background-playback) |
| `soundBackHere()` | `/api/play` makes the handheld the output first | the page's trailing `stop=1` unloaded the handheld's new copy (9434cb7) |

`main.cpp`: `toPage()`/`opened()` (route a handheld pick to the page, or "This one plays on the handheld"), the
take-over check (handheld unpaused → `handheld-play`, clears `browserPlaying`/`pageOut`), `elsewhereShown` (top
card), `stopPlaying(StopWhy)`, `leave()`. `Player`: `holdPaused()`/`heldUnplayed`/`wakeHeld()` (`ao-reload`: silent
ALSA after a paused open, silent-handoff note), `nextAid`/`nextSid` (tracks as `loadfile` options), `back()` +
`RESTART_S` (Prev 5 s). Server: `/api/browser-play` (`stop=1`, `open=tok`), `/api/browser-pos`,
`/api/browser-refuse`, `/api/play` = `/api/handoff` (`t`, `aid`, `sid`), `/api/cmd`; SSE `handheld-skip`,
`-toggle`, `-seek`, `-open`, `-play`; snapshot `browserPlaying`.

Page (`web/js/browser-player.js`, `events.js`, `store.js`): `B.active/meta/queue/index/source`, `B.navTok` +
`B.navTarget` (overlapping `browserNext` lost presses / finished out of order, fast-skip #3), `B.openTok` (answers
`handheld-open`), `B.announced/announcedAt` (1.5 s de-dup of `browser-play`), `B.handheldQuiet` + the `applySnap`
take-over check (needs `s.path`, skips loading, skipped after Stop: background-playback), `B.leaving`, `B.parked` /
`parkedPlaying` (connection lost), `B.want` + `wantHandheldTracks`/`startTracks`/`handheldIds` + `bpTr:<p>`
sessionStorage (tracks across hand-offs, in progress), `wordSeq` (`Date.now()*1000`+n), `store.target` +
`bpTarget` localStorage (`chooseTarget`), `B.remux`/`B.switching` (Direct Stream).

The shape of the problem: **output, item, play state and position each have 2–4 owners**, and every message carries
a different staleness token (`seq`, `tok`, `navTok`, `browserFile`, a timeout, `announcedAt`). Each fix is local and
right; together they can't be reasoned about.

## 2. The model

`src/session.{h,cpp}` in bpcore (no SDL, mpv, network, clock):

```cpp
enum class OutKind { Handheld, Browser };
struct Output { OutKind kind = OutKind::Handheld; std::string tab; };    // tab: TAB_ID when Browser
struct TrackPick { int ord = -1; std::string lang, label; bool external = false; bool set = false; };
	// ord: place within its kind (stream order, side files after), -1 off; set=false: the renderer's default
struct Item { std::string path; double dur = 0; bool audio = false; };
struct Session {
	uint64_t boot = 0, v = 0;           // boot: random per process; (boot, v) orders every Session
	std::vector<std::string> queue;     // real paths, in play order, no photos
	int index = -1;                     // -1: nothing (stopped)
	PlaySource source;                  // Folder / NetFolder / Playlist (player.h)
	uint64_t load = 0;                  // bumps whenever the item (re)loads: the token for reports
	bool playing = false;               // the wish; the output's renderer makes it true
	double pos = 0; int64_t posAt = 0;  // seconds, at the host's ms clock; owned by the output
	double speed = 1; int loop = 0;     // LOOP_OFF/ALL/ONE (mirrored from Settings by an intent)
	TrackPick audio, sub;
	Output out, pref; uint64_t outV = 0; // out: plays now; pref: where picks go (a browser that couldn't play one stays pref)
	uint64_t itemV = 0, seekN = 0;      // itemV: version of the last item change; seekN: user seeks
	bool parked = false;                // a photo holds the handheld's mpv (§5)
	std::string error;                  // the item failed on the output; next/prev still work
	Origins seen;                       // per-origin last seq + result (idempotency, §3)
};
double posNow(const Session&, int64_t now);   // pos + (playing && !parked ? (now-posAt)/1000*speed : 0), clamped to dur
```

**Owner**: the handheld's main thread, in a `SessionHost` (app side, `src/session_host.cpp`) holding the `Session`, the
only caller of `apply`, and the only executor of effects. Everything else submits intents: `Ui` (buttons, touch),
web routes (via `WebServer::call`, already on the main thread), mpv events and timers (as reports/ticks).

**Published**: `WebSnapshot` gains `session` (`{boot, v, queue (virtual), index, source, load, playing, pos, posAt,
speed, loop, audio, sub, out:{kind, tab}, parked, error}`), and the host publishes on every `v` change at once (not
behind the 4/s throttle, which stays for the clock). Clients keep the highest `(boot, v)` seen and drop any snapshot
below it (SSE and `/api/state` can cross). A new `boot` resets the page's renderer (the handheld restarted).

## 3. Intents and the reducer

Every intent: `{origin, seq, basis}` + its fields. `origin`: `"hh"` or a tab id; `seq`: grows per origin
(`Date.now()*1000+n` on the page, as `wordSeq`); `basis`: the `v` the sender last saw.

| Intent | Fields | Kind |
|---|---|---|
| `Open` | queue, index, source, pos, audio?, sub?, out? | absolute |
| `Play`, `Pause` | — | absolute (a toggle is resolved at the edge: the host reads `playing`; the page sends the opposite of its latest known `playing` incl. its own pending intent) |
| `Seek` | t | absolute |
| `SeekBy` | d | relative |
| `Step` | n (±; a burst summed) | relative (next/prev) |
| `SetAudio`, `SetSub` | TrackPick | absolute |
| `SetSpeed`, `SetLoop` | value | absolute |
| `SetOutput` | to {kind, tab}, at? (pos the sender saw) | guarded |
| `Stop` | — | guarded |
| `PhotoPark`, `PhotoResume` | pos | absolute (hh only) |
| `FileGone` | path (a file or folder deleted/renamed) | absolute |
| report `Loaded` | load, dur, audio | report |
| report `Pos` | load, t, playing | report |
| report `Ended` | load | report |
| report `CantPlay` | load, reason (`format`, `net`, `error`) | report |
| `Tick` | — (timeouts; `now` is an argument of every apply) | host only |
| `TabGone`, `TabBack` | tab | host only (from `WebServer` ClientsChanged) |
| `Slept`, `Woke` | — | host only |

```cpp
struct Effect { enum { HhLoad, HhHold, HhPlay, HhPause, HhSeek, HhTracks, HhSpeed, HhUnload, HhPhoto,
                       Note, PageToast } kind; /* fields */ };
struct Result { Session s; std::vector<Effect> fx; std::string reject; bool dup = false; };
Result apply(const Session&, const Intent&, int64_t now);   // pure, total, deterministic
```

Rules the reducer applies, in order:

1. **Duplicate**: `seq <= seen[origin].seq` → return `seen[origin].result` again (`dup`), session unchanged. Retries
   after a timeout are safe. `seen` keeps the last 16 origins (LRU), bpcore-tested.
2. **Reports** are taken only from the current output (`origin == out.tab`, or `"hh"` for the handheld) and only for
   the current `load`; anything else is dropped with `stale-report` (replaces `browserFile`, `staleBrowserWord`,
   `pageOpenTok`, `B.openTok`).
3. **Guarded** intents (`Stop`, `SetOutput`) are rejected (`stale`) when `itemV > basis` or `outV > basis`: they were
   aimed at a state that no longer exists. This is the "stop right after play" fix by construction.
4. **Relative** intents apply to the current state (a burst means "n more from where it is now").
5. **Absolute** intents: last to arrive on the main thread wins. Applying the same one twice changes nothing.
6. Any change bumps `v`; a pure `Pos` report updates `pos/posAt` only (no `v` bump, no publish beyond the clock).

`Step` (the only place for Prev 5 s, end of list and loop): `posNow >= RESTART_S && n < 0` → first press restarts
(`seekN++`), the rest step `n+1`; target clamped (or wrapped for `LOOP_ALL`); at the first/last with nothing to do →
reject `first`/`last` and a `Note`. A new index sets `load++`, `itemV = v`, `pos = 0`, `audio/sub` kept as picks
(matched per file, §4), `error` cleared. `Step ×15` then `×-7` is 22 synchronous applies on the main thread: the index
ends at +8 whatever order the renderers load in, and the executor coalesces (below), so only the last file loads.
The page never computes an index again (no `navTarget`, no probe in the way).

**Effects are executed in one place**: `SessionHost::run(Result)` at the end of each loop pass, after all intents of
the pass are applied. It coalesces: only the last `HhLoad` of the pass runs, `HhSeek` only after it, notes
de-duplicated. The handheld renderer (§4) also **reconciles** each pass against the session (level-triggered), so a
lost effect self-heals. **Effect failure** never throws away state: an mpv load error comes back as `CantPlay(load,
"error")` from `"hh"`; a page that can't play reports `CantPlay`; a renderer that never says `Loaded` within 8 s is
handled by `Tick` as `CantPlay(load, "timeout")`. Each logs `session: <intent> v<old>→<new> fx=[…]` or
`session: <intent> rejected (<why>)` to log.txt.

## 4. Outputs as renderers

**Handheld renderer** (app side, `SessionHost::reconcile(Player&)`), each loop pass:
- `index < 0` → `player.stop()` (+ the screens rule of `stopPlaying`). `parked` → leave mpv to the photo.
- `player`'s loaded load ≠ `s.load` → `open`/`openPlaylist` with `start=pos`, `aid`/`sid` from `matchTrack(pick,
  player.tracks)`; when `out ≠ Handheld` follow it with `holdPaused()` (screens show it, `heldUnplayed` set).
- `out == Handheld`: mpv pause = `!playing` (`unpause()` keeps `wakeHeld()`'s `ao-reload`); `seekN` newer than applied
  → seek; speed, tracks follow. It reports `Loaded`, `Pos` (on pause/seek and every second; it *is* the clock),
  `Ended` (incl. `checkSongEnd()`), `CantPlay` (`player.error`).
- `out ≠ Handheld`: mpv stays paused, never seeked per report; the bar shows `posNow(s, now)` (replaces
  `predicting()`/`shownPosition()`). The top card (`showElsewhere`, `elsewhereShown`) is a pure function of `s`.
- A pre-existing safety net for invariant I2: `out ≠ Handheld && !player.paused && !loading` → pause + log
  `session: invariant I2 repaired`. In tests it's an assertion.

**Page renderer** (`web/js/renderer.js`, replacing `browserPlay`/`browserNext`/`handheldOpen` control logic):
- Acts only when `s.out.kind == 'browser' && s.out.tab == TAB_ID`; otherwise it unloads its element (another tab or
  the handheld took over: the toast "Playing on your handheld now"). No `handheldQuiet`, no take-over guessing.
- `rendered.load ≠ s.load` → probe, then load `s.queue[s.index]` at `posNow` with the tracks: `matchTrack` (the same
  rule in JS, ported from `startTracks`/`handheldIds`, unit-tested against the C++ one with shared vectors).
  Direct Play when the audio pick is the default or Safari's `audioTracks` has it; else Direct Stream (`/api/remux`).
  Probe says no → `CantPlay(load, 'format')`.
- `play()`/`pause()` to match `s.playing`; a refused `play()` (no gesture) → `Pos(playing:0)`; the reducer sets
  `playing=false` and the UI shows Play.
- Reports `Loaded`, `Pos` (play/pause/seeked/10 s), `Ended`, `CantPlay`; everything the user does on the page is an
  intent (`Step`, `Seek`, `Play`…), sent with `basis = lastSeen.v`, and shown optimistically until the next `v`.
- Connection lost: it parks itself (today's `parkHere`) and sends nothing; the host handles the tab's absence (§5).

New route: `POST /api/intent` (JSON `{origin, seq, basis, type, …}`) → `{ok, v}` | `{rejected, reason, v}` |
`{dup, v}`. The old routes become adapters to it until step (f).

## 5. The rules, once

- **Prev**: `Step(n<0)` at ≥ `RESTART_S` restarts; the handheld's L, the page's Prev, the Media Session, `/api/cmd prev`
  all send `Step`. No other copy of the rule (`Player::back()` and `browserNext`'s go).
- **Unplayable in the browser**: `CantPlay(format|timeout)` from the page → `out = Handheld`, `playing` kept, `pos` kept,
  `Note "This one plays on the handheld"` + `PageToast`. The page stays the *preferred* output (`pref`, a field
  beside `out`): the next `Step`/`Open` goes back to it (today's `pageOut`).
- **Network or unserved files** (`servedVirtual` empty): the reducer can't know; the host passes
  `browserCan(path)` into `Open`/`Step` results via the intent's context. Not served → output handheld, same note.
- **Photos** never enter the session (queue has no photos). `PhotoPark(pos)` from the handheld: if `out == Handheld`,
  `parked = true`, `playing = false`, `pos` frozen; if `out == Browser`, nothing changes for the page (it plays on), only
  the handheld's mpv shows the photo. `PhotoResume` (the bar's play, SELECT, a photo closed): `parked = false`, `load++`
  so the handheld reloads at `pos`. Any `Open` clears `parked`.
- **End of list**: `Ended` on the last item, no loop → `index = -1`, `playing = false`, `Note "Finished"` / `PageToast
  "End of <source>"`; the last item and place go to Recent as today (`saveResume`). Loop one: `seekN++`. Loop all: wrap.
- **Stop** (any origin, guarded): clears the item everywhere (`index = -1`), the page unloads too; `pref` survives,
  so the next pick still goes to the browser. (Today the handheld's Stop leaves the page playing; see §8 Q1.)
- **Output tab gone** (`TabGone` while it's the output): grace 10 s (an SSE reconnect, a reload with the same
  `TAB_ID`, `TabBack`, keeps it). After that `Tick`: `out = Handheld`, `playing = false`, pos from the last report,
  `Note "Your browser went · paused here"`. Never starts sound on its own.
- **Tracks across output changes**: the picks are in the session as `{ord, lang, label, external}`; each renderer maps
  them with `matchTrack` (by place within kind when both list as many, else by label, else by language, else default).
  A pick a renderer can't play (DTS in Chrome) → that renderer plays the default and the session keeps the pick, so
  going back to the handheld restores it.
- **Position authority**: only the output's renderer moves `pos` (by `Pos`/`Loaded`); others read `posNow`. `SetOutput`
  freezes `pos = posNow(now)` (or the sender's `at` if it is the old output and its report is fresher) before switching;
  the new output loads there. Times are stamped with the host's clock on arrival; no cross-device timestamps are compared.

### Invariants (checked after every apply in tests; I2 also at run time)

| # | Invariant | Enforced by |
|---|---|---|
| I1 | Exactly one output; `out.kind == Browser ⇒ !out.tab.empty()` | `Output` is one value; `SetOutput` validates |
| I2 | The handheld's mpv never sounds while `out ≠ Handheld` | reducer: `hhShouldPlay(s) = out==Handheld && playing && !parked`, the only input to mpv pause; renderer holds every load; run-time repair + log |
| I3 | `v` strictly grows on every change; unchanged on reject/dup/`Pos` | `apply` bumps once at the end iff `s != old` (compared minus clock) |
| I4 | An intent applies fully or is rejected with a reason | `apply` builds a copy, returns it or the old one + `reject` |
| I5 | No client acts on a version older than one it has seen | page keeps max `(boot,v)`; reports carry `load`; guarded intents carry `basis` |
| I6 | Never stuck: a loaded output plays or reports within 8 s; an item nobody can play ends in `error` with Step live | `CantPlay` paths, `Tick` timeout, `error` state |
| I7 | `index ∈ [-1, queue.size())`; `index == -1 ⇔` nothing plays; no photo in `queue` | reducer clamps; `Open` filters (`isPhoto`) |
| I8 | Reports only from the current output for the current `load` | rule 2 |
| I9 | `0 ≤ posNow ≤ dur` once `dur` known; `playing ⇒ index ≥ 0 && !parked-on-handheld` | clamps in `posNow`/reducer |
| I10 | Same `(origin, seq)` twice ⇒ same result, one effect set | rule 1 |

### Failure modes

| Failure | Session does | User sees |
|---|---|---|
| Network drop / SSE reconnect mid-intent | intent applied once or not at all; the page retries with the same `seq` (dup returns the result) | page: reconnecting banner, then the current state |
| Duplicated / reordered messages | dup by `seq`; old reports dropped by `load`; old guarded intents rejected by `basis` | nothing |
| Output tab closed, or asleep (phone locked, timers frozen) | `TabGone` → 10 s grace → `out = Handheld`, paused | handheld: "Your browser went · paused here"; Play resumes there |
| Two tabs both want the output | last `SetOutput` wins; the loser sees `out.tab ≠ me` and unloads | loser: "Playing in another browser" |
| Handheld sleeps (SIGSTOP / lid) while the page plays | `bye sleep` → page parks; at `Woke` the host applies `Pause` | both paused at the place; Play continues |
| Handheld quits / crashes while the page plays | session lost (positions in resume.tsv); new `boot` | page: down screen, then its renderer resets to the new session |
| `WebServer::call` times out (2 s, 503) | the queued call may still run; the retry with the same `seq` is a dup | at worst a delayed action, never a double |
| mpv fails to load | `CantPlay(load, error)` from hh → `error` set, `playing = false` | the player's error card; Next/Prev work |
| File deleted / renamed mid-play | `FileGone(path)`: current → `Stop`; elsewhere in the queue → removed, `index` shifted | "Stopped: the file was deleted" |
| Clock skew | none: only the host's clock stamps `posAt`; the page extrapolates from its receive time | nothing |
| Browser `play()` refused (no gesture) | `Pos(playing:0)` → `playing = false` | page shows Play; one tap plays |
| Load never answers (probe 4 s, dead stream) | `Tick` → `CantPlay(timeout)` → output handheld | "This one plays on the handheld" |

## 6. Tests (bpcore, `tests/core_test.cpp`, no window)

Named reducer cases (each a sequence of intents with times, then `CHECK_EQ`s on the session and effects):
- `fast Next x15 Prev x7 ends at +8` (from index 3, reports arriving shuffled) and only one `HhLoad` per pass.
- `two Next in one loop pass both count` (fast-skip #1).
- `Next while the held copy loads goes to the page's session, never unpauses mpv` (fast-skip #2; I2).
- `late browser-pos for the previous file is dropped` / `reordered Loaded from an older load is dropped` (#4).
- `A x3 from playing ends paused`; `a toggle seen twice (dup seq) flips once`.
- `stop right after play from a never-played file keeps playing` (`SetOutput(hh)`+`Play`, then the page's `Stop`
  with the old `basis` → rejected `stale`; 9434cb7).
- `handoff to the handheld of a file it only held keeps playing there` (`ao-reload` effect on the first `HhPlay`).
- `tracks survive browser→handheld→browser` (pick ord 2 / sub label "English [ASS]"; renderer counts differ).
- `an unplayable audio pick plays the default in the browser and comes back on the handheld`.
- `photo over music keeps the browser's song` / `photo over music on the handheld parks and resumes at its place`.
- `Prev ≥5 s restarts`, `Prev <5 s goes back`, `Prev burst from 7 s restarts then n-1`, `Prev on the first <5 s rejects
  with "This is the first one"`.
- `end of list stops, says Finished`; `loop one restarts`; `loop all wraps both ways`.
- `handheld Stop with the browser as output stops the page; next pick goes to the browser` (`pref`).
- `browser can't play → handheld plays at the same place with the note`; `unanswered load for 8 s → handheld`.
- `output tab gone 10 s → handheld paused`; `tab back within 9 s keeps it`.
- `a second tab takes the output; the first's reports are dropped`.
- `Woke after sleep pauses`; `new boot resets the page`.
- `FileGone of the current file stops; of a later one shifts index`.
- `the handheld playing again (hold A) makes it the output` (replaces the take-over detector).

**Property / fuzz test** (`fuzzSession(seed, steps)` in core-test; 2 000 sequences × 200 steps by default, `BP_FUZZ=N`
for more; the seed is printed on failure). A model network holds each origin's (`hh`, `tabA`, `tabB`) messages and
delivers them with random delay, reordering, duplication (10 %) and drops (5 %, then a retry with the same `seq`).
Model renderers (pure: the session in, reports out, with random load times and random `CantPlay`) run for the current
output. Random intents: every type above, bursts of `Step`, `Tick` with random time jumps (0–15 s). After each step:
I1–I10. At the end, deliver everything, `Tick` +30 s, and check liveness: either `index == -1`, or the output's
model renderer has `Loaded` the current `load` and `playing` matches its element, or `error` is set. The reducer is
deterministic (`now` passed in, no globals, `boot` from the initial session), so a failing seed replays exactly.

The JS `matchTrack` and `posNow` are checked against the C++ ones on a shared JSON vector file
(`tests/session_vectors.json`) by a node one-liner in `tools/web-smoke.sh`.

## 7. Migration (each step ships and is tested on the handheld)

(a) **Session + reducer + tests in bpcore, unused.** Plus a **shadow** host in `main.cpp` that feeds the same intents
derived from today's events and logs `session: shadow diverges (<field>)` when its output/item/playing disagree with
the flags. Removes nothing. Risk: none (log only); it tells us where the model is wrong before it drives anything.

(b) **Output and tracks.** `SetOutput`, `CantPlay`, `TabGone`, picks + `matchTrack`. `out`/`pref` become the truth;
`browserPlaying`, `pageOut`, `browserTab`, `soundBackHere()`, `handheld-play`, the take-over detectors (`browserSawPause`,
`browserPlayAt`, `B.handheldQuiet`, `applySnap`'s check), `/api/browser-refuse`, `nextAid`/`nextSid` plumbing,
`wantHandheldTracks`/`startTracks`/`handheldIds`, `bpTr:` storage, `store.target` as state (it becomes "where my taps
go", read from `out`) are removed or become derived reads. Risk: the hand-off itself; test Continue here / Play on
handheld both ways on Safari and the handheld, with ASS + a second audio track.

(c) **Next/Prev and opens.** `Open`, `Step`, `Ended`, `Loaded`; the page renderer loads from the session. Removes
`browserSkip`, `handheld-skip`, `handheld-open`, `pageOpenTok/Path/At/Sent/Refused`, `toPage()`/`opened()`, `B.navTok`,
`B.navTarget`, `B.openTok`, `browserNext`'s index arithmetic, the page's own `RESTART_S`. Risk: a round trip per Next
from the page (LAN: ~20 ms); keep the optimistic title. Re-run the fast-skip burst (`R*15,L*7`).

(d) **Play/pause/seek.** `Play`, `Pause`, `Seek`, `SeekBy`, `Pos`. Removes `browserWant*`, `browserToggleSend`,
`handheld-toggle`, `handheld-seek`, `browserSeenPos`, `browserSyncUntil`, `browserPos`, `predicting()`,
`shownPosition()` (→ `posNow`), `noteSeek()`, `/api/browser-pos`, `staleBrowserWord`, `wordSeq`. The hold-A take-over
stays as UI (`playHoldAt`) but sends `SetOutput(hh)`+`Play`. Risk: gesture-less `play()` in Safari from an SSE event.

(e) **Stop, photos, background.** `Stop`, `PhotoPark/Resume`, `FileGone`, `Slept/Woke`. Removes `stopRequest`
(web), `stopped()`, `parked` as a source of truth (its title/art stay as display), `seekPath/seekTo/seekUntil`,
`stopIfPlaying`'s flag poking, `/api/browser-play stop=1`, `B.leaving`. `bgPlay` stays (it's a screen, not playback).
Risk: Stop semantics change (Q1); the end-of-list path in `stopPlaying(End)`.

(f) **Delete the old routes and flags.** `/api/browser-play`, the `handheld-*` events, `snapshot.browserPlaying`,
`/api/cmd`'s transport verbs (kept as adapters for one release), `elsewhereShown` (derived). Update CLAUDE.md,
architecture.md, FEATURES.md. Risk: an old page cached on a phone; the server answers its old posts with 410 and the
page reloads (`boot` check).

## 8. Open questions

The owner approved this design on 2026-11-11 and took the recommendation on every question below.

1. **Stop while the browser is the output** — stop the page too, or let it play on (today)? **Decided (owner,
   2026-11-11): stop everywhere** (one session, one Stop; the handheld's bar/menu say "Stop" and should mean it). `pref` keeps the browser
   as the target for the next pick.
2. **Output lost: pause, or continue on the handheld?** **Decided (owner, 2026-11-11): pause on the handheld** after 10 s; sound starting
   by itself from a pocket is worse than a tap.
3. **Hidden, paused page closes its SSE** (`openSSE`), which reads as `TabGone`. *Recommend*: keep the stream while
   it's the output (cheap: 15 s pings); else accept falling back to the handheld paused.
4. **Page Next latency** (now a round trip). *Recommend*: optimistic UI (title/index immediately), the load waits for
   the new `v`; measure on the handheld's Wi-Fi.
5. **Persist the session across restarts?** *Recommend: no*; resume.tsv already keeps places and Recent covers it.
6. **Shuffle as a queue order in the session** (today the page shuffles its own `B.queue`). *Recommend*: yes; `Open`
   with the shuffled order, one shuffle for both outputs.
7. **The page as a plain remote while the handheld plays** (iPhone, `NO_HERE`) is just `out = Handheld` with intents
   from the tab: no special case. *Recommend*: keep `NO_HERE` only as "can't be the output".
8. **EQ/effects** don't apply to the browser; the top card's "+4" note stays a pure function of `s` and settings.

## 9. Notes from step (a)

Choices made where the text above was open (the code and tests follow these):

- `itemV`/`outV`/`playV` are set to the **new** `v` (old + 1), so a sender who saw the old `v` is behind them (rule 3).
  `playV` is new: the `v` of the last Play/Pause.
- `seq == 0` means "not deduplicated": the host's own `Tick`, `TabGone`/`TabBack`, `Slept`/`Woke`. A `seq` under an
  origin's last one is a dup too (it gets the last answer back), so same-origin intents that overtake each other drop.
- A rejected intent can still carry `Note`/`PageToast` effects ("This is the first one"); nothing else.
- Reports while `parked` are rejected (`parked`): a late `Pos(playing)` from the handheld would otherwise play under
  the photo (I9).
- A `Pos` report's `playing` is taken only when its `basis ≥ playV`: one sent before the last Play/Pause doesn't undo
  that wish ("A x3"). A `Pos` implies `Loaded`.
- `posNow` runs only once the output said `Loaded` (`Session::loaded`); `Loaded` and `Pos` restamp `posAt`.
- `SetOutput` bumps `load` (the old output's late reports drop; the new one reports for the new load) and clears
  `parked`; to the current output it only sets `pref`; with nothing playing it sets `out`/`pref` and nothing else.
- `Open` without `audio`/`sub` resets the picks to the default (`Step` keeps them); an `Open` that names `out` sets
  `pref` too (the page's Play here / Play in this browser).
- The tab-gone timeout freezes `pos` at `posNow` as of when the tab went, and drops `pref` back to the handheld too
  (else the next pick would go to a tab that isn't there).
- A load nobody answers within 8 s on the **handheld** sets `error = "timeout"` (a later `Loaded` clears it).
- `Step` away from an item in `error` plays the next one; a Prev burst whose restart used up the presses is accepted.
- `Stop` empties the queue (`index = -1`); `out` stays as it was, `pref` too.
- `PlaySource` is mirrored as `SessionSource` (bpcore can't include `player.h`: it pulls in mpv). `TrackPick` gains
  `of` (how many tracks of its kind the chooser listed), which "by place when both list as many" needs.
- `coalesce()` (bpcore) is the executor's pass rule: the last `HhLoad`/`HhUnload` only, seeks/tracks/play only after
  it, the last play/pause only, notes once.
- The shadow lives in `src/session_host.{h,cpp}`, not `main.cpp`, and logs `session: disagree <what>` (output, file,
  tab, playing; only what lasts 1.5 s, one line per 2 s). The old routes carry no `basis`: the shadow uses the `v` of
  the last snapshot published (`publishedV`) for the page's words (`browser-play`, its `stop=1`, `browser-pos`,
  `/api/cmd stop`) and the current `v` for `/api/play`. It hears only what today's guards let through.
  The snapshot carries `session: {boot, v}`, which the page ignores for now.
- First disagreements seen (fake page, Mac): **file** after the handheld's Next/Prev while a page plays (today the
  handheld's file follows only once the page answers `handheld-skip`; a page that never answers leaves it behind for
  good, with no timeout), and **playing** after A while a page plays (today's wish lapses after `WANT_MS` back to the
  page's last word if the page doesn't confirm; the session keeps the wish). Both are what steps (c)/(d) replace.
