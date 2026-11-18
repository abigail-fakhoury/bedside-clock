#include <assert.h>
#include "core/civil.h"

int main(void)
{
    assert(utc_seconds(1970, 1, 1, 0, 0, 0) == 0);
    assert(utc_seconds(2000, 1, 1, 0, 0, 0) == 946684800);
    assert(utc_seconds(2024, 2, 29, 12, 0, 0) == 1709208000);
    assert(utc_seconds(2026, 11, 11, 18, 30, 15) == 1794421815);
    assert(utc_seconds(2099, 12, 31, 23, 59, 59) == 4102444799LL);

    assert(civil_valid(2026, 11, 11, 18, 30, 15));
    assert(civil_valid(2024, 2, 29, 0, 0, 0));
    assert(!civil_valid(2025, 2, 29, 0, 0, 0));
    assert(!civil_valid(1999, 12, 31, 0, 0, 0));
    assert(!civil_valid(2100, 1, 1, 0, 0, 0));
    assert(!civil_valid(2026, 13, 1, 0, 0, 0));
    assert(!civil_valid(2026, 4, 31, 0, 0, 0));
    assert(!civil_valid(2026, 1, 0, 0, 0, 0));
    assert(!civil_valid(2026, 1, 1, 24, 0, 0));
    assert(!civil_valid(2026, 1, 1, 0, 60, 0));
    assert(!civil_valid(2026, 1, 1, 0, 0, 60));
    return 0;
}
