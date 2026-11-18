#include "flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "config.h"
#include "core/layout.h"
#include "core/timefmt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "post.h"
#include "readwise.h"
#include "rtc.h"
#include "store.h"
#include "voice.h"

static const char *TAG = "flow";

static flow_t f;
static session_t session;
/* The outbox belongs to the main task; the worker posts from it while it
 * holds outbox_lock (main/post.h), and so the main task takes the lock for
 * every read or write of outbox[] or plan once the worker runs. */
static outbox_item_t outbox[OUTBOX_MAX];
static flush_t plan; /* what posting may still do this wake */
static SemaphoreHandle_t outbox_lock;
static post_outbox_t shared = {.items = outbox, .plan = &plan};
static recent_t recent[RECENT_MAX];
static shelf_t *shelf; /* the shelf of the last start ask; f.shelf borrows it */

/* Jobs for the worker, and what comes back */
typedef enum { J_ASK, J_SYNC, J_POST, J_NET_DOWN } job_kind_t;
typedef struct {
    job_kind_t kind;
    flow_ask_t ask;
    bool keep_net; /* leave Wi-Fi up after a good answer / a good sync */
    bool late;     /* J_NET_DOWN for an ask given up on: only if it kept Wi-Fi */
    uint32_t seq;
    session_t watch; /* J_POST: the session just saved */
} job_t;
typedef struct {
    job_kind_t kind;
    uint32_t seq;
    flow_result_t result;
    heard_t heard;
    shelf_t *shelf; /* a start ask's shelf; the main task takes it */
    bool ok;        /* J_SYNC: Wi-Fi up and time set; J_POST: sent */
    bool net_kept;
} done_t;

static QueueHandle_t jobs, dones;
static int jobs_open;              /* queued or running; __atomic */
static volatile bool net_on;       /* written by the worker only */
static uint32_t kept_by;           /* worker: the ask or sync that kept Wi-Fi up */
static volatile uint32_t rec_seq;  /* the ask the two flags below belong to */
static volatile bool rec_started, rec_ended, stop_early;
static uint32_t seq;               /* the ask the flow waits for */
static bool started_seen, ended_seen;
static int64_t rec_ended_at, last_touch, timer_drawn_at;
static int lowest_heap_logged;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static const char *const SCREEN[] = {"dark", "clock", "timer", "listening", "start check",
                                     "end check", "discard", "message", "saved"};
static const char *const EVENT[] = {"wake", "tap", "plus", "stop", "tick", "cross", "minus",
                                    "page plus", "minus ten", "page plus ten", "pick",
                                    "recording started", "recording ended", "result", "synced",
                                    "posted", "faded", "timeout"};
static const char *const RESULT[] = {"ok", "too quiet", "no Wi-Fi", "not ok", "no shelf"};
static const char *const ASK[] = {"start", "page", "end"};

