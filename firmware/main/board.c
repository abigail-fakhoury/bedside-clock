#include "board.h"

#include <stdio.h>

#include "core/orient.h"
#include "display.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_sleep.h"
#include "esp_rtc_time.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/rtc_io.h"
#include "driver/usb_serial_jtag.h"
#include "hal/rtc_io_ll.h"
#include "hal/usb_serial_jtag_ll.h"
#include "soc/lp_aon_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rtc.h"

static const char *TAG = "board";

/* AXP2101 registers, as used by bedroom-clock's pmic.c */
enum {
    PMIC_ADDR = 0x34,
    REG_STATUS1 = 0x00,
    REG_COMMON = 0x10,
    REG_CHARGER_CONTROL = 0x18,
    REG_ADC_CONTROL = 0x30,
    REG_IRQ_ENABLE = 0x40,
    REG_IRQ_STATUS = 0x48,
    REG_TS_CONTROL = 0x50,
    REG_CHARGE_CURRENT = 0x62,
    REG_TERMINATION = 0x63,
    REG_CHARGE_VOLTAGE = 0x64,
    REG_LED = 0x69,
    REG_DCDC_ENABLE = 0x80,
    REG_DCDC_MODE = 0x81,
    REG_LDO_ENABLE = 0x90,
    REG_LDO_ENABLE2 = 0x91,
    REG_BATTERY_PERCENT = 0xa4,
};

enum { DCDC1 = 1 << 0, DCDC2 = 1 << 1, DCDC3 = 1 << 2, DCDC4 = 1 << 3, DCDC5 = 1 << 4 };
enum {
    ALDO1 = 1 << 0, ALDO2 = 1 << 1, ALDO3 = 1 << 2, ALDO4 = 1 << 3,
    BLDO1 = 1 << 4, BLDO2 = 1 << 5, CPUSLDO = 1 << 6, DLDO1 = 1 << 7,
};
enum {
    ADC_BATTERY = 1 << 0, ADC_TS = 1 << 1, ADC_VBUS = 1 << 2,
    ADC_VSYS = 1 << 3, ADC_DIE_TEMP = 1 << 4, ADC_GENERAL = 1 << 5,
};
enum { GAUGE_ENABLE = 0x08, CHARGE_ENABLE = 0x02, TERMINATION_ENABLE = 0x10 };

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t pmic, imu;

/* Kept in RTC memory across deep sleep; reset by power-on */
RTC_DATA_ATTR static uint32_t boot_count, deep_sleep_count;
RTC_DATA_ATTR static int8_t orientation_turns;
/* What the timer armed for the last deep sleep was for */
enum { TIMER_NONE, TIMER_SAFETY };
RTC_DATA_ATTR static uint8_t timer_kind;
RTC_DATA_ATTR static uint32_t safety_ticks;
RTC_DATA_ATTR static uint8_t tick_seen; /* last safety tick: bit 0 VBUS, bit 1 USB host */
RTC_DATA_ATTR static uint64_t sleep_entered_us;

RTC_NOINIT_ATTR static uint32_t stage_magic, stage_now, stage_prev;
RTC_NOINIT_ATTR static uint32_t trace_now[5], trace_prev[5];
enum { STAGE_MAGIC = 0x5747A6E5 };

