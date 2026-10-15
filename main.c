// BRICKIN'IT NES EDITION
//   * title screen: choose a game MODE and a color THEME, then START (SELECT = high scores)
//   * D-pad moves the cursor (hold to repeat), A removes the group under it
//   * START pauses: resume, change theme, or end the game
//   * top-5 high scores per mode with 3-letter initials (saved to battery RAM if present)
//   * blocks fall a row at a time, then columns slide left a column at a time
//   * score, level, and new rows rising from the bottom (by moves or by the clock)
//   * the screen is updated only where it changed, through NESLib's vblank buffer
// Settings (grid size, speed, modes, font, scoring...) live in config.h; color
// themes live in themes.h. Game rules live in logic.c.
// Graphics: tiles.chr (bank 0 = background + font, bank 1 = sprites).

#include <stddef.h>
#include <string.h>
#include "neslib.h"
#include "logic.h"            // also pulls in config.h
#include "themes.h"
#include "logo.h"          // the title logo's tile map (made by make_logo.py)

// Test hook (does nothing in the real game).
#ifndef PAUSE_HOOK
#define PAUSE_HOOK(x)
#endif

// NESLib's display.sinc expects this zero-page variable (sprite buffer offset).
#pragma bss-name (push, "ZEROPAGE")
unsigned char oam_off;
#pragma bss-name (pop)

#define GRID_LEFT_B  (GRID_LEFT / 2)    // grid origin in 16-pixel units (block columns)
#define GRID_TOP_B   (GRID_TOP / 2)     // ... and block rows
#define DPAD         (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)

#define HUD_SCORE 1                     // bits in hud_dirty: which HUD numbers need redrawing
#define HUD_LEVEL 2
#define HUD_TURN  4
#define HUD_TIME  8

enum { ST_PLAY, ST_FALL, ST_SLIDE, ST_CHECK };

// ---------------------------------------------------------------------------
// Game modes. The numbers come from config.h. The description text is fixed
// wording (30 characters max) -- edit it here if you change a mode a lot.
// ---------------------------------------------------------------------------
#define STR(x)  #x
#define XSTR(x) STR(x)

typedef struct {
	const char   *name;         // shown in the menu, 10 characters max
	const char   *desc;         // shown under the mode name, 30 characters max
	unsigned char start_rows;   // rows of blocks at the start (and after clearing the board)
	unsigned char by_time;      // 1: the period counts frames, 0: it counts your moves
	unsigned int  base;         // period at level 1 (0 = no rising rows)
	unsigned int  min;          // the period never gets shorter than this
	unsigned int  step;         // how much each level shortens the period
} Mode;

#define NUM_MODES 3

#define D0 "FULL BOARD - ROW PER " XSTR(CLASSIC_MOVES) " MOVES"
#define D1 "ROW EVERY " XSTR(TIMED_SECONDS) " SECONDS"
#define D2 "START LOW - ROW PER " XSTR(RISING_MOVES) " MOVES"
// (Descriptions longer than 30 characters are simply cut off when drawn.)

static const Mode modes[NUM_MODES] = {
	{ "CLASSIC", D0, CLASSIC_START_ROWS, 0, CLASSIC_MOVES,       CLASSIC_MOVES_MIN,       1           },
	{ "TIMED",   D1, TIMED_START_ROWS,   1, TIMED_SECONDS * 60,  TIMED_SECONDS_MIN * 60,  TIMED_RAMP  },
	{ "RISING",  D2, RISING_START_ROWS,  0, RISING_MOVES,        RISING_MOVES_MIN,        1           }
};
static const char desc_norise[] = "NO NEW ROWS - PLAY UNTIL STUCK";

// ---------------------------------------------------------------------------
static unsigned char shown[COLS * ROWS];   // what the screen currently displays
static unsigned char upd[192];             // VRAM update buffer, read by the NMI
static unsigned char ulen;
static unsigned char cur_x, cur_y;

static unsigned char mode, theme;          // menu choices (kept between games)
static unsigned long score;
static unsigned char level, hud_dirty;
static unsigned int  moves, turns, elapsed;   // moves = since the last new row; turns = total
static unsigned char sec_frames;
static unsigned int  timer_frames;         // TIMED: frames until the next row
static unsigned char time_up, rows_risen;
static unsigned int  shown_turn, shown_time;  // what the HUD currently shows

const unsigned char palette_spr[16] = {
	0x0f,0x30,0x30,0x30,   // 0: white (cursor)
	0x0f,0x30,0x30,0x30,
	0x0f,0x30,0x30,0x30,
	0x0f,0x30,0x30,0x30
};

static const unsigned long pow10[6] = { 100000UL, 10000UL, 1000UL, 100UL, 10UL, 1UL };

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------
static unsigned char glyph(char c)
{
	if (c >= 'A' && c <= 'Z') return FONT_UPPER + (c - 'A');
	if (c >= 'a' && c <= 'z') return FONT_LOWER + (c - 'a');
	if (c >= '0' && c <= '9') return FONT_DIGIT + (c - '0');
	switch (c) {
	case ':': return FONT_DIGIT + 10;
	case ';': return FONT_DIGIT + 11;
	case '-': return FONT_DIGIT + 12;
	case '_': return FONT_DIGIT + 13;
	case '@': return FONT_DIGIT + 14;
	}
	return T_BLANK;
}

// Rendering must be off for these.
static void put_text(unsigned char x, unsigned char y, const char *s)
{
	vram_adr(NTADR_A(x, y));
	while (*s) vram_put(glyph(*s++));
}

static void clear_nametable(unsigned int adr, unsigned char attr)
{
	vram_adr(adr);
	vram_fill(T_BLANK, 960);
	vram_fill(attr, 64);                       // attribute table
}

// Rendering must be off. Clears both nametables and un-scrolls the screen (menus
// are drawn unscrolled; draw_game_screen() applies the game scroll afterwards).
static void clear_screen(void)
{
	clear_nametable(NAMETABLE_A, TEXT_PAL * 0x55);
	clear_nametable(NAMETABLE_B, TEXT_PAL * 0x55);
	scroll(0, 0);
}

