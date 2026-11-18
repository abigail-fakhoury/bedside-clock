#pragma once
#include <stdbool.h>

#include "core/session.h"

/* Which outbox item to post next and what to do after each answer.
 *
 * One item at a time, the oldest (earliest started_at) O_PENDING item
 * first. Never an item that is O_CHECK or O_SENDING: their outcome is
 * unknown and they are never resent. Never book_id 0 (the debug book).
 * After a request that was sent and answered 401, 403 or 429 nothing more
 * is sent until the next wake (flush_wake). After any answer but
 * "created", the current flush stops. */

typedef struct {
    bool stopped; /* 401/403/429 this wake: send nothing more */
} flush_t;

/* A new wake: sending may start again. */
void flush_wake(flush_t *p);

/* The slot to post next, or -1 (nothing to send, or stopped this wake). */
int flush_next(const flush_t *p, const outbox_item_t items[OUTBOX_MAX]);

/* items[i] O_PENDING -> O_SENDING (save that before the request leaves).
 * False, nothing changed, when items[i] is not O_PENDING. */
bool flush_begin(outbox_item_t items[OUTBOX_MAX], int i);

/* Applies the outcome of posting *sent from slot i: items[i] takes `state`
 * when it is still that session and O_SENDING (otherwise it is left alone:
 * the outbox was cleared meanwhile). request_was_sent false with O_PENDING
 * means the request never left; O_CHECK with request_was_sent false means
 * the item could never be posted (POST_UNPOSTABLE) and is parked. Returns
 * true when the flush may go on to the next item (the session was created,
 * or the item was parked without a request), false to stop. */
bool flush_after(flush_t *p, outbox_item_t items[OUTBOX_MAX], int i, const session_t *sent,
                 outbox_state_t state, bool request_was_sent);
