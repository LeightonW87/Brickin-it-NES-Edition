#include "logic.h"

#if COLS * ROWS > 255
#error Grid has more than 255 cells
#endif

#define MARK 0x80
#define BOTTOM_ROW (COLS * (ROWS - 1))      // index of the first cell in the bottom row

unsigned char grid[COLS * ROWS];
static unsigned char stack[COLS * ROWS];    // each entry packs a cell as (row << 4) | col
static unsigned char rowoff[ROWS];          // rowoff[y] = y * COLS (avoids multiplying)

void logic_init(void)
{
	unsigned char y, o = 0;
	for (y = 0; y < ROWS; ++y) { rowoff[y] = o; o += COLS; }
}

unsigned char remove_group(unsigned char x, unsigned char y)
{
	unsigned char start = rowoff[y] + x;
	unsigned char color = grid[start];
	unsigned char sp = 0, count = 0, i, n, p;

	if (color == 0) return 0;

	grid[start] |= MARK;
	stack[sp++] = (y << 4) | x;

	while (sp) {
		p = stack[--sp];
		x = p & 15;
		y = p >> 4;
		i = rowoff[y] + x;
		++count;

		if (x > 0) {                                  // left
			n = i - 1;
			if (grid[n] == color) { grid[n] |= MARK; stack[sp++] = p - 1; }
		}
		if (x < COLS - 1) {                           // right
			n = i + 1;
			if (grid[n] == color) { grid[n] |= MARK; stack[sp++] = p + 1; }
		}
		if (y > 0) {                                  // up
			n = i - COLS;
			if (grid[n] == color) { grid[n] |= MARK; stack[sp++] = p - 16; }
		}
		if (y < ROWS - 1) {                           // down
			n = i + COLS;
			if (grid[n] == color) { grid[n] |= MARK; stack[sp++] = p + 16; }
		}
	}

	// Marked cells are cleared if the group was big enough, otherwise restored.
	for (i = 0; i < COLS * ROWS; ++i) {
		if (grid[i] & MARK)
			grid[i] = (count >= 2) ? 0 : color;
	}
	return (count >= 2) ? count : 0;
}

void apply_gravity(void)
{
	unsigned char x, r, w, i, c;
	for (x = 0; x < COLS; ++x) {
		w = i = BOTTOM_ROW + x;                    // w = next free slot, i = cell being read
		for (r = 0; r < ROWS; ++r) {
			c = grid[i];
			if (c) {
				if (w != i) { grid[w] = c; grid[i] = 0; }
				w -= COLS;
			}
			i -= COLS;
		}
	}
}

void collapse_columns(void)
{
	unsigned char src, dest = 0, b;
	for (src = 0; src < COLS; ++src) {
		if (grid[BOTTOM_ROW + src]) {              // bottom cell filled = column not empty
			if (dest != src) {
				for (b = 0; b < COLS * ROWS; b += COLS) {
					grid[b + dest] = grid[b + src];
					grid[b + src] = 0;
				}
			}
			++dest;
		}
	}
}

unsigned char gravity_step(void)
{
	unsigned char i, moved = 0;
	// Walk the grid backwards (bottom row first) so a whole stack drops together.
	for (i = BOTTOM_ROW; i > 0; ) {
		--i;                                       // i = a cell that has a row below it
		if (grid[i] && !grid[i + COLS]) {
			grid[i + COLS] = grid[i];
			grid[i] = 0;
			moved = 1;
		}
	}
	return moved;
}

unsigned char slide_step(void)
{
	unsigned char e, x, b;
	// first empty column (only meaningful once gravity has finished)
	for (e = 0; e < COLS; ++e)
		if (!grid[BOTTOM_ROW + e]) break;
	if (e == COLS) return 0;
	// nothing to slide if every column to its right is empty too
	for (x = e + 1; x < COLS; ++x)
		if (grid[BOTTOM_ROW + x]) break;
	if (x == COLS) return 0;

	for (b = 0; b < COLS * ROWS; b += COLS) {
		for (x = e; x < COLS - 1; ++x)
			grid[b + x] = grid[b + x + 1];
		grid[b + COLS - 1] = 0;
	}
	return 1;
}

unsigned char no_moves_left(void)
{
	unsigned char i = 0, x, y, c;
	for (y = 0; y < ROWS; ++y) {
		for (x = 0; x < COLS; ++x, ++i) {
			c = grid[i];
			if (!c) continue;
			if (x < COLS - 1 && grid[i + 1] == c) return 0;
			if (y < ROWS - 1 && grid[i + COLS] == c) return 0;
		}
	}
	return 1;
}

unsigned char board_empty(void)
{
	unsigned char i;
	for (i = 0; i < COLS * ROWS; ++i)
		if (grid[i]) return 0;
	return 1;
}

unsigned char push_row_up(const unsigned char *newrow)
{
	unsigned char i;
	for (i = 0; i < COLS; ++i)
		if (grid[i]) return 0;                     // top row occupied: no room
	for (i = 0; i < BOTTOM_ROW; ++i)
		grid[i] = grid[i + COLS];
	for (i = 0; i < COLS; ++i)
		grid[BOTTOM_ROW + i] = newrow[i];
	return 1;
}
