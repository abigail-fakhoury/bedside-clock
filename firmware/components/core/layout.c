#include "core/layout.h"

/* Corner targets reach the screen edges, as plus_hit: 0-133 and 346-479 */
enum { NEAR_END = BTN_NEAR + BTN_SIZE + BTN_MARGIN, FAR_START = BTN_FAR - BTN_MARGIN };

rect_t button_rect(button_t b)
{
    switch (b) {
    case BTN_PLUS:
    case BTN_STOP:
    case BTN_TICK: return (rect_t){BTN_FAR, BTN_FAR, BTN_SIZE, BTN_SIZE};
    case BTN_CROSS: return (rect_t){BTN_NEAR, BTN_FAR, BTN_SIZE, BTN_SIZE};
    case BTN_MINUS: return (rect_t){BTN_NEAR, BTN_NEAR, BTN_SIZE, BTN_SIZE};
    case BTN_UP: return (rect_t){BTN_FAR, BTN_NEAR, BTN_SIZE, BTN_SIZE};
    case BTN_PICK:
        return (rect_t){NEAR_END, FAR_START, FAR_START - NEAR_END, SCREEN_SIZE - FAR_START};
    default: return (rect_t){0, 0, 0, 0};
    }
}

static bool in(int v, int lo, int hi) { return v >= lo && v < hi; }

button_t button_at(int x, int y, unsigned shown)
{
    bool left = in(x, 0, NEAR_END), right = in(x, FAR_START, SCREEN_SIZE);
    bool top = in(y, 0, NEAR_END), bottom = in(y, FAR_START, SCREEN_SIZE);
    static const button_t br[] = {BTN_PLUS, BTN_STOP, BTN_TICK};
    if (right && bottom) {
        for (unsigned i = 0; i < sizeof(br) / sizeof(br[0]); i++)
            if (shown & br[i]) return br[i];
        return 0;
    }
    if (left && bottom) return shown & BTN_CROSS ? BTN_CROSS : 0;
    if (left && top) return shown & BTN_MINUS ? BTN_MINUS : 0;
    if (right && top) return shown & BTN_UP ? BTN_UP : 0;
    rect_t p = button_rect(BTN_PICK);
    if ((shown & BTN_PICK) && in(x, p.x, p.x + p.w) && in(y, p.y, p.y + p.h)) return BTN_PICK;
    return 0;
}
