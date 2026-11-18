#include "core/flush.h"

void flush_wake(flush_t *p) { p->stopped = false; }

int flush_next(const flush_t *p, const outbox_item_t items[OUTBOX_MAX])
{
    if (p->stopped) return -1;
    int best = -1;
    for (int i = 0; i < OUTBOX_MAX; i++) {
        if (items[i].state != O_PENDING || items[i].s.book_id == 0) continue;
        if (best < 0 || items[i].s.started_at < items[best].s.started_at) best = i;
    }
    return best;
}

bool flush_begin(outbox_item_t items[OUTBOX_MAX], int i)
{
    if (i < 0 || i >= OUTBOX_MAX || items[i].state != O_PENDING) return false;
    items[i].state = O_SENDING;
    return true;
}

bool flush_after(flush_t *p, outbox_item_t items[OUTBOX_MAX], int i, const session_t *sent,
                 outbox_state_t state, bool request_was_sent)
{
    /* Refused (401/403/429) after it was sent: nothing more this wake */
    if (request_was_sent && state == O_PENDING) p->stopped = true;
    if (i < 0 || i >= OUTBOX_MAX) return false;
    outbox_item_t *o = &items[i];
    if (o->state != O_SENDING || o->s.book_id != sent->book_id ||
        o->s.started_at != sent->started_at)
        return false;
    o->state = state;
    /* Parked without a request (unpostable): nothing was said, go on */
    return state == O_EMPTY || (state == O_CHECK && !request_was_sent);
}
