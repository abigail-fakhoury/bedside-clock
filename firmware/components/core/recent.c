#include "core/recent.h"

#include <string.h>

void recent_note(recent_t r[RECENT_MAX], uint32_t book_id, uint16_t page_end, time_t ended_at)
{
    if (ended_at == 0) return;
    int slot = -1;
    for (int i = 0; i < RECENT_MAX && slot < 0; i++)
        if (r[i].ended_at && r[i].book_id == book_id) slot = i;
    for (int i = 0; i < RECENT_MAX && slot < 0; i++)
        if (!r[i].ended_at) slot = i;
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < RECENT_MAX; i++)
            if (r[i].ended_at < r[slot].ended_at) slot = i;
    }
    r[slot] = (recent_t){.book_id = book_id, .page_end = page_end, .ended_at = ended_at};
}

const recent_t *recent_find(const recent_t r[RECENT_MAX], uint32_t book_id)
{
    for (int i = 0; i < RECENT_MAX; i++)
        if (r[i].ended_at && r[i].book_id == book_id) return &r[i];
    return NULL;
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

size_t recent_pack(const recent_t r[RECENT_MAX], uint8_t *buf, size_t cap)
{
    if (cap < RECENT_BLOB_SIZE) return 0;
    buf[0] = RECENT_BLOB_VERSION;
    uint8_t *p = buf + 1;
    for (int i = 0; i < RECENT_MAX; i++, p += 14) {
        put_le(p, r[i].book_id, 4);
        put_le(p + 4, r[i].page_end, 2);
        put_le(p + 6, (uint64_t)(int64_t)r[i].ended_at, 8);
    }
    return RECENT_BLOB_SIZE;
}

bool recent_unpack(recent_t r[RECENT_MAX], const uint8_t *buf, size_t len)
{
    memset(r, 0, RECENT_MAX * sizeof(r[0]));
    if (!buf || len != RECENT_BLOB_SIZE || buf[0] != RECENT_BLOB_VERSION) return false;
    const uint8_t *p = buf + 1;
    for (int i = 0; i < RECENT_MAX; i++, p += 14) {
        r[i].book_id = (uint32_t)get_le(p, 4);
        r[i].page_end = (uint16_t)get_le(p + 4, 2);
        r[i].ended_at = (time_t)(int64_t)get_le(p + 6, 8);
    }
    return true;
}
