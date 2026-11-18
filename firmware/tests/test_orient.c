#include <assert.h>
#include <stdint.h>
#include "core/orient.h"

int main(void)
{
    /* ±2 g full scale: 16384 counts per g */
    const int16_t up[3] = {0, 16384, 0}, down[3] = {0, -16384, 0};
    const int16_t cw[3] = {16384, 0, 0}, ccw[3] = {-16384, 0, 0};
    assert(orient_choose(up, 1) == 0);
    assert(orient_choose(down, 0) == 2);
    assert(orient_choose(cw, 0) == 3);   /* board turned clockwise: counter-rotate */
    assert(orient_choose(ccw, 0) == 1);

    /* Flat, diagonal, weak and shaken readings keep the previous choice */
    const int16_t flat[3] = {0, 0, 16384};
    const int16_t diagonal[3] = {11000, 11000, 0};
    const int16_t tilted[3] = {0, 9500, 9000};
    const int16_t shaken[3] = {0, 30000, 0};
    for (int prev = 0; prev < 4; prev++) {
        assert(orient_choose(flat, prev) == prev);
        assert(orient_choose(diagonal, prev) == prev);
        assert(orient_choose(tilted, prev) == prev);
        assert(orient_choose(shaken, prev) == prev);
    }
    /* A clear edge with a little tilt still counts */
    const int16_t leaning[3] = {3000, 14000, 6000};
    assert(orient_choose(leaning, 2) == 0);

    assert(orient_degrees(0) == 0 && orient_degrees(1) == 90);
    assert(orient_degrees(2) == 180 && orient_degrees(3) == 270);
    return 0;
}
