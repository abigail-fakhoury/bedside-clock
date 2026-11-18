#include <assert.h>

#include "core/layout.h"
#include "core/plus.h"

int main(void)
{
    /* Corner buttons are 76 px circles; plus sits where the clock's does */
    rect_t p = button_rect(BTN_PLUS), c = plus_rect(0);
    assert(p.x == c.x && p.y == c.y && p.w == 76 && p.h == 76);
    rect_t t = button_rect(BTN_TICK), s = button_rect(BTN_STOP);
    assert(t.x == 366 && t.y == 366 && s.x == 366 && s.y == 366);
    rect_t x = button_rect(BTN_CROSS), m = button_rect(BTN_MINUS), u = button_rect(BTN_UP);
    assert(x.x == 38 && x.y == 366 && x.w == 76);
    assert(m.x == 38 && m.y == 38);
    assert(u.x == 366 && u.y == 38);
    rect_t none = button_rect(0);
    assert(none.w == 0 && none.h == 0);

    unsigned check = BTN_MINUS | BTN_UP | BTN_CROSS | BTN_TICK | BTN_PICK;
    /* Centres hit */
    assert(button_at(404, 404, check) == BTN_TICK);
    assert(button_at(76, 404, check) == BTN_CROSS);
    assert(button_at(76, 76, check) == BTN_MINUS);
    assert(button_at(404, 76, check) == BTN_UP);
    /* Targets reach the edges, as the clock's plus: 0-133 and 346-479 */
    assert(button_at(0, 0, check) == BTN_MINUS && button_at(133, 133, check) == BTN_MINUS);
    assert(button_at(134, 100, check) == 0 && button_at(100, 134, check) == 0);
    assert(button_at(479, 479, check) == BTN_TICK && button_at(346, 346, check) == BTN_TICK);
    assert(button_at(345, 400, check) != BTN_TICK);
    for (int y = 346; y < 480; y += 7)
        for (int xx = 346; xx < 480; xx += 7) assert(button_at(xx, y, BTN_PLUS) == plus_hit(xx, y, 0) * BTN_PLUS);
    /* Only shown buttons answer */
    assert(button_at(404, 404, BTN_CROSS) == 0);
    assert(button_at(404, 404, BTN_STOP) == BTN_STOP);
    assert(button_at(76, 76, BTN_TICK | BTN_CROSS) == 0);
    /* The second book's line, between the bottom buttons */
    rect_t k = button_rect(BTN_PICK);
    assert(k.x == 134 && k.x + k.w == 346 && k.y + k.h == 480);
    assert(button_at(240, 420, check) == BTN_PICK);
    assert(button_at(240, 420, check & ~BTN_PICK) == 0);
    assert(button_at(240, 240, check) == 0);
    return 0;
}
