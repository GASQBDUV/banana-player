# Midnight theme (rounded + edge): implementation spec

Picked by the owner on 2026-11-09 from `themes.html` (the theme "Midnight", corners "Rounded + edge"). It replaces
Classic's colours everywhere; layout and glyphs stay as they are. All sizes are in the bottom panel's 640×480 layout
units (multiply by `sx`/`sy`, 1.6 on the RG DS Plus, and round to whole device pixels).

## Palette

| Role | Hex | Replaces (Classic) | Used for |
|---|---|---|---|
| `BG` | `#000000` | `#17161C` | the panel's background |
| `SURFACE` | `#121116` | `#211F28` | tiles, Recent cards, dialogs' bodies, empty states |
| `TEAL` (button) | `#26242E` | `#234346` | buttons, unselected tabs, the focused row's fill |
| `PRESSED` | `#3E3B4A` | `#3A6E72` | a button while held |
| `YELLOW` (accent) | `#FFD23F` | `#F0B429` | key hints, Playing, bar fill, selected tab, cursor's edge and text |
| ink on accent | `#000000` | `#17161C` (BG) | text and glyphs on a yellow fill (selected tab, chips) |
| `CREAM` (text) | `#E8E2D3` | `#F1EDE2` | text, glyphs |
| `DIM` | `#8F8A80` | `#8A867C` | secondary text on BG / SURFACE, disabled ink |
| `DIM_TEAL` (dim on button) | `#ABA597` | `#B4AFA3` | second lines on buttons and on the focused row ("Playlist", "Sound") |
| `RED` (danger) | `#F0645A` | `#E05A4F` | warnings, danger buttons: put **black** (`#000000`) text on a red fill, cream reads 2.4:1 |
| `SCREEN` | `#26242E` | `#3A3A44` | the subtitle preview box |
| bar track | `#1E1D24` | `#234346` (TEAL) | the progress bar's empty part (and other tracks/sliders) |
| bar knob | `#E8E2D3` | `#F1EDE2` | the progress bar's knob |
| keyboard special keys | `#1E1D24` | `#1E3337` (ui.cpp ~5121) | |
| dimmed chip | `#1E1D24` | `#2A2933` (ui.cpp ~6349) | |
| veil | `rgba(0,0,0,170)` | `{0x17,0x16,0x1C,170}` (ui.cpp ~4708) | |

Hard-coded copies to change too: `drawDialogRow` compares an ink to `CREAM` (keep comparing to the new CREAM).

### Top panel (player.cpp, ASS colours are `&HBBGGRR&`)

