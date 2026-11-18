#pragma once
#include <stdbool.h>
#include <stdint.h>

/* CO5300 480×480 AMOLED over QSPI plus LVGL 9 with a 480×40 RGB565 partial
 * buffer. Clears the screen to black with brightness left at 0. A cold boot
 * resets the panel; after a deep-sleep wake it only leaves Sleep In. Call
 * after board_init(). LVGL is single-threaded: call it only from the caller's
 * task, and keep calling lv_timer_handler(). */
void display_init(bool cold_boot);

/* Every strip drawn from now on is turned by quarter_turns (rect_rotate's
 * sense) on its way to the panel, so a screen laid out upright shows turned
 * with the board. 0 for the clock face, which turns its own drawing. A
 * change invalidates the whole screen. */
void display_set_turns(int quarter_turns);

/* Panel brightness register 0x51, 0–255. */
void display_brightness(uint8_t level);

/* Display Off + Sleep In. Pixels are retained. False (logged) if either
 * command failed; display_wake() then still runs its full sequence. */
bool display_sleep(void);

/* display_sleep(), retried once; if both fail, a hardware panel reset
 * followed by Sleep In, so the panel cannot be left lit. */
void display_sleep_reliably(void);

/* Sleep Out, restore module configuration and brightness, Display On, if
 * any sleep command was sent since the last complete wake. Errors are
 * logged and leave the next call to retry the whole sequence. */
void display_wake(void);