static const char *stage_name(uint32_t s)
{
    static const char *names[] = {"?", "app start", "early wake check", "board init",
                                  "display init", "touch init", "clock shown", "main loop",
                                  "deep sleep entry", "esp_deep_sleep_start"};
    return s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

void board_stage(board_stage_t stage)
{
    if (stage == STAGE_APP_START) {
        stage_prev = stage_magic == STAGE_MAGIC ? stage_now : 0;
        for (int i = 0; i < 5; i++) {
            trace_prev[i] = stage_magic == STAGE_MAGIC ? trace_now[i] : 0;
            trace_now[i] = 0;
        }
        trace_now[4] = esp_reset_reason() == ESP_RST_DEEPSLEEP ? esp_sleep_get_wakeup_causes() : 0;
        trace_now[3] = esp_reset_reason() == ESP_RST_DEEPSLEEP
                           ? (uint32_t)((esp_rtc_get_time_us() - sleep_entered_us) / 1000) : 0;
        stage_magic = STAGE_MAGIC;
    }
    stage_now = stage;
}

void board_trace(uint32_t loops, bool usb, bool idle)
{
    trace_now[0] = loops;
    trace_now[1] = usb;
    trace_now[2] = idle;
}

void board_log_stages(void)
{
    ESP_LOGI(TAG, "previous boot trace: wake causes 0x%02lx, %lu loops, USB %lu, idle %lu, "
             "woke %lu ms after sleep entry", (unsigned long)trace_prev[4], (unsigned long)trace_prev[0],
             (unsigned long)trace_prev[1], (unsigned long)trace_prev[2], (unsigned long)trace_prev[3]);
    ESP_LOGI(TAG, "boot stages: previous boot reached \"%s\", this boot \"%s\"",
             stage_name(stage_prev), stage_name(stage_now));
}

static volatile uint32_t button_events;

i2c_master_bus_handle_t board_i2c(void) { return bus; }

static bool pmic_read(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(pmic, &reg, 1, value, 1, 50) == ESP_OK;
}

static bool pmic_write(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(pmic, buf, 2, 50) == ESP_OK;
}

/* Read-modify-write, then verify the masked bits took. */
static bool pmic_update(uint8_t reg, uint8_t mask, uint8_t setting)
{
    uint8_t value, actual;
    if (!pmic_read(reg, &value)) return false;
    value = (value & ~mask) | setting;
    return pmic_write(reg, value) && pmic_read(reg, &actual) &&
           (actual & mask) == setting;
}

/* Installed MakerHawk 1000 mAh pack: 4.2 V, 500 mA, terminate at 50 mA.
 * Configure with charging off, then restore the previous enable state. */
static bool charge_configure(void)
{
    const uint8_t voltage = 3;                         /* 4200 mV */
    const uint8_t current = 8 + (500 - 200) / 100;     /* 500 mA */
    const uint8_t termination = TERMINATION_ENABLE | (50 / 25);
    uint8_t control;
    if (!pmic_read(REG_CHARGER_CONTROL, &control)) return false;
    bool good = pmic_update(REG_CHARGER_CONTROL, CHARGE_ENABLE, 0);
    good = good && pmic_update(REG_CHARGE_VOLTAGE, 0x07, voltage);
    good = good && pmic_update(REG_TERMINATION, 0x1f, termination);
    good = good && pmic_update(REG_CHARGE_CURRENT, 0x1f, current);
    if (good) good = pmic_update(REG_CHARGER_CONTROL, CHARGE_ENABLE, control & CHARGE_ENABLE);
    if (!good) (void)pmic_update(REG_CHARGER_CONTROL, CHARGE_ENABLE, 0);
    return good;
}

static bool interrupts_disable(void)
{
    static const uint8_t masks[] = {0xff, 0xff, 0xdf};
    bool good = true;
    for (unsigned i = 0; i < sizeof(masks); i++) {
        /* Mask first, then acknowledge latched events to release the IRQ line */
        bool masked = pmic_update(REG_IRQ_ENABLE + i, masks[i], 0);
        bool cleared = pmic_write(REG_IRQ_STATUS + i, masks[i]);
        good = masked && cleared && good;
    }
    return good;
}

/* Rails per bedroom-clock board_power.c. DCDC1 powers the board. ALDO1 stays
 * on: cutting it makes the shared I²C bus unresponsive. ALDO2 (speaker amp)
 * off. ALDO3 drives panel reset. RTCLDO keeps the RTC running. */
static bool rails_configure(void)
{
    const uint8_t unused_dcdc = DCDC2 | DCDC3 | DCDC4 | DCDC5;
    const uint8_t unused_ldo = ALDO2 | ALDO4 | BLDO1 | BLDO2 | CPUSLDO | DLDO1;
    bool good = pmic_update(REG_COMMON, 0x20, 0x20);    /* output discharge */
    good = pmic_update(REG_LDO_ENABLE, ALDO1 | ALDO3, ALDO1 | ALDO3) && good;
    good = pmic_update(REG_DCDC_ENABLE, unused_dcdc, 0) && good;
    good = pmic_update(REG_LDO_ENABLE, unused_ldo, 0) && good;
    good = pmic_update(REG_LDO_ENABLE2, 0x01, 0) && good; /* DLDO2 */
    /* DCDC1 automatic mode: clear forced CCM and PWM */
    good = pmic_update(REG_DCDC_ENABLE, 0x40, 0) && good;
    good = pmic_update(REG_DCDC_MODE, 0x04, 0) && good;
    good = pmic_update(REG_LED, 0x37, 0x05) && good;    /* charge LED off */
    good = interrupts_disable() && good;
    /* Keep battery sensing; keep TS only if charger protection uses it */
    uint8_t ts;
    if (pmic_read(REG_TS_CONTROL, &ts)) {
        uint8_t off = ADC_VBUS | ADC_VSYS | ADC_DIE_TEMP | ADC_GENERAL;
        if (ts & 0x10) off |= ADC_TS;
        good = pmic_update(REG_ADC_CONTROL, off, 0) && good;
    } else {
        good = false;
    }
    return good;
}

static void log_rails(void)
{
    uint8_t dcdc, ldo, ldo2;
    if (!pmic_read(REG_DCDC_ENABLE, &dcdc) || !pmic_read(REG_LDO_ENABLE, &ldo) ||
        !pmic_read(REG_LDO_ENABLE2, &ldo2)) {
        ESP_LOGE(TAG, "PMIC rail readback failed");
        return;
    }
    ESP_LOGI(TAG, "PMIC rails: DCDC 0x%02x LDO 0x%02x/0x%02x -> DCDC1 %s, ALDO1 %s, "
             "ALDO2 %s, ALDO3 %s, ALDO4 %s, BLDO1/2 %s/%s, DLDO1/2 %s/%s",
             dcdc & 0x1f, ldo, ldo2 & 1,
             dcdc & DCDC1 ? "on" : "off", ldo & ALDO1 ? "on" : "off",
             ldo & ALDO2 ? "on" : "off", ldo & ALDO3 ? "on" : "off",
             ldo & ALDO4 ? "on" : "off", ldo & BLDO1 ? "on" : "off",
             ldo & BLDO2 ? "on" : "off", ldo & DLDO1 ? "on" : "off",
             ldo2 & 1 ? "on" : "off");
}

static void scan(void)
{
    char found[128] = "";
    int n = 0, len = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) {
            len += snprintf(found + len, sizeof(found) - len, " 0x%02x", addr);
            n++;
        }
    }
    ESP_LOGI(TAG, "I2C scan: %d device(s) ACK:%s", n, n ? found : " none");
}

