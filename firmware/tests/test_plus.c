#include <assert.h>
#include <stdbool.h>
#include "core/plus.h"
#include "core/touchmap.h"
#include <stdint.h>

/* Turn an upright-frame point by q clockwise quarter turns, as the screen
 * contents turn */
static void turn(int x, int y, int q, int *ox, int *oy)
{
    rect_t r = rect_rotate((rect_t){x, y, 1, 1}, q);
    *ox = r.x;
    *oy = r.y;
}

/* Decode the point the old (half-turned) mapping logged as (lx, ly) */
static void decoded(int lx, int ly, int *x, int *y)
{
    int raw_x = 480 - ly, raw_y = lx;
    uint8_t r[TOUCH_REPORT_LEN] = {0x06, (uint8_t)(raw_x >> 4), (uint8_t)(raw_y >> 4),
                                   (uint8_t)((raw_x & 15) << 4 | (raw_y & 15)), 0, 1, 0xAB};
    bool down;
    assert(touch_decode(r, &down, x, y) && down);
}

int main(void)
{
    /* The drawn button: 76 px, bottom-right of the upright clock */
    rect_t b = plus_rect(0);
    assert(b.x == 366 && b.y == 366 && b.w == 76 && b.h == 76);
    b = plus_rect(1);
    assert(b.x == 38 && b.y == 366); /* bottom-left after a clockwise turn */

    /* Touch target: the drawn button plus 20 px, out to the screen edge */
    assert(PLUS_TARGET == 346);
    assert(plus_hit(346, 346, 0) && plus_hit(479, 479, 0));
    assert(!plus_hit(345, 400, 0) && !plus_hit(400, 345, 0));

    /* Owner's real plus taps (old-mapping logs) hit in rotation 0, and turn
     * with the button in every other rotation */
    static const int taps[][2] = {{46, 68}, {66, 57}, {67, 52}};
    for (unsigned i = 0; i < sizeof(taps) / sizeof(taps[0]); i++) {
        int x, y;
        decoded(taps[i][0], taps[i][1], &x, &y);
        assert(plus_hit(x, y, 0));
        for (int q = 1; q < 4; q++) {
            int tx, ty;
            turn(x, y, q, &tx, &ty);
            assert(plus_hit(tx, ty, q));
            assert(!plus_hit(x, y, q)); /* the button has moved */
        }
    }

    /* Owner's corner taps TL, TR, BL miss in rotation 0; BR hits only
     * because it falls inside the target square */
    static const int corners[][2] = {{452, 451}, {15, 454}, {446, 25}, {23, 17}};
    for (int i = 0; i < 4; i++) {
        int x, y;
        decoded(corners[i][0], corners[i][1], &x, &y);
        bool in_target = x >= PLUS_TARGET && y >= PLUS_TARGET;
        assert(plus_hit(x, y, 0) == in_target);
        if (i < 3) assert(!in_target);
    }
    assert(!plus_hit(240, 240, 0));
    return 0;
}
