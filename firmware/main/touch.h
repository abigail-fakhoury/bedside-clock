#pragma once
#include <stdbool.h>
#include "lvgl.h"

/* CST9220 at 0x5A on the shared bus: ten-byte reports from 0xD000, reset
 * GPIO11, IRQ GPIO5 (falling edge per report). On a cold boot the chip is
 * reset and its chip type logged; after a deep-sleep wake it is left as it
 * was, so the waking contact is not lost. Registers an LVGL pointer input on
 * the default display; call after display_init(). */
void touch_init(void);

/* The LVGL pointer input registered by touch_init(). */
lv_indev_t *touch_indev(void);

/* Falling edges seen on the IRQ line since the last call. */
unsigned touch_irq_edges(void);
