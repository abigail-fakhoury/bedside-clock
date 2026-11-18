#pragma once
#include <stdbool.h>

#include "core/session.h"
#include "core/shelf.h"
#include "core/total.h"
#include "post.h" /* BEDSIDE_CLOCK_POSTING */

/* Readwise Books API (https://readwise.io/books_api), token from the
 * rw_token setting. Needs the network up (net_up()). */

/* GET books?status=currently_reading&limit=100, streamed through jscan in
 * 1 KB reads (the body never sits in RAM whole), following "next" while
 * the shelf has room. Sets fetched_at from the RTC (0 when it has no valid
 * time). False, with *out possibly partial, on any network, HTTP or JSON
 * error. Blocks for up to ~15 s per page; any one task with ~8 KB stack. */
bool readwise_fetch_shelf(shelf_t *out);

#if BEDSIDE_CLOCK_POSTING
/* POST sessions with one finished paper session (core/body.c builds it,
 * timezone tz_iana). Reads at most POST_REPLY_MAX - 1 bytes of the reply
 * and returns what the outbox item becomes (core/postreply.c): O_EMPTY
 * only for a 200 whose results[0].status is "created". *request_was_sent
 * is false only when the whole body never reached TLS (the request cannot
 * have been recorded); from then on an unanswered or unreadable request is
 * O_CHECK, never resent. Never follows a redirect. Writes to the real
 * account: compiled only with the switch in post.h on. */
outbox_state_t readwise_post_session(const session_t *s, const char *tz_iana,
                                     bool *request_was_sent);

/* PATCH books with {"items":[{"id":book_id,"manual_total_pages":pages}]}
 * (core/total.c builds it and reads the reply), so a paper session posted
 * after it gets progress. Reads at most TOTAL_REPLY_MAX - 1 bytes of the
 * reply. TOTAL_SET only for a 200 whose results[0].status is "updated";
 * TOTAL_RATE_LIMITED for a 429; TOTAL_FAILED for anything else, no reply
 * included (logged, no secrets). Never follows a redirect. Writes to the
 * real account: compiled only with the switch in post.h on. */
total_result_t readwise_set_total(uint32_t book_id, uint16_t pages);
#endif