// Score as 6 tile numbers, level / moves as 2.
static void digits6(unsigned long v, unsigned char *out)
{
	unsigned char i, d;
	for (i = 0; i < 6; ++i) {
		d = 0;
		while (v >= pow10[i]) { v -= pow10[i]; ++d; }
		out[i] = FONT_DIGIT + d;
	}
}

static void digits2(unsigned char v, unsigned char *out)
{
	unsigned char t = 0;
	while (v >= 10) { v -= 10; ++t; }
	out[0] = FONT_DIGIT + t;
	out[1] = FONT_DIGIT + v;
}

// 0..999 as 3 tile numbers
static void digits3(unsigned int v, unsigned char *out)
{
	unsigned char h = 0, t = 0;
	while (v >= 100) { v -= 100; ++h; }
	while (v >= 10)  { v -= 10;  ++t; }
	out[0] = FONT_DIGIT + h;
	out[1] = FONT_DIGIT + t;
	out[2] = FONT_DIGIT + (unsigned char)v;
}

// ---------------------------------------------------------------------------
// The game screen is scrolled by (SCR_TX, SCR_TY) tiles (see config.h), so a
// "visual" tile position -- where it appears on screen -- maps to a nametable
// tile one further right/down. The right-hand edge can land in nametable B.
// ---------------------------------------------------------------------------
static unsigned int vis_adr(unsigned char vx, unsigned char vy)
{
	unsigned char x = vx + SCR_TX, y = vy + SCR_TY;
	unsigned int base = NAMETABLE_A;
	if (x >= 32) { x -= 32; base = NAMETABLE_B; }
	if (y >= 30) y -= 30;
	return base | ((unsigned int)y << 5) | x;
}

static void put_vis_text(unsigned char vx, unsigned char vy, const char *s)
{
	vram_adr(vis_adr(vx, vy));
	while (*s) vram_put(glyph(*s++));
}

// ---------------------------------------------------------------------------
// Game state helpers
// ---------------------------------------------------------------------------

// Empties the board, then fills the bottom start_rows rows with random blocks.
static void new_board(void)
{
	unsigned char i, first;
	first = (ROWS - modes[mode].start_rows) * COLS;
	for (i = 0; i < first; ++i) grid[i] = 0;
	for (i = first; i < COLS * ROWS; ++i)
		grid[i] = rand8() % NUM_COLORS + 1;
}

static void add_score(unsigned long pts)
{
	score += pts;
	if (score > 999999UL) score = 999999UL;
	hud_dirty |= HUD_SCORE;
}

// Period between rising rows at the current level, in moves or frames
// (0 = this mode has no rising rows).
static unsigned int rise_period(void)
{
	const Mode *m = &modes[mode];
	unsigned int dec;
	if (!m->base) return 0;
	if (m->base <= m->min) return m->base;
	dec = (unsigned int)(level - 1) * m->step;
	if (dec >= m->base || m->base - dec < m->min) return m->min;
	return m->base - dec;
}

// HUD "TURN": in modes with rising rows counted in moves, the turns left before
// the next row; otherwise the number of turns taken so far.
static unsigned int turn_value(void)
{
	unsigned int p, v;
	if (modes[mode].base && !modes[mode].by_time) {
		p = rise_period();
		v = (p > moves) ? p - moves : 0;
	} else {
		v = turns;
	}
	return (v > 999) ? 999 : v;
}

// HUD "TIME": in TIMED mode the seconds left before the next row (rounded up);
// otherwise the seconds played so far.
static unsigned int time_value(void)
{
	unsigned int v;
	if (modes[mode].base && modes[mode].by_time) v = (timer_frames + 59) / 60;
	else                                         v = elapsed;
	return (v > 999) ? 999 : v;
}

// Returns 0 if the top row is occupied (the stack reached the top = game over).
static unsigned char rise_row(void)
{
	unsigned char row[COLS], i;
	for (i = 0; i < COLS; ++i)
		row[i] = rand8() % NUM_COLORS + 1;
	return push_row_up(row);
}

// ---------------------------------------------------------------------------
// Attributes (palette per 16x16, in nametable space). Blocks use palette
// (color - 1), empty cells FIELD_PAL, the HUD text quadrants TEXT_PAL, and
// everything else (the frames) FRAME_PAL.
// ---------------------------------------------------------------------------
#define TEXT_Q_ROW   ((3 + SCR_TY) >> 1)                  // quadrant row holding the HUD text
#define TEXT_Q_LEFT  ((HUD_SCORE_X + SCR_TX) >> 1)
#define TEXT_Q_RIGHT ((HUD_TIME_X + 4 + SCR_TX) >> 1)

static unsigned char attr_byte(unsigned char ax, unsigned char ayc)
{
	unsigned char q, sx, sy, c, p, v = 0;
	for (q = 0; q < 4; ++q) {
		sx = ax * 2 + (q & 1);                 // 16-pixel cell on the whole nametable
		sy = ayc * 2 + (q >> 1);
		p = FRAME_PAL;
		if (sx >= GRID_LEFT_B && sy >= GRID_TOP_B &&
		    sx - GRID_LEFT_B < COLS && sy - GRID_TOP_B < ROWS) {
			c = shown[(sy - GRID_TOP_B) * COLS + (sx - GRID_LEFT_B)];
			p = c ? c - 1 : FIELD_PAL;
		} else if (sy == TEXT_Q_ROW && sx >= TEXT_Q_LEFT && sx <= TEXT_Q_RIGHT) {
			p = TEXT_PAL;
		}
		v |= p << (q * 2);
	}
	return v;
}

