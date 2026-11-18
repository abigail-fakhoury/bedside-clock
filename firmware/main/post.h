#pragma once
#include <stdbool.h>

#include "core/flush.h"
#include "core/session.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Posting finished sessions to Readwise Books (POST /sessions).
 *
 * THE SWITCH. Posting writes to the owner's real account: a session
 * posted appears on her public feed and cannot be deduplicated. With
 * BEDSIDE_CLOCK_POSTING 0 no POST code is compiled at all
 * (readwise_post_session does not exist) and post_flush_pending sends
 * nothing and changes nothing: every saved session stays O_PENDING on the
 * clock ("Saved on clock"), as in Task 9. Set it to 1 to post. */
#ifndef BEDSIDE_CLOCK_POSTING
#define BEDSIDE_CLOCK_POSTING 1
#endif

/* The outbox as the worker shares it with the main task, which owns it.
 * Whoever reads or writes items[] or *plan holds lock (a mutex) while doing
 * so, and saves the outbox to NVS before giving it back. The lock is never
 * held across a network request. */
typedef struct {
    outbox_item_t *items; /* OUTBOX_MAX */
    flush_t *plan;
    SemaphoreHandle_t lock;
} post_outbox_t;

/* Posts the O_PENDING outbox items, one at a time, oldest first (core/
 * flush.c decides): each is saved to NVS as O_SENDING before its request
 * leaves and saved again with its outcome (core/postreply.c). Stops at the
 * first answer that is not "created"; after 401/403/429 nothing more is
 * sent this wake. Before each item, while it is still O_PENDING, its book's
 * paper total is set when the cached shelf shows a catalog page count but
 * no manual total (PATCH books, core/total.h), so the session gets
 * progress; a 429 on that leaves the item O_PENDING and stops the wake like
 * a 429 on the POST, any other failure posts the session without a total.
 * Call only with Wi-Fi up, from the flow worker task.
 * True when *watch (a session just saved; NULL for none) was created. */
bool post_flush_pending(const post_outbox_t *ob, const session_t *watch);
