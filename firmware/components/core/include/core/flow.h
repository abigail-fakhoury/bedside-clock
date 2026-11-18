#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "core/match.h"
#include "core/recent.h"
#include "core/session.h"
#include "core/shelf.h"

/* The session flow: which screen follows which event. The spec's eight
 * steps: tap to wake (clock, plus); plus asks "Which book, what page?";
 * the check screen; reading (dark); a tap peeks at the timer (stop); stop
 * asks "What page did you stop on?"; the end check; saved.
 *
 * Pure: the caller (main/flow.c) draws flow_t.screen, runs the jobs the
 * returned actions ask for (recording, Wi-Fi, the voice request, NVS
 * writes) and feeds their outcomes back as events. A session starts only
 * on the tick of the start check (or a time sync that the tick asked for)
 * and is saved only on the tick of the end check. */

typedef enum {
    SCR_DARK,        /* panel asleep */
    SCR_CLOCK,       /* time, battery ring, plus; fades */
    SCR_TIMER,       /* while reading: elapsed, time, title, stop; fades */
    SCR_LISTEN,      /* the question; "Listening" while the mic runs */
    SCR_CHECK_START, /* "Start reading?" title, page, minus/plus, cross, tick */
    SCR_CHECK_END,   /* "212 -> 240, 42 min", minus/plus, cross, tick */
    SCR_DISCARD,     /* "Discard session?" cross, tick */
    SCR_MESSAGE,     /* flow_t.msg */
    SCR_SAVED,       /* "Saved", "<n> pages · <m> min"; fades */
} flow_screen_t;

typedef enum {
    ASK_START, /* "Which book, what page?" */
    ASK_PAGE,  /* "What page?": a title was heard with no page to fall back on */
    ASK_END,   /* "What page did you stop on?" */
} flow_ask_t;

typedef enum {
    MSG_DIDNT_CATCH,    /* "Didn't catch that" */
    MSG_NOT_ON_SHELF,   /* "Not on your shelf" */
    MSG_NO_WIFI,        /* "No Wi-Fi" */
    MSG_NO_TIME,        /* "No time yet" (while the time syncs) */
    MSG_CHECK_READWISE, /* "Check Readwise" */
    MSG_SAVED_ON_CLOCK, /* "Saved on clock" */
    MSG_SETUP_NEEDED,   /* "Setup needed" */
} flow_msg_t;

/* Where the start page came from: the check screen says so unless heard. */
typedef enum { PAGE_HEARD, PAGE_CLOCK, PAGE_READWISE, PAGE_SET } page_src_t;

/* How a voice job ended. R_NOT_OK covers a failed recording, a request that
 * failed or gave no usable reply, and giving up after FLOW_ASK_GIVEUP_MS. */
typedef enum { R_OK, R_TOO_QUIET, R_NO_WIFI, R_NOT_OK, R_NO_SHELF } flow_result_t;

/* What a time sync was for. */
typedef enum { PEND_NONE, PEND_START, PEND_STOP } flow_pending_t;

typedef enum {
    EV_WAKE,  /* a touch or button while dark */
    EV_TAP,   /* a touch on no button */
    EV_PLUS,  /* BTN_PLUS */
    EV_STOP,
    EV_TICK,
    EV_CROSS,
    EV_MINUS, /* page - 1 */
    EV_UP,    /* page + 1 */
    EV_MINUS10, /* held: page - 10 */
    EV_UP10,
    EV_PICK,
    EV_REC_STARTED, /* the mic runs */
    EV_REC_ENDED,
    EV_RESULT,  /* a voice job ended: flow_in_t result, title, page */
    EV_SYNCED,  /* a time sync ended: flow_in_t ok (Wi-Fi up and time set) */
    EV_POSTED,  /* the post attempt after a save ended: flow_in_t ok (sent) */
    EV_FADED,   /* a fading screen went dark */
    EV_TIMEOUT, /* flow_timed_out() */
} flow_event_t;

/* Actions, a mask returned by flow_event */
enum {
    A_DRAW = 1 << 0,         /* draw flow_t.screen (it changed or its content did) */
    A_ASK = 1 << 1,          /* start a voice job for flow_t.ask */
    A_STOP_REC = 1 << 2,     /* end the recording now */
    A_SYNC = 1 << 3,         /* Wi-Fi up and set the time; keep Wi-Fi for PEND_STOP */
    A_SAVE_SESSION = 1 << 4, /* write *session */
    A_SAVE_OUTBOX = 1 << 5,  /* write the outbox (before the session) */
    A_SAVE_RECENT = 1 << 6,
    A_POST = 1 << 7,         /* try to post pending sessions, then Wi-Fi down */
    A_NET_DOWN = 1 << 8,
    A_DARK = 1 << 9,         /* fade out now */
};

