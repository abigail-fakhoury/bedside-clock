#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "core/flow.h"
#include "core/layout.h"

#define T0 1794452645 /* 2026-11-12T03:04:05Z */

static shelf_t shelf;
static session_t session;
static outbox_item_t outbox[OUTBOX_MAX];
static recent_t recent[RECENT_MAX];
static flow_t f;

static void add_book(uint32_t id, const char *title, uint16_t pages, uint16_t last)
{
    book_t *b = &shelf.books[shelf.count++];
    memset(b, 0, sizeof(*b));
    b->id = id;
    snprintf(b->title, sizeof(b->title), "%s", title);
    b->pages = pages;
    b->last_page = last;
}

static void fresh(void)
{
    memset(&shelf, 0, sizeof(shelf));
    add_book(894419, "Great Circle", 0, 0);   /* 0: no page count */
    add_book(5608920, "Bird by Bird", 240, 30); /* 1 */
    add_book(894230, "Bird by Bird", 0, 0);   /* 2 */
    add_book(1001, "Middlemarch", 880, 150);   /* 3 */
    shelf.fetched_at = T0;
    memset(&session, 0, sizeof(session));
    memset(outbox, 0, sizeof(outbox));
    memset(recent, 0, sizeof(recent));
    memset(&f, 0x5A, sizeof(f)); /* flow_reset must not rely on zeroed memory */
    f.session = &session;
    f.outbox = outbox;
    f.recent = recent;
    f.shelf = &shelf;
    f.setup_done = true;
    flow_reset(&f);
    assert(f.session == &session && f.outbox == outbox && f.recent == recent &&
           f.shelf == &shelf && f.setup_done);
}

static unsigned ev(flow_event_t e, time_t now)
{
    flow_in_t in = {.now = now, .page = -1};
    return flow_event(&f, e, &in);
}

static unsigned result(flow_result_t r, const char *title, int page)
{
    flow_in_t in = {.now = T0, .result = r, .title = title, .page = page};
    return flow_event(&f, EV_RESULT, &in);
}

static unsigned done(flow_event_t e, bool ok, time_t now)
{
    flow_in_t in = {.now = now, .page = -1, .ok = ok};
    return flow_event(&f, e, &in);
}

/* Wake, plus: listening for the start */
static void to_listen_start(void)
{
    assert(ev(EV_WAKE, T0) == A_DRAW && f.screen == SCR_CLOCK);
    assert(flow_buttons(&f) == BTN_PLUS && flow_peek(&f));
    unsigned a = ev(EV_PLUS, T0);
    assert(a == (A_DRAW | A_ASK) && f.screen == SCR_LISTEN && f.ask == ASK_START && f.busy);
}

