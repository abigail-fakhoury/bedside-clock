#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "core/shelf.h"

/* One paper reading session, and the outbox of finished sessions waiting
 * to be posted to Readwise Books. */

/* 2026-01-01 00:00:00 UTC: an RTC reading earlier than this is not set. */
#define SESSION_EARLIEST 1767225600

/* The API rejects sessions longer than this. */
#define SESSION_MAX_SECONDS 86400

typedef enum { S_IDLE, S_READING, S_ENDED } phase_t;

typedef enum { O_EMPTY, O_PENDING, O_SENDING, O_CHECK } outbox_state_t;

typedef struct {
    phase_t phase;
    uint32_t book_id;
    char title[64]; /* for the screens only; never sent */
    uint16_t page_start, page_end;
    time_t started_at, ended_at; /* UTC */
} session_t;

typedef struct {
    outbox_state_t state;
    session_t s;
} outbox_item_t;

#define OUTBOX_MAX 8

/* S_READING from now on page `page` of *book. False (and *s untouched) when
 * now < SESSION_EARLIEST (the RTC is not set), page is 0 or book is NULL. */
bool session_start(session_t *s, const book_t *book, uint16_t page, time_t now);

/* S_READING -> S_ENDED with ended_at = now, frozen; true when it stopped.
 * False, *s untouched (still S_READING), when now < SESSION_EARLIEST: the
 * RTC is not set (e.g. a flat battery mid-session), so the caller must sync
 * the time before stopping. Also false, nothing changed, in any phase other
 * than S_READING (a second stop included). A valid now slightly earlier than
 * started_at (the clock stepped back) ends the session at started_at. */
bool session_stop(session_t *s, time_t now);

/* Sets page_end on an S_ENDED session. False (nothing changed) when
 * page_end < page_start or the session is not S_ENDED. */
bool session_confirm(session_t *s, uint16_t page_end);

/* True when the session is longer than SESSION_MAX_SECONDS: an S_READING
 * session more than 24 h after started_at at `now`, an S_ENDED one whose
 * ended_at is more than 24 h after started_at. Never for S_IDLE. */
bool session_expired(const session_t *s, time_t now);

/* What an outbox item becomes after a POST attempt. Not sent -> O_PENDING;
 * 200 -> O_EMPTY; 401/403/429 -> O_PENDING; anything else, including a
 * timeout after the request was sent (status 0) -> O_CHECK, never resent. */
outbox_state_t outbox_after_post(int http_status, bool request_was_sent);

/* At boot: an item still O_SENDING was cut off mid-request, so its outcome
 * is unknown and it becomes O_CHECK. Returns how many items changed. */
int outbox_recover(outbox_item_t items[OUTBOX_MAX]);

/* The NVS forms. Session blob: version, phase, book_id (LE), title (64),
 * page_start, page_end (LE), started_at, ended_at (int64 LE). Outbox blob:
 * version, then OUTBOX_MAX x (state, the session blob without its version
 * byte). A blob of another version or size, or holding an unknown phase or
 * state, is refused, not misread. */
#define SESSION_BLOB_VERSION 1
#define SESSION_BYTES (1 + 4 + 64 + 2 + 2 + 8 + 8)
#define SESSION_BLOB_SIZE (1 + SESSION_BYTES)
#define OUTBOX_BLOB_SIZE (1 + OUTBOX_MAX * (1 + SESSION_BYTES))

/* Bytes written, 0 when cap is too small. */
size_t session_pack(const session_t *s, uint8_t *buf, size_t cap);
size_t outbox_pack(const outbox_item_t items[OUTBOX_MAX], uint8_t *buf, size_t cap);

/* True and filled when buf holds a valid blob; false (an S_IDLE session,
 * an all-O_EMPTY outbox) otherwise. */
bool session_unpack(session_t *s, const uint8_t *buf, size_t len);
bool outbox_unpack(outbox_item_t items[OUTBOX_MAX], const uint8_t *buf, size_t len);

/* Puts the ended session *s into the first O_EMPTY slot as O_PENDING and
 * returns that slot, or -1 (nothing changed) when the outbox is full or *s
 * is not S_ENDED with page_end >= page_start. A session already in the
 * outbox (same book_id and started_at, in any state but O_EMPTY) is not
 * added again: that slot is returned, unchanged, so a session is never
 * queued (and posted) twice. */
int outbox_add(outbox_item_t items[OUTBOX_MAX], const session_t *s);
