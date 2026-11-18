#include "core/fade.h"

/* Cubic smoothstep in thousandths (bedroom-clock animation.c) */
static unsigned ease(unsigned elapsed, unsigned duration)
{
    if (elapsed >= duration) return 1000;
    unsigned x = elapsed * 1000 / duration;
    return x * x * (3000 - 2 * x) / 1000000;
}

uint8_t fade_level(unsigned ms, unsigned from, unsigned target)
{
    if (ms >= SHOW_MS) return 0;
    if (ms < FADE_IN_MS) {
        unsigned f = ease(ms, FADE_IN_MS);
        return (uint8_t)((from * (1000 - f) + target * f) / 1000);
    }
    if (ms < SHOW_MS - FADE_OUT_MS) return (uint8_t)target;
    unsigned f = ease(ms - (SHOW_MS - FADE_OUT_MS), FADE_OUT_MS);
    return (uint8_t)(target * (1000 - f) / 1000);
}

unsigned fade_next_ms(unsigned ms)
{
    if (ms >= SHOW_MS) return 0;
    if (ms < FADE_IN_MS || ms >= SHOW_MS - FADE_OUT_MS) return 10;
    return SHOW_MS - FADE_OUT_MS - ms;
}