static void test_decisions(void)
{
    assert(page_step(212, 1) == 213 && page_step(212, -1) == 211);
    assert(page_step(1, -1) == 1 && page_step(5, -10) == 1);
    assert(page_step(9999, 1) == 9999 && page_step(9995, 10) == 9999);
    assert(hold_tens(0) == 0 && hold_tens(299) == 0 && hold_tens(300) == 1);
    assert(hold_tens(599) == 1 && hold_tens(600) == 2 && hold_tens(3000) == 10);

    fresh();
    page_src_t src;
    /* heard, else the clock's last end page, else Readwise, else ask */
    assert(flow_start_page(212, recent, &shelf.books[3], &src) == 212 && src == PAGE_HEARD);
    assert(flow_start_page(-1, recent, &shelf.books[3], &src) == 150 && src == PAGE_READWISE);
    recent_note(recent, 1001, 190, T0 - 3600);
    assert(flow_start_page(-1, recent, &shelf.books[3], &src) == 190 && src == PAGE_CLOCK);
    assert(flow_start_page(0, recent, &shelf.books[3], &src) == 190 && src == PAGE_CLOCK);
    assert(flow_start_page(10000, recent, &shelf.books[3], &src) == 190);
    assert(flow_start_page(-1, recent, &shelf.books[0], &src) == 0);
    assert(flow_start_page(7, recent, &shelf.books[0], &src) == 7 && src == PAGE_HEARD);

    /* Two copies: the one the clock last logged goes first */
    match_t m = {1, 2};
    match_t o = flow_order(m, &shelf, recent);
    assert(o.a == 1 && o.b == 2); /* neither logged: order kept */
    recent_note(recent, 894230, 44, T0 - 60);
    o = flow_order(m, &shelf, recent);
    assert(o.a == 2 && o.b == 1);
    recent_note(recent, 5608920, 50, T0);
    o = flow_order(m, &shelf, recent);
    assert(o.a == 1 && o.b == 2);
    o = flow_order((match_t){3, -1}, &shelf, recent);
    assert(o.a == 3 && o.b == -1);

    assert(strcmp(flow_alt_title(&shelf, 1, 2), "the other copy") == 0);
    assert(strcmp(flow_alt_title(&shelf, 0, 3), "Middlemarch") == 0);

    assert(!shelf_stale(T0, T0 + 86399));
    assert(shelf_stale(T0, T0 + 86400));
    assert(shelf_stale(0, T0));
    assert(shelf_stale(T0 + 60, T0)); /* from the future */

    /* The exact copy */
    assert(strcmp(flow_question(ASK_START), "Which book, what page?") == 0);
    assert(strcmp(flow_question(ASK_PAGE), "What page?") == 0);
    assert(strcmp(flow_question(ASK_END), "What page did you stop on?") == 0);
    assert(strcmp(flow_message(MSG_DIDNT_CATCH), "Didn't catch that") == 0);
    assert(strcmp(flow_message(MSG_NOT_ON_SHELF), "Not on your shelf") == 0);
    assert(strcmp(flow_message(MSG_NO_WIFI), "No Wi-Fi") == 0);
    assert(strcmp(flow_message(MSG_NO_TIME), "No time yet") == 0);
    assert(strcmp(flow_message(MSG_CHECK_READWISE), "Check Readwise") == 0);
    assert(strcmp(flow_message(MSG_SAVED_ON_CLOCK), "Saved on clock") == 0);
    assert(strcmp(flow_message(MSG_SETUP_NEEDED), "Setup needed") == 0);
    assert(flow_page_note(PAGE_HEARD)[0] == '\0' && flow_page_note(PAGE_SET)[0] == '\0');
    assert(flow_page_note(PAGE_CLOCK)[0] && flow_page_note(PAGE_READWISE)[0]);
}

/* Steps 1-4: plus, title and page heard, check, tick */
static void test_start_heard(void)
{
    fresh();
    to_listen_start();
    assert(flow_buttons(&f) == 0 && !flow_peek(&f));
    assert(ev(EV_TAP, T0) == 0); /* the mic is not running yet */
    assert(ev(EV_REC_STARTED, T0) == A_DRAW && f.listening);
    assert(ev(EV_TAP, T0) == A_STOP_REC); /* any tap ends the recording */
    assert(ev(EV_REC_ENDED, T0) == A_DRAW && !f.listening);
    unsigned a = result(R_OK, "Middlemarch", 212);
    assert(a & A_DRAW);
    assert(f.screen == SCR_CHECK_START && f.a == 3 && f.b == -1 && f.page == 212);
    assert(f.src == PAGE_HEARD && !f.busy);
    assert(flow_buttons(&f) == (BTN_MINUS | BTN_UP | BTN_CROSS | BTN_TICK));
    assert(session.phase == S_IDLE); /* never starts without the tick */
    assert(flow_net_allowed(&f) == false); /* Wi-Fi down once the check shows */
    /* nudges */
    assert(ev(EV_UP, T0) == A_DRAW && f.page == 213);
    assert(ev(EV_MINUS10, T0) == A_DRAW && f.page == 203);
    assert(ev(EV_PICK, T0) == 0); /* one book: no second line */
    a = ev(EV_TICK, T0 + 5);
    assert(a == (A_SAVE_SESSION | A_DARK | A_NET_DOWN));
    assert(f.screen == SCR_DARK);
    assert(session.phase == S_READING && session.book_id == 1001 && session.page_start == 203);
    assert(session.started_at == T0 + 5);
}

