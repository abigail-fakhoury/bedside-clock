#include "core/flow.h"

#include <string.h>

#include "core/layout.h"

void flow_reset(flow_t *f)
{
    session_t *s = f->session;
    outbox_item_t *o = f->outbox;
    recent_t *r = f->recent;
    const shelf_t *sh = f->shelf;
    bool setup = f->setup_done;
    memset(f, 0, sizeof(*f));
    f->session = s;
    f->outbox = o;
    f->recent = r;
    f->shelf = sh;
    f->setup_done = setup;
    f->screen = SCR_DARK;
    f->a = f->b = -1;
    f->heard_page = -1;
}

uint16_t page_step(uint16_t page, int delta)
{
    int p = (int)page + delta;
    return (uint16_t)(p < 1 ? 1 : p > 9999 ? 9999 : p);
}

int hold_tens(uint32_t held_ms) { return (int)(held_ms / 300); }

uint16_t flow_start_page(int heard_page, const recent_t recent[RECENT_MAX], const book_t *book,
                         page_src_t *src)
{
    if (heard_page >= 1 && heard_page <= 9999) {
        *src = PAGE_HEARD;
        return (uint16_t)heard_page;
    }
    const recent_t *r = book ? recent_find(recent, book->id) : NULL;
    if (r && r->page_end >= 1) {
        *src = PAGE_CLOCK;
        return r->page_end;
    }
    if (book && book->last_page >= 1) {
        *src = PAGE_READWISE;
        return book->last_page;
    }
    *src = PAGE_SET;
    return 0;
}

static time_t last_logged(const shelf_t *shelf, const recent_t recent[RECENT_MAX], int i)
{
    const recent_t *r = recent_find(recent, shelf->books[i].id);
    return r ? r->ended_at : 0;
}

match_t flow_order(match_t m, const shelf_t *shelf, const recent_t recent[RECENT_MAX])
{
    if (m.a < 0 || m.b < 0) return m;
    if (last_logged(shelf, recent, m.b) > last_logged(shelf, recent, m.a))
        return (match_t){m.b, m.a};
    return m;
}

const char *flow_alt_title(const shelf_t *shelf, int a, int b)
{
    if (strcmp(shelf->books[a].title, shelf->books[b].title) == 0) return "the other copy";
    return shelf->books[b].title;
}

bool shelf_stale(time_t fetched_at, time_t now)
{
    return fetched_at == 0 || now < fetched_at || now - fetched_at >= 86400;
}

const char *flow_question(flow_ask_t ask)
{
    switch (ask) {
    case ASK_PAGE: return "What page?";
    case ASK_END: return "What page did you stop on?";
    default: return "Which book, what page?";
    }
}

const char *flow_message(flow_msg_t msg)
{
    switch (msg) {
    case MSG_DIDNT_CATCH: return "Didn't catch that";
    case MSG_NOT_ON_SHELF: return "Not on your shelf";
    case MSG_NO_WIFI: return "No Wi-Fi";
    case MSG_NO_TIME: return "No time yet";
    case MSG_CHECK_READWISE: return "Check Readwise";
    case MSG_SAVED_ON_CLOCK: return "Saved on clock";
    case MSG_SETUP_NEEDED: return "Setup needed";
    }
    return "";
}

const char *flow_page_note(page_src_t src)
{
    switch (src) {
    case PAGE_CLOCK: return "Page not heard. Your last stop";
    case PAGE_READWISE: return "Page not heard. From Readwise";
    default: return "";
    }
}

/* Screens */

static unsigned message(flow_t *f, flow_msg_t m)
{
    f->screen = SCR_MESSAGE;
    f->msg = m;
    return A_DRAW;
}

static unsigned ask(flow_t *f, flow_ask_t k)
{
    f->screen = SCR_LISTEN;
    f->ask = k;
    f->listening = false;
    f->busy = true;
    return A_DRAW | A_ASK;
}

static unsigned dark(flow_t *f)
{
    f->screen = SCR_DARK;
    f->pending = PEND_NONE;
    return A_DARK | A_NET_DOWN;
}

static unsigned sync(flow_t *f, flow_pending_t why)
{
    f->pending = why;
    f->busy = true;
    message(f, MSG_NO_TIME);
    return A_DRAW | A_SYNC;
}

/* The end check, prefilled with the page kept in the session, else the
 * start page */
