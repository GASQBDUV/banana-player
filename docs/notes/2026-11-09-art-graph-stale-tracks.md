# Art graph on a stale track list (2026-11-09)

## Problem

Songs could go silent and freeze at about 0.9 s in art + visualizer mode. The log:
`[error] cplayer: Pad vid1 is not connected to anything.`

## Cause

`Player::load()` clears the tracks, but mpv can still deliver a track-list change carrying the previous file's tracks
(mpv delivers queued events before property changes). `FILE_LOADED` then arrived with stale tracks: a song without art
after one with art got the art graph (`[vid1]...`). mpv tore down the graph and both outputs, leaving `aid` and `vid`
unset.

## Fix (`src/player.cpp`)

- `loadfile` asks for a reply carrying `playlist_entry_id` (`loadEntry`); `START_FILE` records `startedEntry`; track-list
  updates are ignored until our entry has started (`tracksMine`).
- Safety net in `checkAudio()`: a graph with no audio track chosen counts as a failed graph and is redrawn without art,
  or with no graph (at most 3 tries), logged as `the music's graph failed ...`.
- A `music: <file>: has/no art, N tracks; graph ...` log line at each song's start.

## Seen once, not explained

A 192 kHz FLAC with a `cover.jpg` beside it started as "no art". mpv may load external covers in a worker thread. Not
reproduced in about 10 runs. If it comes back, the `music:` line says what the track list held at the start.
