#pragma once
#include <stdbool.h>
#include "core/rect.h"

/* Four digit slots of seven segments plus two colon dots. */
enum { DIGITS_MAX_RECTS = 30 };

/* Seven-segment time in bedroom-clock's geometry. Four slots span
 * 350 × 168 px centred on screen; a single-digit hour drops the first slot
 * and draws 5/4 larger (334 × 210 px). An invalid clock shows --:--.
 * Writes the lit rectangles, rotated by quarter_turns, and returns how many.
 * Every rectangle has even position and size, as the CO5300 needs. */
int digits_rects(int hour12, int minute, bool valid, int quarter_turns,
                 rect_t out[DIGITS_MAX_RECTS]);