static void test_start_failures(void)
{
    fresh();
    to_listen_start();
    unsigned a = result(R_TOO_QUIET, "", -1);
    assert(f.screen == SCR_MESSAGE && f.msg == MSG_DIDNT_CATCH && (a & A_DRAW));
    assert(flow_buttons(&f) == (BTN_CROSS | BTN_PLUS) && !flow_peek(&f));
    /* plus retries the same question */
    assert(ev(EV_PLUS, T0) == (A_DRAW | A_ASK) && f.screen == SCR_LISTEN && f.ask == ASK_START);
    result(R_NO_WIFI, "", -1);
    assert(f.screen == SCR_MESSAGE && f.msg == MSG_NO_WIFI);
    ev(EV_PLUS, T0);
    result(R_NOT_OK, "", -1);
    assert(f.msg == MSG_DIDNT_CATCH);
    ev(EV_PLUS, T0);
    result(R_OK, "", 212); /* null title */
    assert(f.msg == MSG_DIDNT_CATCH);
    ev(EV_PLUS, T0);
    result(R_OK, "The Count of Monte Cristo", 12);
    assert(f.msg == MSG_NOT_ON_SHELF && flow_buttons(&f) == (BTN_CROSS | BTN_PLUS));
    ev(EV_PLUS, T0);
    result(R_NO_SHELF, "", -1);
    assert(f.msg == MSG_NOT_ON_SHELF);
    /* cross: back to dark, nothing started */
    a = ev(EV_CROSS, T0);
    assert(f.screen == SCR_DARK && (a & A_DARK) && (a & A_NET_DOWN) && session.phase == S_IDLE);

    /* A stale result (after giving up) changes nothing */
    assert(result(R_OK, "Middlemarch", 212) == 0 && f.screen == SCR_DARK);

    /* Setup needed: plus does not listen */
    fresh();
    f.setup_done = false;
    ev(EV_WAKE, T0);
    a = ev(EV_PLUS, T0);
    assert(a == A_DRAW && f.screen == SCR_MESSAGE && f.msg == MSG_SETUP_NEEDED && flow_peek(&f));
    assert(ev(EV_FADED, T0) == 0 && f.screen == SCR_DARK);
}

/* A title with no page: fall back, else ask "What page?" */
static void test_start_page_fallback(void)
{
    fresh();
    to_listen_start();
    result(R_OK, "Middlemarch", -1);
    assert(f.screen == SCR_CHECK_START && f.page == 150 && f.src == PAGE_READWISE);

    fresh();
    recent_note(recent, 1001, 190, T0 - 7200);
    to_listen_start();
    result(R_OK, "Middlemarch", -1);
    assert(f.page == 190 && f.src == PAGE_CLOCK);
    ev(EV_UP, T0);
    assert(f.src == PAGE_SET); /* the note goes once set by hand */

    fresh();
    to_listen_start();
    unsigned a = result(R_OK, "Great Circle", -1);
    assert(a == (A_DRAW | A_ASK) && f.screen == SCR_LISTEN && f.ask == ASK_PAGE && f.busy);
    assert(strcmp(f.shelf->books[f.a].title, "Great Circle") == 0);
    result(R_OK, "", -1); /* still nothing */
    assert(f.screen == SCR_MESSAGE && f.msg == MSG_DIDNT_CATCH);
    assert(ev(EV_PLUS, T0) == (A_DRAW | A_ASK) && f.ask == ASK_PAGE);
    result(R_OK, "", 394);
    assert(f.screen == SCR_CHECK_START && f.page == 394 && f.src == PAGE_HEARD && f.a == 0);
}