static unsigned check_end(flow_t *f)
{
    f->screen = SCR_CHECK_END;
    f->page = f->session->page_end ? f->session->page_end : f->session->page_start;
    return A_DRAW;
}

static unsigned discard(flow_t *f, bool expired)
{
    f->screen = SCR_DISCARD;
    f->expired = expired;
    return A_DRAW;
}

/* What a tap from dark shows */
static unsigned wake(flow_t *f, time_t now)
{
    f->pending = PEND_NONE;
    for (int i = 0; i < OUTBOX_MAX; i++)
        if (f->outbox[i].state == O_CHECK) return message(f, MSG_CHECK_READWISE);
    const session_t *s = f->session;
    switch (s->phase) {
    case S_READING:
        if (session_expired(s, now)) return discard(f, true);
        f->screen = SCR_TIMER;
        return A_DRAW;
    case S_ENDED:
        if (session_expired(s, now)) return discard(f, true);
        f->fails = 2; /* no more asking: the page is set by hand */
        return check_end(f);
    default:
        f->screen = SCR_CLOCK;
        return A_DRAW;
    }
}

static unsigned start_tick(flow_t *f, time_t now)
{
    const book_t *b = f->shelf && f->a >= 0 ? &f->shelf->books[f->a] : NULL;
    if (session_start(f->session, b, f->page, now)) {
        f->screen = SCR_DARK;
        f->pending = PEND_NONE;
        return A_SAVE_SESSION | A_DARK | A_NET_DOWN;
    }
    if (!b) return 0;
    return sync(f, PEND_START);
}

static unsigned stop(flow_t *f, time_t now)
{
    if (!session_stop(f->session, now)) return sync(f, PEND_STOP);
    f->pending = PEND_NONE;
    f->fails = 0;
    return A_SAVE_SESSION | ask(f, ASK_END);
}

static flow_msg_t failure_msg(flow_result_t r)
{
    switch (r) {
    case R_NO_WIFI: return MSG_NO_WIFI;
    case R_NO_SHELF: return MSG_NOT_ON_SHELF;
    default: return MSG_DIDNT_CATCH;
    }
}

static unsigned heard_start(flow_t *f, const flow_in_t *in)
{
    if (in->result != R_OK) return message(f, failure_msg(in->result)) | A_NET_DOWN;
    if (!in->title || !in->title[0]) return message(f, MSG_DIDNT_CATCH) | A_NET_DOWN;
    if (!f->shelf || f->shelf->count == 0) return message(f, MSG_NOT_ON_SHELF) | A_NET_DOWN;
    match_t m = match_title(f->shelf, in->title);
    if (m.a < 0) return message(f, MSG_NOT_ON_SHELF) | A_NET_DOWN;
    m = flow_order(m, f->shelf, f->recent);
    f->a = m.a;
    f->b = m.b;
    f->heard_page = in->page;
    f->page = flow_start_page(in->page, f->recent, &f->shelf->books[f->a], &f->src);
    if (f->page == 0) return ask(f, ASK_PAGE);
    f->screen = SCR_CHECK_START;
    return A_DRAW | A_NET_DOWN;
}

static unsigned heard_page(flow_t *f, const flow_in_t *in)
{
    if (in->result == R_OK && in->page >= 1 && in->page <= 9999) {
        f->page = (uint16_t)in->page;
        f->src = PAGE_HEARD;
        f->screen = SCR_CHECK_START;
        return A_DRAW | A_NET_DOWN;
    }
    return message(f, in->result == R_OK ? MSG_DIDNT_CATCH : failure_msg(in->result)) | A_NET_DOWN;
}

static unsigned heard_end(flow_t *f, const flow_in_t *in)
{
    if (in->result == R_OK && in->page >= 1 && in->page <= 9999) {
        f->session->page_end = (uint16_t)in->page;
        f->screen = SCR_CHECK_END;
        f->page = (uint16_t)in->page;
        return A_DRAW | A_SAVE_SESSION; /* Wi-Fi stays up until saved */
    }
    if (++f->fails >= 2) return check_end(f) | A_NET_DOWN;
    flow_msg_t m = in->result == R_OK ? MSG_DIDNT_CATCH : failure_msg(in->result);
    if (m == MSG_NOT_ON_SHELF) m = MSG_DIDNT_CATCH;
    return message(f, m) | A_NET_DOWN;
}

