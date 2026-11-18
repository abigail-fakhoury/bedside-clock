#include "touch.h"

#include "board.h"
#include "core/touchmap.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

enum { TOUCH_ADDR = 0x5A, REPORT_REG = 0xD000 };

static i2c_master_dev_handle_t dev;
static lv_indev_t *indev;
static volatile bool irq_pending;
static volatile unsigned irq_count;
static bool down;
static lv_point_t point;

static void IRAM_ATTR on_irq(void *arg)
{
    irq_pending = true;
    irq_count++;
}

static bool read16(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t addr[2] = {reg >> 8, reg & 0xff};
    return i2c_master_transmit_receive(dev, addr, 2, data, len, 50) == ESP_OK;
}

static void hard_reset(unsigned low_ms, unsigned high_ms)
{
    gpio_set_level(PIN_TOUCH_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(low_ms));
    gpio_set_level(PIN_TOUCH_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(high_ms));
}

/* Command mode (0xD101) exposes the chip type at 0xD204, as SensorLib's
 * TouchDrvCST92xx::getAttribute() reads it. A reset afterwards returns the
 * chip to normal reporting. */
static void log_chip_type(void)
{
    uint8_t enter[2] = {0xD1, 0x01};
    uint8_t info[4] = {0}, code[4] = {0};
    hard_reset(10, 50);
    bool ok = i2c_master_transmit(dev, enter, 2, 50) == ESP_OK;
    vTaskDelay(pdMS_TO_TICKS(10));
    ok = ok && read16(0xD1FC, code, sizeof(code)) && read16(0xD204, info, sizeof(info));
    if (!ok) {
        ESP_LOGE(TAG, "CST92xx command-mode read failed");
        return;
    }
    unsigned type = info[3] << 8 | info[2];
    unsigned check = code[3] << 8 | code[2];
    ESP_LOGI(TAG, "chip type 0x%04x (CST%04x), checkcode 0x%04x%s", type, type, check,
             check == 0xCACA ? "" : " (unexpected)");
}

/* Normal reset configuration, as bedroom-clock's init_touch() does */
static bool reset_and_probe(void)
{
    hard_reset(200, 200);
    uint8_t data[TOUCH_REPORT_LEN];
    return read16(REPORT_REG, data, sizeof(data));
}

/* Report layout from Waveshare's driver via bedroom-clock touch.c; the
 * decode and the panel-to-screen mapping live in core/touchmap.c */
static bool read_report(void)
{
    uint8_t d[TOUCH_REPORT_LEN];
    int x, y;
    if (!read16(REPORT_REG, d, sizeof(d)) || !touch_decode(d, &down, &x, &y)) return false;
    if (down) {
        point.x = x;
        point.y = y;
    }
    return true;
}

static void read_cb(lv_indev_t *in, lv_indev_data_t *data)
{
    /* Read only after an IRQ, or while a contact is held to see it lift */
    if (irq_pending || down) {
        bool was_down = down;
        irq_pending = false;
        if (!read_report()) down = false;
        if (down && !was_down) ESP_LOGI(TAG, "press at %d,%d", (int)point.x, (int)point.y);
    }
    data->point = point;
    data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void touch_init(void)
{
    bool cold = esp_reset_reason() != ESP_RST_DEEPSLEEP;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(board_i2c(), &cfg, &dev));

    /* board_init() already drives reset high and releases any sleep hold */
    gpio_config_t irq = {
        .pin_bit_mask = 1ULL << PIN_TOUCH_IRQ,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&irq));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_TOUCH_IRQ, on_irq, NULL));

    if (cold) {
        log_chip_type();
        bool ok = reset_and_probe();
        if (ok) ESP_LOGI(TAG, "CST9220 answers at 0x%02x after reset", TOUCH_ADDR);
        else ESP_LOGE(TAG, "CST9220 did not answer at 0x%02x", TOUCH_ADDR);
    } else {
        /* Keep the chip as it was; a pending report is the waking tap */
        irq_pending = true;
        ESP_LOGI(TAG, "kept CST9220 state across deep sleep");
    }
    irq_count = 0;
    ESP_LOGI(TAG, "IRQ GPIO%d level %d (idle high)", PIN_TOUCH_IRQ, gpio_get_level(PIN_TOUCH_IRQ));

    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
}

lv_indev_t *touch_indev(void) { return indev; }

unsigned touch_irq_edges(void)
{
    unsigned n = irq_count;
    irq_count = 0;
    return n;
}
