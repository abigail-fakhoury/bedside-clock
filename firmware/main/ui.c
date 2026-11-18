#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "board.h"
#include "config.h"
#include "core/digits.h"
#include "core/fade.h"
#include "core/flow.h"
#include "core/layout.h"
#include "core/plus.h"
#include "core/rect.h"
#include "core/ring.h"
#include "core/rot.h"
#include "core/setup.h"
#include "core/text.h"
#include "core/timefmt.h"
#include "display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "rtc.h"
#include "touch.h"

static const char *TAG = "ui";

/* Pure black and one red. The ring's lit and dim tints follow bedroom-clock's
 * 0x4000 / 0x0800 RGB565 levels of red. */
#define CLOCK_RED 0xE01515
#define RING_LIT 0x420000
#define RING_DIM 0x080000

enum {
    BRIGHTNESS = 128,                 /* bedroom-clock DEFAULT_BRIGHTNESS */
    PLUS_BORDER = 2,
    PLUS_ARM = 36,
    PLUS_STROKE = 4,
    REFRESH_MS = 1000,
};

/* What is on the panel: the clock face, or one of the session screens,
 * which are laid out upright and turned on their way to the panel */
typedef enum { M_CLOCK, M_LISTEN, M_CHECK_START, M_TIMER, M_CHECK_END, M_MESSAGE } ui_mode_t;

static struct {
    bool visible, held;
    ui_mode_t mode;
    bool wait;    /* stays lit until ui_dark() instead of fading */
    bool closing; /* ui_dark(): fading out even while held */
    int turns;
    unsigned from, level;
    int64_t shown_at, refreshed_at;
    /* what is drawn */
    bool valid;
    int hour12, minute, battery;
} s = {.battery = -1};

static lv_obj_t *screen, *plus, *setup, *clock_root;
static int setup_shown = -1; /* unknown until the first update */
static bool tapped, released, wake_key;
static unsigned press_btn;   /* the button under the last press, 0 = none */
static int64_t press_at;
static int tens_sent;        /* ten-page steps already reported for a held press */

/* Session screens (upright frame) */
static lv_obj_t *flow_root, *band, *lbl_top, *lbl_pre, *lbl_main, *lbl_sub, *lbl_note, *lbl_alt;
static lv_obj_t *row, *row_l, *row_mid, *row_r;
static unsigned btn_mask;
static bool tick_dim, mid_arrow;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void toggle_hold_now(void);

static void time_text(char out[6])
{
    if (s.valid) {
        snprintf(out, 6, "%d:%02d", s.hour12, s.minute);
    } else {
        strcpy(out, "--:--");
    }
}

static void draw_cb(lv_event_t *e)
{
    if (s.mode != M_CLOCK) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_rect_dsc_t box;
    lv_draw_rect_dsc_init(&box);
    box.bg_opa = LV_OPA_COVER;
    box.bg_color = lv_color_hex(CLOCK_RED);

    rect_t r[DIGITS_MAX_RECTS];
    int n = digits_rects(s.hour12, s.minute, s.valid, s.turns, r);
    for (int i = 0; i < n; i++) {
        lv_area_t a = {r[i].x, r[i].y, r[i].x + r[i].w - 1, r[i].y + r[i].h - 1};
        lv_draw_rect(layer, &box, &a);
    }

    ring_piece_t p[RING_MAX_PIECES];
    n = ring_pieces(s.battery, s.turns, p);
    for (int i = 0; i < n; i++) {
        lv_color_t color = lv_color_hex(p[i].lit ? RING_LIT : RING_DIM);
        if (p[i].arc) {
            lv_draw_arc_dsc_t arc;
            lv_draw_arc_dsc_init(&arc);
            arc.color = color;
            arc.width = RING_WIDTH;
            arc.center.x = p[i].cx;
            arc.center.y = p[i].cy;
            arc.radius = p[i].radius;
            arc.start_angle = p[i].start;
            arc.end_angle = p[i].end;
            lv_draw_arc(layer, &arc);
        } else {
            rect_t q = p[i].rect;
            lv_area_t a = {q.x, q.y, q.x + q.w - 1, q.y + q.h - 1};
            box.bg_color = color;
            lv_draw_rect(layer, &box, &a);
        }
    }
}

