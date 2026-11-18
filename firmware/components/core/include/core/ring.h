#pragma once
#include <stdbool.h>
#include "core/rect.h"

/* Battery ring: a 4 px band inset 6 px from the screen's rounded edge
 * (glass corner radius 72, so the corner arcs share its centres). The lit
 * part starts at the bottom centre of the upright clock and runs clockwise
 * for `percent` of the border; the rest is dim. Straight pieces have even
 * starts and sizes. */
enum {
    RING_INSET = 6,
    RING_WIDTH = 4,
    RING_RADIUS = 72 - RING_INSET, /* outer radius of each corner arc */
    RING_SIDE = 336,      /* straight run of each side, x/y 72..407 */
    RING_ARC = 100,       /* length counted for each corner (~π/2 × 64) */
    RING_LENGTH = 4 * RING_SIDE + 4 * RING_ARC,
    RING_MAX_PIECES = 10, /* nine runs, one split between lit and dim */
};

typedef struct {
    bool lit;
    bool arc;
    rect_t rect;            /* straight runs */
    int cx, cy, radius;     /* arcs: centre pixel and outer radius */
    int start, end;         /* arcs: degrees, 0 = 3 o'clock, clockwise; 0–359 */
} ring_piece_t;

/* Pieces in travel order, rotated by quarter_turns. Hidden (0 pieces) when
 * percent < 0; over 100 counts as 100. */
int ring_pieces(int percent, int quarter_turns, ring_piece_t out[RING_MAX_PIECES]);