// ---------------------------------------------------------------------------
// Whole-screen draws. Only while rendering is off (startup / new game / unpause).
// ---------------------------------------------------------------------------
static void draw_all(void)                      // the grid and the attribute tables
{
	unsigned char bx, by, c, t, tl, ax, ayc;

	for (by = 0; by < ROWS; ++by) {
		for (t = 0; t < 2; ++t) {
			vram_adr(NTADR_A(GRID_LEFT, GRID_TOP + by * 2 + t));
			for (bx = 0; bx < COLS; ++bx) {
				c = grid[by * COLS + bx];
				tl = c ? T_BLOCK + t * 2 : T_FIELD;
				vram_put(tl);
				vram_put(c ? tl + 1 : T_FIELD);
			}
		}
	}
	for (bx = 0; bx < COLS * ROWS; ++bx) shown[bx] = grid[bx];

	vram_adr(0x23c0);                           // nametable A attributes: 8 x 8 bytes
	for (ayc = 0; ayc < 8; ++ayc)
		for (ax = 0; ax < 8; ++ax)
			vram_put(attr_byte(ax, ayc));
	vram_adr(0x27c0);                           // nametable B (only its first column shows)
	vram_fill(FRAME_PAL * 0x55, 64);
}

static void put_vis(unsigned char vx, unsigned char vy, unsigned char t)
{
	vram_adr(vis_adr(vx, vy));
	vram_put(t);
}

// A bevelled box, w x h tiles, drawn from the 8 frame tiles. Interior untouched.
// (Deliberately written as straight-line code plus two simple loops: a nested
// if / else-if version of this was miscompiled by cc65's optimizer, which dropped
// the right and bottom edges.)
static void draw_box(unsigned char left, unsigned char top, unsigned char w, unsigned char h)
{
	unsigned char x, y;
	unsigned char right  = left + w - 1;
	unsigned char bottom = top + h - 1;

	put_vis(left,  top,    T_FRAME);
	put_vis(right, top,    T_FRAME + 2);
	put_vis(left,  bottom, T_FRAME + 5);
	put_vis(right, bottom, T_FRAME + 7);
	for (x = left + 1; x < right; ++x) {
		put_vis(x, top,    T_FRAME + 1);
		put_vis(x, bottom, T_FRAME + 6);
	}
	for (y = top + 1; y < bottom; ++y) {
		put_vis(left,  y, T_FRAME + 3);
		put_vis(right, y, T_FRAME + 4);
	}
}

// Labels and current values (rendering off).
static void draw_hud(void)
{
	unsigned char t[6], i;

	put_vis_text(HUD_SCORE_X, 3, "SCORE:");
	put_vis_text(HUD_LEVEL_X, 3, "LEVEL:");
	put_vis_text(HUD_TURN_X,  3, "TURN:");
	put_vis_text(HUD_TIME_X,  3, "TIME:");

	shown_turn = turn_value();
	shown_time = time_value();
	digits6(score, t);
	vram_adr(vis_adr(HUD_SCORE_X, 4));
	for (i = 0; i < 6; ++i) vram_put(t[i]);
	digits3(level, t);
	vram_adr(vis_adr(HUD_LEVEL_X + 1, 4));
	for (i = 0; i < 3; ++i) vram_put(t[i]);
	digits3(shown_turn, t);
	vram_adr(vis_adr(HUD_TURN_X + 1, 4));
	for (i = 0; i < 3; ++i) vram_put(t[i]);
	digits3(shown_time, t);
	vram_adr(vis_adr(HUD_TIME_X + 1, 4));
	for (i = 0; i < 3; ++i) vram_put(t[i]);
	hud_dirty = 0;
}

// The whole game screen from the game state, with the game scroll applied.
static void draw_game_screen(void)
{
	clear_screen();
	draw_box(0, 2, 32, 4);                                        // HUD box
	draw_box(FRAME_VIS_LEFT, 6, GRID_COLS * 2 + 2, GRID_ROWS * 2 + 2);   // playfield frame
	draw_all();
	draw_hud();
	scroll(SCR_TX * 8, SCR_TY * 8);
}

// ---------------------------------------------------------------------------
// Buffered updates (rendering on).
// Compares grid[] (the truth) with shown[] (the screen) and queues writes for
// the differences, as many as fit in the vblank budget. Anything left over is
// picked up next frame, so big changes simply take a few frames to appear.
// ---------------------------------------------------------------------------

// Queue a horizontal write of n tile numbers at a nametable address.
static void emit_tiles(unsigned int adr, const unsigned char *t, unsigned char n)
{
	unsigned char i;
	upd[ulen++] = (adr >> 8) | NT_UPD_HORZ;
	upd[ulen++] = adr;
	upd[ulen++] = n;
	for (i = 0; i < n; ++i) upd[ulen++] = t[i];
}

// Queue a write of `n` horizontally adjacent changed blocks starting at (bx0, by).
static void emit_run(unsigned char by, unsigned char bx0, unsigned char n)
{
	unsigned char i, t, c, ax0, ax1, ayc, tl;
	unsigned int adr;

	for (i = 0; i < n; ++i)
		shown[by * COLS + bx0 + i] = grid[by * COLS + bx0 + i];

	// tiles: two tile rows per block row
	for (t = 0; t < 2; ++t) {
		adr = NTADR_A(GRID_LEFT + bx0 * 2, GRID_TOP + by * 2 + t);
		if (n == 1) {
			// a single block is cheaper as four single-byte writes
			c  = shown[by * COLS + bx0];
			tl = c ? T_BLOCK + t * 2 : T_FIELD;
			upd[ulen++] = adr >> 8;
			upd[ulen++] = adr;
			upd[ulen++] = tl;
			upd[ulen++] = adr >> 8;
			upd[ulen++] = adr + 1;
			upd[ulen++] = c ? tl + 1 : T_FIELD;
		} else {
			upd[ulen++] = (adr >> 8) | NT_UPD_HORZ;
			upd[ulen++] = adr;
			upd[ulen++] = n * 2;
			for (i = 0; i < n; ++i) {
				c  = shown[by * COLS + bx0 + i];
				tl = c ? T_BLOCK + t * 2 : T_FIELD;
				upd[ulen++] = tl;
				upd[ulen++] = c ? tl + 1 : T_FIELD;
			}
		}
	}

	// attributes (palette per block). Recomputed from shown[], so a later run
	// that touches the same attribute byte always writes the up-to-date value.
	ayc = (by + GRID_TOP_B) >> 1;
	ax0 = (GRID_LEFT_B + bx0) >> 1;
	ax1 = (GRID_LEFT_B + bx0 + n - 1) >> 1;
	adr = 0x23c0 + ayc * 8 + ax0;
	if (ax0 == ax1) {
		upd[ulen++] = adr >> 8;
		upd[ulen++] = adr;
		upd[ulen++] = attr_byte(ax0, ayc);
	} else {
		upd[ulen++] = (adr >> 8) | NT_UPD_HORZ;
		upd[ulen++] = adr;
		upd[ulen++] = ax1 - ax0 + 1;
		for (i = ax0; i <= ax1; ++i)
			upd[ulen++] = attr_byte(i, ayc);
	}
}