/* Outputs parked and held through deep sleep, with their levels. GPIO0–2
 * have 10 kohm pull-ups (R15/R12/R17), so they are held high; GPIO3/4 have
 * none. Touch IRQ and I²C stay inputs. (bedroom-clock board_power_sleep) */
static const struct {
    uint8_t pin, level;
} parked[] = {
    {PIN_PANEL_CLK, 1}, {PIN_PANEL_D0, 1}, {PIN_PANEL_D1, 1},
    {PIN_PANEL_D2, 0}, {PIN_PANEL_D3, 0}, {PIN_PANEL_CS, 1},
    {PIN_SD_CS, 1}, {PIN_TOUCH_RESET, 1},
    {PIN_AUDIO_MCLK, 0}, {PIN_AUDIO_BCLK, 0}, {PIN_AUDIO_LRCLK, 0}, {PIN_AUDIO_DOUT, 0},
};

static void drive(uint8_t pin, uint8_t level)
{
    gpio_set_level(pin, level);
    /* Input stays enabled so the parked level can be read back and logged */
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&cfg));
}

/* Set every parked output to its safe level, then release the sleep holds
 * so the pads never float. The panel lines are handed to SPI afterwards.
 * Audio I²S lines stay low and SD stays deselected (bedroom-clock
 * board_resume). */
