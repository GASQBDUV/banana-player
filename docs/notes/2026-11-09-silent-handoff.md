# Silent handheld after a browser hand-off (2026-11-09)

## Problem

When the web dashboard plays a file "in this browser", `/api/browser-play` opens the same file on the handheld paused
(`Player::holdPaused`) so its screens show it. On the handheld, an ALSA output opened while paused and never started
stayed silent once played, while the picture and the visualizer (which reads the sound before the output) ran on.
macOS CoreAudio didn't show it.

## Fix

The first play after such a hold issues mpv `ao-reload` (`Player::wakeHeld`), logged as
`audio: <file> held paused for a page: the sound output reopened as it plays`.

Owner confirmation on the handheld is pending.

## Verifying

`BP_AO_PCM`'s recording is untimed and misled here: it showed sound while the player was paused. Check mpv's
properties instead (`current-ao`, `audio-out-params`, with the `prop:` step) or listen on the handheld.
