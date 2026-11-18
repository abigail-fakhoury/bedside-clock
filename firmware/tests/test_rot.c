#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "core/rot.h"

static uint8_t visited[(ROT_MAX_PIXELS + 7) / 8];
static uint16_t px[ROT_MAX_PIXELS];

/* Each pixel's value names its upright position; after rot_strip it must
 * sit where rect_rotate() puts that position. */
static void check(rect_t a, int q)
{
    int n = a.w * a.h;
    for (int j = 0; j < a.h; j++)
        for (int i = 0; i < a.w; i++) px[j * a.w + i] = (uint16_t)(j * a.w + i);
    rect_t r = a;
    rot_strip(px, &r, q, visited);
    rect_t want = rect_rotate(a, q);
    assert(r.x == want.x && r.y == want.y && r.w == want.w && r.h == want.h);
    for (int k = 0; k < n; k++) {
        int i = px[k] % a.w, j = px[k] / a.w; /* upright local position */
        rect_t p = rect_rotate((rect_t){a.x + i, a.y + j, 1, 1}, q);
        int at = (p.y - r.y) * r.w + (p.x - r.x);
        assert(at == k);
    }
}

int main(void)
{
    static const rect_t areas[] = {
        {0, 0, 480, 40}, {0, 440, 480, 40}, {120, 200, 100, 192}, {2, 4, 6, 2},
        {0, 0, 1, 1},    {478, 0, 2, 2},    {10, 20, 160, 120},
    };
    for (unsigned i = 0; i < sizeof(areas) / sizeof(areas[0]); i++)
        for (int q = -1; q < 6; q++) check(areas[i], q);

    /* Even areas stay even (the CO5300 needs it) */
    rect_t e = {120, 200, 100, 40};
    for (int q = 0; q < 4; q++) {
        rect_t r = rect_rotate(e, q);
        assert(r.x % 2 == 0 && r.y % 2 == 0 && r.w % 2 == 0 && r.h % 2 == 0);
    }

    /* Too big: untouched */
    rect_t big = {0, 0, 480, 41};
    px[0] = 7;
    rot_strip(px, &big, 1, visited);
    assert(big.w == 480 && big.h == 41 && px[0] == 7);

    /* A touch is turned back: the upright point the picture shows there */
    for (int q = 0; q < 4; q++) {
        int pts[][2] = {{0, 0}, {479, 0}, {10, 400}, {366, 42}, {240, 240}};
        for (int k = 0; k < 5; k++) {
            rect_t p = rect_rotate((rect_t){pts[k][0], pts[k][1], 1, 1}, q);
            int ux, uy;
            rot_point_upright(p.x, p.y, q, &ux, &uy);
            assert(ux == pts[k][0] && uy == pts[k][1]);
        }
    }
    return 0;
}
