#include "rtc.h"

#include "board.h"
#include "core/rtcreg.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

enum {
    RTC_ADDR = 0x51,
    REG_CONTROL1 = 0x00,
    REG_SECONDS = 0x04, /* 0x04–0x0A: sec, min, hour, day, weekday, month, year */
    CTRL_STOP = 0x20,
    CTRL_12H = 0x02,
};

/* Both set by rtc_init() before any other task runs; every access below
 * holds `lock`, so a write from the network task cannot interleave with a
 * read from the UI */
static i2c_master_dev_handle_t dev;
static SemaphoreHandle_t lock;

void rtc_init(void)
{
    if (lock) return;
    lock = xSemaphoreCreateMutex();
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = RTC_ADDR,
        .scl_speed_hz = 400000,
    };
    if (!lock || i2c_master_bus_add_device(board_i2c(), &cfg, &dev) != ESP_OK) {
        dev = NULL;
        ESP_LOGE("rtc", "PCF85063A device setup failed");
    }
}

static bool take(void)
{
    if (!lock || !dev) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    return true;
}

static void give(void) { xSemaphoreGive(lock); }

static bool rtc_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, 50) == ESP_OK;
}

static bool rtc_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[8];
    if (len > sizeof(buf) - 1) return false;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[i + 1] = data[i];
    return i2c_master_transmit(dev, buf, len + 1, 50) == ESP_OK;
}

bool rtc_get_utc(time_t *out)
{
    uint8_t control, d[7];
    if (!take()) return false;
    bool ok = rtc_read(REG_CONTROL1, &control, 1) && rtc_read(REG_SECONDS, d, sizeof(d));
    give();
    return ok && rtcreg_decode(control, d, out);
}

static bool set_locked(time_t t)
{
    uint8_t control, d[7];
    if (!rtcreg_encode(t, d)) return false;
    if (!rtc_read(REG_CONTROL1, &control, 1)) return false;
    /* Stop the clock while writing; force 24-hour mode */
    control = (control & ~(CTRL_STOP | CTRL_12H)) | CTRL_STOP;
    if (!rtc_write(REG_CONTROL1, &control, 1)) return false;
    /* Seconds written with OS = 0 clears the oscillator-stop flag */
    bool ok = rtc_write(REG_SECONDS, d, sizeof(d));
    /* Restart even if writing the time failed */
    control &= ~CTRL_STOP;
    bool running = rtc_write(REG_CONTROL1, &control, 1);
    return ok && running;
}

bool rtc_set_utc(time_t t)
{
    if (!take()) return false;
    bool ok = set_locked(t);
    give();
    return ok;
}

bool rtc_clkout_disable(void)
{
    enum { REG_CONTROL2 = 0x01 };
    uint8_t control;
    if (!take()) return false;
    if (!rtc_read(REG_CONTROL2, &control, 1)) {
        give();
        return false;
    }
    /* COF = 111 turns CLKOUT off. Writing ones to AF/TF preserves those
     * flags, including one raised between the read and the write
     * (bedroom-clock rtc.c). */
    control |= 0x4f;
    bool ok = rtc_write(REG_CONTROL2, &control, 1) && rtc_read(REG_CONTROL2, &control, 1) &&
              (control & 0x07) == 0x07;
    give();
    return ok;
}
