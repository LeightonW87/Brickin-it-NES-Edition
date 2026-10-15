// Pure game rules: no NES hardware calls, so they can be tested on a PC.
#ifndef LOGIC_H
#define LOGIC_H

#include "config.h"

#define COLS GRID_COLS
#define ROWS GRID_ROWS

// grid[row * COLS + col]: 0 = empty, 1..NUM_COLORS = color. Row 0 is the TOP row.
extern unsigned char grid[COLS * ROWS];

// Call once at startup.
void logic_init(void);

// Removes the connected group (orthogonal, same color) containing the block at
// column x, row y, if it has 2+ blocks. Returns the number removed (0 = nothing).
unsigned char remove_group(unsigned char x, unsigned char y);

// Instant versions: blocks fall to the bottom, empty columns squeeze out to the left.
void apply_gravity(void);
void collapse_columns(void);

// Animation-friendly versions: each call moves things by ONE step and returns 1
// if something moved, 0 when finished. Run gravity_step() until it returns 0,
// then slide_step() until it returns 0.
unsigned char gravity_step(void);   // every block with a gap below falls one row
unsigned char slide_step(void);     // columns right of the first empty column move one left

// 1 if there are no blocks left at all.
unsigned char board_empty(void);

// Pushes a new row of blocks up from the bottom (everything else moves up one row).
// newrow holds COLS block colors (1..NUM_COLORS). Returns 0 and changes nothing if
// the top row already has blocks (they would be pushed off the board = game over).
unsigned char push_row_up(const unsigned char *newrow);

// 1 if no move is left (no two adjacent blocks share a color).
unsigned char no_moves_left(void);

#endif
