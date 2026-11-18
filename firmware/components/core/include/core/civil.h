#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Seconds since 1970-01-01 00:00:00 UTC for a proleptic Gregorian date. */
int64_t utc_seconds(int year, int month, int day, int hour, int minute, int second);

/* True for a real date and time in 2000–2099, the RTC's two-digit range. */
bool civil_valid(int year, int month, int day, int hour, int minute, int second);