/* Two books: the second is a line under the first; picking swaps */
static void test_two_books(void)
{
    fresh();
    recent_note(recent, 894230, 44, T0 - 60); /* the clock last logged copy 2 */
    to_listen_start();
    result(R_OK, "Bird by Bird", -1);
    assert(f.screen == SCR_CHECK_START && f.a == 2 && f.b == 1);
    assert(f.page == 44 && f.src == PAGE_CLOCK);
    assert(flow_buttons(&f) & BTN_PICK);
    assert(ev(EV_PICK, T0) == A_DRAW && f.a == 1 && f.b == 2);
    assert(f.page == 30 && f.src == PAGE_READWISE); /* the other copy's page */
    ev(EV_PICK, T0);
    assert(f.a == 2 && f.page == 44);
    /* a heard page stays when picking */
    fresh();
    to_listen_start();
    result(R_OK, "Bird by Bird", 20);
    assert(f.a == 1 && f.b == 2 && f.page == 20);
    ev(EV_PICK, T0);
    assert(f.a == 2 && f.page == 20 && f.src == PAGE_HEARD);
    ev(EV_TICK, T0);
    assert(session.book_id == 894230 && session.page_start == 20);
}

/* The tick with the clock unset: "No time yet", sync, start */
static void test_start_no_time(void)
{
    fresh();
    to_listen_start();
    result(R_OK, "Middlemarch", 212);
    unsigned a = ev(EV_TICK, 0);
    assert(a == (A_DRAW | A_SYNC) && f.screen == SCR_MESSAGE && f.msg == MSG_NO_TIME);
    assert(f.busy && f.pending == PEND_START && session.phase == S_IDLE);
    assert(flow_buttons(&f) == 0);
    assert(ev(EV_CROSS, T0) == 0); /* buttons ignored while busy */
    a = done(EV_SYNCED, false, 0);
    assert(f.screen == SCR_MESSAGE && f.msg == MSG_NO_WIFI && !f.busy && (a & A_NET_DOWN));
    assert(flow_buttons(&f) == (BTN_CROSS | BTN_PLUS));
    a = ev(EV_PLUS, 0); /* retry the sync */
    assert(a == (A_DRAW | A_SYNC) && f.msg == MSG_NO_TIME && f.busy);
    a = done(EV_SYNCED, true, T0 + 9);
    assert(session.phase == S_READING && session.started_at == T0 + 9 && session.page_start == 212);
    assert(a == (A_SAVE_SESSION | A_DARK | A_NET_DOWN) && f.screen == SCR_DARK);
    assert(f.pending == PEND_NONE);

    /* cross after a failed sync: nothing started */
    fresh();
    to_listen_start();
    result(R_OK, "Middlemarch", 212);
    ev(EV_TICK, 0);
    done(EV_SYNCED, false, 0);
    a = ev(EV_CROSS, 0);
    assert(f.screen == SCR_DARK && session.phase == S_IDLE && f.pending == PEND_NONE);
}

static void start_reading(void)
{
    to_listen_start();
    result(R_OK, "Middlemarch", 212);
    ev(EV_TICK, T0);
    assert(session.phase == S_READING);
}

