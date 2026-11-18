#pragma once
#include <stdbool.h>
#include <stdint.h>

/* What ui_poll() reports. A button event comes from a tap on that button
 * (core/layout.h); UI_PICK_A is unused (the first book is the one shown). */
typedef enum {
    UI_NONE,
    UI_PLUS,
    UI_STOP,
    UI_TICK,
    UI_CROSS,
    UI_MINUS,
    UI_PLUS_PAGE,
    UI_PICK_A,
    UI_PICK_B,
    UI_MINUS_TEN, /* minus held: one per 300 ms */
    UI_PLUS_TEN,
    UI_TAP,       /* a touch (or BOOT) on no button */
    UI_WAKE,      /* a touch or button while dark: show something */
    UI_FADED,     /* a fading screen just went dark */
} ui_event_t;

/* Builds the clock screen: red seven-segment time, battery ring, the plus
 * button and, until config_complete(), a small "Setup needed" under the
 * digits. Call after display_init() and touch_init(). */
void ui_init(void);

/* Wake or tap: wake the panel, rotate to the current orientation, read the
 * time and battery, draw, then fade in from the current brightness and
 * restart the hold. Logs orientation and battery. */
void ui_show_clock(void);

/* The session screens, laid out upright and turned with the board as they
 * go to the panel. Pure black, one red; text sizes 48 for the main line,
 * 20 for the top line, 24 for titles and small lines. Calling one for the
 * screen already lit redraws its content in place (no fade restart);
 * otherwise it is presented (faded in from dark). Titles go through
 * text_ascii. Set ui_hold() and ui_set_buttons() alongside. */

/* The question in the main line; ui_set_listening(true) adds "Listening"
 * on the top line once the mic runs. */
void ui_show_listening(const char *question);
void ui_set_listening(bool on);

/* "Start reading?", the title (up to two lines), "page <n>", and when
 * alt_title is not empty "or <alt_title>" under it (the BTN_PICK line).
 * ui_set_note() adds a small line under the page ("" for none). */
void ui_show_check_start(const char *title, int page, const char *alt_title);
void ui_set_note(const char *note);

/* While reading: the time small on top, elapsed in the main line, the
 * title under it. */
void ui_show_timer(const char *clock, const char *title, const char *elapsed);

/* The title on top, "<from> -> <to>" (a drawn arrow) and "<minutes> min";
 * the tick is drawn dim while to < from. */
void ui_show_check_end(const char *title, int minutes, int from, int to);

/* big in the main line, small under it ("" for none). A small line of the
 * form "<a> · <b>" is drawn with the dot drawn (the font has no middle dot). */
void ui_show_message(const char *big, const char *small);

/* The buttons drawn and hit-tested on the session screens (layout.h mask). */
void ui_set_buttons(unsigned mask);

/* true: the screen stays lit until ui_dark(); false: it fades after one
 * showing (180 + 2470 + 350 ms), as the clock does. */
void ui_hold(bool wait);

/* Fade out now (350 ms), even while staying awake. */
void ui_dark(void);

/* True once the fade-out has finished and the panel is asleep. */
bool ui_idle(void);

/* KEY stay-awake is on. */
bool ui_held(void);

/* Handles taps, BOOT (a tap) and KEY (stay-awake toggle), steps the fade and,
 * while staying awake, refreshes time, orientation and battery each second.
 * While idle, a tap or button press returns UI_WAKE (and draws nothing: the
 * caller shows the right screen). A retap restarts a fading screen. Returns
 * one event per call. Call on every loop pass. */
ui_event_t ui_poll(void);

/* Re-reads the RTC and the settings and redraws if either changed (after
 * the time is set or a CFG line is stored). Shows or hides "Setup needed". */
void ui_refresh_time(void);

/* Logs the current display state. */
void ui_log_status(void);