static void log_heap(const char *where)
{
    ESP_LOGI(TAG, "%s: heap free %u, lowest ever %u", where, (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}

/* ---------------------------------------------------------------- worker */

static SemaphoreHandle_t net_joined;
static volatile bool net_result;

static void join_task(void *arg)
{
    net_result = net_up(20000);
    xSemaphoreGive(net_joined);
    vTaskDelete(NULL);
}

static void wifi_down(void)
{
    if (!net_on) return;
    net_down();
    net_on = false;
    ESP_LOGI(TAG, "Wi-Fi down");
}

/* The cached shelf, fetched again first when 24 h old and Wi-Fi is up.
 * NULL when there is none. */
static shelf_t *start_shelf(void)
{
    shelf_t *sh = malloc(sizeof(*sh));
    if (!sh) return NULL;
    bool cached = store_load_shelf(sh);
    time_t now = 0;
    rtc_get_utc(&now);
    if (net_on && shelf_stale(sh->fetched_at, now)) {
        /* readwise_fetch_shelf empties its output first: fetch into scratch */
        shelf_t *fresh = malloc(sizeof(*fresh));
        if (fresh && readwise_fetch_shelf(fresh)) {
            *sh = *fresh;
            store_save_shelf(sh);
            cached = true;
            ESP_LOGI(TAG, "shelf fetched again: %u books", sh->count);
        } else {
            ESP_LOGW(TAG, "shelf fetch failed; the cache (%s) is used", cached ? "present" : "none");
        }
        free(fresh);
    }
    if (!cached || sh->count == 0) {
        free(sh);
        return NULL;
    }
    return sh;
}

static void run_ask(const job_t *j, done_t *d)
{
    d->result = R_NOT_OK;
    d->heard = (heard_t){.title = "", .page = -1};
    rec_started = rec_ended = stop_early = false;
    rec_seq = j->seq; /* after the reset: the flags now belong to this ask */
    ESP_LOGI(TAG, "ask %s: %s", ASK[j->ask],
             net_on ? "Wi-Fi already up, recording" : "Wi-Fi joining while recording");
    bool joining = false;
    if (!net_on) {
        joining = xTaskCreate(join_task, "join", 6144, NULL, 4, NULL) == pdPASS;
        if (!joining) ESP_LOGE(TAG, "no memory for the Wi-Fi task");
    }
    clip_t clip = {0};
    bool ok = audio_record(&clip, AUDIO_MAX_MS, &stop_early, &rec_started);
    rec_ended = true;
    if (joining) {
        xSemaphoreTake(net_joined, portMAX_DELAY);
        net_on = net_result;
    }
    if (net_on) post_flush_pending(&shared, NULL); /* Wi-Fi is up anyway: send what waits */
    if (!ok) {
        ESP_LOGE(TAG, "recording failed");
    } else if (clip.peak < CLIP_TOO_QUIET) {
        ESP_LOGW(TAG, "too quiet: peak %d under %d; nothing sent", clip.peak, CLIP_TOO_QUIET);
        d->result = R_TOO_QUIET;
    } else if (!net_on) {
        ESP_LOGW(TAG, "no Wi-Fi; nothing sent");
        d->result = R_NO_WIFI;
    } else {
        if (j->ask == ASK_START) d->shelf = start_shelf();
        if (j->ask == ASK_START && !d->shelf) {
            d->result = R_NO_SHELF;
        } else {
            log_heap("before the voice request");
            d->result = voice_ask(&clip, j->ask == ASK_START, d->shelf, &d->heard)
                            ? R_OK : R_NOT_OK;
            log_heap("after the voice request");
        }
    }
    audio_clip_free(&clip);
    if (!(j->keep_net && d->result == R_OK)) wifi_down();
    d->net_kept = net_on;
    if (net_on) kept_by = j->seq;
}

static void run_sync(const job_t *j, done_t *d)
{
    if (!net_on) net_on = net_up(20000);
    d->ok = net_on && net_time_synced();
    ESP_LOGI(TAG, "sync: Wi-Fi %s, time %s", net_on ? "up" : "down",
             d->ok ? "set" : "NOT set");
    if (net_on) post_flush_pending(&shared, NULL); /* Wi-Fi is up anyway: send what waits */
    if (!(j->keep_net && d->ok)) wifi_down();
    d->net_kept = net_on;
    if (net_on) kept_by = j->seq;
}

static void worker(void *arg)
{
    job_t j;
    for (;;) {
        xQueueReceive(jobs, &j, portMAX_DELAY);
        done_t d = {.kind = j.kind, .seq = j.seq};
        switch (j.kind) {
        case J_ASK: run_ask(&j, &d); break;
        case J_SYNC: run_sync(&j, &d); break;
        case J_POST:
            if (net_on) d.ok = post_flush_pending(&shared, &j.watch);
            else ESP_LOGI(TAG, "post: Wi-Fi is down; the session waits on the clock");
            wifi_down();
            break;
        case J_NET_DOWN:
            /* An ask given up on brings Wi-Fi down only if Wi-Fi is still
             * its doing, not a newer ask's kept for the post */
            if (j.late && kept_by != j.seq) ESP_LOGI(TAG, "late Wi-Fi down skipped: kept by a newer job");
            else wifi_down();
            break;
        }
        if (j.kind != J_NET_DOWN) xQueueSend(dones, &d, portMAX_DELAY);
        /* After the ask's result is on its way (every path: answered, failed,
         * too quiet, given up on), so the screen never waits for it */
        if (j.kind == J_ASK) audio_clip_erase();
        __atomic_fetch_sub(&jobs_open, 1, __ATOMIC_SEQ_CST);
    }
}

static void push_job(const job_t *jp)
{
    job_t j = *jp;
    __atomic_fetch_add(&jobs_open, 1, __ATOMIC_SEQ_CST);
    if (xQueueSend(jobs, &j, 0) != pdTRUE) {
        __atomic_fetch_sub(&jobs_open, 1, __ATOMIC_SEQ_CST);
        ESP_LOGE(TAG, "job queue full");
    }
}

static void push(job_kind_t kind, flow_ask_t ask, bool keep_net)
{
    job_t j = {.kind = kind, .ask = ask, .keep_net = keep_net, .seq = seq};
    push_job(&j);
}

static int open_jobs(void) { return __atomic_load_n(&jobs_open, __ATOMIC_SEQ_CST); }

/* ----------------------------------------------------------------- draw */

static time_t rtc_now(void)
{
    time_t t;
    if (!rtc_get_utc(&t)) return 0;
    return t;
}

static void draw_timer(void)
{
    char clock[6] = "--:--", elapsed[9] = "--:--";
    time_t now = rtc_now();
    if (now) {
        struct tm local;
        localtime_r(&now, &local);
        fmt_clock12(local.tm_hour, local.tm_min, clock);
        fmt_elapsed(now > session.started_at ? (uint32_t)(now - session.started_at) : 0, elapsed);
    }
    ui_show_timer(clock, session.title, elapsed);
    timer_drawn_at = now_ms();
}

/* "<n> pages · <m> min" for the session just saved */
static void pages_line(char *out, size_t cap)
{
    int n = f.done.page_end - f.done.page_start;
    time_t secs = f.done.ended_at - f.done.started_at;
    snprintf(out, cap, "%d %s \xC2\xB7 %lu min", n, n == 1 ? "page" : "pages",
             (unsigned long)minutes_rounded(secs > 0 ? (uint32_t)secs : 0));
}

static void draw(void)
{
    char small[48] = "";
    last_touch = now_ms(); /* a new screen's 30 s start now */
    if (f.screen == SCR_DARK) {
        ui_dark();
        return;
    }
    ui_hold(!flow_peek(&f));
    ui_set_buttons(flow_buttons(&f));
    switch (f.screen) {
    case SCR_CLOCK: ui_show_clock(); break;
    case SCR_TIMER: draw_timer(); break;
    case SCR_LISTEN:
        ui_show_listening(flow_question(f.ask));
        ui_set_listening(f.listening);
        break;
    case SCR_CHECK_START:
        ui_show_check_start(f.shelf->books[f.a].title, f.page,
                            f.b >= 0 ? flow_alt_title(f.shelf, f.a, f.b) : "");
        ui_set_note(flow_page_note(f.src));
        break;
    case SCR_CHECK_END: {
        time_t secs = session.ended_at - session.started_at;
        ui_show_check_end(session.title, (int)minutes_rounded(secs > 0 ? (uint32_t)secs : 0),
                          session.page_start, f.page);
        break;
    }
    case SCR_DISCARD: ui_show_message("Discard session?", ""); break;
    case SCR_MESSAGE:
        if (f.msg == MSG_SAVED_ON_CLOCK) pages_line(small, sizeof(small));
        ui_show_message(flow_message(f.msg), small);
        break;
    case SCR_SAVED:
        pages_line(small, sizeof(small));
        ui_show_message("Saved", small);
        break;
    default: break;
    }
}

/* -------------------------------------------------------------- the flow */

/* After handle() did the saves flow_under_lock() names, under the lock */
static void act(unsigned a)
{
    if (a & A_SAVE_SESSION) store_save_session(&session);
    if (a & A_SAVE_RECENT) store_save_recent(recent);
    if (a & A_STOP_REC) {
        stop_early = true;
        ESP_LOGI(TAG, "recording stopped by a tap");
    }
    if (a & A_ASK) {
        seq++;
        started_seen = ended_seen = false;
        push(J_ASK, f.ask, f.ask == ASK_END);
    }
    if (a & A_SYNC) push(J_SYNC, f.ask, f.pending == PEND_STOP);
    if (a & A_POST) {
        job_t j = {.kind = J_POST, .ask = f.ask, .seq = seq, .watch = f.done};
        push_job(&j);
    }
    if ((a & A_NET_DOWN) && (net_on || open_jobs())) push(J_NET_DOWN, f.ask, false);
    if (a & (A_DRAW | A_DARK)) draw();
}

static void handle(flow_event_t ev, const flow_in_t *given)
{
    flow_in_t in = given ? *given : (flow_in_t){.page = -1};
    in.now = rtc_now();
    f.setup_done = config_complete();
    flow_screen_t before = f.screen;
    xSemaphoreTake(outbox_lock, portMAX_DELAY);
    /* A new wake may post again after a 401/403/429 */
    if (ev == EV_WAKE && !f.busy && !open_jobs()) flush_wake(&plan);
    unsigned a = flow_event(&f, ev, &in);
    /* The outbox is written before the session is cleared, and both before
     * the lock goes: a flush cannot post the new item while NVS still holds
     * the ended session. NVS writes only; no request is made under it. */
    unsigned locked = flow_under_lock(a);
    if (locked & A_SAVE_OUTBOX) store_outbox_save(outbox);
    if (locked & A_SAVE_SESSION) store_save_session(&session);
    xSemaphoreGive(outbox_lock);
    if (ev == EV_FADED && !a && before == f.screen) return; /* the usual end of a peek */
    ESP_LOGI(TAG, "%s: %s -> %s%s%s, actions 0x%03x", EVENT[ev], SCREEN[before], SCREEN[f.screen],
             f.screen == SCR_MESSAGE ? ": " : "",
             f.screen == SCR_MESSAGE ? flow_message(f.msg) : "", a);
    act(a & ~locked); /* the rest */
}

void flow_init(void)
{
    outbox_lock = xSemaphoreCreateMutex();
    if (!outbox_lock) abort();
    shared.lock = outbox_lock;
    flush_wake(&plan);
    ESP_LOGI(TAG, "posting sessions: %s", BEDSIDE_CLOCK_POSTING
                                              ? "ON (BEDSIDE_CLOCK_POSTING 1)"
                                              : "off (BEDSIDE_CLOCK_POSTING 0)");
    store_load_session(&session);
    store_outbox_load(outbox); /* an item left O_SENDING becomes O_CHECK */
    store_load_recent(recent);
    f.session = &session;
    f.outbox = outbox;
    f.recent = recent;
    flow_reset(&f);
    jobs = xQueueCreate(4, sizeof(job_t));
    dones = xQueueCreate(4, sizeof(done_t));
    net_joined = xSemaphoreCreateBinary();
    /* Stack as the SAY task had for the same voice request */
    if (!jobs || !dones || !net_joined ||
        xTaskCreate(worker, "flow", 10240, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "no memory for the flow worker");
        abort();
    }
}

void flow_wake(void)
{
    handle(EV_WAKE, NULL);
}

static flow_event_t ui_to_flow(ui_event_t ev)
{
    switch (ev) {
    case UI_PLUS: return EV_PLUS;
    case UI_STOP: return EV_STOP;
    case UI_TICK: return EV_TICK;
    case UI_CROSS: return EV_CROSS;
    case UI_MINUS: return EV_MINUS;
    case UI_PLUS_PAGE: return EV_UP;
    case UI_MINUS_TEN: return EV_MINUS10;
    case UI_PLUS_TEN: return EV_UP10;
    case UI_PICK_B: return EV_PICK;
    case UI_FADED: return EV_FADED;
    case UI_WAKE: return EV_WAKE;
    default: return EV_TAP;
    }
}

void flow_poll(ui_event_t ev)
{
    int64_t now = now_ms();
    if (ev != UI_NONE) {
        if (ev != UI_FADED) last_touch = now;
        flow_event_t e = ui_to_flow(ev);
        /* Any touch on a dark (or darkening) screen wakes */
        if (f.screen == SCR_DARK && ev != UI_FADED) e = EV_WAKE;
        handle(e, NULL);
    }

    /* The worker's recording */
    if (f.screen == SCR_LISTEN && f.busy && rec_seq == seq) {
        if (rec_started && !started_seen) {
            started_seen = true;
            handle(EV_REC_STARTED, NULL);
        }
        if (rec_ended && !ended_seen) {
            ended_seen = true;
            rec_ended_at = now;
            handle(EV_REC_ENDED, NULL);
        }
    }
    if (f.screen == SCR_LISTEN && f.busy && ended_seen && now - rec_ended_at >= FLOW_ASK_GIVEUP_MS) {
        ESP_LOGW(TAG, "no answer %d ms after the recording: giving up", FLOW_ASK_GIVEUP_MS);
        seq++; /* its late answer is dropped */
        flow_in_t in = {.result = R_NOT_OK, .page = -1};
        handle(EV_RESULT, &in);
    }

    done_t d;
    while (xQueueReceive(dones, &d, 0) == pdTRUE) {
        if (d.kind == J_ASK) {
            if (d.seq != seq) {
                ESP_LOGW(TAG, "a late answer (%s) dropped", RESULT[d.result]);
                free(d.shelf);
                if (d.net_kept) {
                    job_t j = {.kind = J_NET_DOWN, .ask = f.ask, .late = true, .seq = d.seq};
                    push_job(&j);
                }
                continue;
            }
            if (d.shelf) {
                free(shelf);
                shelf = d.shelf;
                f.shelf = shelf;
            }
            ESP_LOGI(TAG, "ask %s: %s, title \"%s\", page %d", ASK[f.ask], RESULT[d.result],
                     d.heard.title, d.heard.page);
            flow_in_t in = {.result = d.result, .title = d.heard.title, .page = d.heard.page};
            handle(EV_RESULT, &in);
        } else {
            flow_in_t in = {.ok = d.ok, .page = -1};
            handle(d.kind == J_SYNC ? EV_SYNCED : EV_POSTED, &in);
        }
    }

    /* draw() may have restarted last_touch since `now` was read */
    int64_t untouched = now_ms() - last_touch;
    if (flow_timed_out(&f, untouched > 0 ? (uint32_t)untouched : 0)) {
        ESP_LOGI(TAG, "%d s untouched", FLOW_TIMEOUT_MS / 1000);
        handle(EV_TIMEOUT, NULL);
    }
    if (f.screen == SCR_TIMER && !ui_idle() && now - timer_drawn_at >= 1000) draw_timer();
    /* Wi-Fi only where the flow allows it */
    if (net_on && !open_jobs() && !flow_net_allowed(&f)) {
        ESP_LOGI(TAG, "Wi-Fi not needed on the %s screen: down", SCREEN[f.screen]);
        push(J_NET_DOWN, f.ask, false);
    }
    int low = (int)esp_get_minimum_free_heap_size();
    if (open_jobs() && (lowest_heap_logged == 0 || low < lowest_heap_logged - 4096)) {
        lowest_heap_logged = low;
        log_heap("during a job");
    }
}

bool flow_busy(void) { return open_jobs() > 0 || net_on; }


void flow_log(void)
{
    static const char *const PHASE[] = {"S_IDLE", "S_READING", "S_ENDED"};
    static const char *const STATE[] = {"O_EMPTY", "O_PENDING", "O_SENDING", "O_CHECK"};
    ESP_LOGI(TAG, "screen %s%s%s, ask %s, busy %d, jobs %d, Wi-Fi %s, page %u", SCREEN[f.screen],
             f.screen == SCR_MESSAGE ? ": " : "", f.screen == SCR_MESSAGE ? flow_message(f.msg) : "",
             ASK[f.ask], f.busy, open_jobs(), net_on ? "up" : "down", f.page);
    ESP_LOGI(TAG, "session %s, book %lu \"%s\", pages %u-%u, started_at %lld, ended_at %lld",
             PHASE[session.phase], (unsigned long)session.book_id, session.title,
             session.page_start, session.page_end, (long long)session.started_at,
             (long long)session.ended_at);
    xSemaphoreTake(outbox_lock, portMAX_DELAY);
    int queued = 0;
    for (int i = 0; i < OUTBOX_MAX; i++) {
        const outbox_item_t *o = &outbox[i];
        if (o->state == O_EMPTY) continue;
        queued++;
        ESP_LOGI(TAG, "outbox %d: %s, book %lu, pages %u-%u, started_at %lld", i,
                 STATE[o->state], (unsigned long)o->s.book_id, o->s.page_start, o->s.page_end,
                 (long long)o->s.started_at);
    }
    bool stopped = plan.stopped;
    xSemaphoreGive(outbox_lock);
    ESP_LOGI(TAG, "outbox: %d item(s); posting %s%s", queued,
             BEDSIDE_CLOCK_POSTING ? "on" : "off (BEDSIDE_CLOCK_POSTING 0)",
             stopped ? ", stopped this wake (401/403/429)" : "");
    shelf_t *sh = malloc(sizeof(*sh));
    if (sh) {
        bool cached = store_load_shelf(sh);
        ESP_LOGI(TAG, "shelf cache: %s, %u books, fetched_at %lld", cached ? "present" : "none",
                 sh->count, (long long)sh->fetched_at);
        free(sh);
    }
    for (int i = 0; i < RECENT_MAX; i++)
        if (recent[i].ended_at)
            ESP_LOGI(TAG, "recent %d: book %lu, last page %u, ended_at %lld", i,
                     (unsigned long)recent[i].book_id, recent[i].page_end,
                     (long long)recent[i].ended_at);
}
