#include <assert.h>
#include "core/fade.h"

int main(void)
{
    assert(FADE_IN_MS == 180 && HOLD_MS == 2470 && FADE_OUT_MS == 350);
    assert(SHOW_MS == 3000);

    /* Fade in from dark to the target */
    assert(fade_level(0, 0, 128) == 0);
    assert(fade_level(90, 0, 128) == 64); /* smoothstep midpoint */
    assert(fade_level(180, 0, 128) == 128);
    /* Hold */
    assert(fade_level(1000, 0, 128) == 128);
    assert(fade_level(2649, 0, 128) == 128);
    /* Fade out to dark */
    assert(fade_level(2650, 0, 128) == 128);
    assert(fade_level(2825, 0, 128) == 64);
    assert(fade_level(2999, 0, 128) < 2);
    assert(fade_level(3000, 0, 128) == 0);
    assert(fade_level(99999, 0, 128) == 0);
    /* A retap fades in from the current level, not from dark */
    assert(fade_level(0, 100, 128) == 100);
    assert(fade_level(90, 100, 128) == 114);
    assert(fade_level(180, 100, 128) == 128);

    /* Monotonic up during fade-in, down during fade-out */
    for (unsigned ms = 1; ms <= FADE_IN_MS; ms++)
        assert(fade_level(ms, 0, 255) >= fade_level(ms - 1, 0, 255));
    for (unsigned ms = SHOW_MS - FADE_OUT_MS + 1; ms <= SHOW_MS; ms++)
        assert(fade_level(ms, 0, 255) <= fade_level(ms - 1, 0, 255));

    /* Next update: every 10 ms while fading, the fade-out start while holding */
    assert(fade_next_ms(0) == 10);
    assert(fade_next_ms(179) == 10);
    assert(fade_next_ms(180) == 2650 - 180);
    assert(fade_next_ms(2000) == 650);
    assert(fade_next_ms(2650) == 10);
    assert(fade_next_ms(3000) == 0);
    return 0;
}
