#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "core/touchmap.h"

/* Build the report the chip sent for a point the old firmware logged as
 * (lx, ly); the old mapping was x = raw_y, y = 480 - raw_x. */
static void report_for_old_log(int lx, int ly, uint8_t r[TOUCH_REPORT_LEN])
{
    int raw_x = 480 - ly, raw_y = lx;
    for (int i = 0; i < TOUCH_REPORT_LEN; i++) r[i] = 0;
    r[0] = 0x06;
    r[1] = (uint8_t)(raw_x >> 4);
    r[2] = (uint8_t)(raw_y >> 4);
    r[3] = (uint8_t)((raw_x & 15) << 4 | (raw_y & 15));
    r[5] = 1;
    r[6] = 0xAB;
}

int main(void)
{
    uint8_t r[TOUCH_REPORT_LEN];
    bool down;
    int x, y;

    /* Owner's corner taps, in the order TL, TR, BL, BR, as the old firmware
     * logged them. Correctly mapped they must land in those corners. */
    static const int old_log[4][2] = {{452, 451}, {15, 454}, {446, 25}, {23, 17}};
    for (int i = 0; i < 4; i++) {
        report_for_old_log(old_log[i][0], old_log[i][1], r);
        assert(touch_decode(r, &down, &x, &y) && down);
        bool right = i == 1 || i == 3, bottom = i >= 2;
        assert(right ? x > 400 : x < 80);
        assert(bottom ? y > 400 : y < 80);
        /* Exactly the half-turn of the old reading */
        assert(x == 479 - old_log[i][0] && y == 479 - old_log[i][1]);
    }

    /* Lifted finger: valid report, not down */
    report_for_old_log(100, 100, r);
    r[0] = 0x00;
    assert(touch_decode(r, &down, &x, &y) && !down);
    r[0] = 0x06;
    r[5] = 0;
    assert(touch_decode(r, &down, &x, &y) && !down);

    /* Wrong marker: not a report */
    report_for_old_log(100, 100, r);
    r[6] = 0x00;
    assert(!touch_decode(r, &down, &x, &y));

    /* Out-of-range raw values clamp onto the screen */
    report_for_old_log(4000 & 0xfff, 0, r);
    assert(touch_decode(r, &down, &x, &y) && x >= 0 && x <= 479 && y >= 0 && y <= 479);
    return 0;
}