// Rebuilds the update buffer for the next vblank.
// Returns 1 if some changes did NOT fit this frame (a backlog), 0 if the buffer
// now holds everything that differs -- i.e. the screen will be fully up to date
// after the next vblank.
static unsigned char render_updates(void)
{
	unsigned char by, bx, n, i, row, cells = 0;
	unsigned int budget = 0, cost;
	unsigned char t[6];

	ulen = 0;

	// HUD numbers first (they are small): only the ones that changed.
	if (hud_dirty & HUD_SCORE) {
		digits6(score, t);
		emit_tiles(vis_adr(HUD_SCORE_X, 4), t, 6);
		budget += 73 + 16 * 6;
	}
	if (hud_dirty & HUD_LEVEL) {
		digits3(level, t);
		emit_tiles(vis_adr(HUD_LEVEL_X + 1, 4), t, 3);
		budget += 73 + 16 * 3;
	}
	if (hud_dirty & HUD_TURN) {
		digits3(shown_turn, t);
		emit_tiles(vis_adr(HUD_TURN_X + 1, 4), t, 3);
		budget += 73 + 16 * 3;
	}
	if (hud_dirty & HUD_TIME) {
		digits3(shown_time, t);
		emit_tiles(vis_adr(HUD_TIME_X + 1, 4), t, 3);
		budget += 73 + 16 * 3;
	}
	hud_dirty = 0;

	// Fast exit: if the grid is exactly what the screen shows (most frames), there is
	// nothing to scan. (This matters: scanning every cell each frame made the main loop
	// take three frames per pass, so button presses were read only 20 times a second.)
	if (memcmp(grid, shown, COLS * ROWS) == 0) {
		upd[ulen] = NT_UPD_EOF;
		return 0;
	}

	row = 0;                                         // index of the first cell of row `by`
	for (by = 0; by < ROWS; ++by, row += COLS) {
		if (memcmp(grid + row, shown + row, COLS) == 0) continue;   // this row is unchanged
		bx = 0;
		while (bx < COLS) {
			i = row + bx;
			if (grid[i] == shown[i]) { ++bx; continue; }

			n = 1;                                   // extend the run of changed blocks
			while (n < MAX_RUN && bx + n < COLS && grid[i + n] != shown[i + n]) ++n;

			cost = (n == 1) ? 205 : 235 + 72 * n;    // approx. NMI cycles for this run
			if ((cells || budget) && budget + cost > VBLANK_BUDGET) {
				upd[ulen] = NT_UPD_EOF;
				return 1;                            // out of budget: more next frame
			}

			emit_run(by, bx, n);
			budget += cost;
			cells += n;
			bx += n;
		}
	}
	upd[ulen] = NT_UPD_EOF;
	return 0;
}

// ---------------------------------------------------------------------------

static void draw_cursor(void)
{
	unsigned char x = (GRID_VIS_LEFT << 3) + (cur_x << 4);     // sprites are not scrolled:
	unsigned char y = (GRID_VIS_TOP << 3) + (cur_y << 4) - 1;   // use the visual position     // sprites are drawn 1 px lower
	unsigned char id = 0;
	id = oam_spr(x,     y,     S_CURSOR,     0, id);
	id = oam_spr(x + 8, y,     S_CURSOR + 1, 0, id);
	id = oam_spr(x,     y + 8, S_CURSOR + 2, 0, id);
	id = oam_spr(x + 8, y + 8, S_CURSOR + 3, 0, id);
}

// ---------------------------------------------------------------------------
// Menu helpers
// ---------------------------------------------------------------------------
#define MENU_MODE_Y    15         // title screen, in tile rows (logo rows 2-11, tagline 12-13)
#define MENU_DESC_Y    17
#define MENU_THEME_Y   19
#define MENU_PREVIEW_Y 22         // must be even (blocks align to 16-pixel colour cells)
#define PM_PREVIEW_Y   18         // pause menu's sample blocks; must be even
#define MENU_MARK_X    4
#define MENU_LABEL_X   6
#define MENU_VALUE_X   13
#define MENU_VALUE_W   10

static const char *mode_desc(unsigned char m)
{
	return modes[m].base ? modes[m].desc : desc_norise;
}

// Queue `width` tiles of text, padded with blanks (so old text gets erased).
static void emit_text(unsigned char x, unsigned char y, const char *s, unsigned char width)
{
	unsigned int adr = NTADR_A(x, y);
	unsigned char i, ended = 0;
	upd[ulen++] = (adr >> 8) | NT_UPD_HORZ;
	upd[ulen++] = adr;
	upd[ulen++] = width;
	for (i = 0; i < width; ++i) {
		if (!*s) ended = 1;
		upd[ulen++] = ended ? T_BLANK : glyph(*s++);
	}
}

// Queue one tile.
static void emit_tile(unsigned char x, unsigned char y, unsigned char tile)
{
	unsigned int adr = NTADR_A(x, y);
	upd[ulen++] = adr >> 8;
	upd[ulen++] = adr;
	upd[ulen++] = tile;
}

