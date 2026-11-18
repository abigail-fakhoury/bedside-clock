#include "core/rect.h"

rect_t rect_rotate(rect_t r, int quarter_turns)
{
    switch (quarter_turns & 3) {
    case 1: return (rect_t){SCREEN_SIZE - r.y - r.h, r.x, r.h, r.w};
    case 2: return (rect_t){SCREEN_SIZE - r.x - r.w, SCREEN_SIZE - r.y - r.h, r.w, r.h};
    case 3: return (rect_t){r.y, SCREEN_SIZE - r.x - r.w, r.h, r.w};
    default: return r;
    }
}
