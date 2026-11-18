#pragma once
#include <stdint.h>

/* Clockwise quarter turns (0–3) to draw the clock upright, from one
 * QMI8658 reading at ±2 g (16384 counts/g). Flat, diagonal, weak or
 * moving readings keep `previous`. */
int orient_choose(const int16_t xyz[3], int previous);

/* 0, 90, 180 or 270. */
int orient_degrees(int quarter_turns);