// Four sample blocks, one per palette, so a theme can be judged before playing.
// (Rendering must be off.)
static void draw_preview(unsigned char y)
{
	unsigned char i, x, t, sx, sy, q;
	for (i = 0; i < 4; ++i) {
		x = 8 + i * 4;
		for (t = 0; t < 2; ++t) {
			vram_adr(NTADR_A(x, y + t));
			vram_put(T_BLOCK + t * 2);
			vram_put(T_BLOCK + t * 2 + 1);
		}
		sx = x >> 1;                             // 16-pixel cell of this block
		sy = y >> 1;
		q  = ((sy & 1) << 1) | (sx & 1);         // which quadrant of its attribute byte
		vram_adr(0x23c0 + (sy >> 1) * 8 + (sx >> 1));
		vram_put(((TEXT_PAL * 0x55) & ~(3 << (q * 2))) | (i << (q * 2)));
	}
}

static unsigned char menu_step(unsigned char v, unsigned char n, unsigned char right)
{
	if (right) return (v + 1 == n) ? 0 : v + 1;
	return v ? v - 1 : n - 1;
}

// ---------------------------------------------------------------------------
// High scores: top 5 per mode. The working copy lives in normal RAM. A copy is
// also written to battery RAM ($6000, segment SAVE) so it can survive power-off
// on a cartridge that has it; a checksum guards against garbage.
// ---------------------------------------------------------------------------
#define HS_ENTRIES 5

typedef struct {
	unsigned char magic[2];
	unsigned long score[NUM_MODES][HS_ENTRIES];
	char          name[NUM_MODES][HS_ENTRIES][3];
	unsigned char sum;                           // must stay the last field
} HighScores;

static HighScores hs;                            // working copy
#pragma bss-name (push, "SAVE")
static HighScores hs_save;                       // battery-backed copy
#pragma bss-name (pop)
static char last_name[3];                        // the initials entered last time

static unsigned char hs_checksum(const HighScores *p)
{
	const unsigned char *b = (const unsigned char *)p;
	unsigned int i;
	unsigned char sum = 0x5a;
	for (i = 0; i < offsetof(HighScores, sum); ++i) sum += b[i];     // everything before the checksum field
	return sum;
}

static void hs_defaults(void)
{
	unsigned char m, i;
	hs.magic[0] = 'S';
	hs.magic[1] = 'C';
	for (m = 0; m < NUM_MODES; ++m) {
		for (i = 0; i < HS_ENTRIES; ++i) {
			hs.score[m][i] = (unsigned long)(HS_ENTRIES - i) * HS_DEFAULT_STEP;
			hs.name[m][i][0] = 'N';
			hs.name[m][i][1] = 'E';
			hs.name[m][i][2] = 'S';
		}
	}
}

static void hs_load(void)
{
	if (hs_save.magic[0] == 'S' && hs_save.magic[1] == 'C' && hs_save.sum == hs_checksum(&hs_save))
		memcpy(&hs, &hs_save, sizeof(HighScores));
	else
		hs_defaults();
}

static void hs_store(void)
{
	hs.sum = hs_checksum(&hs);
	memcpy(&hs_save, &hs, sizeof(HighScores));
}

// Position this score would take in the mode's table (HS_ENTRIES = doesn't make it).
static unsigned char hs_rank(unsigned char m, unsigned long sc)
{
	unsigned char i;
	if (sc == 0) return HS_ENTRIES;
	for (i = 0; i < HS_ENTRIES; ++i)
		if (sc > hs.score[m][i]) return i;
	return HS_ENTRIES;
}

static void hs_insert(unsigned char m, unsigned char rank, const char *name, unsigned long sc)
{
	unsigned char i, k;
	for (i = HS_ENTRIES - 1; i > rank; --i) {
		hs.score[m][i] = hs.score[m][i - 1];
		for (k = 0; k < 3; ++k) hs.name[m][i][k] = hs.name[m][i - 1][k];
	}
	hs.score[m][rank] = sc;
	for (k = 0; k < 3; ++k) hs.name[m][rank][k] = name[k];
}

// Rendering must be off.
static void draw_scores(unsigned char m, unsigned char mark)
{
	unsigned char i, k, t[6];

	clear_screen();
	put_text(10, 3, "HIGH SCORES");
	put_text(12, 6, modes[m].name);
	for (i = 0; i < HS_ENTRIES; ++i) {
		vram_adr(NTADR_A(7, 9 + i * 2));
		vram_put(i == mark ? glyph('@') : T_BLANK);
		vram_put(T_BLANK);
		vram_put(FONT_DIGIT + 1 + i);
		vram_put(T_BLANK);
		for (k = 0; k < 3; ++k) vram_put(glyph(hs.name[m][i][k]));
		vram_put(T_BLANK);
		digits6(hs.score[m][i], t);
		for (k = 0; k < 6; ++k) vram_put(t[k]);
	}
	put_text(5, 22, "LEFT RIGHT MODE  START");
}

// Shows the tables; LEFT/RIGHT flips between modes. mark_mode/mark_rank flag a
// fresh entry (pass 255 for none). Leaves with START, A or B.
static void show_scores(unsigned char m, unsigned char mark_mode, unsigned char mark_rank)
{
	unsigned char pad, redraw = 1;

	set_vram_update(0);
	for (;;) {
		if (redraw) {
			ppu_off();
			oam_clear();
			pal_bg(themes[theme]);
			draw_scores(m, (m == mark_mode) ? mark_rank : 255);
			ppu_on_all();
			redraw = 0;
		}
		ppu_wait_nmi();
		pad = pad_trigger(0);
		if (pad & (PAD_START | PAD_A | PAD_B)) break;
		if (pad & (PAD_LEFT | PAD_RIGHT)) {
			m = menu_step(m, NUM_MODES, pad & PAD_RIGHT);
			redraw = 1;
		}
	}
}

#define NM_X       14             // initials entry
#define NM_Y       16
#define NM_LETTERS 1
#define NM_POS     2

static void name_build(unsigned char flags, unsigned char pos)
{
	unsigned char i;
	ulen = 0;
	if (flags & NM_LETTERS)
		for (i = 0; i < 3; ++i) emit_tile(NM_X + i * 2, NM_Y, glyph(last_name[i]));
	if (flags & NM_POS)
		for (i = 0; i < 3; ++i) emit_tile(NM_X + i * 2, NM_Y + 1, (i == pos) ? glyph('-') : T_BLANK);
	upd[ulen] = NT_UPD_EOF;
}

