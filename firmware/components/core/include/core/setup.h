#pragma once
#include "core/rect.h"

/* Where the "Setup needed" label goes: a w × h box (rounded up to even
 * sizes) centred under the digits of the upright clock, rotated by
 * quarter_turns with the rest of the face. */
rect_t setup_rect(int w, int h, int quarter_turns);