/* Steps 5-8: peek, stop, the end page, check, save */
static void test_end(void)
{
    fresh();
    start_reading();
    assert(ev(EV_WAKE, T0 + 600) == A_DRAW && f.screen == SCR_TIMER);
    assert(flow_buttons(&f) == BTN_STOP && flow_peek(&f));
    assert(ev(EV_FADED, T0 + 603) == 0 && f.screen == SCR_DARK);
    ev(EV_WAKE, T0 + 2520);
    unsigned a = ev(EV_STOP, T0 + 2520);
    assert(a == (A_SAVE_SESSION | A_DRAW | A_ASK));
    assert(session.phase == S_ENDED && session.ended_at == T0 + 2520);
    assert(f.screen == SCR_LISTEN && f.ask == ASK_END && flow_net_allowed(&f));
    a = result(R_OK, "", 240);
    assert(a == (A_DRAW | A_SAVE_SESSION) && f.screen == SCR_CHECK_END && f.page == 240);
    assert(session.page_end == 240); /* kept, should the screen time out */
    assert(flow_net_allowed(&f)); /* Wi-Fi stays up for the post */
    assert(flow_buttons(&f) == (BTN_MINUS | BTN_UP | BTN_CROSS | BTN_TICK));
    assert(flow_tick_enabled(&f));
    /* the tick is disabled while end < start */
    for (int i = 0; i < 3; i++) ev(EV_MINUS10, T0);
    assert(f.page == 210 && !flow_tick_enabled(&f));
    assert(ev(EV_TICK, T0) == 0 && session.phase == S_ENDED);
    ev(EV_UP, T0);
    ev(EV_UP, T0);
    assert(f.page == 212 && flow_tick_enabled(&f)); /* zero pages is allowed */
    ev(EV_UP10, T0);
    ev(EV_UP10, T0);
    ev(EV_UP10, T0);
    assert(f.page == 242);
    a = ev(EV_TICK, T0 + 2530);
    assert(a == (A_SAVE_OUTBOX | A_SAVE_SESSION | A_SAVE_RECENT | A_POST));
    assert(f.busy && session.phase == S_IDLE);
    /* Queued and cleared in one step relative to a flush: both under the lock */
    assert(flow_under_lock(a) == (A_SAVE_OUTBOX | A_SAVE_SESSION));
    assert(outbox[0].state == O_PENDING && outbox[0].s.page_start == 212 &&
           outbox[0].s.page_end == 242 && outbox[0].s.book_id == 1001);
    assert(recent_find(recent, 1001) && recent_find(recent, 1001)->page_end == 242);
    assert(f.done.page_end == 242 && f.done.ended_at == T0 + 2520);
    assert(ev(EV_TICK, T0) == 0); /* busy: a second tick does nothing */
    a = done(EV_POSTED, false, T0 + 2531);
    assert(f.screen == SCR_MESSAGE && f.msg == MSG_SAVED_ON_CLOCK && flow_peek(&f));
    assert(a == (A_DRAW | A_NET_DOWN) && !f.busy && !flow_net_allowed(&f));
    ev(EV_FADED, T0);
    assert(f.screen == SCR_DARK);
    ev(EV_WAKE, T0);
    assert(f.screen == SCR_CLOCK);

    /* when sent (Task 10): "Saved" */
    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    ev(EV_STOP, T0 + 60);
    result(R_OK, "", 215);
    ev(EV_TICK, T0 + 61);
    done(EV_POSTED, true, T0 + 62);
    assert(f.screen == SCR_SAVED && flow_peek(&f) && f.done.page_end == 215);
}

/* A power cut after the outbox write but before the session was cleared:
 * the clock wakes with the session ended and its copy queued. The tick
 * saves it without queueing it a second time. */
static void test_end_after_power_cut(void)
{
    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    ev(EV_STOP, T0 + 60);
    result(R_OK, "", 215);
    ev(EV_TICK, T0 + 61);
    assert(outbox[0].state == O_PENDING);
    session_t lost = outbox[0].s; /* what NVS still held as the session */
    fresh();
    outbox[0].state = O_PENDING;
    outbox[0].s = lost;
    session = lost;
    assert(ev(EV_WAKE, T0 + 100) == A_DRAW && f.screen == SCR_CHECK_END);
    unsigned a = ev(EV_TICK, T0 + 101);
    assert(a == (A_SAVE_OUTBOX | A_SAVE_SESSION | A_SAVE_RECENT | A_POST));
    assert(session.phase == S_IDLE);
    int n = 0;
    for (int i = 0; i < OUTBOX_MAX; i++) n += outbox[i].state != O_EMPTY;
    assert(n == 1 && outbox[0].s.page_end == 215);
}

