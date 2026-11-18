#pragma once
#include <stdbool.h>
#include <stdint.h>

/* CST9220 report read from 0xD000. */
enum { TOUCH_REPORT_LEN = 10 };

/* Decodes one report: false unless byte 6 is the 0xAB marker. *down is a
 * pressed first point (byte 0 low nibble 6, a non-zero point count in byte
 * 5); when down, (*x, *y) is that point in upright screen coordinates,
 * clamped to 0–479. The panel's raw axes are swapped and half-turned
 * relative to the display: x = 479 − raw_y, y = raw_x − 1. */
bool touch_decode(const uint8_t r[TOUCH_REPORT_LEN], bool *down, int *x, int *y);
