#include "readwise.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "core/cfgline.h"
#include "core/body.h"
#include "core/jscan.h"
#include "core/postreply.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "rtc.h"

static const char *TAG = "readwise";

/* Query parameters checked against https://readwise.io/books_api: status
 * takes currently_reading; limit is the page size, 1-1000 (default 40). */
#define SHELF_URL SHELF_API_BASE "books?status=currently_reading&limit=100"

enum {
    READ_CHUNK = 1024,
    MAX_PAGES = 3,         /* limit=100 > SHELF_MAX: one page is the normal case */
    HTTP_TIMEOUT_MS = 15000,
};

typedef struct {
    jscan_t scan;
    shelf_builder_t builder;
    char chunk[READ_CHUNK];
    char auth[sizeof("Token ") + CFG_VALUE_MAX];
    char url[JSCAN_VALUE_MAX];
} fetch_t;

static void wipe(void *p, size_t n)
{
    volatile char *v = p;
    while (n--) *v++ = 0;
}

/* A jscan_cb that keeps error.message (ctx: char[JSCAN_VALUE_MAX]) */
static void error_message(void *ctx, const char *path, const char *value, bool is_string)
{
    if (value && is_string && strcmp(path, "error.message") == 0)
        strlcpy(ctx, value, JSCAN_VALUE_MAX);
}

