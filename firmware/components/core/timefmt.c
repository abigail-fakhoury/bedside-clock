#include "core/timefmt.h"
#include <stdio.h>

void fmt_clock12(int hour24, int minute, char out[6])
{
    /* Clamp so the output always fits in 6 bytes ("12:59" + NUL). */
    unsigned h = (unsigned)(hour24 < 0 ? 0 : hour24) % 12u;
    unsigned m = (unsigned)(minute < 0 ? 0 : minute) % 60u;
    if (h == 0) {
        h = 12;
    }
    snprintf(out, 6, "%u:%02u", h, m);
}

void fmt_elapsed(uint32_t seconds, char out[9])
{
    /* Hours are capped at 9 so the output always fits in 9 bytes. */
    unsigned h = seconds / 3600 > 9 ? 9 : (unsigned)(seconds / 3600);
    unsigned m = (seconds % 3600) / 60;
    unsigned s = seconds % 60;
    if (h > 0) {
        snprintf(out, 9, "%u:%02u:%02u", h, m, s);
    } else {
        snprintf(out, 9, "%u:%02u", m, s);
    }
}

uint32_t minutes_rounded(uint32_t seconds)
{
    uint32_t m = (seconds + 30) / 60;
    return m < 1 ? 1 : m;
}
