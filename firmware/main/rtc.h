#pragma once
#include <stdbool.h>
#include <time.h>

/* Attaches the PCF85063A and creates the lock that makes the calls below
 * safe from any task. board_init() calls it; the others fail before it. */
void rtc_init(void);

/* PCF85063A at 0x51 on the shared bus, holding UTC. False when the chip
 * reports its oscillator stopped or the registers do not hold a valid time. */
bool rtc_get_utc(time_t *out);

/* Writes UTC and clears the oscillator-stop flag. */
bool rtc_set_utc(time_t t);

/* Disables the unused CLKOUT (Control_2 COF = 111) and verifies it. */
bool rtc_clkout_disable(void);
