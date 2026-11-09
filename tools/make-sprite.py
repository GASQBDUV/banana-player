#!/usr/bin/env python3
"""Generates web/sprite.svg from the G_* glyphs in src/ui.cpp (rows of '#' cells), so the web app's icons are the handheld's."""
import re, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
src = (root / "src" / "ui_glyphs.h").read_text()

# G_WEB isn't in ui_glyphs.h until the handheld side lands; the spec fixes its cells
extra = {
	# "In this browser": a laptop (the web app's own; the handheld has no use for it)
	"LAPTOP": [".#########.", ".#.......#.", ".#.......#.", ".#.......#.", ".#.......#.", ".#########.", "###########"],
	# the web player's fullscreen button: corners out (enter) and in (leave)
	"FULLSCREEN": ["###...###", "#.......#", "#.......#", ".........", ".........", ".........", "#.......#", "#.......#", "###...###"],
	"FULLSCREEN_EXIT": ["..#...#..", "..#...#..", "###...###", ".........", ".........", ".........", "###...###", "..#...#..", "..#...#.."],
	"WEB": ["..#####..", ".#..#..#.", "#..#.#..#", "#########", "#..#.#..#", "#########", "#..#.#..#", ".#..#..#.", "..#####.."]
}

glyphs = {}
for m in re.finditer(r'GLYPH\(G_(\w+),((?:\s*"[.#]*",?)+)\s*\)', src):
	glyphs[m.group(1)] = re.findall(r'"([.#]*)"', m.group(2))
for k, v in extra.items():
	glyphs.setdefault(k, v)

out = ['<svg xmlns="http://www.w3.org/2000/svg" style="display:none">']
for name, rows in sorted(glyphs.items()):
	w, h = max(len(r) for r in rows), len(rows)
	rects = []
	for y, row in enumerate(rows):
		for run in re.finditer(r"#+", row):
			rects.append(f'<rect x="{run.start()}" y="{y}" width="{run.end() - run.start()}" height="1"/>')
	out.append(f'<symbol id="g-{name.lower().replace("_", "-")}" viewBox="0 0 {w} {h}" shape-rendering="crispEdges">{"".join(rects)}</symbol>')
out.append("</svg>")
(root / "web" / "sprite.svg").write_text("\n".join(out) + "\n")
print(f"{len(glyphs)} glyphs -> web/sprite.svg")

# the app icon (same 32x32 grid as packaging/icon/make-icon.py) as web/logo.svg, for the login page and favicon
icon_src = (root / "packaging" / "icon" / "make-icon.py").read_text()
grid = re.search(r'GRID = """\n(.*?)"""', icon_src, re.S).group(1).split("\n")
colours = {".": "#141318", "Y": "#F8C21C", "W": "#F7F4EA"}
rects = []
for y, row in enumerate(grid):
	for c, fill in colours.items():
		for run in re.finditer(re.escape(c) + "+", row):
			rects.append(f'<rect x="{run.start()}" y="{y}" width="{run.end() - run.start()}" height="1" fill="{fill}"/>')
(root / "web" / "logo.svg").write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 32 32" shape-rendering="crispEdges">{"".join(rects)}</svg>\n')
print("web/logo.svg")