/* A full outbox at the tick (eight sessions waiting): pinned as built in
 * Task 9. The session is kept (S_ENDED, nothing lost, no save asked), the
 * confirmed end page is in RAM only, and "No Wi-Fi" offers plus (asks the
 * end page again) and cross (back to the end check, page kept). */
static void test_end_outbox_full(void)
{
    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    ev(EV_STOP, T0 + 60);
    result(R_OK, "", 215);
    for (int i = 0; i < OUTBOX_MAX; i++) {
        outbox[i].state = O_PENDING;
        outbox[i].s = session;
        outbox[i].s.book_id = 7000 + (uint32_t)i;
    }
    ev(EV_UP, T0);
    assert(f.page == 216);
    unsigned a = ev(EV_TICK, T0 + 61);
    assert(a == (A_DRAW | A_NET_DOWN));
    assert(f.screen == SCR_MESSAGE && f.msg == MSG_NO_WIFI && !f.busy);
    assert(session.phase == S_ENDED && session.page_end == 216);
    for (int i = 0; i < OUTBOX_MAX; i++) assert(outbox[i].s.book_id == 7000 + (uint32_t)i);
    assert(flow_buttons(&f) == (BTN_CROSS | BTN_PLUS));
    a = ev(EV_PLUS, T0);
    assert((a & A_ASK) && f.screen == SCR_LISTEN && f.ask == ASK_END);
    result(R_NOT_OK, "", -1);
    a = ev(EV_CROSS, T0);
    assert(f.screen == SCR_CHECK_END && f.page == 216);
}

/* No Wi-Fi or no answer twice at the end: manual entry from the start page */
static void test_end_failures(void)
{
    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    ev(EV_STOP, T0 + 60);
    unsigned a = result(R_NOT_OK, "", -1);
    assert(f.screen == SCR_MESSAGE && f.msg == MSG_DIDNT_CATCH && (a & A_NET_DOWN));
    assert(flow_buttons(&f) == (BTN_CROSS | BTN_PLUS));
    assert(ev(EV_PLUS, T0) == (A_DRAW | A_ASK) && f.ask == ASK_END);
    a = result(R_NO_WIFI, "", -1);
    assert(f.screen == SCR_CHECK_END && f.page == 212 && (a & A_NET_DOWN));
    assert(session.phase == S_ENDED);

    /* a null page counts as a failure; cross on the message goes to the check */
    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    ev(EV_STOP, T0 + 60);
    result(R_OK, "", -1);
    assert(f.msg == MSG_DIDNT_CATCH);
    a = ev(EV_CROSS, T0);
    assert(f.screen == SCR_CHECK_END && f.page == 212 && session.phase == S_ENDED);

    /* too quiet once, then heard */
    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    ev(EV_STOP, T0 + 60);
    result(R_TOO_QUIET, "", -1);
    ev(EV_PLUS, T0);
    result(R_OK, "", 230);
    assert(f.screen == SCR_CHECK_END && f.page == 230);
}

/* Stop with the clock unset: sync first, never a bogus end time */
static void test_stop_no_time(void)
{
    fresh();
    start_reading();
    ev(EV_WAKE, 0);
    unsigned a = ev(EV_STOP, 0);
    assert(a == (A_DRAW | A_SYNC) && f.msg == MSG_NO_TIME && f.pending == PEND_STOP);
    assert(session.phase == S_READING);
    a = done(EV_SYNCED, true, T0 + 900);
    assert(a == (A_SAVE_SESSION | A_DRAW | A_ASK) && session.phase == S_ENDED);
    assert(session.ended_at == T0 + 900 && f.screen == SCR_LISTEN && f.ask == ASK_END);

    /* sync fails: still reading; cross leaves it reading */
    fresh();
    start_reading();
    ev(EV_WAKE, 0);
    ev(EV_STOP, 0);
    done(EV_SYNCED, false, 0);
    assert(f.msg == MSG_NO_WIFI && session.phase == S_READING);
    ev(EV_CROSS, 0);
    assert(f.screen == SCR_DARK && session.phase == S_READING);
    /* synced but the clock still reads unset: no stop */
    ev(EV_WAKE, 0);
    ev(EV_STOP, 0);
    done(EV_SYNCED, true, 0);
    assert(f.msg == MSG_NO_WIFI && session.phase == S_READING);
}

