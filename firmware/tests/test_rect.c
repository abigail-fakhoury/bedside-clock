#include <assert.h>
#include "core/rect.h"

static int same(rect_t a, rect_t b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

int main(void)
{
    rect_t r = {10, 20, 30, 40};
    assert(same(rect_rotate(r, 0), r));
    /* One clockwise quarter turn: the top-left corner moves to the top right */
    assert(same(rect_rotate(r, 1), (rect_t){480 - 20 - 40, 10, 40, 30}));
    assert(same(rect_rotate(r, 2), (rect_t){480 - 10 - 30, 480 - 20 - 40, 30, 40}));
    assert(same(rect_rotate(r, 3), (rect_t){20, 480 - 10 - 30, 40, 30}));
    /* Four turns, or turns given modulo four, are the identity */
    assert(same(rect_rotate(rect_rotate(rect_rotate(rect_rotate(r, 1), 1), 1), 1), r));
    assert(same(rect_rotate(r, 5), rect_rotate(r, 1)));
    /* A single pixel at (x, y) lands at (479 - y, x) */
    assert(same(rect_rotate((rect_t){72, 407, 1, 1}, 1), (rect_t){72, 72, 1, 1}));
    return 0;
}
