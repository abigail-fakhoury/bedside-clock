#include <assert.h>
#include <string.h>
#include "core/civil.h"
#include "core/rtcreg.h"

/* Registers 0x04–0x0A: sec, min, hour, day, weekday, month, year (BCD) */
static void regs(uint8_t d[7], uint8_t s, uint8_t mi, uint8_t h, uint8_t day,
                 uint8_t wd, uint8_t mo, uint8_t y)
{
    d[0] = s; d[1] = mi; d[2] = h; d[3] = day; d[4] = wd; d[5] = mo; d[6] = y;
}

int main(void)
{
    uint8_t d[7], e[7];
    time_t t;

    /* Plain read: 2026-11-11 23:14:37 UTC, a Wednesday */
    regs(d, 0x37, 0x14, 0x23, 0x11, 3, 0x11, 0x26);
    assert(rtcreg_decode(0x00, d, &t));
    assert(t == (time_t)utc_seconds(2026, 11, 11, 23, 14, 37));

    /* BCD digit edges: 09 and 10, 59 */
    regs(d, 0x09, 0x10, 0x09, 0x10, 0, 0x09, 0x10);
    assert(rtcreg_decode(0x00, d, &t));
    assert(t == (time_t)utc_seconds(2010, 9, 10, 9, 10, 9));
    regs(d, 0x59, 0x59, 0x23, 0x31, 0, 0x12, 0x99);
    assert(rtcreg_decode(0x00, d, &t));
    assert(t == (time_t)utc_seconds(2099, 12, 31, 23, 59, 59));
    regs(d, 0x00, 0x00, 0x00, 0x01, 6, 0x01, 0x00);
    assert(rtcreg_decode(0x00, d, &t));
    assert(t == (time_t)utc_seconds(2000, 1, 1, 0, 0, 0));

    /* Invalid BCD nibble in any field */
    regs(d, 0x1A, 0x14, 0x23, 0x11, 3, 0x11, 0x26); assert(!rtcreg_decode(0, d, &t));
    regs(d, 0x37, 0x1A, 0x23, 0x11, 3, 0x11, 0x26); assert(!rtcreg_decode(0, d, &t));
    regs(d, 0x37, 0x14, 0x23, 0x11, 3, 0x11, 0xA0); assert(!rtcreg_decode(0, d, &t));
    /* Valid BCD but impossible values */
    regs(d, 0x60, 0x14, 0x23, 0x11, 3, 0x11, 0x26); assert(!rtcreg_decode(0, d, &t));
    regs(d, 0x37, 0x14, 0x24, 0x11, 3, 0x11, 0x26); assert(!rtcreg_decode(0, d, &t));
    regs(d, 0x37, 0x14, 0x23, 0x29, 0, 0x02, 0x25); assert(!rtcreg_decode(0, d, &t));

    /* Oscillator-stop flag (seconds bit 7) */
    regs(d, 0x80 | 0x37, 0x14, 0x23, 0x11, 3, 0x11, 0x26);
    assert(!rtcreg_decode(0x00, d, &t));

    /* Control_1: STOP (0x20) and 12-hour mode (0x02) both reject */
    regs(d, 0x37, 0x14, 0x23, 0x11, 3, 0x11, 0x26);
    assert(!rtcreg_decode(0x20, d, &t));
    assert(!rtcreg_decode(0x02, d, &t));
    assert(rtcreg_decode(0x01, d, &t)); /* unrelated control bits ignored */

    /* Unused high bits are masked: min bit 7, hour bits 7–6, day bits 7–6,
     * month bits 7–5 */
    regs(d, 0x37, 0x80 | 0x14, 0xC0 | 0x23, 0xC0 | 0x11, 3, 0xE0 | 0x11, 0x26);
    assert(rtcreg_decode(0x00, d, &t));
    assert(t == (time_t)utc_seconds(2026, 11, 11, 23, 14, 37));

    /* Failed decode leaves *out alone */
    t = 12345;
    regs(d, 0x80, 0, 0, 1, 0, 1, 0);
    assert(!rtcreg_decode(0x00, d, &t) && t == 12345);

    /* Encode: BCD fields, weekday 0=Sunday, OS bit clear */
    assert(rtcreg_encode((time_t)utc_seconds(2026, 11, 11, 23, 14, 37), e));
    regs(d, 0x37, 0x14, 0x23, 0x11, 3, 0x11, 0x26);
    assert(memcmp(d, e, 7) == 0);
    assert(rtcreg_encode((time_t)utc_seconds(2000, 1, 1, 0, 0, 0), e));
    regs(d, 0x00, 0x00, 0x00, 0x01, 6, 0x01, 0x00);
    assert(memcmp(d, e, 7) == 0);
    assert(rtcreg_encode((time_t)utc_seconds(2099, 12, 31, 23, 59, 59), e));
    regs(d, 0x59, 0x59, 0x23, 0x31, 4, 0x12, 0x99);
    assert(memcmp(d, e, 7) == 0);

    /* Out of the chip's 2000–2099 range */
    assert(!rtcreg_encode((time_t)utc_seconds(1999, 12, 31, 23, 59, 59), e));
    assert(!rtcreg_encode((time_t)utc_seconds(2100, 1, 1, 0, 0, 0), e));

    /* Feb 29 round trip */
    time_t leap = (time_t)utc_seconds(2028, 2, 29, 12, 34, 56);
    assert(rtcreg_encode(leap, e));
    assert(e[3] == 0x29 && e[5] == 0x02 && e[6] == 0x28 && e[4] == 2); /* Tuesday */
    assert(rtcreg_decode(0x00, e, &t) && t == leap);
    return 0;
}
