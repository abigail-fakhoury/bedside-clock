#include "core/ring.h"

enum { LEFT, RIGHT, UP, DOWN };

typedef struct {
    bool arc;
    rect_t rect; /* straight run, full extent */
    int dir;     /* travel direction along the run */
    int cx, cy, start; /* arc centre and starting angle, sweeping +90 */
} run_t;

/* Upright frame, clockwise from the bottom centre */
enum { NEAR = RING_INSET, FAR = SCREEN_SIZE - RING_INSET - RING_WIDTH, HALF = RING_SIDE / 2 };
static const run_t runs[] = {
    {false, {72, FAR, HALF, RING_WIDTH}, LEFT, 0, 0, 0},
    {true, {0}, 0, 72, 407, 90},
    {false, {NEAR, 72, RING_WIDTH, RING_SIDE}, UP, 0, 0, 0},
    {true, {0}, 0, 72, 72, 180},
    {false, {72, NEAR, RING_SIDE, RING_WIDTH}, RIGHT, 0, 0, 0},
    {true, {0}, 0, 407, 72, 270},
    {false, {FAR, 72, RING_WIDTH, RING_SIDE}, DOWN, 0, 0, 0},
    {true, {0}, 0, 407, 407, 0},
    {false, {240, FAR, HALF, RING_WIDTH}, LEFT, 0, 0, 0},
};

static int run_length(const run_t *r)
{
    if (r->arc) return RING_ARC;
    return r->rect.w > r->rect.h ? r->rect.w : r->rect.h;
}

/* The part of a straight run from a to b pixels along its travel */
static rect_t straight_part(const run_t *r, int a, int b)
{
    rect_t s = r->rect;
    switch (r->dir) {
    case LEFT: s.x = r->rect.x + r->rect.w - b; s.w = b - a; break;
    case RIGHT: s.x = r->rect.x + a; s.w = b - a; break;
    case UP: s.y = r->rect.y + r->rect.h - b; s.h = b - a; break;
    default: s.y = r->rect.y + a; s.h = b - a; break;
    }
    return s;
}

static int angle(int degrees) { return ((degrees % 360) + 360) % 360; }

/* Appends the a..b part of a run; false if it is empty */
static bool part(const run_t *r, int a, int b, bool lit, int turns, ring_piece_t *p)
{
    if (b <= a) return false;
    *p = (ring_piece_t){.lit = lit, .arc = r->arc};
    if (!r->arc) {
        p->rect = rect_rotate(straight_part(r, a, b), turns);
        return true;
    }
    int start = r->start + a * 90 / RING_ARC;
    int end = r->start + b * 90 / RING_ARC;
    if (start == end) return false;
    rect_t centre = rect_rotate((rect_t){r->cx, r->cy, 1, 1}, turns);
    p->cx = centre.x;
    p->cy = centre.y;
    p->radius = RING_RADIUS;
    p->start = angle(start + 90 * turns);
    p->end = angle(end + 90 * turns);
    return true;
}

int ring_pieces(int percent, int quarter_turns, ring_piece_t out[RING_MAX_PIECES])
{
    if (percent < 0) return 0;
    if (percent > 100) percent = 100;
    /* Even lit length keeps every split, and so every piece, even */
    int lit = (percent * RING_LENGTH + 100) / 200 * 2;
    int n = 0;
    for (unsigned i = 0; i < sizeof(runs) / sizeof(runs[0]); i++) {
        const run_t *r = &runs[i];
        int len = run_length(r);
        int split = lit < 0 ? 0 : lit > len ? len : lit;
        if (part(r, 0, split, true, quarter_turns, &out[n])) n++;
        if (part(r, split, len, false, quarter_turns, &out[n])) n++;
        lit -= len;
    }
    return n;
}
