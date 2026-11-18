#include "core/session.h"

#include <string.h>

bool session_start(session_t *s, const book_t *book, uint16_t page, time_t now)
{
    if (!book || page == 0 || now < SESSION_EARLIEST) return false;
    memset(s, 0, sizeof(*s));
    s->phase = S_READING;
    s->book_id = book->id;
    memcpy(s->title, book->title, sizeof(s->title) - 1);
    s->title[sizeof(s->title) - 1] = '\0';
    s->page_start = page;
    s->started_at = now;
    return true;
}

bool session_stop(session_t *s, time_t now)
{
    if (s->phase != S_READING || now < SESSION_EARLIEST) return false;
    s->ended_at = now < s->started_at ? s->started_at : now;
    s->phase = S_ENDED;
    return true;
}

bool session_confirm(session_t *s, uint16_t page_end)
{
    if (s->phase != S_ENDED || page_end < s->page_start) return false;
    s->page_end = page_end;
    return true;
}

bool session_expired(const session_t *s, time_t now)
{
    switch (s->phase) {
    case S_READING: return now - s->started_at > SESSION_MAX_SECONDS;
    case S_ENDED: return s->ended_at - s->started_at > SESSION_MAX_SECONDS;
    default: return false;
    }
}

outbox_state_t outbox_after_post(int http_status, bool request_was_sent)
{
    if (!request_was_sent) return O_PENDING;
    if (http_status == 200) return O_EMPTY;
    if (http_status == 401 || http_status == 403 || http_status == 429) return O_PENDING;
    return O_CHECK;
}

int outbox_recover(outbox_item_t items[OUTBOX_MAX])
{
    int n = 0;
    for (int i = 0; i < OUTBOX_MAX; i++) {
        if (items[i].state == O_SENDING) {
            items[i].state = O_CHECK;
            n++;
        }
    }
    return n;
}

static void put_le(uint8_t *p, uint64_t v, int n)
{
    for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t get_le(const uint8_t *p, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* SESSION_BYTES at p: phase, book_id, title, page_start, page_end,
 * started_at, ended_at. */
static void put_session(uint8_t *p, const session_t *s)
{
    p[0] = (uint8_t)s->phase;
    put_le(p + 1, s->book_id, 4);
    memcpy(p + 5, s->title, 64);
    put_le(p + 69, s->page_start, 2);
    put_le(p + 71, s->page_end, 2);
    put_le(p + 73, (uint64_t)(int64_t)s->started_at, 8);
    put_le(p + 81, (uint64_t)(int64_t)s->ended_at, 8);
}

static bool get_session(session_t *s, const uint8_t *p)
{
    if (p[0] > S_ENDED) return false;
    s->phase = (phase_t)p[0];
    s->book_id = (uint32_t)get_le(p + 1, 4);
    memcpy(s->title, p + 5, 64);
    s->title[63] = '\0';
    s->page_start = (uint16_t)get_le(p + 69, 2);
    s->page_end = (uint16_t)get_le(p + 71, 2);
    s->started_at = (time_t)(int64_t)get_le(p + 73, 8);
    s->ended_at = (time_t)(int64_t)get_le(p + 81, 8);
    return true;
}

size_t session_pack(const session_t *s, uint8_t *buf, size_t cap)
{
    if (cap < SESSION_BLOB_SIZE) return 0;
    buf[0] = SESSION_BLOB_VERSION;
    put_session(buf + 1, s);
    return SESSION_BLOB_SIZE;
}

bool session_unpack(session_t *s, const uint8_t *buf, size_t len)
{
    memset(s, 0, sizeof(*s));
    if (!buf || len != SESSION_BLOB_SIZE || buf[0] != SESSION_BLOB_VERSION) return false;
    if (get_session(s, buf + 1)) return true;
    memset(s, 0, sizeof(*s));
    return false;
}

size_t outbox_pack(const outbox_item_t items[OUTBOX_MAX], uint8_t *buf, size_t cap)
{
    if (cap < OUTBOX_BLOB_SIZE) return 0;
    buf[0] = SESSION_BLOB_VERSION;
    uint8_t *p = buf + 1;
    for (int i = 0; i < OUTBOX_MAX; i++, p += 1 + SESSION_BYTES) {
        p[0] = (uint8_t)items[i].state;
        put_session(p + 1, &items[i].s);
    }
    return OUTBOX_BLOB_SIZE;
}

bool outbox_unpack(outbox_item_t items[OUTBOX_MAX], const uint8_t *buf, size_t len)
{
    memset(items, 0, OUTBOX_MAX * sizeof(items[0]));
    if (!buf || len != OUTBOX_BLOB_SIZE || buf[0] != SESSION_BLOB_VERSION) return false;
    const uint8_t *p = buf + 1;
    for (int i = 0; i < OUTBOX_MAX; i++, p += 1 + SESSION_BYTES) {
        if (p[0] > O_CHECK || !get_session(&items[i].s, p + 1)) {
            memset(items, 0, OUTBOX_MAX * sizeof(items[0]));
            return false;
        }
        items[i].state = (outbox_state_t)p[0];
    }
    return true;
}

int outbox_add(outbox_item_t items[OUTBOX_MAX], const session_t *s)
{
    if (s->phase != S_ENDED || s->page_end < s->page_start) return -1;
    /* Queued already (a power cut before the session was cleared): once */
    for (int i = 0; i < OUTBOX_MAX; i++)
        if (items[i].state != O_EMPTY && items[i].s.book_id == s->book_id &&
            items[i].s.started_at == s->started_at)
            return i;
    for (int i = 0; i < OUTBOX_MAX; i++) {
        if (items[i].state == O_EMPTY) {
            items[i].state = O_PENDING;
            items[i].s = *s;
            return i;
        }
    }
    return -1;
}
