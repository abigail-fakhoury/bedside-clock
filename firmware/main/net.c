#include "net.h"

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "rtc.h"

static const char *TAG = "net";

enum { GOT_IP = 1 << 0 };

static EventGroupHandle_t events;
static esp_netif_t *sta;
static esp_event_handler_instance_t on_wifi_h, on_ip_h;
static volatile bool stopping;
static volatile int last_reason;
static bool wifi_inited, sntp_on, synced;

static void log_heap(const char *when)
{
    ESP_LOGI(TAG, "heap %s: free %u, lowest ever %u", when, (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, GOT_IP);
        last_reason = ((wifi_event_sta_disconnected_t *)data)->reason;
        if (stopping) return;
        ESP_LOGW(TAG, "Wi-Fi disconnected, reason %d; retrying", last_reason);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(events, GOT_IP);
    }
}

/* Writes network time to the RTC on a whole-second boundary, so the RTC
 * (whose prescaler restarts on the write) is within a few ms */
static bool write_rtc(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t target = tv.tv_sec + 1;
    int wait_ms = (int)((1000000 - tv.tv_usec) / 1000) - 20;
    if (wait_ms > 0) vTaskDelay(pdMS_TO_TICKS(wait_ms));
    do {
        gettimeofday(&tv, NULL);
    } while (tv.tv_sec < target);
    if (!rtc_set_utc(tv.tv_sec)) {
        ESP_LOGE(TAG, "SNTP time received but the RTC write failed");
        return false;
    }
    struct tm tm;
    char s[24];
    gmtime_r(&tv.tv_sec, &tm);
    strftime(s, sizeof(s), "%Y-%m-%dT%H:%M:%SZ", &tm);
    ESP_LOGI(TAG, "SNTP time written to RTC: %s (+%ld us)", s, (long)tv.tv_usec);
    return true;
}

static void sync_time(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    int64_t t0 = esp_timer_get_time();
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "SNTP init failed");
        return;
    }
    sntp_on = true;
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(NET_SNTP_TIMEOUT_MS)) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP: no answer from pool.ntp.org in %d ms", NET_SNTP_TIMEOUT_MS);
        return;
    }
    ESP_LOGI(TAG, "SNTP synced in %lld ms", (esp_timer_get_time() - t0) / 1000);
    synced = write_rtc();
}

bool net_up(uint32_t timeout_ms)
{
    static char ssid[33], pass[65];
    if (wifi_inited) return true; /* already up */
    synced = false;
    if (!config_copy("wifi_ssid", ssid, sizeof(ssid)) ||
        !config_copy("wifi_pass", pass, sizeof(pass))) {
        ESP_LOGW(TAG, "Wi-Fi not configured (wifi_ssid / wifi_pass missing)");
        return false;
    }
    log_heap("before Wi-Fi");
    int64_t t0 = esp_timer_get_time();

    static bool netif_ready;
    if (!netif_ready) {
        ESP_ERROR_CHECK(esp_netif_init()); /* cannot be undone; small */
        netif_ready = true;
    }
    if (!events) events = xEventGroupCreate();
    xEventGroupClearBits(events, GOT_IP);
    stopping = false;
    last_reason = 0;
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    sta = esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0; /* the credentials live in our own settings only */
    if ((err = esp_wifi_init(&init)) != ESP_OK) goto fail;
    wifi_inited = true;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, &on_wifi_h);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, &on_ip_h);

    wifi_config_t cfg = {0};
    memcpy(cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(cfg.sta.password, pass, strlen(pass));
    memset(pass, 0, sizeof(pass));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK ||
        (err = esp_wifi_set_config(WIFI_IF_STA, &cfg)) != ESP_OK ||
        (err = esp_wifi_start()) != ESP_OK) {
        memset(&cfg, 0, sizeof(cfg));
        goto fail;
    }
    memset(&cfg, 0, sizeof(cfg));

    if (!(xEventGroupWaitBits(events, GOT_IP, false, true, pdMS_TO_TICKS(timeout_ms)) & GOT_IP)) {
        ESP_LOGW(TAG, "Wi-Fi did not connect in %lu ms (last disconnect reason %d; is the "
                 "network 2.4 GHz?)", (unsigned long)timeout_ms, last_reason);
        net_down();
        return false;
    }
    wifi_ap_record_t ap;
    int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    ESP_LOGI(TAG, "Wi-Fi up in %lld ms (RSSI %d dBm, channel %d)",
             (esp_timer_get_time() - t0) / 1000, rssi, rssi ? ap.primary : 0);
    log_heap("Wi-Fi up");
    sync_time();
    return true;

fail:
    ESP_LOGE(TAG, "Wi-Fi start failed: %s", esp_err_to_name(err));
    net_down();
    return false;
}

void net_down(void)
{
    if (sntp_on) {
        esp_netif_sntp_deinit();
        sntp_on = false;
    }
    stopping = true;
    if (wifi_inited) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        if (on_wifi_h) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_h);
        if (on_ip_h) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_h);
        on_wifi_h = on_ip_h = NULL;
        esp_wifi_deinit();
        wifi_inited = false;
    }
    if (sta) {
        esp_netif_destroy_default_wifi(sta);
        sta = NULL;
        esp_event_loop_delete_default();
        wifi_mode_t mode;
        bool off = esp_wifi_get_mode(&mode) == ESP_ERR_WIFI_NOT_INIT;
        ESP_LOGI(TAG, "Wi-Fi down%s", off ? " (driver deinitialised)" : " (driver still initialised!)");
        log_heap("after Wi-Fi down");
    }
}

bool net_time_synced(void) { return synced; }
