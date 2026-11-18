#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "core/session.h"

#define T0 1794452645 /* 2026-11-12T03:04:05Z */

static book_t book(void)
{
    book_t b;
    memset(&b, 0, sizeof(b));
    b.id = 5608920;
    snprintf(b.title, sizeof(b.title), "%s", "Great Circle");
    return b;
}

static session_t idle(void)
{
    session_t s;
    memset(&s, 0, sizeof(s));
    return s;
}

static void test_start(void)
{
    book_t b = book();
    session_t s = idle();
    assert(session_start(&s, &b, 394, T0) == true);
    assert(s.phase == S_READING);
    assert(s.book_id == 5608920);
    assert(strcmp(s.title, "Great Circle") == 0);
    assert(s.page_start == 394);
    assert(s.page_end == 0);
    assert(s.started_at == T0);
    assert(s.ended_at == 0);
}

/* Review Focus 5: a session never gets a bogus start time. */
static void test_start_refused(void)
{
    book_t b = book();
    session_t s = idle();
    s.book_id = 77; /* untouched on refusal */
    assert(session_start(&s, &b, 394, 0) == false);
    assert(session_start(&s, &b, 394, SESSION_EARLIEST - 1) == false);
    assert(session_start(&s, &b, 394, -5) == false);
    assert(session_start(&s, &b, 0, T0) == false); /* page 0 */
    assert(session_start(&s, NULL, 394, T0) == false);
    assert(s.phase == S_IDLE && s.book_id == 77 && s.started_at == 0);
    /* The first set second is fine. */
    assert(session_start(&s, &b, 1, SESSION_EARLIEST) == true);
    assert(s.started_at == SESSION_EARLIEST && s.page_start == 1);
}

static void test_title_cut(void)
{
    book_t b = book();
    memset(b.title, 'x', sizeof(b.title)); /* no NUL */
    session_t s = idle();
    assert(session_start(&s, &b, 1, T0));
    assert(strlen(s.title) == 63);
}

static void test_stop(void)
{
    book_t b = book();
    session_t s = idle();
    assert(session_stop(&s, T0) == false); /* idle: nothing */
    assert(s.phase == S_IDLE && s.ended_at == 0);

    assert(session_start(&s, &b, 10, T0));
    assert(session_stop(&s, T0 + 2845) == true);
    assert(s.phase == S_ENDED);
    assert(s.ended_at == T0 + 2845);
    assert(session_stop(&s, T0 + 9999) == false); /* frozen */
    assert(s.phase == S_ENDED && s.ended_at == T0 + 2845);

    /* A valid clock a little behind the start: ends at the start, never
     * before it. */
    s = idle();
    assert(session_start(&s, &b, 10, T0));
    assert(session_stop(&s, T0 - 30) == true);
    assert(s.phase == S_ENDED && s.ended_at == T0);
}

/* Review Focus 4 + 5: the battery went flat mid-session and the RTC reads
 * 0 (or any time before 2026). The stop is refused and the session stays
 * open, untouched, so the caller must sync the time first; no bogus
 * session is ever built. */
static void test_stop_unset_clock(void)
{
    book_t b = book();
    session_t s = idle(), before;
    assert(session_start(&s, &b, 10, T0));
    before = s;
    assert(session_stop(&s, 0) == false);
    assert(memcmp(&s, &before, sizeof(s)) == 0);
    assert(session_stop(&s, -1) == false);
    assert(session_stop(&s, SESSION_EARLIEST - 1) == false);
    assert(memcmp(&s, &before, sizeof(s)) == 0);
    assert(s.phase == S_READING && s.ended_at == 0);
    /* Once the clock is set again the stop goes through. */
    assert(session_stop(&s, T0 + 600) == true);
    assert(s.phase == S_ENDED && s.ended_at == T0 + 600);

    /* A session started at the earliest second, stopped with the clock just
     * under it: refused, not clamped. */
    s = idle();
    assert(session_start(&s, &b, 10, SESSION_EARLIEST));
    assert(session_stop(&s, SESSION_EARLIEST - 1) == false);
    assert(s.phase == S_READING);
}