/* The save actions of mask `a` that the device does while it holds the
 * outbox lock (main/flow.c), so a flush running on the worker never sees
 * one without the other: the outbox and, whenever the outbox is saved, the
 * session with it. Then "item queued" and "session cleared" are one step,
 * and a flush cannot post and empty the new item while NVS still holds the
 * S_ENDED session (a power cut there would queue it again on the next tick:
 * a duplicate post). 0 when the outbox is not saved. Never a slow or
 * network action: the lock is never held across a request. */
unsigned flow_under_lock(unsigned a);

/* A screen waiting for a decision goes dark after this long untouched. */
enum { FLOW_TIMEOUT_MS = 30000 };
/* Waiting for the voice reply after the recording ends: give up after this. */
enum { FLOW_ASK_GIVEUP_MS = 30000 };

typedef struct {
    /* Borrowed, owned by the caller; set before the first event */
    session_t *session;
    outbox_item_t *outbox; /* OUTBOX_MAX */
    recent_t *recent;      /* RECENT_MAX */
    const shelf_t *shelf;  /* set before EV_RESULT of an ASK_START; may be NULL */
    bool setup_done;       /* every setting is stored */

    flow_screen_t screen;
    flow_msg_t msg;
    flow_ask_t ask;        /* the current or last voice job */
    flow_pending_t pending;
    bool busy;             /* a job runs: no timeout, buttons ignored */
    bool listening;        /* the mic runs */
    bool expired;          /* SCR_DISCARD offered for a session over 24 h */
    int fails;             /* failed end-page asks */
    int8_t a, b;           /* SCR_CHECK_START: shelf indexes; b -1 = one book */
    int heard_page;        /* -1 = none */
    uint16_t page;         /* the start page, or the end page at SCR_CHECK_END */
    page_src_t src;
    session_t done;        /* the session just saved, for the saved screens */
} flow_t;

typedef struct {
    time_t now;           /* RTC UTC; 0 when unset */
    flow_result_t result; /* EV_RESULT */
    const char *title;    /* EV_RESULT; "" or NULL = none */
    int page;             /* EV_RESULT; -1 = none */
    bool ok;              /* EV_SYNCED, EV_POSTED */
} flow_in_t;

/* Clears the state to SCR_DARK, keeping the borrowed pointers. */
void flow_reset(flow_t *f);

/* Handles one event; returns the actions. Unexpected events change nothing
 * and return 0. */
unsigned flow_event(flow_t *f, flow_event_t ev, const flow_in_t *in);

/* The buttons on the current screen (layout.h mask). */
unsigned flow_buttons(const flow_t *f);

/* The current screen fades after one showing (180 + 2470 + 350 ms) as the
 * clock does: clock, timer, saved, "Saved on clock", "Setup needed". Every
 * other lit screen waits for a decision. */
bool flow_peek(const flow_t *f);

/* A lit screen that waits for a decision, untouched for FLOW_TIMEOUT_MS,
 * with no job running. Never the dark screen or a fading one. */
bool flow_timed_out(const flow_t *f, uint32_t ms_untouched);

/* Wi-Fi may stay up on this screen: from plus to the start check, and from
 * the end-page recording until saved. */
bool flow_net_allowed(const flow_t *f);

/* The end check's tick works: end page >= start page. */
bool flow_tick_enabled(const flow_t *f);

/* The question on SCR_LISTEN for an ask. */
const char *flow_question(flow_ask_t ask);

/* The copy of a message. */
const char *flow_message(flow_msg_t msg);

/* The small line under the start page saying where it came from, or "" when
 * it was heard or set by hand. */
const char *flow_page_note(page_src_t src);

/* Decisions, exposed for tests and the caller */

/* page + delta, kept within 1-9999. */
uint16_t page_step(uint16_t page, int delta);

/* Ten-page steps owed to a press of minus or plus held for held_ms: one per
 * full 300 ms. */
int hold_tens(uint32_t held_ms);

/* The start page: the heard page (1-9999), else the clock's own last end
 * page for the book, else book->last_page, else 0 (ask "What page?"). */
uint16_t flow_start_page(int heard_page, const recent_t recent[RECENT_MAX], const book_t *book,
                         page_src_t *src);

/* Of two matched books, the one the clock last logged a session for goes
 * first; otherwise the order is kept. */
match_t flow_order(match_t m, const shelf_t *shelf, const recent_t recent[RECENT_MAX]);

/* The second book's line: its title, or "the other copy" when the titles
 * are the same. */
const char *flow_alt_title(const shelf_t *shelf, int a, int b);

/* The cached shelf is too old (24 h or more, or of unknown age, or from
 * the future) to use without fetching it again. */
bool shelf_stale(time_t fetched_at, time_t now);
