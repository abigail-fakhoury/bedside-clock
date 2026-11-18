#include "display.h"

#include <stdbool.h>
#include <stdlib.h>

#include "board.h"
#include "core/rot.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "display";

enum { WIDTH = 480, HEIGHT = 480, BUF_ROWS = 40 };

/* CO5300 init from Waveshare's 09_LVGL_V9_Test, driven through the SH8601
 * driver as Waveshare does. Brightness starts at 0 so nothing shows until the
 * first frame has cleared panel RAM. Entry 0 is Sleep Out (its delay is
 * shortened after a deep-sleep wake); display_wake() replays the rest because
 * Sleep Out reloads the module defaults. */
static sh8601_lcd_init_cmd_t init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 600},
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x36, (uint8_t[]){0x30}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0x00}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x29, (uint8_t[]){0x00}, 0, 100},
};

static esp_lcd_panel_io_handle_t io;
static esp_lcd_panel_handle_t panel;
static lv_display_t *disp;
static uint8_t brightness;
/* asleep: Display Off and Sleep In both acknowledged. maybe_asleep: any
 * sleep command was sent since the last complete wake, so the panel may be
 * dark and display_wake() must run its full sequence. */
static bool asleep, maybe_asleep;
static unsigned flush_count, flush_errors;
/* Quarter turns applied to every strip as it is flushed (0 for the clock
 * face, which draws itself turned), and the scratch rot_strip needs */
static int flush_turns;
static uint8_t rot_visited[(ROT_MAX_PIXELS + 7) / 8];

/* QSPI command framing: opcode 0x02, then the command in the middle byte. */
static esp_err_t command(uint8_t cmd, const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(io, (0x02 << 24) | (cmd << 8), data, len);
}

static bool on_color_done(esp_lcd_panel_io_handle_t panel_io,
                          esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    lv_display_flush_ready((lv_display_t *)ctx);
    return false;
}

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px)
{
    lv_draw_sw_rgb565_swap(px, lv_area_get_size(area));
    rect_t r = {area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area)};
    if (flush_turns) rot_strip((uint16_t *)px, &r, flush_turns, rot_visited);
    esp_err_t err = esp_lcd_panel_draw_bitmap(panel, r.x, r.y, r.x + r.w, r.y + r.h, px);
    flush_count++;
    if (err != ESP_OK) {
        flush_errors++;
        lv_display_flush_ready(d); /* no transfer queued, so no callback */
    }
}

/* CO5300 windows need even start coordinates and even sizes. */
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *a = lv_event_get_param(e);
    a->x1 &= ~1;
    a->y1 &= ~1;
    a->x2 |= 1;
    a->y2 |= 1;
}

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

void display_init(bool cold_boot)
{
    const size_t buf_bytes = WIDTH * BUF_ROWS * sizeof(uint16_t);
    spi_bus_config_t bus = SH8601_PANEL_BUS_QSPI_CONFIG(
        PIN_PANEL_CLK, PIN_PANEL_D0, PIN_PANEL_D1, PIN_PANEL_D2, PIN_PANEL_D3, buf_bytes);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    lv_init();
    lv_tick_set_cb(tick_ms);
    disp = lv_display_create(WIDTH, HEIGHT);

    esp_lcd_panel_io_spi_config_t io_cfg =
        SH8601_PANEL_IO_QSPI_CONFIG(PIN_PANEL_CS, on_color_done, disp);
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io));

    sh8601_vendor_config_t vendor = {
        .init_cmds = init_cmds,
        .init_cmds_size = sizeof(init_cmds) / sizeof(init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    /* After deep sleep the panel is in Sleep In with its reset line still
     * high: Sleep Out needs only the 120 ms transition guard
     * (bedroom-clock display_wake). A cold boot resets it and waits 600 ms. */
    init_cmds[0].delay_ms = cold_boot ? 600 : 120;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1, /* reset is on PMIC ALDO3 */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io, &panel_cfg, &panel));
    if (cold_boot) board_panel_reset();
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_LOGI(TAG, "CO5300 panel init OK (%s)", cold_boot ? "reset" : "Sleep Out after deep sleep");

    void *buf = heap_caps_malloc(buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        ESP_LOGE(TAG, "no DMA memory for the %u-byte draw buffer", (unsigned)buf_bytes);
        abort();
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf, NULL, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_refr_now(disp); /* returns after the last chunk's transfer completes */
    ESP_LOGI(TAG, "first LVGL frame flushed: %u chunks, %u errors", flush_count, flush_errors);
    /* Brightness stays 0; the clock fades itself in */
}

void display_set_turns(int quarter_turns)
{
    if ((quarter_turns & 3) == flush_turns) return;
    flush_turns = quarter_turns & 3;
    lv_obj_invalidate(lv_screen_active());
}

void display_brightness(uint8_t level)
{
    brightness = level;
    if (command(0x51, &level, 1) != ESP_OK) {
        ESP_LOGE(TAG, "brightness write failed");
    }
}

bool display_sleep(void)
{
    if (asleep) return true;
    maybe_asleep = true;
    esp_err_t off = command(0x28, NULL, 0);
    esp_err_t in = command(0x10, NULL, 0);
    /* CO5300: opposite sleep transitions must be 120 ms apart */
    vTaskDelay(pdMS_TO_TICKS(120));
    if (off != ESP_OK || in != ESP_OK) {
        ESP_LOGE(TAG, "panel sleep failed: Display Off %s, Sleep In %s",
                 esp_err_to_name(off), esp_err_to_name(in));
        return false;
    }
    asleep = true;
    return true;
}

void display_sleep_reliably(void)
{
    if (display_sleep()) return;
    ESP_LOGW(TAG, "retrying panel sleep");
    if (display_sleep()) return;
    /* A hardware reset leaves the CO5300 in Sleep In with the display off;
     * send Sleep In as well in case the reset line write failed */
    ESP_LOGE(TAG, "panel sleep failed twice: resetting the panel so it cannot stay lit");
    board_panel_reset();
    esp_err_t in = command(0x10, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    if (in != ESP_OK) ESP_LOGE(TAG, "Sleep In after panel reset failed: %s", esp_err_to_name(in));
    maybe_asleep = true;
    asleep = in == ESP_OK;
}

void display_wake(void)
{
    if (!maybe_asleep) return;
    esp_err_t err = command(0x11, NULL, 0);
    if (err != ESP_OK) ESP_LOGE(TAG, "panel wake: Sleep Out failed: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(120));
    for (size_t i = 1; i < sizeof(init_cmds) / sizeof(init_cmds[0]); i++) {
        esp_err_t e = command(init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].data_bytes);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "panel wake: command 0x%02x failed: %s", init_cmds[i].cmd, esp_err_to_name(e));
            if (err == ESP_OK) err = e;
        }
        if (init_cmds[i].delay_ms) vTaskDelay(pdMS_TO_TICKS(init_cmds[i].delay_ms));
    }
    display_brightness(brightness);
    /* Not asleep any more in either case; a failed step keeps maybe_asleep
     * so the next wake replays the whole sequence */
    asleep = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel wake incomplete: %s", esp_err_to_name(err));
        return;
    }
    maybe_asleep = false;
}
