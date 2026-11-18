#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "core/session.h"

/* What an outbox item becomes after its POST /sessions, from the status
 * code and the start of the reply (https://readwise.io/books_api: 200 with
 * {"results":[{"status":"created","session_id":…,"error":null}]}, status
 * one of created, existing, error).
 *
 * request_was_sent false (the request never left the clock): O_PENDING.
 * 200 whose body is one whole JSON document with results[0].status
 * "created": O_EMPTY. Any other 200 (another status, no results, a
 * malformed, truncated or empty body): O_CHECK. Every other code goes by
 * outbox_after_post: 401/403/429 O_PENDING, the rest (and 0, sent but no
 * reply) O_CHECK. body may be NULL when len is 0; at most POST_REPLY_MAX
 * bytes are looked at, and a reply that did not fit counts as truncated. */
enum { POST_REPLY_MAX = 1024 };

/* http_status for an item whose request body could not be built
 * (body_session returned -1: bad times, pages or tz_iana). It can never be
 * posted: O_CHECK, whatever request_was_sent says. */
enum { POST_UNPOSTABLE = -2 };

outbox_state_t post_reply_state(int http_status, bool request_was_sent, const char *body,
                                size_t len);