static unsigned result(flow_t *f, const flow_in_t *in)
{
    if (f->screen != SCR_LISTEN || !f->busy) return 0; /* given up on: ignore */
    f->busy = false;
    f->listening = false;
    switch (f->ask) {
    case ASK_PAGE: return heard_page(f, in);
    case ASK_END: return heard_end(f, in);
    default: return heard_start(f, in);
    }
}

static unsigned synced(flow_t *f, const flow_in_t *in)
{
    if (!f->busy || f->screen != SCR_MESSAGE || f->msg != MSG_NO_TIME) return 0;
    f->busy = false;
    flow_pending_t why = f->pending;
    if (in->ok) {
        if (why == PEND_START) {
            unsigned a = start_tick(f, in->now);
            if (f->session->phase == S_READING) return a;
            f->busy = false; /* the clock still reads unset */
        } else if (why == PEND_STOP && session_stop(f->session, in->now)) {
            f->pending = PEND_NONE;
            f->fails = 0;
            return A_SAVE_SESSION | ask(f, ASK_END);
        }
    }
    f->pending = why; /* plus retries the sync */
    return message(f, MSG_NO_WIFI) | A_NET_DOWN;
}

static unsigned saved(flow_t *f, time_t now)
{
    (void)now;
    session_t *s = f->session;
    if (!flow_tick_enabled(f) || !session_confirm(s, f->page)) return 0;
    if (outbox_add(f->outbox, s) < 0) {
        /* Full: keep the session; nothing is lost */
        f->busy = false;
        return message(f, MSG_NO_WIFI) | A_NET_DOWN;
    }
    recent_note(f->recent, s->book_id, s->page_end, s->ended_at);
    f->done = *s;
    memset(s, 0, sizeof(*s));
    f->busy = true;
    /* The outbox is written before the session is cleared */
    return A_SAVE_OUTBOX | A_SAVE_SESSION | A_SAVE_RECENT | A_POST;
}

static unsigned message_button(flow_t *f, flow_event_t ev, time_t now)
{
    if (ev == EV_PLUS) {
        if (f->pending != PEND_NONE) return sync(f, f->pending);
        return ask(f, f->ask);
    }
    /* EV_CROSS */
    if (f->pending == PEND_NONE && f->ask == ASK_END && f->session->phase == S_ENDED) {
        f->fails = 2;
        return check_end(f);
    }
    (void)now;
    return dark(f);
}