static void test_confirm(void)
{
    book_t b = book();
    session_t s = idle();
    assert(session_start(&s, &b, 10, T0));
    assert(session_confirm(&s, 42) == false); /* still reading */
    assert(s.page_end == 0);
    session_stop(&s, T0 + 60);
    assert(session_confirm(&s, 9) == false); /* end < start */
    assert(s.page_end == 0);
    assert(session_confirm(&s, 0) == false);
    assert(session_confirm(&s, 10) == true); /* no pages read is allowed */
    assert(s.page_end == 10);
    assert(session_confirm(&s, 42) == true);
    assert(s.page_end == 42);
    assert(s.phase == S_ENDED);

    s = idle();
    assert(session_confirm(&s, 42) == false);
}

static void test_expired(void)
{
    book_t b = book();
    session_t s = idle();
    assert(session_expired(&s, T0 + 999999) == false); /* idle */
    assert(session_start(&s, &b, 10, T0));
    assert(session_expired(&s, T0) == false);
    assert(session_expired(&s, T0 + 86400) == false); /* exactly 24 h */
    assert(session_expired(&s, T0 + 86400 + 1) == true);
    assert(session_expired(&s, 0) == false); /* RTC lost: not "expired" */

    /* Ended: its length counts, not how long ago it was. */
    session_stop(&s, T0 + 3600);
    assert(session_expired(&s, T0 + 7 * 86400) == false);
    s.ended_at = T0 + 86400;
    assert(session_expired(&s, T0 + 7 * 86400) == false);
    s.ended_at = T0 + 86400 + 1;
    assert(session_expired(&s, T0 + 86400 + 1) == true);
}

/* Review Focus 3: an unknown outcome is never resent. */
static void test_after_post(void)
{
    assert(outbox_after_post(0, false) == O_PENDING);
    assert(outbox_after_post(200, false) == O_PENDING); /* not sent wins */
    assert(outbox_after_post(0, true) == O_CHECK);       /* timeout after send */
    assert(outbox_after_post(200, true) == O_EMPTY);
    assert(outbox_after_post(401, true) == O_PENDING);
    assert(outbox_after_post(403, true) == O_PENDING);
    assert(outbox_after_post(429, true) == O_PENDING);
    assert(outbox_after_post(201, true) == O_CHECK);
    assert(outbox_after_post(400, true) == O_CHECK);
    assert(outbox_after_post(404, true) == O_CHECK);
    assert(outbox_after_post(500, true) == O_CHECK);
    assert(outbox_after_post(502, true) == O_CHECK);
    assert(outbox_after_post(-1, true) == O_CHECK);
}

static void test_recover(void)
{
    outbox_item_t o[OUTBOX_MAX];
    memset(o, 0, sizeof(o));
    o[0].state = O_PENDING;
    o[1].state = O_SENDING;
    o[2].state = O_CHECK;
    o[7].state = O_SENDING;
    assert(outbox_recover(o) == 2);
    assert(o[0].state == O_PENDING);
    assert(o[1].state == O_CHECK);
    assert(o[2].state == O_CHECK);
    assert(o[3].state == O_EMPTY);
    assert(o[7].state == O_CHECK);
    assert(outbox_recover(o) == 0);
}

static session_t sample(void)
{
    book_t b = book();
    session_t s = idle();
    assert(session_start(&s, &b, 394, T0));
    session_stop(&s, T0 + 2845);
    assert(session_confirm(&s, 412));
    return s;
}