static void pins_restore(void)
{
    for (size_t i = 0; i < sizeof(parked) / sizeof(parked[0]); i++) {
        drive(parked[i].pin, parked[i].pin <= PIN_PANEL_D3 ? 0 : parked[i].level);
        gpio_hold_dis(parked[i].pin);
    }
    gpio_hold_dis(PIN_TOUCH_IRQ);
    gpio_deep_sleep_wakeup_disable(PIN_TOUCH_IRQ);
    rtc_gpio_deinit(PIN_TOUCH_IRQ); /* back from the LP domain to an ordinary GPIO */
}

static void IRAM_ATTR on_button(void *arg)
{
    button_events |= (uint32_t)(uintptr_t)arg;
}

static void buttons_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_BOOT | 1ULL << PIN_KEY,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BOOT, on_button, (void *)(uintptr_t)BUTTON_BOOT));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_KEY, on_button, (void *)(uintptr_t)BUTTON_KEY));
}

unsigned board_buttons(void)
{
    static int64_t last_us;
    uint32_t events = __atomic_exchange_n(&button_events, 0, __ATOMIC_SEQ_CST);
    if (!events) return 0;
    int64_t now = esp_timer_get_time();
    if (last_us && now - last_us <= 200 * 1000) return 0;
    last_us = now;
    /* Both at once is a bounce or a squeeze; KEY wins as bedroom-clock's */
    return events & BUTTON_KEY ? BUTTON_KEY : BUTTON_BOOT;
}

/* ---- Audio: software shutdown with ALDO1 kept on ---------------------- */

/* Power-down sequences from Espressif's ES8311 suspend and ES7210 stop
 * drivers, with stable readback bits, as bedroom-clock audio.c */
static bool dev_write(i2c_master_dev_handle_t d, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(d, buf, 2, 50) == ESP_OK;
}

static bool dev_read(i2c_master_dev_handle_t d, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(d, &reg, 1, value, 1, 50) == ESP_OK;
}

static bool shutdown_chip(uint8_t addr, const uint8_t (*seq)[2], size_t n,
                          const uint8_t (*checks)[3], size_t n_checks)
{
    i2c_master_dev_handle_t d;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
    if (i2c_master_bus_add_device(bus, &cfg, &d) != ESP_OK) return false;
    bool off = false;
    for (int attempt = 0; attempt < 3 && !off; attempt++) {
        bool good = true;
        for (size_t i = 0; i < n; i++) good = dev_write(d, seq[i][0], seq[i][1]) && good;
        for (size_t i = 0; i < n_checks; i++) {
            uint8_t v;
            good = dev_read(d, checks[i][0], &v) && (v & checks[i][1]) == checks[i][2] && good;
        }
        off = good;
    }
    i2c_master_bus_rm_device(d);
    return off;
}

static const uint8_t adc_seq[][2] = {
    {0x47, 0xff}, {0x48, 0xff}, {0x49, 0xff}, {0x4a, 0xff}, {0x4b, 0xff},
    {0x4c, 0xff}, {0x40, 0xc0}, {0x01, 0x7f}, {0x06, 7},
};
static const uint8_t adc_checks[][3] = {
    {0x4b, 0xfc, 0xfc}, {0x4c, 0xfc, 0xfc}, {0x40, 0xc3, 0xc0}, {0x01, 0x7f, 0x7f}, {0x06, 7, 7},
};

static bool adc_shutdown(void)
{
    return shutdown_chip(0x40, adc_seq, sizeof(adc_seq) / sizeof(adc_seq[0]), adc_checks,
                         sizeof(adc_checks) / sizeof(adc_checks[0]));
}