// UP/DOWN change the letter (hold to repeat), A moves on (and confirms on the 3rd),
// B goes back, START confirms. Fills last_name.
static void enter_name(void)
{
	unsigned char t[6], i, pad, held, pos = 0, flags, rep = 0, c;

	set_vram_update(0);
	ppu_off();
	clear_screen();
	oam_clear();
	pal_bg(themes[theme]);
	put_text(9, 7, "NEW HIGH SCORE");
	put_text(10, 10, "SCORE");
	digits6(score, t);
	vram_adr(NTADR_A(16, 10));
	for (i = 0; i < 6; ++i) vram_put(t[i]);
	put_text(8, 13, "ENTER YOUR NAME");
	put_text(5, 21, "UP DOWN LETTER  A NEXT");
	put_text(9, 23, "START WHEN DONE");
	name_build(NM_LETTERS | NM_POS, pos);
	flush_vram_update(upd);
	upd[0] = NT_UPD_EOF;
	set_vram_update(upd);
	ppu_on_all();

	for (;;) {
		ppu_wait_nmi();
		pad  = pad_trigger(0);
		held = pad_state(0);
		flags = 0;

		if ((held & (PAD_UP | PAD_DOWN)) == 0) {
			rep = 0;
		} else {
			if (pad & (PAD_UP | PAD_DOWN))  rep = 14;
			else if (rep)                   --rep;
			if (!rep && !(pad & (PAD_UP | PAD_DOWN))) { pad |= held & (PAD_UP | PAD_DOWN); rep = 5; }
		}

		if (pad & PAD_START) break;
		if (pad & (PAD_UP | PAD_DOWN)) {
			c = last_name[pos];
			if (pad & PAD_UP) c = (c == 'Z') ? 'A' : c + 1;
			else              c = (c == 'A') ? 'Z' : c - 1;
			last_name[pos] = c;
			flags |= NM_LETTERS;
		}
		if ((pad & PAD_RIGHT) && pos < 2) { ++pos; flags |= NM_POS; }
		if ((pad & PAD_LEFT)  && pos > 0) { --pos; flags |= NM_POS; }
		if (pad & PAD_A) {
			if (pos < 2) { ++pos; flags |= NM_POS; }
			else break;
		}
		if ((pad & PAD_B) && pos > 0) { --pos; flags |= NM_POS; }
		name_build(flags, pos);
	}
	set_vram_update(0);
}

// After a game: if the score makes the table, take initials and show it.
static void highscore_flow(void)
{
	unsigned char rank = hs_rank(mode, score);
	if (rank >= HS_ENTRIES) return;
	enter_name();
	hs_insert(mode, rank, last_name, score);
	hs_store();
	show_scores(mode, mode, rank);
}

// ---------------------------------------------------------------------------
// Title screen with the MODE / THEME menu
// ---------------------------------------------------------------------------
#define MN_MODE  1                // what the menu needs to redraw
#define MN_THEME 2
#define MN_MARK  4

static void menu_build(unsigned char flags, unsigned char row)
{
	ulen = 0;
	if (flags & MN_MODE) {
		emit_text(MENU_VALUE_X, MENU_MODE_Y, modes[mode].name, MENU_VALUE_W);
		emit_text(1, MENU_DESC_Y, mode_desc(mode), 30);
	}
	if (flags & MN_THEME)
		emit_text(MENU_VALUE_X, MENU_THEME_Y, theme_names[theme], MENU_VALUE_W);
	if (flags & MN_MARK) {
		emit_tile(MENU_MARK_X, MENU_MODE_Y,  row == 0 ? glyph('@') : T_BLANK);
		emit_tile(MENU_MARK_X, MENU_THEME_Y, row == 1 ? glyph('@') : T_BLANK);
	}
	upd[ulen] = NT_UPD_EOF;
}

// ---------------------------------------------------------------------------
// The title logo: lettering from the generated tile map plus three blocks in a
// staircase (red, blue, green, as in the Android game's logo). Colours come from
// the theme's palettes, so the logo changes with the theme. Rendering must be off.
// The tagline ("NES EDITION") is drawn in the same style and palette.
// ---------------------------------------------------------------------------
#define LOGO_X 4                  // tile position of the lettering (24 x 6 tiles)
#define LOGO_Y 6
#define TAG_X  ((32 - TAG_W) / 2) // the "NES EDITION" lettering, centred under it
#define TAG_Y  12

// Set the palette of the 16x16 colour cell holding tile (tx, ty) in the attribute
// bytes at[] (which cover tile rows 0..15).
static void logo_pal(unsigned char *at, unsigned char tx, unsigned char ty, unsigned char pal)
{
	unsigned char i  = (ty >> 2) * 8 + (tx >> 2);
	unsigned char sh = ((((ty >> 1) & 1) << 1) | ((tx >> 1) & 1)) << 1;
	at[i] = (at[i] & ~(3 << sh)) | (pal << sh);
}

static void logo_block(unsigned char *at, unsigned char tx, unsigned char ty, unsigned char pal)
{
	unsigned char r, c;
	for (r = 0; r < 2; ++r) {
		vram_adr(NTADR_A(tx, ty + r));
		vram_put(T_BLOCK + r * 2);
		vram_put(T_BLOCK + r * 2 + 1);
		for (c = 0; c < 2; ++c) logo_pal(at, tx + c, ty + r, pal);
	}
}