/* One page into the builder; true when it arrived whole and parsed */
static bool fetch_page(fetch_t *f, int page)
{
    esp_http_client_config_t cfg = {
        .url = f->url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* A redirect could carry the token to another host: treat it as an error */
        .disable_auto_redirect = true,
        .buffer_size = READ_CHUNK,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "page %d: HTTP client init failed", page);
        return false;
    }
    esp_http_client_set_header(client, "Authorization", f->auth);
    esp_http_client_set_header(client, "Accept", "application/json");

    bool ok = false;
    long total = 0;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "page %d: connect failed: %s", page, esp_err_to_name(err));
        goto done;
    }
    int64_t length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        /* The API explains itself in {"error":{"message":...}} (no secrets) */
        char message[JSCAN_VALUE_MAX] = "";
        jscan_init(&f->scan, error_message, message);
        int n = esp_http_client_read(client, f->chunk, 512);
        if (n > 0) jscan_feed(&f->scan, f->chunk, (size_t)n);
        ESP_LOGE(TAG, "page %d: HTTP %d%s %s", page, status,
                 status == 401 ? " (rw_token refused)"
                 : status == 429 ? " (rate limited: 20 requests/min)" : "", message);
        goto done;
    }
    jscan_init(&f->scan, shelf_on_value, &f->builder);
    for (;;) {
        int n = esp_http_client_read(client, f->chunk, sizeof(f->chunk));
        if (n < 0) {
            ESP_LOGE(TAG, "page %d: read failed after %ld bytes", page, total);
            goto done;
        }
        if (n == 0) break;
        total += n;
        if (!jscan_feed(&f->scan, f->chunk, (size_t)n)) {
            ESP_LOGE(TAG, "page %d: malformed JSON near byte %ld", page, total);
            goto done;
        }
    }
    if (!jscan_finish(&f->scan)) {
        ESP_LOGE(TAG, "page %d: response ended early (%ld bytes, Content-Length %lld)", page,
                 total, length);
        goto done;
    }
    ok = true;
    ESP_LOGI(TAG, "page %d: %ld bytes, %u books so far; heap free %u, lowest ever %u", page,
             total, f->builder.seen, (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

bool readwise_fetch_shelf(shelf_t *out)
{
    fetch_t *f = calloc(1, sizeof(*f));
    if (!f) {
        ESP_LOGE(TAG, "no memory to fetch the shelf");
        return false;
    }
    shelf_builder_init(&f->builder, out);
    bool ok = false;
    char token[CFG_VALUE_MAX + 1];
    if (!config_copy("rw_token", token, sizeof(token))) {
        ESP_LOGW(TAG, "rw_token not set");
        goto done;
    }
    snprintf(f->auth, sizeof(f->auth), "Token %s", token);
    wipe(token, sizeof(token));
    strlcpy(f->url, SHELF_URL, sizeof(f->url));

    for (int page = 1;; page++) {
        if (!fetch_page(f, page)) goto done;
        if (out->count >= SHELF_MAX || !f->builder.next[0]) break;
        if (page == MAX_PAGES) {
            ESP_LOGW(TAG, "stopping after %d pages with %u books", page, out->count);
            break;
        }
        if (!shelf_next_url_ok(f->builder.next)) {
            ESP_LOGW(TAG, "next page URL unusable (%u bytes); keeping %u books",
                     (unsigned)strlen(f->builder.next), out->count);
            break;
        }
        strlcpy(f->url, f->builder.next, sizeof(f->url));
        shelf_builder_next_page(&f->builder);
    }
    time_t now;
    out->fetched_at = rtc_get_utc(&now) ? now : 0;
    ok = true;
    if (f->builder.seen > out->count)
        ESP_LOGW(TAG, "%u books currently reading; kept the first %u", f->builder.seen,
                 out->count);
done:
    wipe(f->auth, sizeof(f->auth));
    free(f);
    return ok;
}

/* ------------------------------------------------------- POST sessions */

#if BEDSIDE_CLOCK_POSTING

#define SESSIONS_URL SHELF_API_BASE "sessions"

typedef struct {
    char auth[sizeof("Token ") + CFG_VALUE_MAX];
    char body[512];
    char reply[POST_REPLY_MAX];
} post_t;

/* Reads the reply into buf, at most cap bytes: the length, cap when there
 * was at least that much (more may have followed), or -1 on a read error. */
static int read_reply(esp_http_client_handle_t client, char *buf, int cap)
{
    int len = 0;
    while (len < cap) {
        int n = esp_http_client_read(client, buf + len, cap - len);
        if (n < 0) return -1;
        if (n == 0) break;
        len += n;
    }
    return len;
}

outbox_state_t readwise_post_session(const session_t *s, const char *tz_iana,
                                     bool *request_was_sent)
{
    *request_was_sent = false;
    post_t *p = calloc(1, sizeof(*p));
    if (!p) {
        ESP_LOGE(TAG, "post: no memory");
        return post_reply_state(0, false, NULL, 0);
    }
    int status = 0, len = 0;
    esp_http_client_handle_t client = NULL;
    int body_len = body_session(p->body, sizeof(p->body), s, tz_iana);
    char token[CFG_VALUE_MAX + 1];
    if (body_len < 0) {
        /* Never sendable: parked O_CHECK so it cannot block newer items */
        ESP_LOGE(TAG, "post: session not postable (started_at %lld, ended_at %lld, pages %u-%u, "
                 "tz_iana %u bytes): parked for \"Check Readwise\"", (long long)s->started_at,
                 (long long)s->ended_at, s->page_start, s->page_end, (unsigned)strlen(tz_iana));
        status = POST_UNPOSTABLE;
        goto done;
    }
    if (!config_copy("rw_token", token, sizeof(token))) {
        ESP_LOGW(TAG, "post: rw_token not set");
        goto done;
    }
    snprintf(p->auth, sizeof(p->auth), "Token %s", token);
    wipe(token, sizeof(token));

    esp_http_client_config_t cfg = {
        .url = SESSIONS_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* A redirect could carry the token, or the session, elsewhere */
        .disable_auto_redirect = true,
        .buffer_size = READ_CHUNK,
        .buffer_size_tx = 1024,
    };
    client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "post: HTTP client init failed");
        goto done;
    }
    esp_http_client_set_header(client, "Authorization", p->auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");

    /* Connects and sends the headers only: no session can be recorded yet */
    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "post: connect failed: %s (not sent)", esp_err_to_name(err));
        goto done;
    }
    int w = esp_http_client_write(client, p->body, body_len);
    if (w <= 0) {
        /* Nothing of the body was taken: the server holds headers only */
        ESP_LOGE(TAG, "post: body not written (%d): not sent", w);
        goto done;
    }
    /* From here the outcome is the server's: unknown unless it answers */
    *request_was_sent = true;
    if (w != body_len) {
        ESP_LOGE(TAG, "post: body cut at %d of %d bytes: outcome unknown", w, body_len);
        goto done;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGE(TAG, "post: no answer: outcome unknown");
        goto done;
    }
    status = esp_http_client_get_status_code(client);
    len = read_reply(client, p->reply, sizeof(p->reply));
    if (len < 0) {
        ESP_LOGE(TAG, "post: HTTP %d, reply unreadable", status);
        len = 0;
        if (status == 200) status = 0; /* a 200 counts only with its reply */
    }
    if (status != 200) {
        /* The API explains itself in {"error":{"message":...}} (no secrets) */
        char message[JSCAN_VALUE_MAX] = "";
        jscan_t *scan = malloc(sizeof(*scan));
        if (scan) {
            jscan_init(scan, error_message, message);
            jscan_feed(scan, p->reply, (size_t)len);
            free(scan);
        }
        ESP_LOGE(TAG, "post: HTTP %d%s %s", status,
                 status == 401 ? " (rw_token refused)"
                 : status == 429 ? " (rate limited)" : "", message);
    } else {
        ESP_LOGI(TAG, "post: HTTP 200, %d bytes of reply; heap free %u, lowest ever %u", len,
                 (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());
    }
done:
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    outbox_state_t st = post_reply_state(status, *request_was_sent, p->reply, (size_t)len);
    wipe(p->auth, sizeof(p->auth));
    free(p);
    return st;
}