/* Every press lands on the screen object (nothing else is clickable).
 * On the clock the plus is hit-tested against its rotated touch target; on
 * a session screen the point is turned back upright and tested against the
 * buttons shown. A press while dark only wakes: ui_poll() drops it then. */
static void press(int x, int y)
{
    tapped = true;
    released = false;
    tens_sent = 0;
    press_at = now_ms();
    if (s.mode == M_CLOCK) {
        press_btn = plus_hit(x, y, s.turns) ? BTN_PLUS : 0;
    } else {
        int ux, uy;
        rot_point_upright(x, y, s.turns, &ux, &uy);
        press_btn = button_at(ux, uy, btn_mask);
    }
}

static void on_press(lv_event_t *e)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    press(p.x, p.y);
}

static void on_release(lv_event_t *e) { released = true; }

static void place_plus(void)
{
    rect_t r = plus_rect(s.turns);
    lv_obj_set_pos(plus, r.x, r.y);
}

/* The label keeps its upright w × h layout box, centred where the rotated
 * box goes, and turns about its centre */
static void place_setup(void)
{
    int w = lv_obj_get_width(setup), h = lv_obj_get_height(setup);
    rect_t r = setup_rect(w, h, s.turns);
    lv_obj_set_pos(setup, r.x + r.w / 2 - w / 2, r.y + r.h / 2 - h / 2);
    lv_obj_set_style_transform_pivot_x(setup, w / 2, 0);
    lv_obj_set_style_transform_pivot_y(setup, h / 2, 0);
    lv_obj_set_style_transform_rotation(setup, s.turns * 900, 0);
}

/* "Setup needed" while any setting is missing; logs each change */
static void update_setup(void)
{
    bool show = !config_complete();
    if (show == setup_shown) return;
    setup_shown = show;
    if (show) lv_obj_remove_flag(setup, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(setup, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "\"Setup needed\" %s", show ? "shown (settings incomplete)" : "hidden (settings complete)");
}

static lv_obj_t *bare(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* Session screens: buttons, the arrow and the middle dot are drawn here
 * (the fonts hold ASCII only), upright; the panel flush turns them */
static void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, int w, uint32_t color)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(color);
    d.width = w;
    d.round_start = d.round_end = 1;
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    lv_draw_line(layer, &d);
}

static void draw_button(lv_layer_t *layer, button_t b)
{
    rect_t r = button_rect(b);
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2, arm = PLUS_ARM / 2;
    uint32_t color = b == BTN_TICK && tick_dim ? RING_LIT : CLOCK_RED;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_hex(color);
    arc.width = PLUS_BORDER;
    arc.center.x = cx;
    arc.center.y = cy;
    arc.radius = BTN_SIZE / 2;
    arc.start_angle = 0;
    arc.end_angle = 360;
    lv_draw_arc(layer, &arc);
    switch (b) {
    case BTN_PLUS:
    case BTN_UP:
        line(layer, cx - arm, cy, cx + arm, cy, PLUS_STROKE, color);
        line(layer, cx, cy - arm, cx, cy + arm, PLUS_STROKE, color);
        break;
    case BTN_MINUS: line(layer, cx - arm, cy, cx + arm, cy, PLUS_STROKE, color); break;
    case BTN_CROSS:
        line(layer, cx - 13, cy - 13, cx + 13, cy + 13, PLUS_STROKE, color);
        line(layer, cx - 13, cy + 13, cx + 13, cy - 13, PLUS_STROKE, color);
        break;
    case BTN_TICK:
        line(layer, cx - 16, cy + 1, cx - 5, cy + 12, PLUS_STROKE, color);
        line(layer, cx - 5, cy + 12, cx + 16, cy - 11, PLUS_STROKE, color);
        break;
    case BTN_STOP: {
        lv_draw_rect_dsc_t box;
        lv_draw_rect_dsc_init(&box);
        box.bg_opa = LV_OPA_COVER;
        box.bg_color = lv_color_hex(color);
        lv_area_t a = {cx - 12, cy - 12, cx + 11, cy + 11};
        lv_draw_rect(layer, &box, &a);
        break;
    }
    default: break;
    }
}

