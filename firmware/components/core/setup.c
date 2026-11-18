#include "core/setup.h"

/* Centre line of the label in the upright clock: under the 210 px digits
 * (bottom edge 345) and above the plus button's lower edge */
enum { SETUP_CENTRE_Y = 380 };

rect_t setup_rect(int w, int h, int quarter_turns)
{
    w += w & 1;
    h += h & 1;
    int x = SCREEN_SIZE / 2 - w / 2, y = SETUP_CENTRE_Y - h / 2;
    rect_t r = {x - (x & 1), y - (y & 1), w, h};
    return rect_rotate(r, quarter_turns);
}
