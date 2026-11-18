#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A streaming JSON scanner in fixed memory: feed it the document in pieces
 * of any size and it calls back once per scalar value with the value's path.
 *
 * Paths: object members join with '.', array elements add "[]", so a book's
 * page count in {"results":[{"product":{"page_count":320}}]} arrives as
 * "results[].product.page_count". A scalar at the root has the path "".
 *
 * Values: strings arrive decoded (escapes resolved, \u escapes as UTF-8,
 * a lone surrogate as U+FFFD) with is_string true. Numbers, true, false
 * and null arrive as their literal text with is_string false. A value
 * longer than JSCAN_VALUE_MAX - 1 (127) bytes is truncated at a UTF-8
 * character boundary.
 *
 * Container ends: when an object or array closes, the callback gets its
 * path with value NULL (so "results[]" with NULL means one book ended).
 *
 * Skipping: the contents of containers nested deeper than JSCAN_DEPTH, and
 * any value whose path or key would not fit, are still checked for syntax
 * but produce no callbacks; that is not an error. Nesting deeper than
 * JSCAN_STACK is treated as malformed. */

enum {
    JSCAN_DEPTH = 8,       /* deepest container whose contents are reported */
    JSCAN_STACK = 64,      /* deepest nesting accepted at all */
    JSCAN_VALUE_MAX = 128, /* value buffer, terminator included */
    JSCAN_PATH_MAX = 128,  /* path buffer, terminator included */
    JSCAN_KEY_MAX = 64,    /* key buffer, terminator included */
};

typedef void (*jscan_cb)(void *ctx, const char *path, const char *value, bool is_string);

typedef struct {
    jscan_cb cb;
    void *ctx;
    uint8_t state;            /* what the next byte may be */
    uint8_t str_state;        /* inside a string: plain, after '\', in \u hex */
    uint8_t num_state;        /* inside a number: grammar position */
    bool error, done;
    uint16_t depth;           /* open containers */
    uint16_t skip_at;         /* depth of the outermost unreported container, 0 none */
    uint64_t is_array;        /* bit d-1: container at depth d is an array */
    uint8_t plen[JSCAN_DEPTH + 1]; /* path length of the container at each depth */
    bool named;               /* the value being read is reported */
    bool in_key;              /* the string being read is a key */
    bool key_ok;              /* the last key fitted */
    const char *lit;          /* rest of an expected literal (true/false/null) */
    uint8_t hex_left;         /* \u escape: hex digits still to read */
    uint16_t hex, high;       /* \u escape value; pending high surrogate (0 none) */
    size_t len;               /* bytes in key[] or val[] */
    bool trunc;               /* the string or number did not fit */
    char key[JSCAN_KEY_MAX];
    char val[JSCAN_VALUE_MAX];
    char path[JSCAN_PATH_MAX];
} jscan_t;

void jscan_init(jscan_t *s, jscan_cb cb, void *ctx);

/* Scans the next len bytes. False once the input is malformed (and from
 * then on). True means "no error so far", not "complete". */
bool jscan_feed(jscan_t *s, const char *chunk, size_t len);

/* Call after the last byte: true only when exactly one complete JSON value
 * was read without error (a truncated document gives false). */
bool jscan_finish(jscan_t *s);

/* The longest prefix of s[0..len) that is at most max bytes and does not
 * end inside a UTF-8 character. */
size_t utf8_fit(const char *s, size_t len, size_t max);
