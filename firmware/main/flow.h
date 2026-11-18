#pragma once
#include <stdbool.h>

#include "core/flow.h"
#include "core/session.h"
#include "ui.h"

/* The session flow on the device: core/flow.c decides, this runs it. Owns
 * the open session, the outbox, the clock's recent books and the shelf in
 * use, and one worker task that does everything slow (recording, Wi-Fi, the
 * voice request, the time sync, the post) so the screen keeps running. The
 * worker alone brings Wi-Fi up and down, and brings it down after every job
 * unless the flow keeps it (end page heard, until saved). */

/* Loads the session, outbox (O_SENDING becomes O_CHECK) and recent books,
 * and starts the worker. After config_init(), before ui_init(). */
void flow_init(void);

/* A wake (boot by touch, or UI_WAKE): shows what the flow says (the clock,
 * the timer, "Check Readwise", the end check, "Discard session?"). */
void flow_wake(void);

/* Every main-loop pass, with ui_poll()'s event: feeds it to the flow, picks
 * up the worker's results, gives up on a voice reply after
 * FLOW_ASK_GIVEUP_MS, times waiting screens out after FLOW_TIMEOUT_MS,
 * refreshes the timer and brings Wi-Fi down wherever the flow does not
 * allow it. */
void flow_poll(ui_event_t ev);

/* A job runs or is queued, or Wi-Fi is up: no deep sleep. */
bool flow_busy(void);

/* Logs the screen, the session, the outbox, the shelf cache and the recent
 * books (the FLOW command). Read-only. */
void flow_log(void);
