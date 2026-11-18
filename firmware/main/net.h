#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Wi-Fi is off unless something calls net_up(); nothing starts it at wake
 * or in the background. */

/* Starts Wi-Fi as a station on the configured wifi_ssid / wifi_pass (the
 * ESP32-C6 radio is 2.4 GHz only) and waits up to timeout_ms for an IP
 * address. Then syncs time from pool.ntp.org (up to NET_SNTP_TIMEOUT_MS
 * more) and on success writes it to the RTC with rtc_set_utc(). True when
 * connected, whether or not SNTP answered (net_time_synced() says). On
 * failure Wi-Fi is already fully down again. Blocks; any one task. */
bool net_up(uint32_t timeout_ms);

/* Stops SNTP and Wi-Fi and deinitialises them, freeing the driver's
 * memory. Safe to call when already down. */
void net_down(void);

/* The last net_up() wrote network time to the RTC. */
bool net_time_synced(void);

enum { NET_SNTP_TIMEOUT_MS = 10000 };
