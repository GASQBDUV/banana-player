# Design mocks

- `themes.html`: the theme study of 2026-11-09. Open it in a browser (it loads Pixelify Sans and Inter from Google
  Fonts). Five dark palettes drawn on the bottom panel's real 640×480 layout (the playing screen of a song, Home,
  Settings' groups, the top panel's idle screen and volume card) plus the web dashboard, with each palette's hex
  values and contrast ratios, and a Square / Rounded / Rounded + edge switch for the corners. Keys 1–5 switch themes;
  `?t=N&c=square|round|edge` opens one directly.
- `midnight.md`: the chosen theme as an implementation spec (every colour, radius, edge, lip, focus rule, ratio).

## The choice

**Midnight, Rounded + edge.** True black behind the bottom panel matches the black around a video on the top one,
so the two screens read as one device in a dark room; a brighter yellow (`#FFD23F`) and a softer cream (`#E8E2D3`)
keep text crisp on black. The rounded corners with a 2-unit edge and a darker bottom lip come from BananaOS's own
menu and BananaDS's in-game menu (`ui/rnds/RndsUI.cpp`'s box styles, `ui/src/nds/pages.cpp`'s `slot`/`pick`/`hero`
boxes in JorreFog/BananaLinux): radius 10 on buttons, tabs and rows, 14 on tiles and cards, 18 on dialogs. On black,
the edge is also what outlines a button (its fill is only 1.4:1 off the page).

## The others, for later

- **Classic:** today's look (BananaOS's ink and yellow with the player's teal).
- **Banana OS:** BananaOS's plum-grey panels (`#3A3540`, `#2E2B33`), a solid yellow cursor with dark ink and a white
  edge (how BananaOS picks a row), and its faint dot pattern on the background. The most "native" one.
- **Sunset:** plum `#1A0C2E` with an orange `#FF8C42` accent, from BananaOS's Sunset theme.
- **Ocean:** navy with a cyan `#3CC6E8` accent, from BananaOS's Ocean theme; the closest cousin of Classic.

These could come back as a theme setting (the mock suggests it under Settings › Controls & screen).