bool board_mic_adc_off(void) { return adc_shutdown(); }

static void audio_shutdown(void)
{
    static const uint8_t codec[][2] = {
        {0x32, 0},    {0x17, 0}, {0x0e, 0xff}, {0x12, 2},    {0x14, 0},
        {0x0d, 0xfa}, {0x15, 0}, {0x02, 0x10}, {0x00, 0},    {0x00, 0x1f},
        {0x01, 0x30}, {0x01, 0}, {0x45, 0},    {0x0d, 0xfc}, {0x02, 0},
    };
    static const uint8_t codec_checks[][3] = {
        {0x0d, 0xfb, 0xf8}, {0x0e, 0x60, 0x60}, {0x12, 2, 2}, {0x01, 0x3f, 0},
    };
    bool codec_off = shutdown_chip(0x18, codec, sizeof(codec) / sizeof(codec[0]), codec_checks,
                                   sizeof(codec_checks) / sizeof(codec_checks[0]));
    bool adc_off = adc_shutdown();
    if (codec_off && adc_off) ESP_LOGI(TAG, "audio off: ES8311 and ES7210 shut down, verified");
    else ESP_LOGE(TAG, "audio shutdown failed: ES8311 %s, ES7210 %s",
                  codec_off ? "off" : "FAILED", adc_off ? "off" : "FAILED");
}

/* ---- QMI8658: one averaged sample, then shutdown ------------------------- */

enum {
    IMU_WHO_AM_I = 0x00, IMU_CTRL1 = 0x02, IMU_CTRL2 = 0x03, IMU_CTRL5 = 0x06,
    IMU_CTRL7 = 0x08, IMU_STATUS0 = 0x2e, IMU_ACCEL_X = 0x35,
    IMU_AUTO_INCREMENT = 0x40, IMU_SENSOR_DISABLE = 0x01, IMU_ACCEL_ENABLE = 0x01,
    IMU_ACCEL_2G_500HZ = 0x04, IMU_ID = 0x05,
};

/* Accelerometer and gyro off (CTRL7 = 0), oscillator stopped
 * (CTRL1 SensorDisable), verified, at most three tries */
static bool imu_standby(void)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        bool disabled = dev_write(imu, IMU_CTRL7, 0);
        bool stopped = dev_write(imu, IMU_CTRL1, IMU_AUTO_INCREMENT | IMU_SENSOR_DISABLE);
        uint8_t c1, c7;
        if (disabled && stopped && dev_read(imu, IMU_CTRL7, &c7) && dev_read(imu, IMU_CTRL1, &c1) &&
            c7 == 0 && c1 == (IMU_AUTO_INCREMENT | IMU_SENSOR_DISABLE))
            return true;
    }
    return false;
}

static void imu_init(void)
{
    for (uint8_t addr = 0x6a; addr <= 0x6b; addr++) {
        if (i2c_master_probe(bus, addr, 20) != ESP_OK) continue;
        i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
        uint8_t id;
        if (i2c_master_bus_add_device(bus, &cfg, &imu) != ESP_OK) continue;
        if (dev_read(imu, IMU_WHO_AM_I, &id) && id == IMU_ID) {
            if (imu_standby()) {
                ESP_LOGI(TAG, "QMI8658 at 0x%02x, shut down (CTRL7=0, CTRL1=0x41)", addr);
                return;
            }
            ESP_LOGE(TAG, "QMI8658 at 0x%02x did not shut down", addr);
            return; /* keep the handle: sampling still tries again */
        }
        i2c_master_bus_rm_device(imu);
        imu = NULL;
    }
    ESP_LOGE(TAG, "QMI8658 not found at 0x6a/0x6b");
}

