#include <assert.h>
#include "core/digits.h"
#include "core/plus.h"
#include "core/setup.h"

static int overlaps(rect_t a, rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

int main(void)
{
    /* "Setup needed" sits centred under the digits in the upright clock */
    rect_t r = setup_rect(150, 28, 0);
    assert(r.w == 150 && r.h == 28);
    assert(r.x + r.w / 2 >= 239 && r.x + r.w / 2 <= 240); /* even x */
    assert(r.y >= 345 + 8); /* below the taller single-digit-hour digits */
    assert(r.y + r.h <= 480 - 10 - 20); /* clear of the battery ring */
    assert(r.x % 2 == 0 && r.y % 2 == 0 && r.w % 2 == 0 && r.h % 2 == 0);

    /* Never over the digits or the plus button, in any rotation */
    for (int q = 0; q < 4; q++) {
        rect_t s = setup_rect(150, 28, q);
        rect_t d[DIGITS_MAX_RECTS];
        int n = digits_rects(1, 23, 1, q, d);
        for (int i = 0; i < n; i++) assert(!overlaps(s, d[i]));
        n = digits_rects(12, 59, 1, q, d);
        for (int i = 0; i < n; i++) assert(!overlaps(s, d[i]));
        assert(!overlaps(s, plus_rect(q)));
        /* Turns with the clock: the same as rotating the upright box */
        rect_t t = rect_rotate(setup_rect(150, 28, 0), q);
        assert(s.x == t.x && s.y == t.y && s.w == t.w && s.h == t.h);
    }

    /* Odd label sizes are rounded up to even */
    r = setup_rect(141, 27, 0);
    assert(r.w == 142 && r.h == 28 && r.x % 2 == 0 && r.y % 2 == 0);
    return 0;
}
