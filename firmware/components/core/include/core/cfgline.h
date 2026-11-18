#pragma once
#include <stdbool.h>
#include <stddef.h>

/* The USB settings protocol: "CFG <key> <value>" answered by "OK <key>" or
 * "ERR <key>". Replies and logs name the key only, never the value. */

typedef enum {
    CFG_WIFI_SSID,
    CFG_WIFI_PASS,
    CFG_RW_TOKEN,
    CFG_OR_KEY,
    CFG_TZ_POSIX,
    CFG_TZ_IANA,
    CFG_KEY_COUNT
} cfg_key_t;

enum {
    CFG_VALUE_MAX = 128, /* the longest value of any key */
    CFG_LINE_MAX = 160,  /* line buffer, NUL included: the longest legal line fits */
};

/* "wifi_ssid" … "tz_iana"; NULL outside 0 … CFG_KEY_COUNT − 1. */
const char *cfg_key_name(int key);

/* The key with exactly this name, or −1. */
int cfg_key_find(const char *name);

/* Longest value accepted for a key (bytes). */
size_t cfg_value_max(int key);

typedef enum {
    CFG_NONE,      /* not a CFG line */
    CFG_SET,       /* *key and *value are good */
    CFG_BAD_KEY,   /* not one of the six names; *key = −1 */
    CFG_BAD_VALUE, /* known *key, value empty, too long, or bad characters */
    CFG_REFUSED,   /* good line, but not accepted now (see cfg_accepting) */
} cfg_result_t;

/* Parses one line (no line ending). `truncated` marks a line cut at the
 * buffer size: it is never CFG_SET. On CFG_SET, *value points into `line`.
 * Values are printable ASCII; the SSID and password may contain spaces,
 * the other keys may not. SSID 1–32 bytes, password 8–64. */
cfg_result_t cfg_parse(const char *line, bool truncated, int *key, const char **value);

/* Writes "OK <key>" for CFG_SET, "ERR <key>" otherwise ("ERR ?" when the key
 * is unknown). Returns the length, or 0 for CFG_NONE or a short buffer. */
int cfg_reply(char *out, size_t cap, cfg_result_t result, int key);

/* Settings are taken while KEY stay-awake is on, or while the clock idles
 * awake with a USB host attached (development). */
bool cfg_accepting(bool held, bool usb_host, bool idle);

/* Byte-at-a-time line assembly over a caller's buffer. */
typedef struct {
    char *buf;
    size_t cap, len;
    bool overflow; /* the last line was longer than cap − 1 and was cut */
    bool done;
} linebuf_t;

void linebuf_init(linebuf_t *lb, char *buf, size_t cap);

/* True when c ends a non-empty line: lb->buf then holds it, NUL-terminated,
 * until the next feed. CR and LF both end lines; empty lines are skipped. */
bool linebuf_feed(linebuf_t *lb, char c);

/* Zeroes the whole buffer (after a line that held a secret). */
void linebuf_wipe(linebuf_t *lb);
