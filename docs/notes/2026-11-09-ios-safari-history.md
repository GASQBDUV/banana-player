# iOS Safari history in the web dashboard (2026-11-09)

## Problem

On iPhone, after scanning the QR code (the page opens straight on the player, without a tap), the player's chevron did
`history.go(-1)`. Safari skips, going back, an entry that a step made without user activation, so it left the page
(the loading indicator) and playback stopped. An entry from an earlier load of the page (after a reload) also makes
`go(-1)` reload. Chrome doesn't skip a programmatic `go(-1)`, so the headless Chrome tests passed.

## Fix (`web/app.js`)

- Each history entry carries `bpDoc` (this load's id), `bpIdx` (how many of ours lie behind) and `bpOk` (made with a
  tap: `navigator.userActivation.isActive`).
- The chevron goes back only when `canGoBack()`; otherwise it replaces (`replaceHash`).
- The direct-open path no longer pushes an entry without a tap.
- A `popstate` with a null state (a fragment change fires one too) is left to `hashchange`.
- Swipe-back uses `e.hasUAVisualTransition` to skip our own View Transition.

## Also

Chrome's two-finger swipe Back was killed by `overscroll-behavior: none/contain` on the x axis. Keep overscroll
blocking on the y axis only.

Playing in the browser was off on iPhone and iPad (`NO_HERE`) until 0.5.5, when the owner tested it there and it worked.

## Testing

Headless Chrome cannot show this; use WebKit or a real iPhone.
