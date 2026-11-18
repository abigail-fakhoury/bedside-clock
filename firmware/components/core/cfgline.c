#include "core/cfgline.h"

#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    size_t min, max;
    bool spaces;
} keys[CFG_KEY_COUNT] = {
    [CFG_WIFI_SSID] = {"wifi_ssid", 1, 32, true},
    [CFG_WIFI_PASS] = {"wifi_pass", 8, 64, true}, /* WPA2 passphrase, or 64 hex */
    [CFG_RW_TOKEN] = {"rw_token", 1, CFG_VALUE_MAX, false},
    [CFG_OR_KEY] = {"or_key", 1, CFG_VALUE_MAX, false},
    [CFG_TZ_POSIX] = {"tz_posix", 1, 64, false},
    [CFG_TZ_IANA] = {"tz_iana", 1, 64, false},
};

const char *cfg_key_name(int key)
{
    return key >= 0 && key < CFG_KEY_COUNT ? keys[key].name : NULL;
}

static int find(const char *name, size_t len)
{
    for (int k = 0; k < CFG_KEY_COUNT; k++)
        if (strlen(keys[k].name) == len && memcmp(keys[k].name, name, len) == 0) return k;
    return -1;
}

int cfg_key_find(const char *name) { return find(name, strlen(name)); }

size_t cfg_value_max(int key) { return key >= 0 && key < CFG_KEY_COUNT ? keys[key].max : 0; }

static bool value_ok(int key, const char *v)
{
    size_t n = 0;
    for (; v[n]; n++) {
        unsigned char c = (unsigned char)v[n];
        if (c < 0x20 || c > 0x7e || (c == ' ' && !keys[key].spaces)) return false;
    }
    return n >= keys[key].min && n <= keys[key].max;
}

cfg_result_t cfg_parse(const char *line, bool truncated, int *key, const char **value)
{
    *key = -1;
    *value = NULL;
    if (strncmp(line, "CFG", 3) != 0 || (line[3] != ' ' && line[3] != '\0')) return CFG_NONE;
    if (line[3] == '\0') return CFG_BAD_KEY;
    const char *name = line + 4;
    const char *end = strchr(name, ' ');
    size_t len = end ? (size_t)(end - name) : strlen(name);
    int k = find(name, len);
    if (k < 0) return CFG_BAD_KEY;
    *key = k;
    if (truncated || !end || !value_ok(k, end + 1)) return CFG_BAD_VALUE;
    *value = end + 1;
    return CFG_SET;
}

int cfg_reply(char *out, size_t cap, cfg_result_t result, int key)
{
    if (result == CFG_NONE) return 0;
    const char *name = cfg_key_name(key);
    int n = snprintf(out, cap, "%s %s", result == CFG_SET ? "OK" : "ERR", name ? name : "?");
    if (n < 0 || (size_t)n >= cap) {
        if (cap) out[0] = '\0';
        return 0;
    }
    return n;
}

bool cfg_accepting(bool held, bool usb_host, bool idle) { return held || (usb_host && idle); }

void linebuf_init(linebuf_t *lb, char *buf, size_t cap)
{
    *lb = (linebuf_t){.buf = buf, .cap = cap};
    if (cap) buf[0] = '\0';
}

bool linebuf_feed(linebuf_t *lb, char c)
{
    if (lb->done) {
        lb->done = lb->overflow = false;
        lb->len = 0;
    }
    if (c == '\r' || c == '\n') {
        if (lb->len == 0 && !lb->overflow) return false;
        lb->buf[lb->len] = '\0';
        lb->done = true;
        return true;
    }
    if (lb->len < lb->cap - 1) lb->buf[lb->len++] = c;
    else lb->overflow = true;
    return false;
}

void linebuf_wipe(linebuf_t *lb)
{
    /* volatile: the zeroing must not be optimised away */
    volatile char *p = lb->buf;
    for (size_t i = 0; i < lb->cap; i++) p[i] = 0;
    lb->len = 0;
    lb->overflow = false;
    lb->done = false;
}
