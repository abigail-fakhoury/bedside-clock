#pragma once

/* A rectangle on the 480×480 screen, in pixels. */
typedef struct {
    int x, y, w, h;
} rect_t;

enum { SCREEN_SIZE = 480 };

/* Rotate a rectangle within the screen by clockwise quarter turns (any
 * count; taken modulo four). A pixel at (x, y) moves to (479 − y, x). */
rect_t rect_rotate(rect_t r, int quarter_turns);
