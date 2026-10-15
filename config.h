// ===========================================================================
//  BRICKIN'IT NES EDITION -- settings. Edit this file to customise the game.
//  (Each value can also be overridden on the compiler command line with -D.)
// ===========================================================================
#ifndef CONFIG_H
#define CONFIG_H

// ---------------------------------------------------------------------------
//  GRID SIZE
//  Blocks are 16x16 pixels = 2x2 tiles. The screen follows your mockup design:
//  two blank tile rows, the HUD box (4 rows), then the framed playfield. The
//  playfield frame is centred and sized to fit the grid.
// ---------------------------------------------------------------------------
#ifndef GRID_COLS
#define GRID_COLS   15     // blocks across   (2..15)
#endif
#ifndef GRID_ROWS
#define GRID_ROWS   10     // blocks down     (2..10)
#endif
#ifndef NUM_COLORS
#define NUM_COLORS  4      // block colors in play (2..4, one background palette each)
#endif

// ---- derived layout: do not edit ------------------------------------------
// "Visual" positions are where things appear on screen, in tiles. The playfield
// starts one tile in from its frame, which would put blocks half-way across the
// NES's 16x16 colour cells. So the game screen is scrolled by SCR_TX/SCR_TY tiles
// to bring them back onto the colour grid; "GRID_LEFT/TOP" are nametable positions.
#define FRAME_VIS_LEFT  (15 - GRID_COLS)          // playfield frame, centred
#define GRID_VIS_LEFT   (FRAME_VIS_LEFT + 1)      // first block column (visual tile)
#define GRID_VIS_TOP    7                         // first block row (visual tile)
#define SCR_TX          (GRID_VIS_LEFT & 1)       // scroll the screen by this many tiles ...
#define SCR_TY          1                         // ... in X and Y
#define GRID_LEFT       (GRID_VIS_LEFT + SCR_TX)  // nametable tile column (always even)
#define GRID_TOP        (GRID_VIS_TOP + SCR_TY)   // nametable tile row (always even)

// ---------------------------------------------------------------------------
//  PACE (in frames; 60 frames = 1 second). 0 = as fast as the screen can draw.
// ---------------------------------------------------------------------------
#ifndef HOLE_DELAY
#define HOLE_DELAY    3    // pause after a group vanishes, before blocks fall
#endif
#ifndef FALL_DELAY
#define FALL_DELAY    1    // extra frames between one-row drops
#endif
#ifndef SLIDE_DELAY
#define SLIDE_DELAY   1    // extra frames between one-column slides
#endif
#ifndef REPEAT_FIRST
#define REPEAT_FIRST  10   // D-pad held: frames before the cursor starts repeating
#endif
#ifndef REPEAT_NEXT
#define REPEAT_NEXT   2    // D-pad held: frames between repeats
#endif

// ---------------------------------------------------------------------------
//  TILE NUMBERS in tiles.chr  (change these if you redraw the graphics)
//  A block uses 4 consecutive tiles: top-left, top-right, bottom-left, bottom-right.
// ---------------------------------------------------------------------------
#ifndef T_BLANK
#define T_BLANK     0      // empty background tile   (pattern table 0)
#endif
#ifndef T_BLOCK
#define T_BLOCK     1      // first block tile        (pattern table 0)
#endif
#ifndef S_CURSOR
#define S_CURSOR    1      // first cursor tile, 4 consecutive (pattern table 1)
#endif
#ifndef T_FIELD
#define T_FIELD     5      // solid tile shown in empty playfield cells
#endif
#ifndef T_FRAME
#define T_FRAME     6      // 8 consecutive frame tiles, in this order:
#endif                     //   top-left, top, top-right, left, right, bottom-left, bottom, bottom-right
#ifndef T_LOGO
#define T_LOGO      0x70   // first of the title-logo tiles (about 133, up to $FF); see make_logo.py,
                           //   which also owns tiles $0E-$1F for the "NES EDITION" lettering
#endif

// ---------------------------------------------------------------------------
//  FONT (tile numbers in pattern table 0 of tiles.chr)
//  Layout matches your NES Screen Tool font: A-Z, a-z, 0-9, then  : ; - _ @
// ---------------------------------------------------------------------------
#ifndef FONT_UPPER
#define FONT_UPPER  0x20   // 'A'..'Z'
#endif
#ifndef FONT_LOWER
#define FONT_LOWER  0x40   // 'a'..'z'
#endif
#ifndef FONT_DIGIT
#define FONT_DIGIT  0x60   // '0'..'9', followed by ':' ';' '-' '_' '@'
#endif

// ---------------------------------------------------------------------------
//  PALETTES. Block color n uses background palette n-1. These choose the palette
//  (0..3) for everything else. Palette 3 is the "interface" palette in every theme:
//  its dark shade is the playfield floor colour (see themes.h).
// ---------------------------------------------------------------------------
#ifndef TEXT_PAL
#define TEXT_PAL    3      // all text
#endif
#ifndef FRAME_PAL
#define FRAME_PAL   3      // the two bevelled frames
#endif
#ifndef FIELD_PAL
#define FIELD_PAL   3      // the playfield floor (empty cells)
#endif
#ifndef LOGO_PAL
#define LOGO_PAL    0      // the title logo's lettering (0 = the first block colour's palette)
#endif