unsigned flow_event(flow_t *f, flow_event_t ev, const flow_in_t *in)
{
    time_t now = in ? in->now : 0;
    /* Only the buttons on the screen count, and none while a job runs */
    static const struct { flow_event_t ev; unsigned btn; } need[] = {
        {EV_PLUS, BTN_PLUS},   {EV_STOP, BTN_STOP},   {EV_TICK, BTN_TICK},   {EV_CROSS, BTN_CROSS},
        {EV_MINUS, BTN_MINUS}, {EV_UP, BTN_UP},       {EV_MINUS10, BTN_MINUS}, {EV_UP10, BTN_UP},
        {EV_PICK, BTN_PICK},
    };
    for (unsigned i = 0; i < sizeof(need) / sizeof(need[0]); i++)
        if (need[i].ev == ev && (f->busy || !(flow_buttons(f) & need[i].btn))) return 0;

    switch (ev) {
    case EV_WAKE:
        if (f->busy) return 0;
        return wake(f, now);
    case EV_TAP:
        if (f->screen == SCR_LISTEN && f->listening) return A_STOP_REC;
        if (f->screen == SCR_MESSAGE && f->msg == MSG_CHECK_READWISE) {
            for (int i = 0; i < OUTBOX_MAX; i++)
                if (f->outbox[i].state == O_CHECK) f->outbox[i].state = O_EMPTY;
            return A_SAVE_OUTBOX | wake(f, now);
        }
        return 0;
    case EV_PLUS:
        if (f->screen == SCR_MESSAGE) return message_button(f, ev, now);
        if (f->session->phase != S_IDLE) return 0;
        if (!f->setup_done) return message(f, MSG_SETUP_NEEDED);
        return ask(f, ASK_START);
    case EV_STOP:
        if (f->session->phase != S_READING) return 0;
        return stop(f, now);
    case EV_TICK:
        switch (f->screen) {
        case SCR_CHECK_START: return start_tick(f, now);
        case SCR_CHECK_END: return saved(f, now);
        case SCR_DISCARD:
            memset(f->session, 0, sizeof(*f->session));
            f->screen = SCR_DARK;
            return A_SAVE_SESSION | A_NET_DOWN | A_DARK;
        default: return 0;
        }
    case EV_CROSS:
        switch (f->screen) {
        case SCR_CHECK_END: return discard(f, false);
        case SCR_DISCARD:
            if (!f->expired && f->session->phase == S_ENDED) {
                f->screen = SCR_CHECK_END; /* back, with the page as it was */
                return A_DRAW;
            }
            return dark(f);
        case SCR_MESSAGE: return message_button(f, ev, now);
        default: return dark(f);
        }
    case EV_MINUS:
    case EV_UP:
    case EV_MINUS10:
    case EV_UP10: {
        static const int delta[] = {[EV_MINUS] = -1, [EV_UP] = 1, [EV_MINUS10] = -10,
                                    [EV_UP10] = 10};
        f->page = page_step(f->page, delta[ev]);
        if (f->screen == SCR_CHECK_START) f->src = PAGE_SET;
        return A_DRAW;
    }
    case EV_PICK: {
        int8_t t = f->a;
        f->a = f->b;
        f->b = t;
        if (f->src != PAGE_HEARD) {
            page_src_t src;
            uint16_t p = flow_start_page(-1, f->recent, &f->shelf->books[f->a], &src);
            if (p) {
                f->page = p;
                f->src = src;
            } else {
                f->src = PAGE_SET;
            }
        }
        return A_DRAW;
    }
    case EV_REC_STARTED:
    case EV_REC_ENDED:
        if (f->screen != SCR_LISTEN || !f->busy) return 0;
        f->listening = ev == EV_REC_STARTED;
        return A_DRAW;
    case EV_RESULT: return in ? result(f, in) : 0;
    case EV_SYNCED: return in ? synced(f, in) : 0;
    case EV_POSTED:
        if (!f->busy || f->screen != SCR_CHECK_END) return 0;
        f->busy = false;
        if (in && in->ok) f->screen = SCR_SAVED;
        else message(f, MSG_SAVED_ON_CLOCK);
        return A_DRAW | A_NET_DOWN;
    case EV_FADED:
        if (flow_peek(f)) f->screen = SCR_DARK;
        return 0;
    case EV_TIMEOUT:
        if (f->screen == SCR_DARK || f->busy || flow_peek(f)) return 0;
        return dark(f);
    }
    return 0;
}

unsigned flow_buttons(const flow_t *f)
{
    switch (f->screen) {
    case SCR_CLOCK: return BTN_PLUS;
    case SCR_TIMER: return BTN_STOP;
    case SCR_CHECK_START:
        return BTN_MINUS | BTN_UP | BTN_CROSS | BTN_TICK | (f->b >= 0 ? BTN_PICK : 0);
    case SCR_CHECK_END: return BTN_MINUS | BTN_UP | BTN_CROSS | BTN_TICK;
    case SCR_DISCARD: return BTN_CROSS | BTN_TICK;
    case SCR_MESSAGE:
        switch (f->msg) {
        case MSG_DIDNT_CATCH:
        case MSG_NOT_ON_SHELF:
        case MSG_NO_WIFI: return BTN_CROSS | BTN_PLUS;
        default: return 0;
        }
    default: return 0;
    }
}

bool flow_peek(const flow_t *f)
{
    switch (f->screen) {
    case SCR_CLOCK:
    case SCR_TIMER:
    case SCR_SAVED: return true;
    case SCR_MESSAGE: return f->msg == MSG_SAVED_ON_CLOCK || f->msg == MSG_SETUP_NEEDED;
    default: return false;
    }
}

bool flow_timed_out(const flow_t *f, uint32_t ms_untouched)
{
    return f->screen != SCR_DARK && !flow_peek(f) && !f->busy && ms_untouched >= FLOW_TIMEOUT_MS;
}

bool flow_net_allowed(const flow_t *f)
{
    switch (f->screen) {
    case SCR_LISTEN: return true;
    case SCR_CHECK_END:
    case SCR_DISCARD: return f->session->phase == S_ENDED || f->busy;
    case SCR_MESSAGE: return f->busy; /* the time sync */
    default: return false;
    }
}

bool flow_tick_enabled(const flow_t *f)
{
    return f->screen != SCR_CHECK_END || f->page >= f->session->page_start;
}

unsigned flow_under_lock(unsigned a)
{
    return (a & A_SAVE_OUTBOX) ? a & (A_SAVE_OUTBOX | A_SAVE_SESSION) : 0;
}