/* ------------------------------------------------ PATCH books (total) */

#define BOOKS_URL SHELF_API_BASE "books"

/* Like error_message, for the first item error of a batch reply */
static void item_error_message(void *ctx, const char *path, const char *value, bool is_string)
{
    if (value && is_string && strcmp(path, "results[].error.message") == 0 && !*(char *)ctx)
        strlcpy(ctx, value, JSCAN_VALUE_MAX);
}

typedef struct {
    char auth[sizeof("Token ") + CFG_VALUE_MAX];
    char body[TOTAL_BODY_MAX];
    char reply[TOTAL_REPLY_MAX];
} total_t;

total_result_t readwise_set_total(uint32_t book_id, uint16_t pages)
{
    total_t *p = calloc(1, sizeof(*p));
    if (!p) {
        ESP_LOGE(TAG, "total: no memory");
        return TOTAL_FAILED;
    }
    int status = 0, len = 0;
    esp_http_client_handle_t client = NULL;
    int body_len = total_body(p->body, sizeof(p->body), book_id, pages);
    char token[CFG_VALUE_MAX + 1];
    if (body_len < 0) {
        ESP_LOGE(TAG, "total: book %lu, %u pages: no body", (unsigned long)book_id, pages);
        goto done;
    }
    if (!config_copy("rw_token", token, sizeof(token))) {
        ESP_LOGW(TAG, "total: rw_token not set");
        goto done;
    }
    snprintf(p->auth, sizeof(p->auth), "Token %s", token);
    wipe(token, sizeof(token));

    esp_http_client_config_t cfg = {
        .url = BOOKS_URL,
        .method = HTTP_METHOD_PATCH,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        /* A redirect could carry the token elsewhere */
        .disable_auto_redirect = true,
        .buffer_size = READ_CHUNK,
        .buffer_size_tx = 1024,
    };
    client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "total: HTTP client init failed");
        goto done;
    }
    esp_http_client_set_header(client, "Authorization", p->auth);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "total: connect failed: %s", esp_err_to_name(err));
        goto done;
    }
    int w = esp_http_client_write(client, p->body, body_len);
    if (w != body_len) {
        ESP_LOGE(TAG, "total: body written %d of %d bytes", w, body_len);
        goto done;
    }
    if (esp_http_client_fetch_headers(client) < 0) {
        ESP_LOGE(TAG, "total: no answer");
        goto done;
    }
    status = esp_http_client_get_status_code(client);
    len = read_reply(client, p->reply, sizeof(p->reply));
    if (len < 0) {
        ESP_LOGE(TAG, "total: HTTP %d, reply unreadable", status);
        len = 0;
        if (status == 200) status = 0; /* a 200 counts only with its reply */
    }
done:
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    total_result_t r = total_reply(status, p->reply, (size_t)len);
    if (r != TOTAL_SET && status) {
        /* {"error":{"message":...}} whole, or results[0].error.message in a
         * 200 (no secrets: messages never repeat the value sent) */
        char message[JSCAN_VALUE_MAX] = "";
        jscan_t *scan = malloc(sizeof(*scan));
        if (scan) {
            jscan_init(scan, error_message, message);
            jscan_feed(scan, p->reply, (size_t)len);
            if (!message[0]) {
                jscan_init(scan, item_error_message, message);
                jscan_feed(scan, p->reply, (size_t)len);
            }
            free(scan);
        }
        ESP_LOGE(TAG, "total: book %lu to %u pages: HTTP %d%s %s", (unsigned long)book_id, pages,
                 status, status == 401 ? " (rw_token refused)"
                         : status == 429 ? " (rate limited)" : "", message);
    } else if (r == TOTAL_SET) {
        ESP_LOGI(TAG, "total: book %lu manual_total_pages set to %u", (unsigned long)book_id,
                 pages);
    }
    wipe(p->auth, sizeof(p->auth));
    free(p);
    return r;
}

#endif /* BEDSIDE_CLOCK_POSTING */
