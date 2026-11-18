#include "core/reply.h"

#include <string.h>

#include "core/jscan.h"

/* 1-9999 from a run of ASCII digits (leading zeros allowed), else -1 */
static int page_value(const char *v)
{
    if (!v || !*v) return -1;
    int n = 0;
    for (; *v; v++) {
        if (*v < '0' || *v > '9') return -1;
        n = n * 10 + (*v - '0');
        if (n > 9999) return -1;
    }
    return n >= 1 ? n : -1;
}

static void on_value(void *ctx, const char *path, const char *value, bool is_string)
{
    heard_t *h = ctx;
    if (!value) return;
    if (strcmp(path, "title") == 0) {
        size_t n = is_string ? utf8_fit(value, strlen(value), sizeof(h->title) - 1) : 0;
        memcpy(h->title, value, n);
        h->title[n] = '\0';
    } else if (strcmp(path, "page") == 0) {
        h->page = page_value(value); /* null, true, -5, 39.5, 4e2 all give -1 */
    }
}

heard_t reply_parse(const char *content)
{
    heard_t h = {.title = "", .page = -1, .ok = false};
    const char *start = content ? strchr(content, '{') : NULL;
    if (!start) return h;

    /* The brace that closes the first one, skipping string contents */
    const char *end = NULL;
    size_t depth = 0;
    bool in_str = false, esc = false;
    for (const char *p = start; *p && !end; p++) {
        if (in_str) {
            if (esc) esc = false;
            else if (*p == '\\') esc = true;
            else if (*p == '"') in_str = false;
        } else if (*p == '"') {
            in_str = true;
        } else if (*p == '{') {
            depth++;
        } else if (*p == '}' && --depth == 0) {
            end = p;
        }
    }
    if (!end) return h;

    jscan_t scan;
    jscan_init(&scan, on_value, &h);
    if (jscan_feed(&scan, start, (size_t)(end - start) + 1) && jscan_finish(&scan)) {
        h.ok = true;
    } else {
        h.title[0] = '\0';
        h.page = -1;
    }
    return h;
}

/* ---- The message content inside a chat completions response ---------- */

typedef struct {
    char *out;
    size_t cap, len;
    bool full; /* stop writing: what follows would leave a gap */
} sink_t;

static void put(sink_t *s, const char *bytes, size_t n)
{
    if (s->full) return;
    if (s->len + n > s->cap - 1) {
        s->full = true;
        return;
    }
    memcpy(s->out + s->len, bytes, n);
    s->len += n;
}

static void put_code(sink_t *s, unsigned cp)
{
    char u[4];
    size_t n;
    if (cp < 0x80) {
        u[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        u[0] = (char)(0xc0 | cp >> 6);
        u[1] = (char)(0x80 | (cp & 0x3f));
        n = 2;
    } else if (cp < 0x10000) {
        u[0] = (char)(0xe0 | cp >> 12);
        u[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        u[2] = (char)(0x80 | (cp & 0x3f));
        n = 3;
    } else {
        u[0] = (char)(0xf0 | cp >> 18);
        u[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
        u[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
        u[3] = (char)(0x80 | (cp & 0x3f));
        n = 4;
    }
    put(s, u, n);
}

/* Four hex digits at p (bounded by end) into *v */
static bool hex4(const char *p, const char *end, unsigned *v)
{
    if (end - p < 4) return false;
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        unsigned d = c >= '0' && c <= '9'   ? (unsigned)(c - '0')
                     : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
                     : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10)
                                            : 16;
        if (d == 16) return false;
        *v = *v << 4 | d;
    }
    return true;
}

/* The end of the string whose opening quote is at p[-1]: the closing
 * quote, or NULL when it does not close before end */
static const char *string_end(const char *p, const char *end)
{
    for (; p < end; p++) {
        if (*p == '\\') {
            if (++p >= end) break;
        } else if (*p == '"') {
            return p;
        }
    }
    return NULL;
}

/* Decodes the string body [p, q) into s; false on a bad escape */
static bool decode(const char *p, const char *q, sink_t *s)
{
    while (p < q) {
        if (*p != '\\') {
            /* A whole UTF-8 character at once, so a cut never splits one */
            unsigned char c = (unsigned char)*p;
            size_t n = c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
            if (n > (size_t)(q - p)) n = (size_t)(q - p);
            put(s, p, n);
            p += n;
            continue;
        }
        p++;
        char c = *p++;
        const char *simple = c == '"' ? "\"" : c == '\\' ? "\\" : c == '/' ? "/" : c == 'b' ? "\b"
                           : c == 'f' ? "\f" : c == 'n' ? "\n" : c == 'r' ? "\r" : c == 't' ? "\t"
                                                                                       : NULL;
        if (simple) {
            put(s, simple, 1);
            continue;
        }
        unsigned cp, lo;
        if (c != 'u' || !hex4(p, q, &cp)) return false;
        p += 4;
        if (cp >= 0xd800 && cp < 0xdc00 && q - p >= 6 && p[0] == '\\' && p[1] == 'u' &&
            hex4(p + 2, q, &lo) && lo >= 0xdc00 && lo < 0xe000) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
            p += 6;
        } else if (cp >= 0xd800 && cp < 0xe000) {
            cp = 0xfffd; /* a lone surrogate */
        }
        put_code(s, cp);
    }
    return true;
}

static const char *skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

bool reply_content(const char *resp, size_t len, char *out, size_t cap)
{
    if (!out || cap == 0) return false;
    out[0] = '\0';
    if (!resp) return false;
    const char *p = resp, *end = resp + len;
    bool after_choices = false;
    while (p < end) {
        if (*p++ != '"') continue;
        const char *q = string_end(p, end);
        if (!q) return false;
        const char *k = p;
        size_t klen = (size_t)(q - p);
        p = skip_ws(q + 1, end);
        if (p >= end || *p != ':') continue; /* a value, not a key */
        if (klen == 7 && memcmp(k, "choices", 7) == 0) after_choices = true;
        if (!after_choices || klen != 7 || memcmp(k, "content", 7) != 0) continue;
        p = skip_ws(p + 1, end);
        if (p >= end || *p != '"') return false; /* null, or cut off */
        q = string_end(p + 1, end);
        if (!q) return false;
        sink_t s = {.out = out, .cap = cap};
        if (!decode(p + 1, q, &s)) {
            out[0] = '\0';
            return false;
        }
        out[s.len] = '\0';
        return true;
    }
    return false;
}