static void test_session_blob(void)
{
    session_t s = sample(), r;
    uint8_t buf[SESSION_BLOB_SIZE + 4];
    assert(session_pack(&s, buf, SESSION_BLOB_SIZE - 1) == 0);
    assert(session_pack(&s, buf, sizeof(buf)) == SESSION_BLOB_SIZE);
    assert(buf[0] == SESSION_BLOB_VERSION);
    memset(&r, 0xAB, sizeof(r));
    assert(session_unpack(&r, buf, SESSION_BLOB_SIZE));
    assert(r.phase == S_ENDED && r.book_id == s.book_id && strcmp(r.title, s.title) == 0);
    assert(r.page_start == 394 && r.page_end == 412);
    assert(r.started_at == T0 && r.ended_at == T0 + 2845);

    /* A time past 2038 survives. */
    s.started_at = 4102444799;
    assert(session_pack(&s, buf, sizeof(buf)) == SESSION_BLOB_SIZE);
    assert(session_unpack(&r, buf, SESSION_BLOB_SIZE) && r.started_at == 4102444799);

    /* Refused, not misread: each leaves an idle session. */
    assert(session_unpack(&r, buf, SESSION_BLOB_SIZE - 1) == false);
    assert(r.phase == S_IDLE && r.book_id == 0 && r.started_at == 0);
    assert(session_unpack(&r, buf, SESSION_BLOB_SIZE + 1) == false);
    assert(session_unpack(&r, NULL, 0) == false);
    assert(session_unpack(&r, buf, 0) == false);
    buf[0] = SESSION_BLOB_VERSION + 1;
    assert(session_unpack(&r, buf, SESSION_BLOB_SIZE) == false);
    assert(r.phase == S_IDLE);
    buf[0] = SESSION_BLOB_VERSION;
    buf[1] = 3; /* no such phase */
    assert(session_unpack(&r, buf, SESSION_BLOB_SIZE) == false);

    /* A title without a NUL comes back cut to 63 bytes. */
    s = sample();
    memset(s.title, 'q', sizeof(s.title));
    assert(session_pack(&s, buf, sizeof(buf)) == SESSION_BLOB_SIZE);
    assert(session_unpack(&r, buf, SESSION_BLOB_SIZE) && strlen(r.title) == 63);
}

static void test_outbox_blob(void)
{
    outbox_item_t o[OUTBOX_MAX], r[OUTBOX_MAX];
    memset(o, 0, sizeof(o));
    o[0].state = O_SENDING;
    o[0].s = sample();
    o[5].state = O_CHECK;
    o[5].s = sample();
    o[5].s.book_id = 894230;
    static uint8_t buf[OUTBOX_BLOB_SIZE + 4];
    assert(outbox_pack(o, buf, OUTBOX_BLOB_SIZE - 1) == 0);
    assert(outbox_pack(o, buf, sizeof(buf)) == OUTBOX_BLOB_SIZE);
    memset(r, 0xCD, sizeof(r));
    assert(outbox_unpack(r, buf, OUTBOX_BLOB_SIZE));
    assert(r[0].state == O_SENDING && r[0].s.book_id == 5608920 && r[0].s.page_end == 412);
    assert(r[0].s.started_at == T0 && r[0].s.ended_at == T0 + 2845);
    assert(r[5].state == O_CHECK && r[5].s.book_id == 894230);
    assert(r[1].state == O_EMPTY && r[1].s.phase == S_IDLE && r[1].s.book_id == 0);

    assert(outbox_unpack(r, buf, OUTBOX_BLOB_SIZE - 1) == false);
    for (int i = 0; i < OUTBOX_MAX; i++) assert(r[i].state == O_EMPTY && r[i].s.book_id == 0);
    assert(outbox_unpack(r, NULL, 0) == false);
    buf[0] = SESSION_BLOB_VERSION + 1;
    assert(outbox_unpack(r, buf, OUTBOX_BLOB_SIZE) == false);
    buf[0] = SESSION_BLOB_VERSION;
    buf[1] = 4; /* no such state */
    assert(outbox_unpack(r, buf, OUTBOX_BLOB_SIZE) == false);
    assert(r[0].state == O_EMPTY);
}