| Role | Hex | ASS | Replaces |
|---|---|---|---|
| idle title "Banana Player", logo swirl, volume/L2-R2 card fill, "Playing in your browser" | `#FFD23F` | `&H3FD2FF&` | `&H29B4F0&` |
| card text | `#E8E2D3` | `&HD3E2E8&` | `&HE2EDF1&` |
| idle subtitle ("Pick a video…", "Hold A to play here") | `#8F8A80` | `&H808A8F&` | `&H7C868A&` |
| card body (volume, chapters, titles' plate), browser-play backdrop | `#0B0A0D` | `&H0D0A0B&` | `&H1C1617&` |
| card bar track | `#26242E` | `&H2E2426&` | `&H464323&` |
| logo's dark square (the `.` cells, if drawn) | `#121116` | `&H161112&` | |

The visualizer colours (lavfi graphs) and the app icon stay as they are.

### Web dashboard (web/style.css `:root`)

```
--bg: #000000; --surface: #121116; --screen: #26242E; --teal: #26242E; --pressed: #3E3B4A; --cream: #E8E2D3;
--dim: #8F8A80; --dim-teal: #ABA597; --yellow: #FFD23F; --red: #F0645A; --scrim: rgba(0, 0, 0, .75);
--edge: #605D60;        /* new: edge of a button (mix rule below) */
--edge-surface: #52504F; /* new: edge of a .card / surface panel */
```
Hard-coded ones: `.btn.primary:active` `#d99d18` → `#E0B937`; `.btn.primary:hover` `#ffc94a` → `#FFE68A`.
`.btn.danger` text → `#000000`. Borders that use `--teal` around a `--surface` card → `--edge-surface`.
Radii as below (buttons 8 px, cards and panels 12 px, progress tracks 5 px, segmented controls: outer corners only).

## Corners

Smooth (anti-aliased) quarter circles, as BananaOS's menu draws them (`rnds::paintMask`), not stepped.

| Element | Radius (640×480 units) |
|---|---|
| buttons: grid buttons (108×60), bottom row (Lock, Info, Screen off, Quit), back button, bar buttons, dialog buttons | 10 |
| tabs (START menu, Tracks/Sound tabs), list rows' focus box (browser, settings rows, menus), section markers | 10 |
| tiles and cards: Home's Recent cards and section tiles, Settings group tiles | 14 |
| dialogs / pop-ups (update pop-up, confirms, the X menu panel) | 18 |
| small chips (EQ preset chips, badges) | 6 |
| progress bar track | 6 (the yellow fill is rounded on its left end only; the knob 2) |
| text fields, keyboard keys | 4 |
| top panel ASS cards (960×720 ASS units) | 18 (the card's 3 px-high bars: 4) |
| web dashboard | buttons 8, cards 12, tracks 5 |

Anything drawn inside a rounded box that touches its edge (the Recent card's progress line, the bar's fill) is
clipped to the box or given the same corners.

## Edge and lip (every button, tab, tile, card, dialog)

- **Edge:** 2 units wide (3 px on the RG DS Plus), drawn inside the box along its rounded outline. Colour = the fill
  mixed 30% towards CREAM: `edge = fill + (CREAM - fill) * 0.3` per channel.
  - button `#26242E` → `#605D60`; pressed `#3E3B4A` → `#716D73`; surface `#121116` → `#52504F`;
    yellow `#FFD23F` → `#F8D76B`; red `#F0645A` → `#EE8A7E`; track `#1E1D24` → `#5B5858`.
- **Lip:** a 3-unit band (5 px on the Plus) at the box's bottom, just above the bottom edge, in the fill darkened by
  30% (`fill * 0.7`): button `#1B1920`, pressed `#2B2934`, surface `#0D0C0F`, yellow `#B2932C`, red `#A8463F`.
  Content stays centred in the whole box, as today.
- Not on: text fields, keyboard keys, the progress bar, chips (radius only).

## Focus / cursor

- The focused button, row, tile or card keeps its fill (`#26242E`; a tile's fill turns from SURFACE to `#26242E`).
- Its edge becomes the accent: 2 units of `#FFD23F` along the rounded outline (instead of the grey edge). The lip stays.
- Its text and glyph turn `#FFD23F`; its second line `#ABA597`.
- Selected tab: yellow fill, black text, edge `#F8D76B`, lip `#B2932C`.
- Pressed: fill `#3E3B4A`, edge `#716D73`, lip `#2B2934`; text stays CREAM.
- Disabled: ink `#8F8A80` (4.4:1 on a button, fine for an inactive control).

## Contrast (WCAG)

| Pair | Ratio |
|---|---|
| CREAM on BG / SURFACE / button / pressed | 16.3 / 14.5 / 11.8 / 8.4 |
| DIM on BG / SURFACE | 6.1 / 5.5 |
| DIM_TEAL on button | 6.2 |
| YELLOW on BG / button (key hints, focus text) | 14.5 / 10.6 |
| black on YELLOW (selected tab) | 14.5 |
| RED on BG | 6.7; black on RED 6.7 (cream on RED only 2.4: don't) |
| YELLOW fill on track `#1E1D24` | 11.6 |
| button / SURFACE against BG | 1.4 / 1.1: the edge (`#605D60` / `#52504F`) (3.3 / 2.6:1 on black) is what outlines them |
| top: title on black 14.5, subtitle 6.1, card text on `#0B0A0D` 15.3 |
