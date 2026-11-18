#include "voice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "core/body.h"
#include "core/cfgline.h"
#include "core/jscan.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"

static const char *TAG = "voice";

#define VOICE_URL "https://openrouter.ai/api/v1/chat/completions"

enum {
    HEAD_MAX = 6144,     /* a full shelf of long titles needs about 4.6 KB */
    RESP_MAX = 4096,     /* the reply is read into this much and no more */
    CONTENT_MAX = 512,   /* the model's message text */
    ENC_IN = 1536,       /* WAV bytes per base64 piece: a multiple of 3 */
    HTTP_TIMEOUT_MS = 30000,
};
_Static_assert(4 * (ENC_IN / 3) + 1 <= RESP_MAX, "base64 piece fits the buffer");

typedef struct {
    char head[HEAD_MAX];
    char buf[RESP_MAX];  /* base64 pieces on the way out, the reply on the way in */
    char content[CONTENT_MAX];
    char auth[sizeof("Bearer ") + CFG_VALUE_MAX];
} ask_t;

static void wipe(void *p, size_t n)
{
    volatile char *v = p;
    while (n--) *v++ = 0;
}

static bool write_all(esp_http_client_handle_t c, const char *p, size_t len)
{
    while (len > 0) {
        int w = esp_http_client_write(c, p, (int)len);
        if (w <= 0) return false;
        p += w;
        len -= (size_t)w;
    }
    return true;
}

/* A jscan_cb that keeps error.message (ctx: char[JSCAN_VALUE_MAX]) */
static void error_message(void *ctx, const char *path, const char *value, bool is_string)
{
    if (value && is_string && strcmp(path, "error.message") == 0)
        strlcpy(ctx, value, JSCAN_VALUE_MAX);
}

bool voice_ask(const clip_t *clip, bool want_title, const shelf_t *shelf, heard_t *out)
{
    *out = (heard_t){.title = "", .page = -1, .ok = false};
    if (!clip || !clip->wav || clip->len == 0) return false;
    ask_t *a = calloc(1, sizeof(*a));
    if (!a) {
        ESP_LOGE(TAG, "no memory for the request (%u bytes)", (unsigned)sizeof(*a));
        return false;
    }
    bool ok = false;
    esp_http_client_handle_t c = NULL;
    int head_len = body_voice_head(a->head, sizeof(a->head), VOICE_MODEL, want_title, shelf);
    if (head_len < 0) {
        ESP_LOGE(TAG, "request head does not fit %d bytes", HEAD_MAX);
        goto done;
    }
    {
        char key[CFG_VALUE_MAX + 1];
        if (!config_copy("or_key", key, sizeof(key))) {
            ESP_LOGW(TAG, "or_key not set");
            goto done;
        }
        snprintf(a->auth, sizeof(a->auth), "Bearer %s", key);
        wipe(key, sizeof(key));
    }

    size_t b64_len = 4 * ((clip->len + 2) / 3);
    size_t tail_len = strlen(BODY_VOICE_TAIL);
    size_t total = (size_t)head_len + b64_len + tail_len;
    esp_http_client_config_t cfg = {
        .url = VOICE_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* A redirect could carry the key to another host: treat it as an error */
        .disable_auto_redirect = true,
        .buffer_size_tx = 1024,
    };
    c = esp_http_client_init(&cfg);
    if (!c) {
        ESP_LOGE(TAG, "HTTP client init failed");
        goto done;
    }
    esp_http_client_set_header(c, "Authorization", a->auth);
    esp_http_client_set_header(c, "Content-Type", "application/json");
    wipe(a->auth, sizeof(a->auth)); /* the client keeps its own copy until cleanup */

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = esp_http_client_open(c, (int)total);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect failed: %s", esp_err_to_name(err));
        goto done;
    }
    int64_t t1 = esp_timer_get_time();
    bool sent = write_all(c, a->head, (size_t)head_len);
    for (size_t off = 0; sent && off < clip->len; off += ENC_IN) {
        size_t n = clip->len - off < ENC_IN ? clip->len - off : ENC_IN;
        size_t olen = 0;
        sent = mbedtls_base64_encode((unsigned char *)a->buf, sizeof(a->buf), &olen,
                                     clip->wav + off, n) == 0 &&
               write_all(c, a->buf, olen);
    }
    sent = sent && write_all(c, BODY_VOICE_TAIL, tail_len);
    if (!sent) {
        ESP_LOGE(TAG, "upload failed");
        goto done;
    }
    int64_t t2 = esp_timer_get_time();

    esp_http_client_fetch_headers(c);
    int status = esp_http_client_get_status_code(c);
    int got = 0;
    while (got < RESP_MAX - 1) {
        int r = esp_http_client_read(c, a->buf + got, RESP_MAX - 1 - got);
        if (r <= 0) break;
        got += r;
    }
    a->buf[got] = '\0';
    int64_t t3 = esp_timer_get_time();
    ESP_LOGI(TAG, "HTTP %d: connect %lld ms, upload %lld ms (%u bytes), answer %lld ms, "
             "%d reply bytes%s", status, (t1 - t0) / 1000, (t2 - t1) / 1000, (unsigned)total,
             (t3 - t2) / 1000, got, got == RESP_MAX - 1 ? " (cut: reply longer than the buffer)" : "");

    if (status != 200) {
        /* OpenRouter explains itself in {"error":{"message":...}} (no secrets) */
        char message[JSCAN_VALUE_MAX] = "";
        jscan_t scan;
        jscan_init(&scan, error_message, message);
        jscan_feed(&scan, a->buf, (size_t)got);
        ESP_LOGE(TAG, "HTTP %d%s %s", status,
                 status == 401 ? " (or_key refused)" : status == 402 ? " (key spending limit)" : "",
                 message);
        goto done;
    }
    if (!reply_content(a->buf, (size_t)got, a->content, sizeof(a->content))) {
        ESP_LOGW(TAG, "no message content in the reply");
        goto done;
    }
    ESP_LOGI(TAG, "reply: %s", a->content);
    *out = reply_parse(a->content);
    if (!out->ok) ESP_LOGW(TAG, "reply holds no readable JSON object");
    ok = out->ok;
done:
    if (c) {
        esp_http_client_close(c);
        esp_http_client_cleanup(c);
    }
    wipe(a->auth, sizeof(a->auth));
    free(a);
    return ok;
}
