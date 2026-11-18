#pragma once
#include <stdint.h>

/* One showing of the clock: fade in, hold, fade out. */
enum { FADE_IN_MS = 180, HOLD_MS = 2470, FADE_OUT_MS = 350 };
enum { SHOW_MS = FADE_IN_MS + HOLD_MS + FADE_OUT_MS };

/* Panel brightness ms into a showing that started at brightness `from`
 * and rises to `target`. Smoothstep fades; 0 from SHOW_MS on. */
uint8_t fade_level(unsigned ms, unsigned from, unsigned target);

/* Milliseconds until the level next needs updating: 10 while fading, the
 * start of the fade-out while holding, 0 once the showing is over. */
unsigned fade_next_ms(unsigned ms);