static void test_outbox_add(void)
{
    book_t b = book();
    session_t s = idle();
    outbox_item_t items[OUTBOX_MAX];
    memset(items, 0, sizeof(items));
    assert(outbox_add(items, &s) == -1); /* not ended */
    assert(session_start(&s, &b, 212, T0));
    assert(outbox_add(items, &s) == -1); /* still reading */
    assert(session_stop(&s, T0 + 2520));
    s.page_end = 100; /* below the start page */
    assert(outbox_add(items, &s) == -1);
    assert(session_confirm(&s, 240));
    items[0].state = O_CHECK; /* a taken slot is skipped */
    assert(outbox_add(items, &s) == 1);
    assert(items[1].state == O_PENDING && items[1].s.page_end == 240 &&
           items[1].s.page_start == 212 && items[1].s.ended_at == T0 + 2520);
    for (int i = 2; i < OUTBOX_MAX; i++) {
        s.started_at += 3600; /* another session each time */
        s.ended_at += 3600;
        assert(outbox_add(items, &s) == i);
    }
    outbox_item_t before[OUTBOX_MAX];
    memcpy(before, items, sizeof(items));
    s.started_at += 3600;
    s.ended_at += 3600;
    assert(outbox_add(items, &s) == -1); /* full: nothing changed */
    assert(memcmp(before, items, sizeof(items)) == 0);
}

/* A power cut between the outbox write and the session clear leaves the
 * session ended and a copy queued; the next tick must not queue it again. */
static void test_outbox_add_once(void)
{
    book_t b = book();
    session_t s = idle();
    outbox_item_t items[OUTBOX_MAX];
    memset(items, 0, sizeof(items));
    assert(session_start(&s, &b, 212, T0));
    assert(session_stop(&s, T0 + 2520));
    assert(session_confirm(&s, 240));

    /* Same session twice: one item, the slot it already holds */
    assert(outbox_add(items, &s) == 0);
    outbox_item_t before[OUTBOX_MAX];
    memcpy(before, items, sizeof(items));
    assert(outbox_add(items, &s) == 0);
    assert(memcmp(before, items, sizeof(items)) == 0);
    int n = 0;
    for (int i = 0; i < OUTBOX_MAX; i++) n += items[i].state != O_EMPTY;
    assert(n == 1);

    /* The second tick may carry another end page: the queued copy stands */
    session_t again = s;
    again.page_end = 250;
    assert(outbox_add(items, &again) == 0);
    assert(items[0].s.page_end == 240 && items[0].state == O_PENDING);

    /* Same book, another start: a second session */
    session_t later = s;
    later.started_at = T0 + 7200;
    later.ended_at = T0 + 9000;
    assert(outbox_add(items, &later) == 1);
    assert(items[1].state == O_PENDING && items[1].s.started_at == T0 + 7200);

    /* Same start, another book: a second session */
    session_t other = s;
    other.book_id = 4790033;
    assert(outbox_add(items, &other) == 2);

    /* A copy whose post is under way or unknown blocks a re-add, unchanged */
    items[0].state = O_SENDING;
    memcpy(before, items, sizeof(items));
    assert(outbox_add(items, &s) == 0);
    assert(memcmp(before, items, sizeof(items)) == 0);
    items[0].state = O_CHECK;
    memcpy(before, items, sizeof(items));
    assert(outbox_add(items, &s) == 0);
    assert(memcmp(before, items, sizeof(items)) == 0);

    /* Once the slot is empty (sent, or the check dismissed) it may be added */
    items[0].state = O_EMPTY;
    assert(outbox_add(items, &s) == 0);
    assert(items[0].state == O_PENDING);

    /* The match is found in any slot, not only the first free one */
    memset(items, 0, sizeof(items));
    items[5].state = O_PENDING;
    items[5].s = s;
    assert(outbox_add(items, &s) == 5);
    assert(items[0].state == O_EMPTY);
}

int main(void)
{
    test_outbox_add();
    test_outbox_add_once();
    test_start();
    test_start_refused();
    test_title_cut();
    test_stop();
    test_stop_unset_clock();
    test_confirm();
    test_expired();
    test_after_post();
    test_recover();
    test_session_blob();
    test_outbox_blob();
    return 0;
}
