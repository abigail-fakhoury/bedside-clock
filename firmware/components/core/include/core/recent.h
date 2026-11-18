#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* The clock's own memory of the books it last logged a session for: the end
 * page and when that session ended. It outlives the outbox (which empties
 * once a session is posted), so the next start can fall back to "where you
 * stopped last" and put the most recently read copy of a title first. */

#define RECENT_MAX 8

typedef struct {
    uint32_t book_id;
    uint16_t page_end;
    time_t ended_at; /* UTC; 0 = an empty slot */
} recent_t;

/* Records a saved session: replaces the entry for book_id, else fills an
 * empty slot, else replaces the entry that ended longest ago. Ignored when
 * ended_at is 0. */
void recent_note(recent_t r[RECENT_MAX], uint32_t book_id, uint16_t page_end, time_t ended_at);

/* The entry for book_id, or NULL when the clock has none. */
const recent_t *recent_find(const recent_t r[RECENT_MAX], uint32_t book_id);

/* The NVS form: a version byte, then RECENT_MAX x (book_id LE 4, page_end
 * LE 2, ended_at int64 LE 8). Another version or size is refused, not
 * misread. */
#define RECENT_BLOB_VERSION 1
#define RECENT_BLOB_SIZE (1 + RECENT_MAX * 14)

/* Bytes written, 0 when cap is too small. */
size_t recent_pack(const recent_t r[RECENT_MAX], uint8_t *buf, size_t cap);

/* True and filled when buf holds a valid blob; false (all empty) otherwise. */
bool recent_unpack(recent_t r[RECENT_MAX], const uint8_t *buf, size_t len);