static void test_discard(void)
{
    /* cross on the end check asks first */
    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    ev(EV_STOP, T0 + 60);
    result(R_OK, "", 240);
    assert(ev(EV_CROSS, T0) == A_DRAW && f.screen == SCR_DISCARD && !f.expired);
    assert(flow_buttons(&f) == (BTN_CROSS | BTN_TICK) && flow_net_allowed(&f));
    assert(ev(EV_CROSS, T0) == A_DRAW && f.screen == SCR_CHECK_END && f.page == 240);
    /* a page set by hand survives the round trip through "Discard session?" */
    ev(EV_UP10, T0);
    ev(EV_CROSS, T0);
    ev(EV_CROSS, T0);
    assert(f.screen == SCR_CHECK_END && f.page == 250);
    ev(EV_CROSS, T0);
    unsigned a = ev(EV_TICK, T0);
    assert(a == (A_SAVE_SESSION | A_NET_DOWN | A_DARK) && session.phase == S_IDLE);
    assert(f.screen == SCR_DARK && outbox[0].state == O_EMPTY);

    /* over 24 h: the next tap offers only "Discard session?" */
    fresh();
    start_reading();
    a = ev(EV_WAKE, T0 + SESSION_MAX_SECONDS + 1);
    assert(a == A_DRAW && f.screen == SCR_DISCARD && f.expired);
    assert(flow_buttons(&f) == (BTN_CROSS | BTN_TICK));
    a = ev(EV_CROSS, T0 + SESSION_MAX_SECONDS + 1);
    assert(f.screen == SCR_DARK && (a & A_DARK) && session.phase == S_READING);
    ev(EV_WAKE, T0 + SESSION_MAX_SECONDS + 2);
    assert(f.screen == SCR_DISCARD);
    ev(EV_TICK, T0);
    assert(session.phase == S_IDLE && f.screen == SCR_DARK);
}

/* The 30 s timeout: waiting screens only, never while a job runs, and it
 * never ends a session */
static void test_timeout(void)
{
    fresh();
    assert(!flow_timed_out(&f, 999999)); /* dark */
    ev(EV_WAKE, T0);
    assert(!flow_timed_out(&f, 999999)); /* the clock fades by itself */
    ev(EV_PLUS, T0);
    assert(!flow_timed_out(&f, 999999)); /* busy listening */
    result(R_OK, "Middlemarch", 212);
    assert(!flow_timed_out(&f, FLOW_TIMEOUT_MS - 1));
    assert(flow_timed_out(&f, FLOW_TIMEOUT_MS));
    unsigned a = ev(EV_TIMEOUT, T0);
    assert(a == (A_DARK | A_NET_DOWN) && f.screen == SCR_DARK && session.phase == S_IDLE);

    fresh();
    start_reading();
    ev(EV_WAKE, T0 + 60);
    assert(!flow_timed_out(&f, 999999)); /* the timer peek fades by itself */
    ev(EV_STOP, T0 + 60);
    result(R_OK, "", 250);
    assert(flow_timed_out(&f, FLOW_TIMEOUT_MS));
    a = ev(EV_TIMEOUT, T0);
    assert(a == (A_DARK | A_NET_DOWN) && session.phase == S_ENDED && session.page_end == 250);
    /* the next tap comes back to the end check with that page */
    assert(ev(EV_WAKE, T0 + 100) == A_DRAW && f.screen == SCR_CHECK_END && f.page == 250);
    /* with no page yet, the start page */
    session.page_end = 0;
    ev(EV_TIMEOUT, T0);
    ev(EV_WAKE, T0);
    assert(f.screen == SCR_CHECK_END && f.page == 212);
    /* posting is a job: no timeout */
    ev(EV_TICK, T0);
    assert(f.busy && !flow_timed_out(&f, 999999));
}

