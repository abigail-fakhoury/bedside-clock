#pragma once
#include <stdint.h>

#include "core/rect.h"

/* Turning a strip of drawn pixels as it goes to the panel, so screens laid
 * out upright are shown turned with the board. The turn is the clock's:
 * rect_rotate() (a pixel at (x, y) moves to (479 - y, x) per clockwise
 * quarter turn). */

/* Largest strip, in pixels: the 480 x 40 RGB565 draw buffer. */
enum { ROT_MAX_PIXELS = 480 * 40 };

/* Rearranges the w x h row-major pixels of the upright area *a in place so
 * they are the row-major pixels of rect_rotate(*a, quarter_turns), and
 * turns *a to that rectangle. visited is scratch of at least
 * (w * h + 7) / 8 bytes. Nothing changes for w * h > ROT_MAX_PIXELS. */
void rot_strip(uint16_t *px, rect_t *a, int quarter_turns, uint8_t *visited);

/* The upright-frame point that the screen point (x, y) shows when the
 * picture is turned by quarter_turns (the inverse of the turn). */
void rot_point_upright(int x, int y, int quarter_turns, int *ux, int *uy);