static void draw_logo(void)
{
	unsigned char at[32], x, y, t;

	for (x = 0; x < 32; ++x) at[x] = TEXT_PAL * 0x55;
	for (y = 0; y < LOGO_H; ++y) {
		vram_adr(NTADR_A(LOGO_X, LOGO_Y + y));
		for (x = 0; x < LOGO_W; ++x) {
			t = logo_map[y * LOGO_W + x];
			vram_put(t ? T_LOGO + t - 1 : T_BLANK);
			logo_pal(at, LOGO_X + x, LOGO_Y + y, LOGO_PAL);
		}
	}
	for (y = 0; y < TAG_H; ++y) {
		vram_adr(NTADR_A(TAG_X, TAG_Y + y));
		for (x = 0; x < TAG_W; ++x) {
			vram_put(tag_map[y * TAG_W + x]);
			logo_pal(at, TAG_X + x, TAG_Y + y, LOGO_PAL);
		}
	}
	logo_block(at, 16, 2, 1);                   // green, top right
	logo_block(at, 14, 3, 2);                   // blue
	logo_block(at, 12, 4, 0);                   // red, bottom left
	vram_adr(0x23c0);
	for (x = 0; x < 32; ++x) vram_put(at[x]);
}

static void title_setup(unsigned char row)
{
	set_vram_update(0);
	ppu_off();
	clear_screen();
	oam_clear();
	pal_bg(themes[theme]);
	pal_spr(palette_spr);

	draw_logo();
	put_text(MENU_LABEL_X, MENU_MODE_Y, "MODE");
	put_text(MENU_LABEL_X, MENU_THEME_Y, "THEME");
	draw_preview(MENU_PREVIEW_Y);
	put_text(3, 25, "UP DOWN  LEFT RIGHT  START");
	put_text(5, 27, "SELECT FOR HIGH SCORES");
	menu_build(MN_MODE | MN_THEME | MN_MARK, row);
	flush_vram_update(upd);                     // rendering is off: write the menu text now
	upd[0] = NT_UPD_EOF;
	set_vram_update(upd);
	ppu_on_all();
}

static void title_screen(void)
{
	unsigned int seed = 1;
	unsigned char pad, row = 0, flags;

	title_setup(row);

	for (;;) {                                  // the number of frames until START seeds the RNG
		ppu_wait_nmi();
		++seed;
		pad = pad_trigger(0);
		flags = 0;

		if (pad & (PAD_START | PAD_A)) break;
		if (pad & PAD_SELECT) {
			show_scores(mode, 255, 0);
			title_setup(row);
			continue;
		}
		if (pad & (PAD_UP | PAD_DOWN)) { row ^= 1; flags |= MN_MARK; }
		if (pad & (PAD_LEFT | PAD_RIGHT)) {
			if (row == 0) {
				mode = menu_step(mode, NUM_MODES, pad & PAD_RIGHT);
				flags |= MN_MODE;
			} else {
				theme = menu_step(theme, NUM_THEMES, pad & PAD_RIGHT);
				pal_bg(themes[theme]);          // live preview
				flags |= MN_THEME;
			}
		}
		menu_build(flags, row);                 // sent at the next vblank
	}
	set_vram_update(0);
	set_rand(seed);
}

// ---------------------------------------------------------------------------
// Pause menu: RESUME / THEME / END GAME. The board is hidden while paused and
// redrawn from the game state afterwards. Returns 1 if the player ends the game.
// ---------------------------------------------------------------------------
#define PM_RESUME_Y 10
#define PM_THEME_Y  12
#define PM_END_Y    14
#define PB_THEME 1
#define PB_MARK  2
#define PB_END   4

static void pause_build(unsigned char flags, unsigned char row, unsigned char confirm)
{
	ulen = 0;
	if (flags & PB_THEME)
		emit_text(MENU_VALUE_X, PM_THEME_Y, theme_names[theme], MENU_VALUE_W);
	if (flags & PB_END)
		emit_text(MENU_LABEL_X, PM_END_Y, confirm ? "PRESS A AGAIN" : "END GAME", 14);
	if (flags & PB_MARK) {
		emit_tile(MENU_MARK_X, PM_RESUME_Y, row == 0 ? glyph('@') : T_BLANK);
		emit_tile(MENU_MARK_X, PM_THEME_Y,  row == 1 ? glyph('@') : T_BLANK);
		emit_tile(MENU_MARK_X, PM_END_Y,    row == 2 ? glyph('@') : T_BLANK);
	}
	upd[ulen] = NT_UPD_EOF;
}

static unsigned char pause_menu(void)
{
	unsigned char pad, row = 0, confirm = 0, flags, result = 0;

	PAUSE_HOOK(1);
	set_vram_update(0);
	ppu_off();
	clear_screen();
	oam_clear();
	put_text(13, 6, "PAUSED");
	put_text(MENU_LABEL_X, PM_RESUME_Y, "RESUME");
	put_text(MENU_LABEL_X, PM_THEME_Y, "THEME");
	draw_preview(PM_PREVIEW_Y);
	put_text(9, 22, "START RESUMES");
	pause_build(PB_THEME | PB_MARK | PB_END, row, confirm);
	flush_vram_update(upd);
	upd[0] = NT_UPD_EOF;
	set_vram_update(upd);
	ppu_on_all();

	for (;;) {
		ppu_wait_nmi();
		pad = pad_trigger(0);
		flags = 0;

		if (pad & (PAD_START | PAD_B)) { result = 0; break; }
		if (pad & (PAD_UP | PAD_DOWN)) {
			if (pad & PAD_DOWN) row = (row == 2) ? 0 : row + 1;
			else                row = row ? row - 1 : 2;
			if (confirm) { confirm = 0; flags |= PB_END; }
			flags |= PB_MARK;
		}
		if ((pad & (PAD_LEFT | PAD_RIGHT)) && row == 1) {
			theme = menu_step(theme, NUM_THEMES, pad & PAD_RIGHT);
			pal_bg(themes[theme]);
			flags |= PB_THEME;
		}
		if (pad & PAD_A) {
			if (row == 0) { result = 0; break; }
			if (row == 2) {
				if (confirm) { result = 1; break; }
				confirm = 1;                    // ask once more before ending the game
				flags |= PB_END;
			}
		}
		pause_build(flags, row, confirm);
	}
	set_vram_update(0);
	return result;
}

