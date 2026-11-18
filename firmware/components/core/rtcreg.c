#include "core/rtcreg.h"

#include "core/civil.h"

enum { CTRL_STOP = 0x20, CTRL_12H = 0x02, SECONDS_OS = 0x80 };

static int from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 15); }
static uint8_t to_bcd(int v) { return (uint8_t)((v / 10) << 4 | v % 10); }
static bool bcd_ok(uint8_t v) { return (v & 15) < 10 && (v >> 4) < 10; }

bool rtcreg_decode(uint8_t control1, const uint8_t d[7], time_t *out)
{
    if (control1 & (CTRL_STOP | CTRL_12H)) return false;
    if (d[0] & SECONDS_OS) return false;
    /* Masks drop the unused high bits of each field; weekday is not used */
    const uint8_t f[6] = {d[0], d[1] & 0x7f, d[2] & 0x3f, d[3] & 0x3f, d[5] & 0x1f, d[6]};
    for (int i = 0; i < 6; i++) {
        if (!bcd_ok(f[i])) return false;
    }
    int second = from_bcd(f[0]), minute = from_bcd(f[1]), hour = from_bcd(f[2]);
    int day = from_bcd(f[3]), month = from_bcd(f[4]), year = 2000 + from_bcd(f[5]);
    if (!civil_valid(year, month, day, hour, minute, second)) return false;
    *out = (time_t)utc_seconds(year, month, day, hour, minute, second);
    return true;
}

bool rtcreg_encode(time_t t, uint8_t d[7])
{
    int64_t s = (int64_t)t;
    int64_t days = s >= 0 ? s / 86400 : (s - 86399) / 86400;
    int64_t rem = s - days * 86400;
    /* civil_from_days (Howard Hinnant) */
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    int day = (int)(doy - (153 * mp + 2) / 5 + 1);
    int month = (int)(mp < 10 ? mp + 3 : mp - 9);
    int year = (int)(yoe + era * 400 + (month <= 2));
    int hour = (int)(rem / 3600), minute = (int)(rem % 3600 / 60), second = (int)(rem % 60);
    if (!civil_valid(year, month, day, hour, minute, second)) return false;
    d[0] = to_bcd(second); /* OS bit clear */
    d[1] = to_bcd(minute);
    d[2] = to_bcd(hour);
    d[3] = to_bcd(day);
    d[4] = (uint8_t)((days + 4) % 7); /* 1970-01-01 was a Thursday */
    d[5] = to_bcd(month);
    d[6] = to_bcd(year - 2000);
    return true;
}
