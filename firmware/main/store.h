#pragma once
#include <stdbool.h>

#include "core/recent.h"
#include "core/session.h"
#include "core/shelf.h"

/* The last fetched shelf, cached in NVS (namespace "store", blob "shelf",
 * format in core/shelf.h). NVS must be initialised (config_init()). */

/* True and *s filled from the cache; false (*s empty) when there is no
 * cache or it is of another version or size. */
bool store_load_shelf(shelf_t *s);

/* Replaces the cache; logs on failure. */
void store_save_shelf(const shelf_t *s);

/* The open session (blob "session") and the outbox (blob "outbox"), formats
 * in core/session.h. Each save writes NVS, so the data survives a flat
 * battery, and mirrors it in RTC memory, which survives deep sleep. Save on
 * every phase change; save an outbox item as O_SENDING before its request.
 * The session: main task only. The outbox: main task, or the flow worker
 * while it holds the outbox lock (main/post.h); the main task holds it too
 * whenever the worker may be posting. */

/* Writes NVS and the RTC mirror; logs on failure. */
void store_save_session(const session_t *s);

/* From the RTC mirror after a deep sleep, else from NVS. True and *s
 * filled when one was found; false (*s S_IDLE) when there is none or it is
 * of another version or size. A failed NVS read (any error but "not found")
 * also gives false and S_IDLE, logged, but leaves the RTC mirror invalid so
 * the next load reads NVS again. */
bool store_load_session(session_t *s);

/* True when NVS took it (the RTC mirror is always written). */
bool store_outbox_save(const outbox_item_t items[OUTBOX_MAX]);

/* Like store_load_session, for the outbox (all O_EMPTY when none). For boot:
 * any item still O_SENDING becomes O_CHECK, and that is saved at once. */
bool store_outbox_load(outbox_item_t items[OUTBOX_MAX]);

/* The books the clock last logged (blob "recent", core/recent.h). Load:
 * true and filled when found; false (all empty) otherwise. Main task only. */
bool store_load_recent(recent_t r[RECENT_MAX]);
void store_save_recent(const recent_t r[RECENT_MAX]);

