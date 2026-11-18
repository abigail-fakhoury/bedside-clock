#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* PCF85063A register codec. d[] is registers 0x04–0x0A: seconds, minutes,
 * hours, days, weekday, months, years, all BCD except weekday (0 = Sunday). */

/* control1 is register 0x00. False when STOP or 12-hour mode is set, the
 * oscillator-stop flag (seconds bit 7) is set, or the fields are not a valid
 * BCD date in 2000–2099; *out is untouched then. */
bool rtcreg_decode(uint8_t control1, const uint8_t d[7], time_t *out);

/* Fills d[] for UTC time t with the oscillator-stop flag clear. False outside
 * 2000–2099. */
bool rtcreg_encode(time_t t, uint8_t d[7]);
