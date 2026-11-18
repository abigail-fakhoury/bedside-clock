#include "core/orient.h"

static int magnitude(int v) { return v < 0 ? -v : v; }

/* Ported from bedroom-clock orientation.c */
int orient_choose(const int16_t xyz[3], int previous)
{
    int x = xyz[0], y = xyz[1], z = xyz[2];
    int ax = magnitude(x), ay = magnitude(y), az = magnitude(z);
    int largest = ax > ay ? ax : ay;
    int other = ax > ay ? ay : ax;
    int64_t norm = (int64_t)x * x + (int64_t)y * y + (int64_t)z * z;
    /* Total near 1 g; a clear edge carries >= 0.55 g and beats the other
     * axes by >= 0.15 g */
    if (norm < (int64_t)12000 * 12000 || norm > (int64_t)21000 * 21000 ||
        largest < 9000 || largest < other + 2500 || largest < az + 2500)
        return previous;
    if (ay > ax) return y > 0 ? 0 : 2;
    /* Board turned clockwise reads +X; counter-rotate the glyphs */
    return x > 0 ? 3 : 1;
}

int orient_degrees(int quarter_turns) { return (quarter_turns & 3) * 90; }
