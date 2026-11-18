#include "core/postreply.h"

#include <string.h>

#include "core/jscan.h"

typedef struct {
    int result;   /* elements of "results" seen so far (closed) */
    bool created; /* results[0].status was the string "created" */
    bool other;   /* results[0].status was something else */
} reply_t;

static void on_value(void *ctx, const char *path, const char *value, bool is_string)
{
    reply_t *r = ctx;
    if (!value) {
        if (strcmp(path, "results[]") == 0) r->result++; /* one result ended */
        return;
    }
    if (r->result != 0 || strcmp(path, "results[].status") != 0) return;
    if (is_string && strcmp(value, "created") == 0) r->created = true;
    else r->other = true;
}

outbox_state_t post_reply_state(int http_status, bool request_was_sent, const char *body,
                                size_t len)
{
    if (http_status == POST_UNPOSTABLE) return O_CHECK; /* can never be sent */
    if (!request_was_sent || http_status != 200) return outbox_after_post(http_status,
                                                                          request_was_sent);
    /* A 200 counts only when the whole reply says so */
    if (!body || len == 0 || len >= POST_REPLY_MAX) return O_CHECK;
    reply_t r = {0};
    jscan_t scan;
    jscan_init(&scan, on_value, &r);
    if (!jscan_feed(&scan, body, len) || !jscan_finish(&scan)) return O_CHECK;
    if (!r.created || r.other || r.result < 1) return O_CHECK;
    return outbox_after_post(200, true);
}
