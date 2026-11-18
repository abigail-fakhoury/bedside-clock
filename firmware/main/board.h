#pragma once
#include <stdbool.h>
#include "driver/i2c_master.h"

/* Waveshare ESP32-C6 Touch AMOLED 2.16. Panel CS and touch IRQ are swapped
 * in some vendor examples; these match the schematic. */
enum {
    PIN_PANEL_CLK = 0,
    PIN_PANEL_D0 = 1,
    PIN_PANEL_D1 = 2,
    PIN_PANEL_D2 = 3,
    PIN_PANEL_D3 = 4,
    PIN_TOUCH_IRQ = 5,
    PIN_SD_CS = 6,
    PIN_I2C_SCL = 7,
    PIN_I2C_SDA = 8,
    PIN_BOOT = 9,
    PIN_KEY = 10,
    PIN_TOUCH_RESET = 11,
    PIN_PANEL_CS = 15,
    PIN_AUDIO_MCLK = 19,
    PIN_AUDIO_BCLK = 20,
    PIN_AUDIO_DIN = 21,
    PIN_AUDIO_LRCLK = 22,
    PIN_AUDIO_DOUT = 23,
};

/* Restores pins parked by board_deep_sleep(), brings up the shared I²C bus,
 * the PMIC rails, the buttons, the IMU (left shut down) and turns the RTC's
 * CLKOUT off. A cold boot also reconfigures charging, shuts the audio chips
 * down and logs every ACKing address. */
void board_init(bool cold_boot);

/* Shuts the ES7210 microphone ADC down (Espressif's stop sequence, up to
 * three tries) and reads the registers back; true when verified off. Only
 * the ES7210: the ES8311 is left as it is. For after each recording. */
bool board_mic_adc_off(void);

/* Logs the reset or wake cause and the boot and sleep counts kept in RTC
 * memory across deep sleep. */
void board_log_wake(void);

/* The one I²C master bus (port 0, SCL 7 / SDA 8). */
i2c_master_bus_handle_t board_i2c(void);

/* Pulses ALDO3, which drives the panel's reset line. */
void board_panel_reset(void);

/* PMIC fuel-gauge estimate 0–100, or −1 with no battery or a bad read. */
int board_battery_percent(void);

/* Debounced (200 ms) button presses since the last call. */
enum { BUTTON_BOOT = 1 << 0, BUTTON_KEY = 1 << 1 };
unsigned board_buttons(void);

/* 0, 90, 180 or 270: clockwise rotation that draws the clock upright, from
 * one QMI8658 sample. The IMU is shut down again after each sample. Keeps
 * the previous answer (retained across deep sleep) when the reading is
 * ambiguous or fails. */
int board_orientation(void);

/* Development safety net, OFF in the committed build: it costs battery.
 * Set to 1 only while developing: every deep sleep then also arms a timer
 * wake every BOARD_DEV_SAFETY_WAKE_S seconds, so a device whose touch wake
 * fails is still recoverable over USB without a power cycle. On such a
 * wake board_early_wake() goes straight back to sleep unless a USB host is
 * attached. (Separately, and always: the main loop idles awake instead of
 * deep sleeping while a USB host is attached.) */
#define BOARD_DEV_SAFETY_WAKE 0
#define BOARD_DEV_SAFETY_WAKE_S 60

typedef enum {
    BOARD_BOOT_NORMAL,     /* cold boot or touch wake */
    BOARD_BOOT_QUIET_HOST, /* safety tick with a USB host: idle, panel dark */
} board_boot_t;

/* Call first in app_main. On a safety-tick wake with no USB host (no VBUS,
 * or no USB start-of-frame within 1 s) it goes straight back to deep sleep
 * without touching the panel or the held pins, and does not return. */
board_boot_t board_early_wake(void);

/* Panel Sleep In (retried, then reset), park and hold pins as
 * bedroom-clock's sleep handoff does, wake on the touch IRQ (GPIO5: LP falling edge; was EXT1
 * low plus an LP GPIO falling-edge latch) and the timer, log the pin levels
 * and holds, then deep sleep. Never returns: waking restarts the firmware. */
void board_deep_sleep(void) __attribute__((noreturn));

/* Boot-stage breadcrumbs kept in RTC no-init memory, which survives deep
 * sleep and RTS/software resets: STATUS shows how far the previous boot got
 * if it hung. */
typedef enum {
    STAGE_APP_START = 1, STAGE_EARLY_WAKE, STAGE_BOARD, STAGE_DISPLAY, STAGE_TOUCH,
    STAGE_SHOWN, STAGE_LOOP, STAGE_SLEEP_ENTRY, STAGE_SLEEP_START,
} board_stage_t;
void board_stage(board_stage_t stage);
/* Logs the previous boot's last stage and this boot's. */
void board_log_stages(void);

/* Debug trace kept in RTC no-init memory, reported by board_log_stages()
 * for the previous boot. */
void board_trace(uint32_t loops, bool usb, bool idle);
