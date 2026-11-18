#include <assert.h>
#include <string.h>

#include "core/flush.h"
#include "core/postreply.h"

#define T0 1794452645 /* 2026-11-12T03:04:05Z */

static outbox_item_t items[OUTBOX_MAX];
static flush_t plan;

static void put(int i, outbox_state_t st, uint32_t book, time_t start)
{
    items[i].state = st;
    memset(&items[i].s, 0, sizeof(items[i].s));
    items[i].s.phase = S_ENDED;
    items[i].s.book_id = book;
    items[i].s.page_start = 10;
    items[i].s.page_end = 20;
    items[i].s.started_at = start;
    items[i].s.ended_at = start + 600;
}

static void fresh(void)
{
    memset(items, 0, sizeof(items));
    memset(&plan, 0x5A, sizeof(plan));
    flush_wake(&plan);
}

/* One request: begin, then the answer as post_reply_state gives it */
static bool post(int i, int status, bool sent, const char *body)
{
    assert(flush_begin(items, i));
    assert(items[i].state == O_SENDING);
    session_t s = items[i].s;
    outbox_state_t st = post_reply_state(status, sent, body, body ? strlen(body) : 0);
    return flush_after(&plan, items, i, &s, st, sent);
}

static const char CREATED[] = "{\"results\":[{\"status\":\"created\",\"session_id\":1}]}";

static void test_next_oldest_first(void)
{
    fresh();
    assert(flush_next(&plan, items) == -1); /* empty */
    put(3, O_PENDING, 11, T0 + 7200);
    put(1, O_PENDING, 12, T0 + 3600);
    put(6, O_PENDING, 13, T0);
    put(2, O_PENDING, 14, T0 + 3600); /* same start as slot 1: slot order */
    assert(flush_next(&plan, items) == 6);
    items[6].state = O_EMPTY;
    assert(flush_next(&plan, items) == 1);
    items[1].state = O_EMPTY;
    assert(flush_next(&plan, items) == 2);
    items[2].state = O_EMPTY;
    assert(flush_next(&plan, items) == 3);
}

static void test_never_unknown(void)
{
    fresh();
    put(0, O_CHECK, 11, T0);
    put(1, O_SENDING, 12, T0 + 1);
    put(2, O_PENDING, 0, T0 + 2); /* the debug book */
    assert(flush_next(&plan, items) == -1);
    assert(!flush_begin(items, 0) && items[0].state == O_CHECK);
    assert(!flush_begin(items, 1) && items[1].state == O_SENDING);
    assert(!flush_begin(items, 7) && items[7].state == O_EMPTY);
    put(3, O_PENDING, 13, T0 + 3);
    assert(flush_next(&plan, items) == 3);
}

static void test_created_goes_on(void)
{
    fresh();
    put(0, O_PENDING, 11, T0);
    put(1, O_PENDING, 12, T0 + 1);
    assert(post(0, 200, true, CREATED));
    assert(items[0].state == O_EMPTY);
    assert(flush_next(&plan, items) == 1);
    assert(post(1, 200, true, CREATED));
    assert(flush_next(&plan, items) == -1);
}

/* An unknown outcome becomes O_CHECK, stops the flush, and is never picked
 * again: not this wake, not the next. */
static void test_unknown_never_retried(void)
{
    static const struct { int status; const char *body; } unknown[] = {
        {0, NULL},  /* sent, no reply */
        {200, "{\"results\":[{\"status\":\"existing\"}]}"},
        {200, "{\"results\":[{\"status\":\"crea"}, /* truncated */
        {200, ""},
        {500, NULL},
        {400, "{\"error\":{\"code\":\"invalid_value\"}}"},
    };
    for (unsigned k = 0; k < sizeof(unknown) / sizeof(unknown[0]); k++) {
        fresh();
        put(0, O_PENDING, 11, T0);
        put(1, O_PENDING, 12, T0 + 1);
        assert(!post(0, unknown[k].status, true, unknown[k].body));
        assert(items[0].state == O_CHECK);
        assert(!plan.stopped); /* other items may go at the next flush */
        assert(flush_next(&plan, items) == 1);
        items[1].state = O_EMPTY;
        assert(flush_next(&plan, items) == -1);
        flush_wake(&plan);
        assert(flush_next(&plan, items) == -1);
        assert(items[0].state == O_CHECK);
    }
}