static bool imu_sample(int16_t xyz[3])
{
    int32_t sums[3] = {0};
    /* Auto-increment, little endian, ±2 g at 500 Hz; gyro stays off */
    bool good = dev_write(imu, IMU_CTRL1, IMU_AUTO_INCREMENT) &&
                dev_write(imu, IMU_CTRL2, IMU_ACCEL_2G_500HZ) && dev_write(imu, IMU_CTRL5, 0) &&
                dev_write(imu, IMU_CTRL7, IMU_ACCEL_ENABLE);
    if (good) esp_rom_delay_us(20000);
    for (int s = 0; good && s < 4; s++) {
        uint8_t status = 0, d[6], reg = IMU_ACCEL_X;
        esp_rom_delay_us(3000); /* shorter than a 10 ms tick */
        good = dev_read(imu, IMU_STATUS0, &status) && (status & 0x01) &&
               i2c_master_transmit_receive(imu, &reg, 1, d, sizeof(d), 50) == ESP_OK;
        if (!good) ESP_LOGW(TAG, "QMI8658 sample %d: STATUS0 0x%02x", s, status);
        for (int i = 0; good && i < 6; i += 2) sums[i / 2] += (int16_t)(d[i] | d[i + 1] << 8);
    }
    bool asleep = imu_standby();
    if (!asleep) ESP_LOGE(TAG, "QMI8658 shutdown after sample failed");
    if (!good) return false;
    for (int i = 0; i < 3; i++) xyz[i] = (int16_t)(sums[i] / 4);
    return true;
}

int board_orientation(void)
{
    int16_t xyz[3];
    if (imu && imu_sample(xyz)) {
        ESP_LOGI(TAG, "accel %d %d %d", xyz[0], xyz[1], xyz[2]);
        orientation_turns = (int8_t)orient_choose(xyz, orientation_turns);
    } else {
        ESP_LOGW(TAG, "orientation sample failed; keeping %d", orient_degrees(orientation_turns));
    }
    return orient_degrees(orientation_turns);
}

/* ---- Startup, wake and deep sleep ---------------------------------------- */

static void bus_init(void)
{
    if (bus) return;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .scl_io_num = PIN_I2C_SCL,
        .sda_io_num = PIN_I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus));
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = PMIC_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dev_cfg, &pmic));
}

void board_init(bool cold_boot)
{
    boot_count++;
    pins_restore();
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    buttons_init();
    bus_init();

    if (cold_boot && !charge_configure()) {
        ESP_LOGE(TAG, "PMIC charge profile failed; charging requested off");
    }
    if (!rails_configure()) {
        ESP_LOGE(TAG, "PMIC rail configuration incomplete");
    }
    log_rails();
    if (cold_boot) {
        scan();
        /* The chips keep this state while ALDO1 stays on */
        audio_shutdown();
    }
    rtc_init();
    if (rtc_clkout_disable()) ESP_LOGI(TAG, "RTC CLKOUT off (COF=111), verified");
    else ESP_LOGE(TAG, "RTC CLKOUT disable failed");
    imu_init();
}

static const char *timer_name(uint8_t kind)
{
    return kind == TIMER_SAFETY ? "dev safety tick" : "?";
}