/* An O_CHECK item: "Check Readwise" once at the next wake, dropped on tap */
static void test_check_readwise(void)
{
    fresh();
    outbox[2].state = O_CHECK;
    outbox[3].state = O_PENDING;
    assert(ev(EV_WAKE, T0) == A_DRAW && f.screen == SCR_MESSAGE && f.msg == MSG_CHECK_READWISE);
    assert(flow_buttons(&f) == 0 && !flow_peek(&f));
    unsigned a = ev(EV_TAP, T0);
    assert(a == (A_SAVE_OUTBOX | A_DRAW) && f.screen == SCR_CLOCK);
    assert(outbox[2].state == O_EMPTY && outbox[3].state == O_PENDING);
    ev(EV_FADED, T0);
    ev(EV_WAKE, T0);
    assert(f.screen == SCR_CLOCK);

    /* while reading, the tap goes on to the timer */
    fresh();
    start_reading();
    outbox[0].state = O_CHECK;
    ev(EV_WAKE, T0 + 5);
    assert(f.msg == MSG_CHECK_READWISE);
    ev(EV_TAP, T0 + 5);
    assert(f.screen == SCR_TIMER && outbox[0].state == O_EMPTY);
}

/* Net allowed: only from plus to the start check and from the end-page
 * recording until saved */
/* Saves done while the outbox lock is held: the outbox, and the session
 * whenever the outbox is saved with it (never the slow or network ones) */
static void test_under_lock(void)
{
    unsigned tick = A_SAVE_OUTBOX | A_SAVE_SESSION | A_SAVE_RECENT | A_POST;
    assert(flow_under_lock(tick) == (A_SAVE_OUTBOX | A_SAVE_SESSION));
    assert(flow_under_lock(A_SAVE_OUTBOX | A_DRAW) == A_SAVE_OUTBOX);
    assert(flow_under_lock(A_SAVE_SESSION | A_DARK | A_NET_DOWN) == 0);
    assert(flow_under_lock(A_SAVE_SESSION | A_DRAW | A_ASK) == 0);
    assert(flow_under_lock(A_POST | A_SYNC | A_ASK | A_SAVE_RECENT) == 0);
    assert(flow_under_lock(0) == 0);
}

static void test_net_allowed(void)
{
    fresh();
    assert(!flow_net_allowed(&f));
    ev(EV_WAKE, T0);
    assert(!flow_net_allowed(&f));
    ev(EV_PLUS, T0);
    assert(flow_net_allowed(&f));
    result(R_TOO_QUIET, "", -1);
    assert(!flow_net_allowed(&f));
    ev(EV_PLUS, T0);
    result(R_OK, "Middlemarch", 212);
    ev(EV_TICK, 0);
    assert(flow_net_allowed(&f)); /* syncing the time */
    done(EV_SYNCED, true, T0);
    assert(!flow_net_allowed(&f) && f.screen == SCR_DARK);
}

int main(void)
{
    test_decisions();
    test_start_heard();
    test_start_failures();
    test_start_page_fallback();
    test_two_books();
    test_start_no_time();
    test_end();
    test_end_after_power_cut();
    test_end_outbox_full();
    test_end_failures();
    test_stop_no_time();
    test_discard();
    test_timeout();
    test_check_readwise();
    test_net_allowed();
    test_under_lock();
    return 0;
}
