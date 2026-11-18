#include "core/plus.h"

rect_t plus_rect(int quarter_turns)
{
    return rect_rotate((rect_t){PLUS_POS, PLUS_POS, PLUS_SIZE, PLUS_SIZE}, quarter_turns);
}

bool plus_hit(int x, int y, int quarter_turns)
{
    const int side = SCREEN_SIZE - PLUS_TARGET;
    rect_t t = rect_rotate((rect_t){PLUS_TARGET, PLUS_TARGET, side, side}, quarter_turns);
    return x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h;
}