static void flow_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    static const button_t corner[] = {BTN_PLUS, BTN_STOP, BTN_TICK, BTN_CROSS, BTN_MINUS, BTN_UP};
    for (unsigned i = 0; i < sizeof(corner) / sizeof(corner[0]); i++)
        if (btn_mask & corner[i]) draw_button(layer, corner[i]);
}

/* The arrow of "212 -> 240" or the dot of "12 pages · 42 min" */
static void mid_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(row_mid, &a);
    int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
    if (mid_arrow) {
        line(layer, a.x1 + 6, cy, a.x2 - 6, cy, PLUS_STROKE, CLOCK_RED);
        line(layer, a.x2 - 18, cy - 12, a.x2 - 6, cy, PLUS_STROKE, CLOCK_RED);
        line(layer, a.x2 - 18, cy + 12, a.x2 - 6, cy, PLUS_STROKE, CLOCK_RED);
    } else {
        lv_draw_rect_dsc_t box;
        lv_draw_rect_dsc_init(&box);
        box.bg_opa = LV_OPA_COVER;
        box.bg_color = lv_color_hex(CLOCK_RED);
        box.radius = LV_RADIUS_CIRCLE;
        lv_area_t d = {cx - 3, cy - 3, cx + 2, cy + 2};
        lv_draw_rect(layer, &box, &d);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int width)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(CLOCK_RED), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    if (width) lv_obj_set_width(l, width);
    lv_label_set_text(l, "");
    return l;
}

enum {
    BAND_X = 20, BAND_Y = 118, BAND_W = 440, BAND_H = 244, /* between the button rows */
    TOP_Y = 64, ALT_Y = 392, LINE_H20 = 26,
};

