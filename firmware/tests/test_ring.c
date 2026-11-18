#include <assert.h>
#include <stdbool.h>
#include "core/ring.h"

static int piece_length(const ring_piece_t *p)
{
    if (!p->arc) return p->rect.w > p->rect.h ? p->rect.w : p->rect.h;
    return 0;
}

static void lengths(int percent, int q, int *lit_straight, int *dark_straight,
                    int *lit_arcs, int *n_out)
{
    ring_piece_t p[RING_MAX_PIECES];
    int n = ring_pieces(percent, q, p);
    *lit_straight = *dark_straight = *lit_arcs = 0;
    for (int i = 0; i < n; i++) {
        if (!p[i].arc) {
            assert(RING_WIDTH == 4);
            assert(p[i].rect.w == RING_WIDTH || p[i].rect.h == RING_WIDTH);
            assert(p[i].rect.x >= 6 && p[i].rect.y >= 6);
            assert(p[i].rect.x + p[i].rect.w <= 474 && p[i].rect.y + p[i].rect.h <= 474);
            if (p[i].lit) *lit_straight += piece_length(&p[i]);
            else *dark_straight += piece_length(&p[i]);
        } else {
            assert(p[i].radius == RING_RADIUS && RING_RADIUS == 66); /* 72 − 6 */
            assert(p[i].start >= 0 && p[i].start < 360 && p[i].end >= 0 && p[i].end < 360);
            assert(p[i].start != p[i].end);
            if (p[i].lit) (*lit_arcs)++;
        }
    }
    *n_out = n;
}

int main(void)
{
    ring_piece_t p[RING_MAX_PIECES];
    int ls, ds, la, n;

    /* Hidden without a reading */
    assert(ring_pieces(-1, 0, p) == 0);

    /* Full: four straight sides and four corners, all lit */
    lengths(100, 0, &ls, &ds, &la, &n);
    assert(n == 9 && ls == 4 * 336 && ds == 0 && la == 4);
    /* Empty: everything dim */
    lengths(0, 0, &ls, &ds, &la, &n);
    assert(n == 9 && ls == 0 && ds == 4 * 336 && la == 0);
    /* Every percentage covers the whole border exactly once */
    for (int pct = 0; pct <= 100; pct++)
        for (int q = 0; q < 4; q++) {
            lengths(pct, q, &ls, &ds, &la, &n);
            assert(ls + ds == 4 * 336);
            assert(n <= RING_MAX_PIECES);
            /* Even starts and sizes everywhere, at every split */
            ring_piece_t e[RING_MAX_PIECES];
            int m = ring_pieces(pct, q, e);
            for (int i = 0; i < m; i++) {
                if (e[i].arc) continue;
                assert(e[i].rect.x % 2 == 0 && e[i].rect.y % 2 == 0);
                assert(e[i].rect.w % 2 == 0 && e[i].rect.h % 2 == 0);
            }
        }

    /* Upright, the lit part starts at the bottom centre and runs clockwise
     * (leftwards along the bottom edge) */
    n = ring_pieces(5, 0, p);
    assert(p[0].lit && !p[0].arc);
    int len5 = 88; /* 5% of 1744, rounded to even */
    assert(RING_LENGTH == 1744);
    assert(p[0].rect.x == 240 - len5 && p[0].rect.w == len5);
    /* 4 px wide, inset 6 px from the screen edge */
    assert(p[0].rect.y == 470 && p[0].rect.h == 4);
    assert(!p[1].lit && !p[1].arc && p[1].rect.x == 72 && p[1].rect.x + p[1].rect.w == 240 - len5);

    /* Half full upright: bottom-left, left side, top-left corner, part of top */
    n = ring_pieces(50, 0, p);
    assert(p[1].arc && p[1].lit && p[1].cx == 72 && p[1].cy == 407 && p[1].start == 90 && p[1].end == 180);
    assert(p[2].lit && p[2].rect.x == 6 && p[2].rect.w == 4);
    assert(p[2].rect.y == 72 && p[2].rect.h == 336);
    assert(p[3].arc && p[3].lit && p[3].start == 180 && p[3].end == 270);
    assert(p[4].lit && p[4].rect.y == 6 && p[4].rect.h == 4 && p[4].rect.x == 72);

    /* A quarter turn starts at the left centre and heads up */
    n = ring_pieces(5, 1, p);
    assert(p[0].lit && p[0].rect.x == 6 && p[0].rect.w == 4);
    assert(p[0].rect.y == 240 - len5 && p[0].rect.h == len5);
    /* ...and its corners move with it */
    n = ring_pieces(50, 1, p);
    assert(p[1].arc && p[1].cx == 72 && p[1].cy == 72 && p[1].start == 180 && p[1].end == 270);
    n = ring_pieces(100, 1, p);
    assert(p[7].arc && p[7].start == 90 && p[7].end == 180); /* was the BR corner */
    n = ring_pieces(100, 2, p);
    assert(p[5].arc && p[5].cx == 72 && p[5].cy == 407 && p[5].start == 90 && p[5].end == 180);
    n = ring_pieces(100, 0, p);
    assert(p[5].arc && p[5].start == 270 && p[5].end == 0); /* TR wraps through 360 */

    /* Over 100 is treated as full */
    lengths(150, 0, &ls, &ds, &la, &n);
    assert(ls == 4 * 336 && la == 4);
    return 0;
}
