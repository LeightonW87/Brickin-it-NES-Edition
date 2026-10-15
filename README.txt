BRICKIN'IT NES EDITION

Build:   make          (or double-click build.bat)
Run:     open brickinit.nes in Mesen   (the file can be renamed freely)
Needs:   cc65\bin on your PATH

TITLE SCREEN
  The logo is made from logo_source.png: the lettering is converted to 133 tiles
  (tiles.chr, from tile $70; the tagline uses $0E-$1F and the top few) and drawn with the first block colour's palette, so it
  changes with the theme. The three blocks above it use the game's own block tiles
  (red, blue, green, as in the Android logo). To change the logo: replace
  logo_source.png (or edit the crop / colour settings at the top of make_logo.py),
  then run   python make_logo.py   (needs Pillow + numpy) and   make.
  The "NES EDITION" line under it is lettering drawn in the same style (dark fill, light
  outline, mid-shade edge) from fonts/FredokaOne-Regular.ttf, by the same tool; change
  the words, size or spacing at the top of make_logo.py. The tool owns tiles $0E-$1F and
  $70-$FF of tiles.chr and rewrites them on every run.
  The game font has no apostrophe, so the full name appears only as the logo graphic.

SCREEN LAYOUT (from your mockup)
  2 blank tile rows, the HUD box (SCORE / LEVEL / TURN / TIME), the framed
  playfield, 2 blank rows. 15 x 10 blocks by default; the playfield frame is
  centred and resizes with GRID_COLS / GRID_ROWS (HUD box is always full width).
  The playfield sits one tile inside its frame, which would put blocks half-way
  across the NES's 16x16 colour cells (so they could not have different colours).
  The game screen is therefore scrolled by 8 pixels to realign them -- invisible
  to the player, but it is why the code talks about "visual" and "nametable"
  positions. Menus are not scrolled.

HOW IT PLAYS
  Title screen: UP/DOWN picks MODE or THEME, LEFT/RIGHT changes it (themes preview
  live on the four sample blocks), START or A begins, SELECT shows the high scores.
  In game: D-pad moves the cursor (hold to repeat), A removes the group of 2+
  same-colored blocks under it. Blocks fall, empty columns slide left.
  Score = n*(n-1) for a group of n (SCORE_MULT in config.h scales it). Clear the
  whole board for a bonus and the next level. If the stack reaches the top when a
  new row is due, it's game over. A new row also rises early if you have no moves
  left (a lifeline).

HUD
  SCORE   points
  LEVEL   rises when you clear the board (and every few rows in TIMED)
  TURN    CLASSIC / RISING: turns left before the next row rises. TIMED: turns taken.
  TIME    TIMED: seconds left before the next row. Other modes: seconds played.

PAUSE: START during a game. RESUME (START, A or B), THEME (LEFT/RIGHT, changes the
  colors live), END GAME (press A twice). The board is hidden while paused.

HIGH SCORES: top 5 per mode with 3-letter initials. After a game that makes the
  table: UP/DOWN change a letter (hold to repeat), A moves on (confirms on the 3rd),
  B goes back, START confirms. Scores are kept in a battery-RAM copy at $6000; the
  ROM header declares battery-backed PRG-RAM. If your emulator/cartridge does not
  keep it, the table simply resets each time the game starts (it always works while
  the game is running).

MODES
  CLASSIC  full board, a new row after every 8 of your moves
  TIMED    starts with 5 rows, a new row every 3 seconds (the clock pauses while
           blocks are moving). Faster each level; level rises every 8 rows.
  RISING   starts with just 2 rows, a new row after every 3 moves
  Each level makes rows come a little sooner. All numbers are in config.h.

THEMES (themes.h): ARCADE, PASTEL, NEON, OCEAN, SUNSET, FOREST
  Each theme is 4 palettes of { backdrop, dark, medium, light }. Palette n colors
  blocks of color n+1. Palette 3 is also the interface palette: text, both frames
  and the playfield floor use it, and its DARK shade is the floor colour.
  (theme_preview.png shows approximate renderings of all six.)

CUSTOMISING: open config.h  (make rebuilds automatically when it changes)
  GRID_COLS (2..15) / GRID_ROWS (2..10)   grid size in blocks
  NUM_COLORS              2..4 block colors
  CLASSIC_* / TIMED_* / RISING_*   start rows and rise speed for each mode
                          (a period of 0 turns rising rows off for that mode)
  SCORE_MULT, CLEAR_BONUS, HS_DEFAULT_STEP   scoring and the starting high scores
  HOLE_DELAY, FALL_DELAY, SLIDE_DELAY, REPEAT_FIRST, REPEAT_NEXT   pace, in frames
  HUD_SCORE_X / HUD_LEVEL_X / HUD_TURN_X / HUD_TIME_X   where each HUD item sits
  TEXT_PAL, FRAME_PAL, FIELD_PAL, LOGO_PAL   which palette (0..3) text, frames, floor and logo use
  T_BLOCK, T_FIELD, T_FRAME, T_LOGO, S_CURSOR, FONT_*   tile numbers of your graphics
The build stops with a message if a combination can't fit the screen.
The mode descriptions on the title screen are fixed text in main.c (D0, D1, D2).

TILE NUMBERS in tiles.chr (pattern table 0): 1-4 block, 5 floor, 6-13 frame
  (TL, T, TR, L, R, BL, B, BR), 0x20.. font, $0E-$1F and $70-$FF title logo + tagline. Table 1: 1-4 cursor. tiles.chr was
  rebuilt pixel-for-pixel from your mockup; if you redraw tiles, keep the numbers
  or change them in config.h.

Files
  config.h    all the settings above
  themes.h    color themes
  main.c      NES-specific code: screens, menus, input, buffered screen updates
  logic.c/.h  game rules: flood fill, gravity, column collapse, rising row (no NES calls)
  tiles.chr   8 KB graphics
  logo.h, logo_source.png, make_logo.py, fonts/   the title logo and tagline (see above)
  nes.cfg     linker config (memory layout, save RAM at $6000)
  chr.s       includes tiles.chr in the ROM
  crt0.s, neslib.h, *.sinc, lz4vram.s   NESLib (Shiru / Lauri Kasanen), see NESLIB_LICENSE.txt

COMPILER NOTE
  cc65's optimizer (-Oisr, set in the Makefile and build.bat) miscompiled an earlier,
  nested if/else version of draw_box() in main.c, which dropped the right and bottom
  frame edges. It is rewritten as straight-line code and verified, but if a screen ever
  draws wrongly in a way the source doesn't explain, suspect the optimizer: rebuilding
  with the -Oisr flags removed from the two cl65 lines in the Makefile is a quick test.