// ---------------------------------------------------------------------------
//  HUD: SCORE / LEVEL / TURN / TIME, inside the top box. Values are visual tile
//  columns of each label (labels are on tile row 3, numbers on row 4; the numbers
//  of LEVEL, TURN and TIME start one tile to the right of their label).
// ---------------------------------------------------------------------------
#ifndef HUD_SCORE_X
#define HUD_SCORE_X 2
#endif
#ifndef HUD_LEVEL_X
#define HUD_LEVEL_X 10
#endif
#ifndef HUD_TURN_X
#define HUD_TURN_X  18
#endif
#ifndef HUD_TIME_X
#define HUD_TIME_X  25
#endif

// ---------------------------------------------------------------------------
//  SCORING AND DIFFICULTY
// ---------------------------------------------------------------------------
#ifndef SCORE_MULT
#define SCORE_MULT  1      // removing a group of n blocks scores n*(n-1)*SCORE_MULT
#endif                     //   (2 blocks = 2 points, 5 = 20, 10 = 90, 20 = 380)
#ifndef CLEAR_BONUS
#define CLEAR_BONUS 100    // bonus (times the level) for clearing the whole board
#endif
#ifndef HS_DEFAULT_STEP
#define HS_DEFAULT_STEP 100  // the empty high score table starts at 5x this, down to 1x this
#endif
// ---------------------------------------------------------------------------
//  GAME MODES (chosen on the title screen)
//  For each mode: how many rows you start with, and what makes a new row rise
//  from the bottom. A period of 0 turns rising rows off for that mode (the game
//  then ends only when you run out of moves). Each level shortens the period.
//  Rows also rise early whenever you are stuck, as a lifeline.
// ---------------------------------------------------------------------------
// CLASSIC: full board, a new row after every N of YOUR moves.
#ifndef CLASSIC_START_ROWS
#define CLASSIC_START_ROWS  GRID_ROWS
#endif
#ifndef CLASSIC_MOVES
#define CLASSIC_MOVES       8      // moves per new row (0 = never)
#endif
#ifndef CLASSIC_MOVES_MIN
#define CLASSIC_MOVES_MIN   3      // each level takes one move off, down to this
#endif

// TIMED: a new row every N seconds, ready or not. (The clock pauses while blocks
// are falling.) Speeds up as the level rises; the level also rises every few rows.
#ifndef TIMED_START_ROWS
#define TIMED_START_ROWS    5
#endif
#ifndef TIMED_SECONDS
#define TIMED_SECONDS       3      // seconds per new row (0 = never)
#endif
#ifndef TIMED_SECONDS_MIN
#define TIMED_SECONDS_MIN   1
#endif
#ifndef TIMED_RAMP
#define TIMED_RAMP          10     // frames taken off the period per level (60 frames = 1 s)
#endif
#ifndef TIMED_LEVEL_ROWS
#define TIMED_LEVEL_ROWS    8      // the level goes up every N rows in this mode
#endif

// RISING: start nearly empty; a new row rises after every N moves.
#ifndef RISING_START_ROWS
#define RISING_START_ROWS   2
#endif
#ifndef RISING_MOVES
#define RISING_MOVES        3      // moves per new row (0 = never)
#endif
#ifndef RISING_MOVES_MIN
#define RISING_MOVES_MIN    1
#endif

// ---------------------------------------------------------------------------
//  Vblank drawing budget (rough CPU cycles per frame spent updating the screen).
//  If you see garbage at the top of the screen during big changes, lower these.
// ---------------------------------------------------------------------------
#ifndef VBLANK_BUDGET
#define VBLANK_BUDGET 1250
#endif
#ifndef MAX_RUN
#define MAX_RUN       12   // longest run of blocks written in one go
#endif

// ---------------------------------------------------------------------------
//  Sanity checks -- the build stops with a message if the layout can't work.
// ---------------------------------------------------------------------------
#if GRID_COLS < 2 || GRID_COLS > 15
#error GRID_COLS must be between 2 and 15
#endif
#if GRID_ROWS < 2 || GRID_ROWS > 10
#error GRID_ROWS must be between 2 and 10 (the HUD box and frames use the rest of the screen)
#endif
#if NUM_COLORS < 2 || NUM_COLORS > 4
#error NUM_COLORS must be between 2 and 4
#endif
#if HUD_SCORE_X < 1 || HUD_TIME_X + 5 > 31 || HUD_SCORE_X + 6 > HUD_LEVEL_X || HUD_LEVEL_X + 6 > HUD_TURN_X || HUD_TURN_X + 5 > HUD_TIME_X
#error HUD items overlap or leave the HUD box; adjust the HUD_*_X values
#endif
#if TEXT_PAL > 3 || FRAME_PAL > 3 || FIELD_PAL > 3 || LOGO_PAL > 3
#error TEXT_PAL, FRAME_PAL, FIELD_PAL and LOGO_PAL must be 0..3
#endif
#if CLASSIC_START_ROWS < 1 || CLASSIC_START_ROWS > GRID_ROWS || TIMED_START_ROWS < 1 || TIMED_START_ROWS > GRID_ROWS || RISING_START_ROWS < 1 || RISING_START_ROWS > GRID_ROWS
#error Every *_START_ROWS value must be between 1 and GRID_ROWS
#endif
#if CLASSIC_MOVES_MIN < 1 || RISING_MOVES_MIN < 1 || TIMED_SECONDS_MIN < 1 || TIMED_LEVEL_ROWS < 1
#error The *_MIN values and TIMED_LEVEL_ROWS must be at least 1
#endif
#if TIMED_SECONDS > 999 || CLASSIC_MOVES > 999 || RISING_MOVES > 999
#error Periods above 999 do not fit the 3-digit HUD counters
#endif

#endif
