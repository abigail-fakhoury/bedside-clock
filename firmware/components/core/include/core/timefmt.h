#pragma once
#include <stdint.h>

/* "9:41", "12:05" (12-hour, no leading zero on hour). */
void fmt_clock12(int hour24, int minute, char out[6]);

/* "17:04" under an hour, "1:02:09" from one hour. */
void fmt_elapsed(uint32_t seconds, char out[9]);

/* Nearest minute, minimum 1. */
uint32_t minutes_rounded(uint32_t seconds);
