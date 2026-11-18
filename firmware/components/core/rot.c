#include "core/rot.h"

#include <string.h>

/* Where the pixel at upright local (i, j) of a w x h strip goes, as an
 * index into the turned strip */
static int dest(int k, int w, int h, int q)
{
    int i = k % w, j = k / w;
    switch (q) {
    case 1: return i * h + (h - 1 - j);       /* turned strip is h wide */
    case 2: return w * h - 1 - k;
    case 3: return (w - 1 - i) * h + j;
    default: return k;
    }
}

void rot_strip(uint16_t *px, rect_t *a, int quarter_turns, uint8_t *visited)
{
    int q = quarter_turns & 3, w = a->w, h = a->h, n = w * h;
    if (q == 0 || w <= 0 || h <= 0 || n > ROT_MAX_PIXELS) return;
    if (q == 2) {
        for (int k = 0; k < n / 2; k++) {
            uint16_t t = px[k];
            px[k] = px[n - 1 - k];
            px[n - 1 - k] = t;
        }
    } else {
        /* Follow each cycle of the permutation once */
        memset(visited, 0, (size_t)(n + 7) / 8);
        for (int s = 0; s < n; s++) {
            if (visited[s >> 3] & (1 << (s & 7))) continue;
            uint16_t carry = px[s];
            int k = s;
            do {
                int d = dest(k, w, h, q);
                uint16_t t = px[d];
                px[d] = carry;
                carry = t;
                visited[d >> 3] |= (uint8_t)(1 << (d & 7));
                k = d;
            } while (k != s);
        }
    }
    *a = rect_rotate(*a, q);
}

void rot_point_upright(int x, int y, int quarter_turns, int *ux, int *uy)
{
    rect_t p = rect_rotate((rect_t){x, y, 1, 1}, 4 - (quarter_turns & 3));
    *ux = p.x;
    *uy = p.y;
}
