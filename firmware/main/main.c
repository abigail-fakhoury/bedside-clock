#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "config.h"
#include "core/civil.h"
#include "display.h"
#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "flow.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "rtc.h"
#include "touch.h"
#include "ui.h"

static const char *TAG = "main";

/* All set and read on the main task: config_usb_poll() runs the commands */
static bool refresh_now, status_now;
static uint32_t first_frame_ms; /* log timestamp, which includes the bootloader */

static void log_utc(const char *what, time_t t)
{
    struct tm tm;
    char s[24];
    gmtime_r(&t, &tm);
    strftime(s, sizeof(s), "%Y-%m-%dT%H:%M:%SZ", &tm);
    ESP_LOGI(TAG, "%s %s", what, s);
}

/* "TIME YYYYMMDDhhmmss" (UTC) over USB sets the RTC by hand; net_up()
 * sets it from the network. */
static void set_time(const char *arg)
{
    int y, mo, d, h, mi, s;
    if (strlen(arg) != 14 || sscanf(arg, "%4d%2d%2d%2d%2d%2d", &y, &mo, &d, &h, &mi, &s) != 6 ||
        !civil_valid(y, mo, d, h, mi, s)) {
        /* Never echo the input: a merged line could hold a secret */
        ESP_LOGW(TAG, "TIME wants YYYYMMDDhhmmss UTC (got %u bytes)", (unsigned)strlen(arg));
        return;
    }
    time_t t = (time_t)utc_seconds(y, mo, d, h, mi, s);
    if (rtc_set_utc(t)) {
        log_utc("RTC set to", t);
    } else {
        ESP_LOGE(TAG, "RTC write failed");
    }
    refresh_now = true;
}

/* USB line commands (config_usb_poll() handles CFG itself):
 *   CFG <key> <value>    store a setting (see config.h; tools/provision.py)
 *   TIME YYYYMMDDhhmmss  set the RTC (UTC) by hand
 *   STATUS               log wake cause, counters and display state
 *   FLOW                 log the screen, session, outbox, shelf cache and
 *                        recent books
 * STATUS and FLOW only read. */
static void handle_line(const char *line)
{
    if (strncmp(line, "TIME ", 5) == 0) {
        set_time(line + 5);
    } else if (strcmp(line, "STATUS") == 0) {
        status_now = true;
    } else if (strcmp(line, "FLOW") == 0) {
        flow_log();
    } else {
        /* Never echo the line: it could be a mistyped setting */
        ESP_LOGW(TAG, "unknown command (%u bytes)", (unsigned)strlen(line));
    }
}

static void log_status(void)
{
    time_t t;
    if (rtc_get_utc(&t)) log_utc("RTC reads", t);
    else ESP_LOGW(TAG, "RTC read failed or holds no valid time");
    board_log_wake();
    board_log_stages();
    ui_log_status();
    ESP_LOGI(TAG, "first clock frame of this boot at %lu ms; settings %s",
             (unsigned long)first_frame_ms, config_complete() ? "complete" : "incomplete");
    ESP_LOGI(TAG, "uptime %lld ms, USB host %s, touch IRQ level %d, %u IRQ edge(s) since last "
             "report, heap free %u, min %u", esp_timer_get_time() / 1000,
             usb_serial_jtag_is_connected() ? "attached" : "absent", gpio_get_level(PIN_TOUCH_IRQ),
             touch_irq_edges(), (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}

void app_main(void)
{
    /* A quiet safety tick without a USB host never gets past this */
    board_stage(STAGE_APP_START);
    board_boot_t boot = board_early_wake();
    board_stage(STAGE_EARLY_WAKE);
    bool cold = esp_reset_reason() != ESP_RST_DEEPSLEEP;
    ESP_LOGI(TAG, "boot (%s), free heap %u", cold ? "cold" : "deep-sleep wake",
             (unsigned)esp_get_free_heap_size());
    usb_serial_jtag_driver_config_t usb_cfg = {.tx_buffer_size = 256, .rx_buffer_size = 256};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));

    board_init(cold);
    board_stage(STAGE_BOARD);
    board_log_wake();
    board_log_stages();

    config_init(handle_line); /* also sets TZ from tz_posix */
    flow_init();
    flow_log();

    display_init(cold);
    board_stage(STAGE_DISPLAY);
    touch_init();
    board_stage(STAGE_TOUCH);
    ui_init();

    if (boot == BOARD_BOOT_QUIET_HOST) {
        /* Safety tick with a computer attached: idle as USB-idle mode does */
        display_sleep_reliably();
        ESP_LOGI(TAG, "safety tick with USB host: staying awake, panel dark");
    } else {
        flow_wake(); /* the clock, or what the session needs */
        first_frame_ms = esp_log_timestamp();
        ESP_LOGI(TAG, "clock on screen %lld ms after app start, heap free %u, min %u",
                 esp_timer_get_time() / 1000, (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size());
    }

    board_stage(STAGE_SHOWN);
    bool was_idle = false;
    TickType_t last_heap_log = xTaskGetTickCount();
    for (;;) {
        config_usb_poll();
        if (config_changed()) refresh_now = true;
        if (refresh_now) {
            refresh_now = false;
            ui_refresh_time();
        }
        if (status_now) {
            status_now = false;
            log_status();
        }

        board_stage(STAGE_LOOP);
        flow_poll(ui_poll());

        bool idle = ui_idle();
        static uint32_t loops;
        board_trace(++loops, usb_serial_jtag_is_connected(), idle);
        if (idle && !usb_serial_jtag_is_connected() && !flow_busy()) {
            board_deep_sleep();
        }
        if (idle && !was_idle) {
            /* Stay flashable: the panel sleeps, the chip stays up */
            ESP_LOGI(TAG, "idle with USB host attached: staying awake, panel asleep "
                     "(touch IRQ level %d)", gpio_get_level(PIN_TOUCH_IRQ));
        }
        was_idle = idle;

        TickType_t now = xTaskGetTickCount();
        if (now - last_heap_log >= pdMS_TO_TICKS(60000)) {
            last_heap_log = now;
            log_status();
        }
        uint32_t wait = lv_timer_handler();
        if (!idle) wait = 10; /* fade steps */
        vTaskDelay(pdMS_TO_TICKS(wait < 10 ? 10 : wait > 50 ? 50 : wait));
    }
}
