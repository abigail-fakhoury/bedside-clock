#include <assert.h>
#include <string.h>

#include "core/recent.h"

#define T0 1794452645 /* 2026-11-12T03:04:05Z */

static void test_note_find(void)
{
    recent_t r[RECENT_MAX];
    memset(r, 0, sizeof(r));
    assert(recent_find(r, 894419) == NULL);
    recent_note(r, 894419, 412, T0);
    const recent_t *e = recent_find(r, 894419);
    assert(e && e->page_end == 412 && e->ended_at == T0);
    /* The same book again replaces its entry, no duplicate */
    recent_note(r, 894419, 440, T0 + 100);
    e = recent_find(r, 894419);
    assert(e && e->page_end == 440 && e->ended_at == T0 + 100);
    int n = 0;
    for (int i = 0; i < RECENT_MAX; i++) n += r[i].book_id == 894419 && r[i].ended_at;
    assert(n == 1);
    /* Book id 0 (the debug session) is a book like any other */
    recent_note(r, 0, 2, T0);
    assert(recent_find(r, 0) && recent_find(r, 0)->page_end == 2);
    /* ended_at 0 is ignored */
    recent_note(r, 7, 9, 0);
    assert(recent_find(r, 7) == NULL);
}

static void test_full_replaces_oldest(void)
{
    recent_t r[RECENT_MAX];
    memset(r, 0, sizeof(r));
    for (int i = 0; i < RECENT_MAX; i++) recent_note(r, 100 + i, 10 + i, T0 + 1000 - i);
    /* book 107 ended longest ago */
    recent_note(r, 999, 5, T0 + 2000);
    assert(recent_find(r, 107) == NULL);
    assert(recent_find(r, 999) && recent_find(r, 999)->page_end == 5);
    for (int i = 0; i < RECENT_MAX - 1; i++) assert(recent_find(r, 100 + i));
}

static void test_pack(void)
{
    recent_t r[RECENT_MAX], back[RECENT_MAX];
    memset(r, 0, sizeof(r));
    recent_note(r, 4000000000u, 65535, T0);
    recent_note(r, 5, 1, T0 + 1);
    uint8_t buf[RECENT_BLOB_SIZE];
    assert(recent_pack(r, buf, sizeof(buf) - 1) == 0);
    assert(recent_pack(r, buf, sizeof(buf)) == RECENT_BLOB_SIZE);
    assert(recent_unpack(back, buf, sizeof(buf)));
    for (int i = 0; i < RECENT_MAX; i++)
        assert(back[i].book_id == r[i].book_id && back[i].page_end == r[i].page_end &&
               back[i].ended_at == r[i].ended_at);
    assert(recent_find(back, 4000000000u)->page_end == 65535);
    /* Another version or size is refused and leaves everything empty */
    buf[0] = 2;
    assert(!recent_unpack(back, buf, sizeof(buf)));
    assert(recent_find(back, 5) == NULL);
    buf[0] = RECENT_BLOB_VERSION;
    assert(!recent_unpack(back, buf, sizeof(buf) - 1));
    assert(!recent_unpack(back, NULL, 0));
    assert(recent_find(back, 5) == NULL);
}

int main(void)
{
    test_note_find();
    test_full_replaces_oldest();
    test_pack();
    return 0;
}
