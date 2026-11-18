#include "store.h"

#include <stdlib.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "store";
static const char *NAMESPACE = "store";
static const char *SHELF_KEY = "shelf";
static const char *SESSION_KEY = "session";
static const char *OUTBOX_KEY = "outbox";
static const char *RECENT_KEY = "recent";

/* RTC mirrors: kept through deep sleep, zeroed (not valid) by any reset */
RTC_DATA_ATTR static bool rtc_session_ok, rtc_outbox_ok;
RTC_DATA_ATTR static session_t rtc_session;
RTC_DATA_ATTR static outbox_item_t rtc_outbox[OUTBOX_MAX];

bool store_load_shelf(shelf_t *s)
{
    nvs_handle_t h;
    bool ok = false;
    size_t len = 0;
    uint8_t *buf = NULL;
    shelf_unpack(s, NULL, 0); /* empty until a blob is read */
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    if (nvs_get_blob(h, SHELF_KEY, NULL, &len) == ESP_OK && len <= SHELF_BLOB_MAX &&
        (buf = malloc(len ? len : 1)) != NULL && nvs_get_blob(h, SHELF_KEY, buf, &len) == ESP_OK) {
        ok = shelf_unpack(s, buf, len);
        if (!ok) ESP_LOGW(TAG, "shelf cache of another version or size (%u bytes) ignored",
                          (unsigned)len);
    }
    free(buf);
    nvs_close(h);
    return ok;
}

void store_save_shelf(const shelf_t *s)
{
    uint8_t *buf = malloc(SHELF_BLOB_MAX);
    if (!buf) {
        ESP_LOGE(TAG, "no memory to save the shelf");
        return;
    }
    size_t len = shelf_pack(s, buf, SHELF_BLOB_MAX);
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, SHELF_KEY, buf, len);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    free(buf);
    if (err != ESP_OK) ESP_LOGE(TAG, "saving the shelf failed: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "shelf saved: %u books, %u bytes", s->count, (unsigned)len);
}

static bool write_blob(const char *key, const uint8_t *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, key, buf, len);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

/* *got = bytes read into buf (exactly len of them), else 0. Returns ESP_OK
 * when read; ESP_ERR_NVS_NOT_FOUND when there is no namespace or blob;
 * ESP_ERR_INVALID_SIZE when the blob is of another size (logged); any other
 * error when the read itself failed, so what NVS holds is unknown. */
static esp_err_t read_blob(const char *key, uint8_t *buf, size_t len, size_t *got)
{
    nvs_handle_t h;
    size_t n = 0;
    *got = 0;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_blob(h, key, NULL, &n);
    if (err == ESP_OK && n != len) {
        ESP_LOGW(TAG, "%s of another size (%u bytes) ignored", key, (unsigned)n);
        err = ESP_ERR_INVALID_SIZE;
    } else if (err == ESP_OK) {
        err = nvs_get_blob(h, key, buf, &n);
        if (err == ESP_OK) *got = n;
    }
    nvs_close(h);
    return err;
}

/* The outcome is known: the blob was read, there is none, or it is of
 * another size (ignored; the next save replaces it). Anything else leaves
 * the RTC mirror invalid, so the next load reads NVS again and a save made
 * meanwhile cannot be mistaken for "nothing was there". */
static bool definite(esp_err_t err)
{
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_SIZE;
}

void store_save_session(const session_t *s)
{
    rtc_session = *s;
    rtc_session_ok = true;
    uint8_t buf[SESSION_BLOB_SIZE];
    size_t len = session_pack(s, buf, sizeof(buf));
    if (write_blob(SESSION_KEY, buf, len))
        ESP_LOGI(TAG, "session saved: phase %d, started_at %lld", (int)s->phase,
                 (long long)s->started_at);
}

bool store_load_session(session_t *s)
{
    if (rtc_session_ok) {
        *s = rtc_session;
        ESP_LOGI(TAG, "session from RTC memory: phase %d", (int)s->phase);
        return true;
    }
    uint8_t buf[SESSION_BLOB_SIZE];
    size_t len;
    esp_err_t err = read_blob(SESSION_KEY, buf, sizeof(buf), &len);
    bool ok = session_unpack(s, buf, len);
    if (len && !ok) ESP_LOGW(TAG, "session of another version ignored");
    if (ok) ESP_LOGI(TAG, "session from NVS: phase %d", (int)s->phase);
    if (!definite(err)) {
        ESP_LOGE(TAG, "reading the session failed: %s; RTC mirror left invalid, read again "
                 "next load", esp_err_to_name(err));
        return false;
    }
    rtc_session = *s;
    rtc_session_ok = true;
    return ok;
}

bool store_outbox_save(const outbox_item_t items[OUTBOX_MAX])
{
    memcpy(rtc_outbox, items, sizeof(rtc_outbox));
    rtc_outbox_ok = true;
    uint8_t *buf = malloc(OUTBOX_BLOB_SIZE);
    if (!buf) {
        ESP_LOGE(TAG, "no memory to save the outbox");
        return false;
    }
    size_t len = outbox_pack(items, buf, OUTBOX_BLOB_SIZE);
    bool ok = write_blob(OUTBOX_KEY, buf, len);
    if (ok) ESP_LOGI(TAG, "outbox saved");
    free(buf);
    return ok;
}

bool store_outbox_load(outbox_item_t items[OUTBOX_MAX])
{
    bool ok;
    if (rtc_outbox_ok) {
        memcpy(items, rtc_outbox, sizeof(rtc_outbox));
        ok = true;
        ESP_LOGI(TAG, "outbox from RTC memory");
    } else {
        uint8_t *buf = malloc(OUTBOX_BLOB_SIZE);
        size_t len = 0;
        esp_err_t err = buf ? read_blob(OUTBOX_KEY, buf, OUTBOX_BLOB_SIZE, &len) : ESP_ERR_NO_MEM;
        ok = outbox_unpack(items, buf, len);
        if (len && !ok) ESP_LOGW(TAG, "outbox of another version ignored");
        if (ok) ESP_LOGI(TAG, "outbox from NVS");
        free(buf);
        if (!definite(err)) {
            ESP_LOGE(TAG, "reading the outbox failed: %s; RTC mirror left invalid, read again "
                     "next load", esp_err_to_name(err));
            return false;
        }
        memcpy(rtc_outbox, items, sizeof(rtc_outbox));
        rtc_outbox_ok = true;
    }
    int n = outbox_recover(items);
    if (n) {
        /* Cut off mid-request: the outcome is unknown, never resend */
        ESP_LOGW(TAG, "%d outbox item(s) were sending at boot: now O_CHECK", n);
        store_outbox_save(items);
    }
    return ok;
}

bool store_load_recent(recent_t r[RECENT_MAX])
{
    uint8_t buf[RECENT_BLOB_SIZE];
    size_t len;
    read_blob(RECENT_KEY, buf, sizeof(buf), &len);
    bool ok = recent_unpack(r, buf, len);
    if (len && !ok) ESP_LOGW(TAG, "recent books of another version ignored");
    return ok;
}

void store_save_recent(const recent_t r[RECENT_MAX])
{
    uint8_t buf[RECENT_BLOB_SIZE];
    size_t len = recent_pack(r, buf, sizeof(buf));
    if (write_blob(RECENT_KEY, buf, len)) ESP_LOGI(TAG, "recent books saved");
}
