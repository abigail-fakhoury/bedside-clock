#pragma once
#include <stdbool.h>
#include "core/rect.h"

/* The plus button: a 76 px circle at the bottom-right of the upright clock,
 * turning with it. */
enum { PLUS_SIZE = 76, PLUS_POS = 366, PLUS_TARGET = PLUS_POS - 20 };

/* Where the button is drawn, rotated by quarter_turns. */
rect_t plus_rect(int quarter_turns);

/* Whether a touch at screen point (x, y) taps the button. The target is
 * the drawn button plus a 20 px margin, extended out to the screen edge
 * (x and y from PLUS_TARGET to 479 in the upright frame), rotated with the
 * clock. */
bool plus_hit(int x, int y, int quarter_turns);
