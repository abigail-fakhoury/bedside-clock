#include "core/jscan.h"

#include <string.h>

enum {
    ST_VALUE,     /* a value must come */
    ST_ARR_FIRST, /* after '[': a value or ']' */
    ST_OBJ_FIRST, /* after '{': a key or '}' */
    ST_KEY,       /* after ',' in an object: a key */
    ST_COLON,     /* after a key */
    ST_AFTER,     /* after a value inside a container: ',' or the closer */
    ST_STRING,
    ST_NUMBER,
    ST_LITERAL,
    ST_DONE,      /* the one top-level value is complete */
};

enum { STR_PLAIN, STR_ESC, STR_HEX };

enum { N_MINUS, N_ZERO, N_INT, N_DOT, N_FRAC, N_E, N_ESIGN, N_EXP };

size_t utf8_fit(const char *s, size_t len, size_t max)
{
    if (len <= max) return len;
    size_t n = max;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

/* Drops a character cut off at the end of buf[0..len) */
static size_t trim_partial(const char *buf, size_t len)
{
    size_t i = len, back = 0;
    while (i > 0 && back < 4 && ((unsigned char)buf[i - 1] & 0xC0) == 0x80) {
        i--;
        back++;
    }
    if (i == 0) return len; /* only continuation bytes: leave as is */
    unsigned char lead = (unsigned char)buf[i - 1];
    size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    return (i - 1) + need > len ? i - 1 : len;
}

void jscan_init(jscan_t *s, jscan_cb cb, void *ctx)
{
    memset(s, 0, sizeof(*s));
    s->cb = cb;
    s->ctx = ctx;
    s->state = ST_VALUE;
}

static bool fail(jscan_t *s)
{
    s->error = true;
    return false;
}

static bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static bool top_is_array(const jscan_t *s)
{
    return s->depth && s->depth <= 64 && ((s->is_array >> (s->depth - 1)) & 1);
}

/* Sets path[] to the path of the value that is starting and decides
 * whether it is reported */
static void begin_value(jscan_t *s)
{
    uint16_t d = s->depth;
    s->named = false;
    if (s->skip_at) return;
    if (d == 0) {
        s->path[0] = '\0';
        s->named = true;
        return;
    }
    if (d > JSCAN_DEPTH) return;
    size_t base = s->plen[d];
    if (top_is_array(s)) {
        if (base + 2 > JSCAN_PATH_MAX - 1) return;
        memcpy(s->path + base, "[]", 3);
    } else {
        if (!s->key_ok) return;
        size_t klen = strlen(s->key), dot = base ? 1 : 0;
        if (base + dot + klen > JSCAN_PATH_MAX - 1) return;
        if (dot) s->path[base] = '.';
        memcpy(s->path + base + dot, s->key, klen + 1);
    }
    s->named = true;
}

static void emit(jscan_t *s, bool is_string)
{
    if (s->named && s->cb) s->cb(s->ctx, s->path, s->val, is_string);
}

static void value_done(jscan_t *s) { s->state = s->depth ? ST_AFTER : ST_DONE; }

static bool open_container(jscan_t *s, bool array)
{
    if (s->depth >= JSCAN_STACK) return fail(s);
    uint16_t d = ++s->depth;
    if (array) s->is_array |= (uint64_t)1 << (d - 1);
    else s->is_array &= ~((uint64_t)1 << (d - 1));
    if (!s->skip_at) {
        if (s->named && d <= JSCAN_DEPTH) s->plen[d] = (uint8_t)strlen(s->path);
        else s->skip_at = d;
    }
    s->state = array ? ST_ARR_FIRST : ST_OBJ_FIRST;
    return true;
}

static bool close_container(jscan_t *s, bool array)
{
    if (!s->depth || top_is_array(s) != array) return fail(s);
    uint16_t d = s->depth;
    if (s->skip_at == d) {
        s->skip_at = 0;
    } else if (!s->skip_at) {
        s->path[s->plen[d]] = '\0';
        if (s->cb) s->cb(s->ctx, s->path, NULL, false);
    }
    s->depth--;
    value_done(s);
    return true;
}

static void put_byte(jscan_t *s, char c)
{
    size_t cap = s->in_key ? JSCAN_KEY_MAX : JSCAN_VALUE_MAX;
    char *buf = s->in_key ? s->key : s->val;
    if (s->trunc) return;
    if (s->len < cap - 1) buf[s->len++] = c;
    else s->trunc = true;
}

static void put_cp(jscan_t *s, uint32_t cp)
{
    if (cp < 0x80) {
        put_byte(s, (char)cp);
    } else if (cp < 0x800) {
        put_byte(s, (char)(0xC0 | (cp >> 6)));
        put_byte(s, (char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        put_byte(s, (char)(0xE0 | (cp >> 12)));
        put_byte(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        put_byte(s, (char)(0x80 | (cp & 0x3F)));
    } else {
        put_byte(s, (char)(0xF0 | (cp >> 18)));
        put_byte(s, (char)(0x80 | ((cp >> 12) & 0x3F)));
        put_byte(s, (char)(0x80 | ((cp >> 6) & 0x3F)));
        put_byte(s, (char)(0x80 | (cp & 0x3F)));
    }
}

static void flush_high(jscan_t *s)
{
    if (s->high) {
        put_cp(s, 0xFFFD);
        s->high = 0;
    }
}

static void unicode_escape(jscan_t *s, uint16_t h)
{
    if (s->high) {
        if (h >= 0xDC00 && h <= 0xDFFF) {
            put_cp(s, 0x10000 + (((uint32_t)s->high - 0xD800) << 10) + (h - 0xDC00));
            s->high = 0;
            return;
        }
        flush_high(s);
    }
    if (h >= 0xD800 && h <= 0xDBFF) s->high = h;
    else if (h >= 0xDC00 && h <= 0xDFFF) put_cp(s, 0xFFFD);
    else put_cp(s, h);
}

static void begin_string(jscan_t *s, bool key)
{
    s->in_key = key;
    s->len = 0;
    s->trunc = false;
    s->high = 0;
    s->str_state = STR_PLAIN;
    s->state = ST_STRING;
}

static void end_string(jscan_t *s)
{
    char *buf = s->in_key ? s->key : s->val;
    size_t len = s->trunc ? trim_partial(buf, s->len) : s->len;
    buf[len] = '\0';
    if (s->in_key) {
        s->key_ok = !s->trunc;
        s->state = ST_COLON;
    } else {
        emit(s, true);
        value_done(s);
    }
}

static bool string_byte(jscan_t *s, char c)
{
    unsigned char u = (unsigned char)c;
    switch (s->str_state) {
    case STR_PLAIN:
        if (c == '\\') {
            s->str_state = STR_ESC;
            return true;
        }
        flush_high(s);
        if (c == '"') end_string(s);
        else if (u < 0x20) return fail(s);
        else put_byte(s, c);
        return true;
    case STR_ESC:
        s->str_state = STR_PLAIN;
        if (c == 'u') {
            s->str_state = STR_HEX;
            s->hex_left = 4;
            s->hex = 0;
            return true;
        }
        flush_high(s);
        switch (c) {
        case '"': put_byte(s, '"'); break;
        case '\\': put_byte(s, '\\'); break;
        case '/': put_byte(s, '/'); break;
        case 'b': put_byte(s, '\b'); break;
        case 'f': put_byte(s, '\f'); break;
        case 'n': put_byte(s, '\n'); break;
        case 'r': put_byte(s, '\r'); break;
        case 't': put_byte(s, '\t'); break;
        default: return fail(s);
        }
        return true;
    default: { /* STR_HEX */
        int v = c >= '0' && c <= '9' ? c - '0'
              : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) return fail(s);
        s->hex = (uint16_t)(s->hex << 4 | v);
        if (--s->hex_left == 0) {
            s->str_state = STR_PLAIN;
            unicode_escape(s, s->hex);
        }
        return true;
    }
    }
}

/* Adds c to the number; false when c does not belong to it */
static bool number_byte(jscan_t *s, char c)
{
    bool digit = c >= '0' && c <= '9';
    switch (s->num_state) {
    case N_MINUS:
        if (!digit) return false;
        s->num_state = c == '0' ? N_ZERO : N_INT;
        break;
    case N_ZERO:
    case N_INT:
        if (digit && s->num_state == N_INT) break;
        if (c == '.') s->num_state = N_DOT;
        else if (c == 'e' || c == 'E') s->num_state = N_E;
        else return false;
        break;
    case N_DOT:
        if (!digit) return false;
        s->num_state = N_FRAC;
        break;
    case N_FRAC:
        if (digit) break;
        if (c == 'e' || c == 'E') s->num_state = N_E;
        else return false;
        break;
    case N_E:
        if (c == '+' || c == '-') s->num_state = N_ESIGN;
        else if (digit) s->num_state = N_EXP;
        else return false;
        break;
    case N_ESIGN:
        if (!digit) return false;
        s->num_state = N_EXP;
        break;
    default: /* N_EXP */
        if (!digit) return false;
    }
    put_byte(s, c);
    return true;
}

static bool end_number(jscan_t *s)
{
    if (s->num_state != N_ZERO && s->num_state != N_INT && s->num_state != N_FRAC &&
        s->num_state != N_EXP)
        return fail(s);
    s->val[s->len] = '\0';
    emit(s, false);
    value_done(s);
    return true;
}

/* The first byte of a value */
static bool start_value(jscan_t *s, char c)
{
    begin_value(s);
    if (c == '{') return open_container(s, false);
    if (c == '[') return open_container(s, true);
    if (c == '"') {
        begin_string(s, false);
        return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        s->in_key = false;
        s->len = 0;
        s->trunc = false;
        s->num_state = N_MINUS;
        s->state = ST_NUMBER;
        if (c != '-') number_byte(s, c);
        else put_byte(s, c);
        return true;
    }
    const char *word = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
    if (!word) return fail(s);
    strcpy(s->val, word);
    s->lit = word + 1;
    s->state = ST_LITERAL;
    return true;
}

static bool step(jscan_t *s, char c)
{
    switch (s->state) {
    case ST_STRING:
        return string_byte(s, c);
    case ST_NUMBER:
        if (number_byte(s, c)) return true;
        if (!end_number(s)) return false;
        return step(s, c); /* the delimiter belongs to what follows */
    case ST_LITERAL:
        if (c != *s->lit) return fail(s);
        if (!*++s->lit) {
            emit(s, false);
            value_done(s);
        }
        return true;
    default:
        break;
    }
    if (is_ws(c)) return true;
    switch (s->state) {
    case ST_ARR_FIRST:
        if (c == ']') return close_container(s, true);
        return start_value(s, c);
    case ST_VALUE:
        return start_value(s, c);
    case ST_OBJ_FIRST:
        if (c == '}') return close_container(s, false);
        /* fall through */
    case ST_KEY:
        if (c != '"') return fail(s);
        begin_string(s, true);
        return true;
    case ST_COLON:
        if (c != ':') return fail(s);
        s->state = ST_VALUE;
        return true;
    case ST_AFTER:
        if (c == ',') {
            s->state = top_is_array(s) ? ST_VALUE : ST_KEY;
            return true;
        }
        if (c == ']') return close_container(s, true);
        if (c == '}') return close_container(s, false);
        return fail(s);
    default: /* ST_DONE: only whitespace may follow */
        return fail(s);
    }
}

bool jscan_feed(jscan_t *s, const char *chunk, size_t len)
{
    if (s->error) return false;
    for (size_t i = 0; i < len; i++)
        if (!step(s, chunk[i])) return false;
    return true;
}

bool jscan_finish(jscan_t *s)
{
    if (s->error) return false;
    if (s->state == ST_NUMBER && s->depth == 0 && !end_number(s)) return false;
    s->done = s->state == ST_DONE;
    return s->done;
}
