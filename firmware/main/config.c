#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/cfgline.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "ui.h"

static const char *TAG = "config";
static const char *NAMESPACE = "cfg";
static const char *NO_TZ = "UTC0";

static char values[CFG_KEY_COUNT][CFG_VALUE_MAX + 1];
static bool present[CFG_KEY_COUNT];
static bool changed;
static config_command_fn other;
static char line[CFG_LINE_MAX];
static linebuf_t lb;
/* Guards values[]/present[] writes against config_copy() from other tasks */
static SemaphoreHandle_t lock;

static void apply_tz(void)
{
    const char *tz = present[CFG_TZ_POSIX] ? values[CFG_TZ_POSIX] : NO_TZ;
    setenv("TZ", tz, 1);
    tzset();
}

static void log_presence(void)
{
    char missing[80] = "";
    for (int k = 0; k < CFG_KEY_COUNT; k++) {
        if (present[k]) continue;
        strlcat(missing, " ", sizeof(missing));
        strlcat(missing, cfg_key_name(k), sizeof(missing));
    }
    if (config_complete()) ESP_LOGI(TAG, "settings complete");
    else ESP_LOGW(TAG, "settings incomplete, missing:%s", missing);
}

void config_init(config_command_fn other_commands)
{
    other = other_commands;
    lock = xSemaphoreCreateMutex();
    assert(lock);
    linebuf_init(&lb, line, sizeof(line));

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS layout unusable (%s); erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    } else {
        nvs_handle_t h;
        if (nvs_open(NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            for (int k = 0; k < CFG_KEY_COUNT; k++) {
                size_t len = sizeof(values[k]);
                present[k] = nvs_get_str(h, cfg_key_name(k), values[k], &len) == ESP_OK &&
                             len > 1;
                if (!present[k]) memset(values[k], 0, sizeof(values[k]));
            }
            nvs_close(h);
        }
    }
    apply_tz();
    log_presence();
    ESP_LOGI(TAG, "time zone %s", present[CFG_TZ_POSIX] ? "from tz_posix" : "UTC (unset)");
}

const char *config_get(const char *key)
{
    int k = cfg_key_find(key);
    return k >= 0 && present[k] ? values[k] : NULL;
}

bool config_copy(const char *key, char *out, size_t cap)
{
    int k = cfg_key_find(key);
    bool ok = false;
    if (cap) out[0] = '\0';
    if (k < 0 || !cap) return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (present[k] && strlen(values[k]) < cap) {
        strcpy(out, values[k]);
        ok = true;
    }
    xSemaphoreGive(lock);
    return ok;
}

bool config_complete(void)
{
    for (int k = 0; k < CFG_KEY_COUNT; k++)
        if (!present[k]) return false;
    return true;
}

bool config_changed(void)
{
    bool c = changed;
    changed = false;
    return c;
}

static bool store(int key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, cfg_key_name(key), value);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storing %s failed: %s", cfg_key_name(key), esp_err_to_name(err));
        return false;
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    strlcpy(values[key], value, sizeof(values[key]));
    present[key] = true;
    xSemaphoreGive(lock);
    return true;
}

static void handle_cfg(cfg_result_t r, int key, const char *value)
{
    if (r == CFG_SET && !cfg_accepting(ui_held(), usb_serial_jtag_is_connected(), ui_idle())) {
        r = CFG_REFUSED;
        ESP_LOGW(TAG, "CFG %s refused: turn KEY stay-awake on, or let the clock go dark on USB",
                 cfg_key_name(key));
    }
    if (r == CFG_SET && !store(key, value)) r = CFG_BAD_VALUE;
    if (r == CFG_SET) {
        ESP_LOGI(TAG, "stored %s (%u bytes)", cfg_key_name(key), (unsigned)strlen(value));
        if (key == CFG_TZ_POSIX) apply_tz();
        changed = true;
        log_presence();
    } else if (r == CFG_BAD_KEY) {
        ESP_LOGW(TAG, "CFG with an unknown key");
    } else if (r == CFG_BAD_VALUE) {
        ESP_LOGW(TAG, "CFG %s: bad value (empty, too long, bad characters or not stored)",
                 cfg_key_name(key));
    }
    char reply[32];
    if (cfg_reply(reply, sizeof(reply), r, key) > 0) {
        /* The same stream as the log, so the reply is never split by it */
        printf("%s\n", reply);
        fflush(stdout);
    }
}

static void handle_line(void)
{
    int key;
    const char *value;
    cfg_result_t r = cfg_parse(lb.buf, lb.overflow, &key, &value);
    if (r != CFG_NONE) {
        handle_cfg(r, key, value);
    } else if (lb.overflow) {
        ESP_LOGW(TAG, "USB line longer than %u bytes ignored", (unsigned)(sizeof(line) - 1));
    } else if (other) {
        other(lb.buf);
    }
    /* A CFG line may have held a secret; none stays in RAM beyond values[] */
    linebuf_wipe(&lb);
}

void config_usb_poll(void)
{
    uint8_t buf[64];
    int n;
    while ((n = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < n; i++)
            if (linebuf_feed(&lb, (char)buf[i])) handle_line();
    }
    memset(buf, 0, sizeof(buf));
}
