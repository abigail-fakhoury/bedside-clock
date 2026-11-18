#include "core/touchmap.h"

static int clamp(int v) { return v < 0 ? 0 : v > 479 ? 479 : v; }

bool touch_decode(const uint8_t r[TOUCH_REPORT_LEN], bool *down, int *x, int *y)
{
    if (r[6] != 0xAB) return false;
    *down = (r[5] & 0x7f) != 0 && (r[0] & 0x0f) == 6;
    if (*down) {
        int raw_x = r[1] << 4 | r[3] >> 4;
        int raw_y = r[2] << 4 | (r[3] & 0x0f);
        /* Confirmed against the owner's corner taps (fix round 2) */
        *x = clamp(479 - raw_y);
        *y = clamp(raw_x - 1);
    }
    return true;
}
