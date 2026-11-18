#include <assert.h>
#include <stdbool.h>
#include "core/digits.h"

static void check_bounds(const rect_t *r, int n)
{
    for (int i = 0; i < n; i++) {
        /* CO5300 windows: even starts and even sizes, all on screen */
        assert(r[i].x % 2 == 0 && r[i].y % 2 == 0);
        assert(r[i].w > 0 && r[i].h > 0 && r[i].w % 2 == 0 && r[i].h % 2 == 0);
        assert(r[i].x >= 0 && r[i].y >= 0 && r[i].x + r[i].w <= 480 && r[i].y + r[i].h <= 480);
    }
}

static rect_t bounds(const rect_t *r, int n)
{
    int x1 = 480, y1 = 480, x2 = 0, y2 = 0;
    for (int i = 0; i < n; i++) {
        if (r[i].x < x1) x1 = r[i].x;
        if (r[i].y < y1) y1 = r[i].y;
        if (r[i].x + r[i].w > x2) x2 = r[i].x + r[i].w;
        if (r[i].y + r[i].h > y2) y2 = r[i].y + r[i].h;
    }
    return (rect_t){x1, y1, x2 - x1, y2 - y1};
}

int main(void)
{
    rect_t r[DIGITS_MAX_RECTS];

    /* 12:38 lights 2 + 5 + 5 + 7 segments and two colon dots */
    int n = digits_rects(12, 38, true, 0, r);
    assert(n == 2 + 5 + 5 + 7 + 2);
    check_bounds(r, n);
    rect_t b = bounds(r, n);
    /* Four slots span x 64..414 and y 156..324; a leading 1 lights only
     * the right-hand column of its slot */
    assert(b.x == 124 && b.x + b.w == 414 && b.y == 156 && b.h == 168);

    /* 8:88: three digits drawn 5/4 larger, still centred */
    n = digits_rects(8, 58, true, 0, r);
    assert(n == 7 + 5 + 7 + 2);
    check_bounds(r, n);
    b = bounds(r, n);
    assert(b.x == 72 && b.w == 334 && b.y == 134 && b.h == 210);

    /* An invalid clock shows dashes: --:-- */
    n = digits_rects(0, 0, false, 0, r);
    assert(n == 4 + 2);
    check_bounds(r, n);
    for (int i = 0; i < 4; i++) assert(r[i].w > r[i].h); /* flat bars */

    /* Rotation keeps everything on screen and even */
    for (int q = 0; q < 4; q++) {
        n = digits_rects(10, 47, true, q, r);
        check_bounds(r, n);
    }
    /* A half turn mirrors the bounds about the centre */
    rect_t up[DIGITS_MAX_RECTS], flip[DIGITS_MAX_RECTS];
    n = digits_rects(4, 20, true, 0, up);
    assert(digits_rects(4, 20, true, 2, flip) == n);
    for (int i = 0; i < n; i++) {
        assert(flip[i].x == 480 - up[i].x - up[i].w);
        assert(flip[i].y == 480 - up[i].y - up[i].h);
    }
    return 0;
}