static void build_flow(void)
{
    flow_root = bare(screen);
    lv_obj_set_size(flow_root, SCREEN_SIZE, SCREEN_SIZE);
    lv_obj_add_event_cb(flow_root, flow_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);

    /* The top line sits between the top buttons; the second book's line
     * between the bottom ones */
    rect_t pick = button_rect(BTN_PICK);
    lbl_top = label(flow_root, &lv_font_montserrat_20, pick.w + 40);
    lv_obj_set_pos(lbl_top, pick.x - 20, TOP_Y);
    lbl_alt = label(flow_root, &lv_font_montserrat_20, pick.w);
    lv_obj_set_pos(lbl_alt, pick.x, ALT_Y);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = i ? lbl_alt : lbl_top;
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(l, LINE_H20);
    }

    /* The middle: a centred column */
    band = bare(flow_root);
    lv_obj_set_pos(band, BAND_X, BAND_Y);
    lv_obj_set_size(band, BAND_W, BAND_H);
    lv_obj_set_flex_flow(band, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(band, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(band, 8, 0);
    lbl_pre = label(band, &lv_font_montserrat_24, BAND_W);
    lbl_main = label(band, &lv_font_montserrat_48, BAND_W);
    row = bare(band);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    row_l = label(row, &lv_font_montserrat_48, 0);
    row_mid = bare(row);
    lv_obj_add_event_cb(row_mid, mid_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    row_r = label(row, &lv_font_montserrat_48, 0);
    lbl_sub = label(band, &lv_font_montserrat_24, BAND_W);
    lbl_note = label(band, &lv_font_montserrat_20, BAND_W);
    lv_obj_add_flag(flow_root, LV_OBJ_FLAG_HIDDEN);
}

void ui_init(void)
{
    screen = lv_screen_active();
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(screen, draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(screen, on_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen, on_release, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(screen, on_release, LV_EVENT_PRESS_LOST, NULL);

    /* The clock face's objects; hidden while a session screen shows */
    clock_root = bare(screen);
    lv_obj_set_size(clock_root, SCREEN_SIZE, SCREEN_SIZE);

    /* Plus: a circle outline with a cross, symmetric under rotation, so only
     * its position turns. */
    plus = bare(clock_root);
    lv_obj_set_size(plus, PLUS_SIZE, PLUS_SIZE);
    lv_obj_set_style_radius(plus, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(plus, PLUS_BORDER, 0);
    lv_obj_set_style_border_color(plus, lv_color_hex(CLOCK_RED), 0);
    lv_obj_set_style_border_opa(plus, LV_OPA_COVER, 0);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *bar = bare(plus);
        lv_obj_set_size(bar, i ? PLUS_STROKE : PLUS_ARM, i ? PLUS_ARM : PLUS_STROKE);
        lv_obj_set_style_bg_color(bar, lv_color_hex(CLOCK_RED), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_center(bar);
    }
    place_plus();

    setup = lv_label_create(clock_root);
    lv_label_set_text(setup, "Setup needed");
    lv_obj_set_style_text_font(setup, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(setup, lv_color_hex(CLOCK_RED), 0);
    lv_obj_remove_flag(setup, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_update_layout(setup);
    place_setup();
    update_setup();

    build_flow();
}

/* Reads the RTC (local 12-hour time) and the battery; redraws on change */
static void read_face(void)
{
    time_t utc;
    bool valid = rtc_get_utc(&utc);
    int hour12 = 0, minute = 0;
    if (valid) {
        struct tm local;
        localtime_r(&utc, &local);
        hour12 = local.tm_hour % 12 ? local.tm_hour % 12 : 12;
        minute = local.tm_min;
    }
    int battery = board_battery_percent();
    if (valid != s.valid || hour12 != s.hour12 || minute != s.minute || battery != s.battery) {
        s.valid = valid;
        s.hour12 = hour12;
        s.minute = minute;
        s.battery = battery;
        lv_obj_invalidate(screen);
    }
}

/* Re-sample orientation; on a change redraw. The clock turns its own
 * drawing; session screens are turned in the flush. */
static bool rotate(void)
{
    int turns = board_orientation() / 90;
    if (turns == s.turns) return false;
    s.turns = turns;
    place_plus();
    place_setup();
    display_set_turns(s.mode == M_CLOCK ? 0 : s.turns);
    lv_obj_invalidate(screen);
    return true;
}

static const char *const MODE[] = {"clock", "listening", "start check", "timer", "end check",
                                   "message"};

static void log_show(const char *why)
{
    char text[6];
    time_text(text);
    if (s.mode == M_CLOCK)
        ESP_LOGI(TAG, "show (%s): %s, orientation %d deg, battery %d%%", why, text,
                 s.turns * 90, s.battery);
    else
        ESP_LOGI(TAG, "show (%s): %s screen, %s, orientation %d deg", why, MODE[s.mode],
                 s.wait ? "waits" : "fades", s.turns * 90);
}

static void set_mode(ui_mode_t mode)
{
    if (mode == s.mode) return;
    s.mode = mode;
    if (mode == M_CLOCK) {
        lv_obj_add_flag(flow_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(clock_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(clock_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(flow_root, LV_OBJ_FLAG_HIDDEN);
    }
    display_set_turns(mode == M_CLOCK ? 0 : s.turns);
    lv_obj_invalidate(screen);
}

/* Puts the current mode on the panel: from dark it wakes, turns, draws
 * and fades in; while lit it redraws at the current level and restarts
 * the showing */
static void present(const char *why)
{
    bool waking = !s.visible;
    display_wake();
    bool rotated = rotate();
    /* A retap fades in again from the current level */
    s.from = s.visible && !rotated ? s.level : 0;
    s.level = s.from;
    s.closing = false;
    display_brightness(s.level);
    if (s.mode == M_CLOCK) read_face();
    lv_refr_now(NULL);
    /* The interval starts once the frame is on the panel */
    s.visible = true;
    s.shown_at = s.refreshed_at = now_ms();
    if (waking) {
        /* The waking contact is not a tap on whatever lies under it */
        lv_indev_wait_release(touch_indev());
    }
    if (wake_key) {
        wake_key = false;
        toggle_hold_now();
    }
    log_show(waking ? "wake" : why);
}

void ui_show_clock(void)
{
    set_mode(M_CLOCK);
    s.wait = false;
    present("tap");
}

/* A session screen: content changes on the screen already shown are
 * drawn in place; anything else is presented */
static void show_mode(ui_mode_t mode)
{
    if (s.visible && s.mode == mode && !s.closing) {
        lv_obj_invalidate(flow_root);
        return;
    }
    set_mode(mode);
    present("screen");
}

bool ui_idle(void) { return !s.visible; }

bool ui_held(void) { return s.held; }

static void toggle_hold_now(void)
{
    s.held = !s.held && s.visible;
    ESP_LOGI(TAG, "stay-awake %s", s.held ? "on" : "off");
    if (s.held || !s.visible) return;
    /* Releasing an expired hold starts the fade-out now */
    int64_t fade_start = SHOW_MS - FADE_OUT_MS;
    if (now_ms() - s.shown_at > fade_start) s.shown_at = now_ms() - fade_start;
}

static void refresh_held(void)
{
    s.refreshed_at = now_ms();
    if (rotate()) {
        display_brightness(0);
        if (s.mode == M_CLOCK) read_face();
        lv_refr_now(NULL);
        display_brightness(s.level);
        log_show("stay-awake rotate");
        return;
    }
    if (s.mode != M_CLOCK) return;
    int hour12 = s.hour12, minute = s.minute, battery = s.battery;
    read_face();
    if (hour12 != s.hour12 || minute != s.minute || battery != s.battery)
        log_show("stay-awake refresh");
}

/* One fade step; true when the screen just went dark */
static bool step(void)
{
    int64_t elapsed = now_ms() - s.shown_at;
    if (elapsed > SHOW_MS) elapsed = SHOW_MS;
    bool fading_in = elapsed < FADE_IN_MS;
    bool keep = (s.held || s.wait) && !s.closing;
    if (s.held && !s.closing && !fading_in && now_ms() - s.refreshed_at >= REFRESH_MS)
        refresh_held();
    unsigned at = keep && !fading_in ? FADE_IN_MS : (unsigned)elapsed;
    unsigned level = fade_level(at, s.from, BRIGHTNESS);
    if (level != s.level) {
        display_brightness(level);
        s.level = level;
    }
    if (keep || elapsed < SHOW_MS) return false;
    display_sleep_reliably();
    s.visible = false;
    s.closing = false;
    ESP_LOGI(TAG, "faded out; panel asleep");
    return true;
}

static ui_event_t button_event(unsigned b)
{
    switch (b) {
    case BTN_PLUS: return UI_PLUS;
    case BTN_STOP: return UI_STOP;
    case BTN_TICK: return UI_TICK;
    case BTN_CROSS: return UI_CROSS;
    case BTN_MINUS: return UI_MINUS;
    case BTN_UP: return UI_PLUS_PAGE;
    case BTN_PICK: return UI_PICK_B;
    default: return UI_TAP;
    }
}

static const char *const EVENT[] = {"none", "plus", "stop", "tick", "cross", "minus",
                                    "page plus", "pick A", "pick B", "minus ten",
                                    "page plus ten", "tap", "wake", "faded"};

ui_event_t ui_poll(void)
{
    unsigned buttons = board_buttons();
    bool tap = tapped;
    tapped = false;

    if (!s.visible) {
        if (!tap && !buttons) return UI_NONE;
        ESP_LOGI(TAG, "wake from idle by %s", tap ? "touch" : buttons & BUTTON_KEY ? "KEY" : "BOOT");
        press_btn = 0; /* the waking contact is not a button press */
        wake_key = buttons & BUTTON_KEY;
        return UI_WAKE;
    }
    if (buttons & BUTTON_BOOT) {
        ESP_LOGI(TAG, "BOOT pressed");
        if (!tap) press_btn = 0;
        tap = true;
    }
    /* A retap restarts a fading screen's showing */
    if (tap && !s.wait && !s.closing) present("tap");
    if (buttons & BUTTON_KEY) toggle_hold_now();
    if (step()) return UI_FADED;
    if (tap) {
        ui_event_t ev = button_event(press_btn);
        ESP_LOGI(TAG, "%s", press_btn ? EVENT[ev] : "tap on no button");
        return ev;
    }
    /* Held minus or plus: ten pages per 300 ms */
    if ((press_btn == BTN_MINUS || press_btn == BTN_UP) && !released) {
        if (hold_tens((uint32_t)(now_ms() - press_at)) > tens_sent) {
            tens_sent++;
            ui_event_t ev = press_btn == BTN_MINUS ? UI_MINUS_TEN : UI_PLUS_TEN;
            ESP_LOGI(TAG, "%s (held %lld ms)", EVENT[ev], now_ms() - press_at);
            return ev;
        }
    }
    return UI_NONE;
}

void ui_hold(bool wait)
{
    if (!wait && s.wait && s.visible) {
        /* A waiting screen gives way to a fading one: its showing starts now */
        s.from = s.level;
        s.shown_at = now_ms();
    }
    s.wait = wait;
}

void ui_dark(void)
{
    s.wait = false;
    if (!s.visible || s.closing) return;
    s.closing = true;
    /* The 350 ms fade-out starts now, however long the screen was lit */
    s.shown_at = now_ms() - (SHOW_MS - FADE_OUT_MS);
    ESP_LOGI(TAG, "going dark");
}

void ui_set_buttons(unsigned mask)
{
    if (mask == btn_mask) return;
    btn_mask = mask;
    ESP_LOGI(TAG, "buttons:%s%s%s%s%s%s%s%s", mask & BTN_MINUS ? " minus" : "",
             mask & BTN_UP ? " page-plus" : "", mask & BTN_CROSS ? " cross" : "",
             mask & BTN_TICK ? " tick" : "", mask & BTN_PLUS ? " plus" : "",
             mask & BTN_STOP ? " stop" : "", mask & BTN_PICK ? " second-book line" : "",
             mask ? "" : " none");
    if (s.mode != M_CLOCK) lv_obj_invalidate(flow_root);
}

/* Text through text_ascii (the fonts hold ASCII only) */
static void set(lv_obj_t *l, const char *text)
{
    char buf[160];
    text_ascii(buf, sizeof(buf), text ? text : "");
    lv_label_set_text(l, buf);
    if (buf[0]) lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
}

/* At most `lines` lines, the last ending in dots when cut */
static void fit(lv_obj_t *l, int lines)
{
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_height(l, LV_SIZE_CONTENT);
    lv_obj_update_layout(l);
    const lv_font_t *font = lv_obj_get_style_text_font(l, 0);
    int max = lines * lv_font_get_line_height(font);
    if (lv_obj_get_height(l) > max) {
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_height(l, max);
    }
}

/* Every session screen starts from blank */
static void clear(void)
{
    lv_obj_t *all[] = {lbl_top, lbl_pre, lbl_main, lbl_sub, lbl_note, lbl_alt};
    for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++) set(all[i], "");
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    tick_dim = false;
}

static void set_row(const char *left, bool arrow, const char *right, const lv_font_t *font)
{
    set(row_l, left);
    set(row_r, right);
    lv_obj_set_style_text_font(row_l, font, 0);
    lv_obj_set_style_text_font(row_r, font, 0);
    mid_arrow = arrow;
    lv_obj_set_size(row_mid, arrow ? 60 : 12, lv_font_get_line_height(font));
    lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_listening(const char *question)
{
    clear();
    set(lbl_main, question);
    fit(lbl_main, 3);
    ESP_LOGI(TAG, "listening screen: \"%s\"", question);
    show_mode(M_LISTEN);
}

void ui_set_listening(bool on)
{
    set(lbl_top, on ? "Listening" : "");
    ESP_LOGI(TAG, "\"Listening\" %s", on ? "shown" : "hidden");
}

void ui_show_check_start(const char *title, int page, const char *alt_title)
{
    char text[24];
    clear();
    set(lbl_top, "Start reading?");
    set(lbl_pre, title);
    fit(lbl_pre, 2);
    snprintf(text, sizeof(text), "page %d", page);
    set(lbl_main, text);
    if (alt_title && alt_title[0]) {
        char alt[80];
        snprintf(alt, sizeof(alt), "or %s", alt_title);
        set(lbl_alt, alt);
    }
    ESP_LOGI(TAG, "start check: \"Start reading?\" / \"%s\" / \"%s\" / alt \"%s\"",
             lv_label_get_text(lbl_pre), text, lv_label_get_text(lbl_alt));
    show_mode(M_CHECK_START);
}

void ui_set_note(const char *note)
{
    set(lbl_note, note);
    if (note && note[0]) ESP_LOGI(TAG, "start check note: \"%s\"", note);
}

void ui_show_timer(const char *clock, const char *title, const char *elapsed)
{
    clear();
    set(lbl_top, clock);
    set(lbl_main, elapsed);
    set(lbl_sub, title);
    fit(lbl_sub, 2);
    if (!(s.visible && s.mode == M_TIMER))
        ESP_LOGI(TAG, "timer screen: \"%s\" / \"%s\" / \"%s\"", clock, elapsed, title);
    show_mode(M_TIMER);
}

void ui_show_check_end(const char *title, int minutes, int from, int to)
{
    char a[8], b[8], m[16];
    clear();
    set(lbl_top, title);
    snprintf(a, sizeof(a), "%d", from);
    snprintf(b, sizeof(b), "%d", to);
    set_row(a, true, b, &lv_font_montserrat_48);
    snprintf(m, sizeof(m), "%d min", minutes);
    set(lbl_sub, m);
    tick_dim = to < from;
    ESP_LOGI(TAG, "end check: \"%s\" / \"%s -> %s\" / \"%s\", tick %s", lv_label_get_text(lbl_top), a,
             b, m, tick_dim ? "dim (disabled)" : "lit");
    show_mode(M_CHECK_END);
}

void ui_show_message(const char *big, const char *small)
{
    clear();
    set(lbl_main, big);
    fit(lbl_main, 3);
    /* "12 pages · 42 min": the dot is drawn */
    const char *dot = small ? strstr(small, " \xC2\xB7 ") : NULL;
    if (dot) {
        char left[40];
        snprintf(left, sizeof(left), "%.*s", (int)(dot - small), small);
        set_row(left, false, dot + 4, &lv_font_montserrat_24);
    } else {
        set(lbl_sub, small);
        fit(lbl_sub, 2);
    }
    ESP_LOGI(TAG, "message screen: \"%s\" / \"%s\"", big, small ? small : "");
    show_mode(M_MESSAGE);
}

void ui_refresh_time(void)
{
    update_setup();
    if (!s.visible || s.mode != M_CLOCK) return;
    read_face();
    log_show("time set");
}

void ui_log_status(void)
{
    char text[6];
    time_text(text);
    ESP_LOGI(TAG, "status: %s, %s screen, stay-awake %s, level %u, showing %s, orientation %d "
             "deg, battery %d%%", s.visible ? "visible" : "idle", MODE[s.mode],
             s.held ? "on" : "off", s.level, text, s.turns * 90, s.battery);
}
