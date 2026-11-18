#include "core/civil.h"

/* Days from 1970-01-01 (Howard Hinnant's days_from_civil). */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t utc_seconds(int year, int month, int day, int hour, int minute, int second)
{
    return days_from_civil(year, (unsigned)month, (unsigned)day) * 86400 +
           hour * 3600 + minute * 60 + second;
}

bool civil_valid(int year, int month, int day, int hour, int minute, int second)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 2000 || year > 2099 || month < 1 || month > 12) {
        return false;
    }
    int leap = month == 2 && year % 4 == 0; /* exact for 2000–2099 */
    if (day < 1 || day > days[month - 1] + leap) {
        return false;
    }
    return hour >= 0 && hour < 24 && minute >= 0 && minute < 60 &&
           second >= 0 && second < 60;
}
