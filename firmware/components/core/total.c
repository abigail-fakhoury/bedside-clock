#include "core/total.h"

#include <stdio.h>
#include <string.h>

#include "core/jscan.h"

static book_t *find(const shelf_t *sh, uint32_t book_id)
{
    if (!sh || book_id == 0) return NULL;
    unsigned count = sh->count > SHELF_MAX ? SHELF_MAX : sh->count;
    for (unsigned i = 0; i < count; i++)
        if (sh->books[i].id == book_id) return (book_t *)&sh->books[i];
    return NULL;
}

uint16_t total_to_set(const shelf_t *sh, uint32_t book_id, uint16_t page_end)
{
    const book_t *k = find(sh, book_id);
    if (!k || k->manual_pages || !k->catalog_pages || page_end > k->catalog_pages) return 0;
    return k->catalog_pages;
}

bool total_mark_set(shelf_t *sh, uint32_t book_id, uint16_t pages)
{
    book_t *k = find(sh, book_id);
    if (!k || pages == 0) return false;
    k->manual_pages = pages;
    return true;
}

int total_body(char *buf, size_t cap, uint32_t book_id, uint16_t pages)
{
    if (book_id == 0 || pages == 0) return -1;
    int n = snprintf(buf, cap, "{\"items\":[{\"id\":%lu,\"manual_total_pages\":%u}]}",
                     (unsigned long)book_id, (unsigned)pages);
    return n < 0 || (size_t)n >= cap ? -1 : n;
}

typedef struct {
    int result;   /* elements of "results" seen so far (closed) */
    bool updated; /* results[0].status was the string "updated" */
    bool other;   /* a status that was anything else, in any result */
} reply_t;

static void on_value(void *ctx, const char *path, const char *value, bool is_string)
{
    reply_t *r = ctx;
    if (!value) {
        if (strcmp(path, "results[]") == 0) r->result++; /* one result ended */
        return;
    }
    if (strcmp(path, "results[].status") != 0) return;
    if (r->result == 0 && is_string && strcmp(value, "updated") == 0) r->updated = true;
    else r->other = true;
}

total_result_t total_reply(int http_status, const char *body, size_t len)
{
    if (http_status == 429) return TOTAL_RATE_LIMITED;
    if (http_status != 200 || !body || len == 0 || len >= TOTAL_REPLY_MAX) return TOTAL_FAILED;
    reply_t r = {0};
    jscan_t scan;
    jscan_init(&scan, on_value, &r);
    if (!jscan_feed(&scan, body, len) || !jscan_finish(&scan)) return TOTAL_FAILED;
    return r.updated && !r.other && r.result == 1 ? TOTAL_SET : TOTAL_FAILED;
}
