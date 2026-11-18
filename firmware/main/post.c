#include "post.h"

#include "esp_log.h"

static const char *TAG = "post";

#if !BEDSIDE_CLOCK_POSTING

bool post_flush_pending(const post_outbox_t *ob, const session_t *watch)
{
    (void)ob;
    (void)watch;
    ESP_LOGI(TAG, "posting is off (BEDSIDE_CLOCK_POSTING 0): not sent");
    return false;
}

#else

#include <stdlib.h>

#include "config.h"
#include "core/total.h"
#include "readwise.h"
#include "store.h"

static const char *const STATE[] = {"O_EMPTY", "O_PENDING", "O_SENDING", "O_CHECK"};

/* Before *s is posted: when its book is on the cached shelf with a catalog
 * page count but no manual total, PATCH that count as the book's total so
 * the session gets progress (core/total.h), and mark it set in the cache so
 * it is not sent again. *sh is the cache, loaded on first use (NULL until
 * then; the caller frees it). Never under the outbox lock. False only after
 * a 429: the session is not posted this wake. Any other failure is logged
 * and the session is posted as before, without a total. */
static bool set_total_first(const session_t *s, shelf_t **sh, bool *loaded)
{
    if (!*loaded) {
        *loaded = true;
        *sh = malloc(sizeof(**sh));
        if (!*sh) ESP_LOGE(TAG, "no memory for the shelf: no total set");
        else if (!store_load_shelf(*sh)) ESP_LOGW(TAG, "no shelf cache: no total set");
    }
    uint16_t pages = *sh ? total_to_set(*sh, s->book_id, s->page_end) : 0;
    if (!pages) {
        ESP_LOGI(TAG, "book %lu: no total to set", (unsigned long)s->book_id);
        return true;
    }
    total_result_t r = readwise_set_total(s->book_id, pages);
    if (r == TOTAL_SET && total_mark_set(*sh, s->book_id, pages)) store_save_shelf(*sh);
    if (r == TOTAL_FAILED)
        ESP_LOGW(TAG, "book %lu: total not set; the session is posted without it",
                 (unsigned long)s->book_id);
    return r != TOTAL_RATE_LIMITED;
}

bool post_flush_pending(const post_outbox_t *ob, const session_t *watch)
{
    char tz[64];
    if (!config_copy("tz_iana", tz, sizeof(tz))) {
        ESP_LOGW(TAG, "tz_iana not set: not sent");
        return false;
    }
    bool watched = false;
    shelf_t *sh = NULL;
    bool sh_loaded = false;
    for (int round = 0; round < OUTBOX_MAX; round++) {
        session_t s;
        bool saved = false;
        /* The next item, looked at only: its book's total is set first,
         * while the item is still O_PENDING and without the lock */
        xSemaphoreTake(ob->lock, portMAX_DELAY);
        int i = flush_next(ob->plan, ob->items);
        if (i >= 0) s = ob->items[i].s;
        xSemaphoreGive(ob->lock);
        if (i < 0) break;
        if (!set_total_first(&s, &sh, &sh_loaded)) {
            /* 429 on the PATCH: as after a 429 on the POST, the item stays
             * O_PENDING and nothing more is sent this wake */
            xSemaphoreTake(ob->lock, portMAX_DELAY);
            ob->plan->stopped = true;
            xSemaphoreGive(ob->lock);
            ESP_LOGW(TAG, "outbox %d (book %lu): not sent, still O_PENDING; nothing more is "
                     "sent this wake", i, (unsigned long)s.book_id);
            break;
        }

        xSemaphoreTake(ob->lock, portMAX_DELAY);
        i = flush_next(ob->plan, ob->items);
        bool same = i >= 0 && ob->items[i].s.book_id == s.book_id &&
                    ob->items[i].s.started_at == s.started_at;
        if (same && flush_begin(ob->items, i)) {
            s = ob->items[i].s;
            /* On flash before the request leaves: a reset from here on
             * makes it O_CHECK at boot, never a second post */
            saved = store_outbox_save(ob->items);
            if (!saved) ob->items[i].state = O_PENDING; /* not on flash: do not send */
        }
        xSemaphoreGive(ob->lock);
        if (i < 0) break;
        if (!same) continue; /* the outbox changed meanwhile: look again */
        if (!saved) {
            ESP_LOGE(TAG, "outbox %d could not be marked sending: not sent", i);
            break;
        }

        bool sent = false;
        outbox_state_t st = readwise_post_session(&s, tz, &sent);

        xSemaphoreTake(ob->lock, portMAX_DELAY);
        bool go_on = flush_after(ob->plan, ob->items, i, &s, st, sent);
        store_outbox_save(ob->items);
        bool stopped = ob->plan->stopped;
        xSemaphoreGive(ob->lock);

        ESP_LOGI(TAG, "outbox %d (book %lu, pages %u-%u): %s, now %s%s", i,
                 (unsigned long)s.book_id, s.page_start, s.page_end,
                 sent ? "sent" : "not sent", STATE[st],
                 stopped ? "; nothing more is sent this wake" : "");
        if (watch && watch->book_id == s.book_id && watch->started_at == s.started_at)
            watched = st == O_EMPTY;
        if (!go_on) break;
    }
    free(sh);
    return watched;
}

#endif
