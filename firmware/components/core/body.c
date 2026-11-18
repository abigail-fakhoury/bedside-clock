#include "core/body.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ISO_MAX 32 /* 20 bytes and NUL, with room the compiler can prove */

/* "YYYY-MM-DDThh:mm:ssZ" for t >= 0 (Howard Hinnant's civil_from_days). */
static void iso_utc(int64_t t, char out[ISO_MAX])
{
    int64_t days = t / 86400, sec = t % 86400;
    int64_t z = days + 719468;
    int64_t era = z / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    int64_t y = (int64_t)yoe + era * 400 + (m <= 2);
    snprintf(out, ISO_MAX, "%04d-%02u-%02uT%02u:%02u:%02uZ", (int)y, m, d, (unsigned)(sec / 3600),
             (unsigned)(sec / 60 % 60), (unsigned)(sec % 60));
}

/* An IANA zone name: 1-63 bytes of letters, digits, '/', '_', '+', '-'. */
static bool tz_ok(const char *tz)
{
    if (!tz) return false;
    size_t n = strlen(tz);
    if (n == 0 || n > 63) return false;
    for (size_t i = 0; i < n; i++) {
        char c = tz[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '/' || c == '_' || c == '+' || c == '-'))
            return false;
    }
    return true;
}

int body_session(char *out, size_t cap, const session_t *s, const char *tz_iana)
{
    if (!out || cap == 0 || !s || !tz_ok(tz_iana)) return -1;
    if (s->started_at < SESSION_EARLIEST || s->ended_at < s->started_at) return -1;
    if (s->page_end < s->page_start) return -1;
    char start[ISO_MAX], end[ISO_MAX];
    iso_utc((int64_t)s->started_at, start);
    iso_utc((int64_t)s->ended_at, end);
    int n = snprintf(out, cap,
                     "{\"items\":[{\"book_id\":%" PRIu32 ",\"format\":\"paper\","
                     "\"started_at\":\"%s\",\"ended_at\":\"%s\","
                     "\"page_at_start\":%u,\"page_at_end\":%u,\"timezone\":\"%s\","
                     "\"publish_to_feed\":true,"
                     "\"device\":{\"model\":\"Bedside Clock\",\"vendor\":\"Waveshare\"}}]}",
                     s->book_id, start, end, (unsigned)s->page_start, (unsigned)s->page_end,
                     tz_iana);
    if (n < 0 || (size_t)n >= cap) return -1;
    return n;
}

/* ---- The voice request ------------------------------------------------ */

static const char PROMPT_START[] =
    "The audio is someone saying which book they are about to read and the page they are on. "
    "Their shelf, to help with spelling:\n";
static const char PROMPT_START_REPLY[] =
    "Reply with only JSON: {\"title\": \"<book title as said, or null>\", \"page\": <integer or "
    "null>}. Use null for anything not clearly said.";
static const char PROMPT_END[] =
    "The audio is someone saying the page number they stopped reading on. Reply with only JSON: "
    "{\"page\": <integer or null>}. Use null if no number is clearly said.";

const char BODY_VOICE_TAIL[] = "\"}}]}]}";

typedef struct {
    char *out;
    size_t cap, len;
    bool over;
} writer_t;

static void raw(writer_t *w, const char *s, size_t n)
{
    if (w->over || n > w->cap - 1 - w->len) {
        w->over = true;
        return;
    }
    memcpy(w->out + w->len, s, n);
    w->len += n;
}

/* s[0..n) as the inside of a JSON string; bytes >= 0x80 pass through */
static void esc(writer_t *w, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        char e[7];
        if (c == '"' || c == '\\') {
            e[0] = '\\';
            e[1] = (char)c;
            raw(w, e, 2);
        } else if (c == '\n' || c == '\r' || c == '\t') {
            e[0] = '\\';
            e[1] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't';
            raw(w, e, 2);
        } else if (c < 0x20) {
            snprintf(e, sizeof(e), "\\u%04x", c);
            raw(w, e, 6);
        } else {
            raw(w, (const char *)&s[i], 1);
        }
    }
}

/* A NUL-terminated field of at most cap bytes, even without its NUL */
static size_t field_len(const char *s, size_t cap)
{
    const char *z = memchr(s, '\0', cap);
    return z ? (size_t)(z - s) : cap;
}

int body_voice_head(char *out, size_t cap, const char *model, bool want_title,
                    const shelf_t *shelf)
{
    if (!out || cap == 0 || !model || (want_title && !shelf)) return -1;
    writer_t w = {.out = out, .cap = cap};
    raw(&w, "{\"model\":\"", 10);
    esc(&w, model, strlen(model));
    static const char mid[] = "\",\"max_tokens\":300,\"messages\":[{\"role\":\"user\","
                              "\"content\":[{\"type\":\"text\",\"text\":\"";
    raw(&w, mid, sizeof(mid) - 1);
    if (want_title) {
        esc(&w, PROMPT_START, sizeof(PROMPT_START) - 1);
        unsigned count = shelf->count < SHELF_MAX ? shelf->count : SHELF_MAX;
        for (unsigned i = 0; i < count; i++) {
            const book_t *b = &shelf->books[i];
            size_t tn = field_len(b->title, sizeof(b->title));
            size_t an = field_len(b->author, sizeof(b->author));
            if (tn == 0) continue;
            esc(&w, b->title, tn);
            if (an) {
                raw(&w, " - ", 3);
                esc(&w, b->author, an);
            }
            raw(&w, "\\n", 2);
        }
        esc(&w, PROMPT_START_REPLY, sizeof(PROMPT_START_REPLY) - 1);
    } else {
        esc(&w, PROMPT_END, sizeof(PROMPT_END) - 1);
    }
    static const char audio[] = "\"},{\"type\":\"input_audio\",\"input_audio\":"
                                "{\"format\":\"wav\",\"data\":\"";
    raw(&w, audio, sizeof(audio) - 1);
    if (w.over) return -1;
    out[w.len] = '\0';
    return (int)w.len;
}