void board_log_wake(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
        uint32_t causes = esp_sleep_get_wakeup_causes();
        bool gpio = causes & BIT(ESP_SLEEP_WAKEUP_GPIO), ext1 = causes & BIT(ESP_SLEEP_WAKEUP_EXT1);
        bool timer = causes & BIT(ESP_SLEEP_WAKEUP_TIMER);
        if (gpio || ext1) {
            ESP_LOGI(TAG, "WAKE BY TOUCH: GPIO%d via%s%s%s (causes 0x%02lx, gpio pins 0x%llx, "
                     "ext1 pins 0x%llx), %lu ms after sleep entry", PIN_TOUCH_IRQ, gpio ? " LP-GPIO edge" : "",
                     ext1 ? " EXT1" : "", timer ? " (timer also due)" : "", (unsigned long)causes,
                     (unsigned long long)esp_sleep_get_gpio_wakeup_status(),
                     (unsigned long long)esp_sleep_get_ext1_wakeup_status(),
                     (unsigned long)trace_now[3]);
        } else if (timer) {
            ESP_LOGI(TAG, "WAKE BY TIMER (%s), no touch (causes 0x%02lx), %lu ms after sleep "
                     "entry", timer_name(timer_kind), (unsigned long)causes,
                     (unsigned long)trace_now[3]);
        } else {
            ESP_LOGW(TAG, "deep-sleep wake with unexpected causes 0x%02lx", (unsigned long)causes);
        }
    } else {
        ESP_LOGI(TAG, "cold start: reset reason %d", (int)reason);
    }
    ESP_LOGI(TAG, "retained across deep sleep: boot #%lu, %lu deep sleep(s) so far, "
             "%lu quiet safety tick(s) (last saw VBUS %d, host %d), orientation %d",
             (unsigned long)boot_count, (unsigned long)deep_sleep_count, (unsigned long)safety_ticks,
             tick_seen & 1, (tick_seen >> 1) & 1,
             orient_degrees(orientation_turns));
}

/* Wake sources for every deep sleep: a falling edge on the touch IRQ, plus
 * a timer when the dev safety tick is on. */
static void arm_wake(void)
{
    /* The CST9220 signals a tap with a short low pulse. A level wake misses
     * it on this board (bedroom-clock power audit; confirmed here by a tap
     * test), so wake on a latched falling edge, as bedroom-clock's
     * hal/sleep.c arm_wake_pin does. ESP-IDF only supports level wake, so
     * the edge type is written to the LP_IO pin register directly, after
     * IDF has registered the wake source.
     *
     * edge_wakeup_clr (bit 3) is essential: an edge-type wake has its own
     * latch, IDF never clears it, and without this bit the chip wakes the
     * instant it sleeps. The LP peripheral domain must stay powered for the
     * detector to work. A tap that lands after this point stays latched and
     * wakes the chip as soon as it sleeps, so no tap is lost. */
    int lp = rtc_io_number_get(PIN_TOUCH_IRQ);
    ESP_ERROR_CHECK(esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON));
    ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(BIT64(PIN_TOUCH_IRQ), ESP_GPIO_WAKEUP_GPIO_LOW));
    LP_IO.pin[lp].wakeup_enable = 0;
    ESP_ERROR_CHECK(rtc_gpio_init(PIN_TOUCH_IRQ));
    ESP_ERROR_CHECK(rtc_gpio_set_direction(PIN_TOUCH_IRQ, RTC_GPIO_MODE_INPUT_ONLY));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(PIN_TOUCH_IRQ));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(PIN_TOUCH_IRQ));
    esp_rom_delay_us(5000); /* let the pad settle in the LP domain */
    lp_io_pin_reg_t pin = {.int_type = GPIO_INTR_NEGEDGE, .wakeup_enable = 1, .edge_wakeup_clr = 1};
    LP_IO.pin[lp].val = pin.val;
    rtcio_ll_clear_interrupt_status();
    timer_kind = BOARD_DEV_SAFETY_WAKE ? TIMER_SAFETY : TIMER_NONE;
    if (BOARD_DEV_SAFETY_WAKE)
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)BOARD_DEV_SAFETY_WAKE_S * 1000000));
    deep_sleep_count++;
}

/* A safety tick with no USB host: straight back to sleep. The pins are
 * still held from the previous sleep and the panel was never touched. */
static void __attribute__((noreturn)) resleep_quiet(void)
{
    safety_ticks++;
    board_stage(STAGE_SLEEP_START);
    arm_wake();
    sleep_entered_us = esp_rtc_get_time_us();
    esp_deep_sleep_start();
}

/* AXP2101 status 1 bit 5: VBUS good */
static bool vbus_present(void)
{
    uint8_t status;
    return pmic_read(REG_STATUS1, &status) && (status & 0x20);
}

