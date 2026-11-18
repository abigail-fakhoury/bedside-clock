#pragma once
#include <stdbool.h>

#include "core/rect.h"

/* Where the session screens put their buttons, in the upright frame (the
 * picture is turned with the board as it is sent to the panel; touches are
 * turned back with rot_point_upright first). Buttons are 76 px circles in
 * the corners, as the clock's plus: plus, tick and stop share the
 * bottom-right (never two at once), cross the bottom-left, minus the
 * top-left, page-plus the top-right. Each corner's touch target is the
 * drawn button plus 20 px, out to the screen edges, as plus_hit. The
 * second book's line sits between the bottom buttons. */

typedef enum {
    BTN_PLUS = 1 << 0,  /* start: "Which book, what page?"; retry */
    BTN_STOP = 1 << 1,
    BTN_TICK = 1 << 2,
    BTN_CROSS = 1 << 3,
    BTN_MINUS = 1 << 4, /* page - 1 */
    BTN_UP = 1 << 5,    /* page + 1 */
    BTN_PICK = 1 << 6,  /* the second book's line */
} button_t;

enum { BTN_SIZE = 76, BTN_NEAR = 38, BTN_FAR = 366, BTN_MARGIN = 20 };

/* The drawn 76 x 76 square of a corner button, or of BTN_PICK the line's
 * touch target. A zero rect for anything else. */
rect_t button_rect(button_t b);

/* The button among `shown` (a mask) whose target holds the upright point
 * (x, y), or 0. */
button_t button_at(int x, int y, unsigned shown);