static void game_over_screen(void)
{
	unsigned char t[6], i, pad, wait = 45;

	set_vram_update(0);
	ppu_off();
	clear_screen();
	oam_clear();
	pal_bg(themes[theme]);
	put_text(11, 8, "GAME OVER");
	put_text(9, 12, "SCORE");
	digits6(score, t);
	vram_adr(NTADR_A(15, 12));
	for (i = 0; i < 6; ++i) vram_put(t[i]);
	put_text(9, 14, "LEVEL");
	digits2(level, t);
	vram_adr(NTADR_A(15, 14));
	vram_put(t[0]); vram_put(t[1]);
	put_text(9, 16, "MODE");
	put_text(15, 16, modes[mode].name);
	put_text(10, 20, "PRESS START");
	ppu_on_all();

	for (;;) {
		ppu_wait_nmi();
		pad = pad_trigger(0);
		if (wait) --wait;                       // short lockout so a button mash doesn't skip this
		else if (pad & PAD_START) break;
	}
}

// ---------------------------------------------------------------------------
static void play_game(void)
{
	unsigned char pad, held, state, timer, backlog, rep, n, over, due;
	unsigned int tv;

	score = 0;
	level = 1;
	moves = 0;
	turns = 0;
	elapsed = 0;
	sec_frames = 0;
	rows_risen = 0;
	time_up = 0;
	hud_dirty = 0;
	timer_frames = modes[mode].by_time ? rise_period() : 0;

	ppu_off();
	pal_bg(themes[theme]);
	new_board();
	draw_game_screen();

	upd[0] = NT_UPD_EOF;       // empty buffer until something changes
	set_vram_update(upd);

	cur_x = COLS / 2;
	cur_y = ROWS / 2;
	state = ST_CHECK;          // makes sure the opening board has a move
	timer = 0;
	rep = 0;
	over = 0;

	ppu_on_all();

	while (!over) {
		ppu_wait_nmi();                 // the NMI just sent last frame's buffer to the PPU
		backlog = render_updates();     // build this frame's buffer (sent at the next vblank)

		pad  = pad_trigger(0);          // buttons pressed this frame
		held = pad_state(0);            // buttons currently down

		if (pad & PAD_START) {          // pause: resume, change theme, or end the game
			if (pause_menu()) {
				over = 1;
			} else {
				ppu_off();              // bring the board back from the game state
				pal_bg(themes[theme]);
				draw_game_screen();
				upd[0] = NT_UPD_EOF;
				set_vram_update(upd);
				ppu_on_all();
				PAUSE_HOOK(0);
			}
			rep = 0;
			continue;
		}

		// D-pad auto-repeat: move on press, pause, then repeat quickly while held
		if (held & DPAD) {
			if (pad & DPAD)        rep = REPEAT_FIRST;
			else if (rep)          --rep;
			if (!rep && !(pad & DPAD)) { pad |= held & DPAD; rep = REPEAT_NEXT; }
		} else {
			rep = 0;
		}

		if ((pad & PAD_LEFT)  && cur_x > 0)        --cur_x;
		if ((pad & PAD_RIGHT) && cur_x < COLS - 1) ++cur_x;
		if ((pad & PAD_UP)    && cur_y > 0)        --cur_y;
		if ((pad & PAD_DOWN)  && cur_y < ROWS - 1) ++cur_y;

		if (state == ST_PLAY) {
			if (pad & PAD_A) {
				n = remove_group(cur_x, cur_y);
				if (n) {
					add_score((unsigned long)n * (n - 1) * SCORE_MULT);
					++moves;
					if (turns < 999) ++turns;
					state = ST_FALL;
					timer = HOLE_DELAY;
				}
			}
			// TIMED mode: the clock only runs while you are thinking, not while blocks move
			if (state == ST_PLAY && timer_frames) {
				if (--timer_frames == 0) {
					time_up = 1;
					state = ST_CHECK;
				}
			}
		} else if (!backlog) {          // the screen is caught up: take the next step
			if (timer) {
				--timer;
			} else if (state == ST_FALL) {
				if (gravity_step()) timer = FALL_DELAY;
				else                state = ST_SLIDE;
			} else if (state == ST_SLIDE) {
				if (slide_step())   timer = SLIDE_DELAY;
				else                state = ST_CHECK;
			} else {                    // ST_CHECK: everything has settled -- what now?
				due = modes[mode].by_time ? time_up
				                          : (modes[mode].base && moves >= rise_period());
				if (board_empty()) {                         // cleared it: next level
					if (level < 99) ++level;
					add_score((unsigned long)CLEAR_BONUS * level);
					moves = 0;
					time_up = 0;
					timer_frames = modes[mode].by_time ? rise_period() : 0;
					new_board();
					hud_dirty |= HUD_LEVEL;
					// stay in ST_CHECK: make sure the new board has a move
				}
				else if (due || no_moves_left()) {
					if (!modes[mode].base) {
						over = 1;                            // no rising rows in this mode: stuck = over
					} else {
						// time for a new row -- or the player is stuck, so give them one
						moves = 0;
						time_up = 0;
						if (modes[mode].by_time) {
							timer_frames = rise_period();
							if (++rows_risen >= TIMED_LEVEL_ROWS) {
								rows_risen = 0;
								if (level < 99) ++level;
								hud_dirty |= HUD_LEVEL;
							}
						}
						if (!rise_row()) over = 1;           // no room: game over
					}
				}
				else {
					state = ST_PLAY;
				}
			}
		}

		// The clock for the HUD's seconds, and refresh the TURN / TIME numbers if they changed.
		if (++sec_frames >= 60) {
			sec_frames = 0;
			if (elapsed < 999) ++elapsed;
		}
		tv = turn_value();
		if (tv != shown_turn) { shown_turn = tv; hud_dirty |= HUD_TURN; }
		tv = time_value();
		if (tv != shown_time) { shown_time = tv; hud_dirty |= HUD_TIME; }

		oam_clear();
		draw_cursor();
	}

	set_vram_update(0);
	scroll(0, 0);
}

void main(void)
{
	ppu_off();
	bank_bg(0);
	bank_spr(1);
	logic_init();
	hs_load();
	last_name[0] = last_name[1] = last_name[2] = 'A';

	for (;;) {
		title_screen();
		play_game();
		game_over_screen();
		highscore_flow();
	}
}