/* The same check the main loop uses. ESP-IDF updates it from the tick
 * hook as USB start-of-frames arrive, so poll it for a while: a host needs
 * time to notice the device, longer through a hub. */
static bool usb_host_seen(unsigned ms)
{
    for (unsigned t = 0; t < ms; t += 50) {
        if (usb_serial_jtag_is_connected()) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return usb_serial_jtag_is_connected();
}

board_boot_t board_early_wake(void)
{
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) return BOARD_BOOT_NORMAL;
    uint32_t causes = esp_sleep_get_wakeup_causes();
    if (causes & (BIT(ESP_SLEEP_WAKEUP_GPIO) | BIT(ESP_SLEEP_WAKEUP_EXT1))) return BOARD_BOOT_NORMAL;
    if (!(causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) || timer_kind != TIMER_SAFETY) return BOARD_BOOT_NORMAL;
    bus_init();
    /* A host needs time to notice the device and start sending frames,
     * longer through a hub or a monitor: measured too short at 1 s */
    bool vbus = vbus_present();
    bool host = vbus && usb_host_seen(4000);
    tick_seen = (vbus ? 1 : 0) | (host ? 2 : 0);
    if (!host) resleep_quiet();
    return BOARD_BOOT_QUIET_HOST;
}

void board_deep_sleep(void)
{
    board_stage(STAGE_SLEEP_ENTRY);
    display_sleep_reliably();
    /* The IMU is already shut down after its last sample; the audio chips
     * and RTC CLKOUT stay as board_init() left them */
    gpio_intr_disable(PIN_TOUCH_IRQ);
    int hold_errors = 0;
    for (size_t i = 0; i < sizeof(parked) / sizeof(parked[0]); i++) {
        drive(parked[i].pin, parked[i].level);
        if (gpio_hold_en(parked[i].pin) != ESP_OK) hold_errors++;
    }
    arm_wake();
    ESP_LOGI(TAG, "entering deep sleep #%lu (wake: touch GPIO%d falling edge, timer %lu s "
             "for %s)", (unsigned long)deep_sleep_count, PIN_TOUCH_IRQ,
             (unsigned long)(BOARD_DEV_SAFETY_WAKE ? BOARD_DEV_SAFETY_WAKE_S : 0),
             timer_kind == TIMER_NONE ? "nothing" : timer_name(timer_kind));
    ESP_LOGI(TAG, "sleep pins: touch IRQ GPIO5 level %d, touch reset GPIO11 level %d, panel CS "
             "GPIO15 level %d; holds 0x%08lx (%d hold error(s)); LP_IO pin5 0x%08lx",
             gpio_get_level(PIN_TOUCH_IRQ), gpio_get_level(PIN_TOUCH_RESET),
             gpio_get_level(PIN_PANEL_CS), (unsigned long)REG_READ(LP_AON_GPIO_HOLD0_REG),
             hold_errors, (unsigned long)LP_IO.pin[rtc_io_number_get(PIN_TOUCH_IRQ)].val);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100)); /* let the USB log drain */
    board_stage(STAGE_SLEEP_START);
    sleep_entered_us = esp_rtc_get_time_us();
    esp_deep_sleep_start();
}

void board_panel_reset(void)
{
    static const uint8_t steps[] = {ALDO3, 0, ALDO3};
    for (size_t i = 0; i < sizeof(steps); i++) {
        if (!pmic_update(REG_LDO_ENABLE, ALDO3, steps[i])) {
            ESP_LOGE(TAG, "panel reset: ALDO3 %s failed", steps[i] ? "on" : "off");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

int board_battery_percent(void)
{
    uint8_t status, control, value;
    if (!pmic_read(REG_STATUS1, &status) || !(status & 0x08)) return -1;
    if (!pmic_read(REG_CHARGER_CONTROL, &control) || !(control & GAUGE_ENABLE)) return -1;
    if (!pmic_read(REG_BATTERY_PERCENT, &value) || value > 100) return -1;
    return value;
}
