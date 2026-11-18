#include "core/digits.h"

/* Ported from bedroom-clock digits.c. Coordinates are in the upright frame
 * at 4/4 scale; rotation is applied last. */

typedef struct {
    bool large;
    int turns;
    rect_t *out;
    int n;
} canvas_t;

static void segment(canvas_t *c, int x, int y, int w, int h)
{
    int scale = c->large ? 5 : 4;
    int left = c->large ? 72 : 65;
    int top = c->large ? 135 : 156;
    int right = (left + (x + w) * scale / 4) & ~1;
    int bottom = (top + (y + h) * scale / 4) & ~1;
    x = (left + x * scale / 4) & ~1;
    y = (top + y * scale / 4) & ~1;
    c->out[c->n++] = rect_rotate((rect_t){x, y, right - x, bottom - y}, c->turns);
}

static void digit(canvas_t *c, int x, int d, bool valid)
{
    static const unsigned char masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                                          0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    static const unsigned char seg[7][4] = {
        {12, 0, 48, 10}, {60, 12, 10, 66}, {60, 90, 10, 66}, {12, 158, 48, 10},
        {0, 90, 10, 66},  {0, 12, 10, 66},  {12, 80, 48, 10}};
    unsigned bits = valid ? masks[d % 10] : 0x40; /* dash: middle bar */
    for (int i = 0; i < 7; i++)
        if (bits & (1u << i)) segment(c, x + seg[i][0], seg[i][1], seg[i][2], seg[i][3]);
}

int digits_rects(int hour12, int minute, bool valid, int quarter_turns,
                 rect_t out[DIGITS_MAX_RECTS])
{
    if (hour12 < 0) hour12 = 0;
    if (minute < 0) minute = 0;
    canvas_t c = {.large = valid && hour12 < 10, .turns = quarter_turns, .out = out};
    int offset = c.large ? 0 : 82;
    if (!c.large) digit(&c, 0, hour12 / 10, valid);
    digit(&c, offset, hour12 % 10, valid);
    digit(&c, offset + 116, minute / 10, valid);
    digit(&c, offset + 198, minute % 10, valid);
    segment(&c, offset + 86, 46, 10, 10);
    segment(&c, offset + 86, 108, 10, 10);
    return c.n;
}