/* 429: kept, and nothing more this wake; the next wake sends it */
static void test_429_stops_the_wake(void)
{
    fresh();
    put(0, O_PENDING, 11, T0);
    put(1, O_PENDING, 12, T0 + 1);
    assert(!post(0, 429, true, "{\"error\":{\"code\":\"rate_limited\"}}"));
    assert(items[0].state == O_PENDING && items[1].state == O_PENDING);
    assert(plan.stopped);
    assert(flush_next(&plan, items) == -1); /* not item 0, not item 1 */
    flush_wake(&plan);
    assert(flush_next(&plan, items) == 0);

    /* 401 and 403 the same: the token is refused */
    fresh();
    put(0, O_PENDING, 11, T0);
    assert(!post(0, 401, true, NULL));
    assert(items[0].state == O_PENDING && flush_next(&plan, items) == -1);
    fresh();
    put(0, O_PENDING, 11, T0);
    assert(!post(0, 403, true, NULL));
    assert(items[0].state == O_PENDING && flush_next(&plan, items) == -1);
}

/* Never left the clock: kept, this flush stops, a later one in the same
 * wake (Wi-Fi up again) may send it */
static void test_not_sent(void)
{
    fresh();
    put(0, O_PENDING, 11, T0);
    assert(!post(0, 0, false, NULL));
    assert(items[0].state == O_PENDING && !plan.stopped);
    assert(flush_next(&plan, items) == 0);
}

/* An item whose body cannot be built is parked O_CHECK and the flush goes
 * on: it never blocks the newer items behind it */
static void test_unpostable_parked(void)
{
    fresh();
    put(0, O_PENDING, 11, T0);      /* oldest, unpostable */
    put(1, O_PENDING, 12, T0 + 60);
    assert(flush_next(&plan, items) == 0);
    assert(post(0, POST_UNPOSTABLE, false, NULL)); /* goes on */
    assert(items[0].state == O_CHECK && !plan.stopped);
    assert(flush_next(&plan, items) == 1);
    assert(post(1, 200, true, CREATED));
    assert(items[1].state == O_EMPTY && flush_next(&plan, items) == -1);
    assert(items[0].state == O_CHECK); /* shown once as "Check Readwise" */
}

/* The outbox changed under the request (FLOW RESET): the slot is left */
static void test_slot_changed(void)
{
    fresh();
    put(0, O_PENDING, 11, T0);
    assert(flush_begin(items, 0));
    session_t s = items[0].s;
    memset(items, 0, sizeof(items));
    assert(!flush_after(&plan, items, 0, &s, O_CHECK, true));
    assert(items[0].state == O_EMPTY);
    /* another session took the slot */
    put(0, O_PENDING, 99, T0 + 5);
    assert(!flush_after(&plan, items, 0, &s, O_EMPTY, true));
    assert(items[0].state == O_PENDING && items[0].s.book_id == 99);
    /* the same session but no longer O_SENDING */
    put(0, O_CHECK, 11, T0);
    s = items[0].s;
    assert(!flush_after(&plan, items, 0, &s, O_PENDING, false));
    assert(items[0].state == O_CHECK);
    /* bad slot numbers */
    assert(!flush_after(&plan, items, -1, &s, O_EMPTY, true));
    assert(!flush_after(&plan, items, OUTBOX_MAX, &s, O_EMPTY, true));
    assert(!flush_begin(items, -1) && !flush_begin(items, OUTBOX_MAX));
}

int main(void)
{
    test_next_oldest_first();
    test_never_unknown();
    test_created_goes_on();
    test_unknown_never_retried();
    test_429_stops_the_wake();
    test_not_sent();
    test_slot_changed();
    test_unpostable_parked();
    return 0;
}
