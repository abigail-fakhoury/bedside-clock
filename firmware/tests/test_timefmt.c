#include <assert.h>
#include <string.h>
#include "core/timefmt.h"

int main(void)
{
    char c[6], e[9];
    fmt_clock12(0, 5, c);  assert(strcmp(c, "12:05") == 0);
    fmt_clock12(13, 0, c); assert(strcmp(c, "1:00") == 0);
    fmt_clock12(9, 41, c); assert(strcmp(c, "9:41") == 0);

    fmt_elapsed(1024, e); assert(strcmp(e, "17:04") == 0);
    fmt_elapsed(59, e);   assert(strcmp(e, "0:59") == 0);
    fmt_elapsed(3729, e); assert(strcmp(e, "1:02:09") == 0);

    assert(minutes_rounded(29) == 1);
    assert(minutes_rounded(2520) == 42);
    assert(minutes_rounded(89) == 1);
    assert(minutes_rounded(90) == 2);
    return 0;
}
