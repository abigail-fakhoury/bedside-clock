#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "core/cfgline.h"

static const char *SECRET = "hunter2-very-secret";

static cfg_result_t parse(const char *line, int *key, const char **value)
{
    *key = -1;
    *value = NULL;
    return cfg_parse(line, false, key, value);
}

static void feed(linebuf_t *lb, const char *s, int *lines)
{
    for (; *s; s++)
        if (linebuf_feed(lb, *s)) (*lines)++;
}

int main(void)
{
    int key;
    const char *value;

    /* The six keys, in the order the brief lists them */
    static const char *names[CFG_KEY_COUNT] = {"wifi_ssid", "wifi_pass", "rw_token",
                                               "or_key", "tz_posix", "tz_iana"};
    for (int k = 0; k < CFG_KEY_COUNT; k++) {
        assert(strcmp(cfg_key_name(k), names[k]) == 0);
        assert(cfg_key_find(names[k]) == k);
    }
    assert(cfg_key_name(-1) == NULL && cfg_key_name(CFG_KEY_COUNT) == NULL);
    assert(cfg_key_find("wifi") == -1 && cfg_key_find("WIFI_SSID") == -1);
    assert(cfg_key_find("") == -1);

    /* A good line: key and the value as sent, spaces kept */
    assert(parse("CFG wifi_ssid HomeNet", &key, &value) == CFG_SET);
    assert(key == CFG_WIFI_SSID && strcmp(value, "HomeNet") == 0);
    assert(parse("CFG wifi_ssid My Home Net", &key, &value) == CFG_SET);
    assert(strcmp(value, "My Home Net") == 0);
    assert(parse("CFG tz_posix PST8PDT,M3.2.0,M11.1.0", &key, &value) == CFG_SET);
    assert(key == CFG_TZ_POSIX);
    assert(parse("CFG tz_iana America/Los_Angeles", &key, &value) == CFG_SET);

    /* Not a CFG line at all: someone else's command */
    assert(parse("TIME 20261111183015", &key, &value) == CFG_NONE);
    assert(parse("CFGX wifi_ssid a", &key, &value) == CFG_NONE);
    assert(parse("cfg wifi_ssid a", &key, &value) == CFG_NONE);

    /* Unknown or missing key */
    assert(parse("CFG", &key, &value) == CFG_BAD_KEY);
    assert(parse("CFG ", &key, &value) == CFG_BAD_KEY);
    assert(parse("CFG nope value", &key, &value) == CFG_BAD_KEY && key == -1);
    assert(parse("CFG  wifi_ssid a", &key, &value) == CFG_BAD_KEY);
    /* A value typed where the key belongs is not taken for a key */
    assert(parse("CFG hunter2-very-secret", &key, &value) == CFG_BAD_KEY && key == -1);

    /* Known key, bad value: the key is still reported */
    assert(parse("CFG wifi_ssid", &key, &value) == CFG_BAD_VALUE && key == CFG_WIFI_SSID);
    assert(parse("CFG wifi_ssid ", &key, &value) == CFG_BAD_VALUE);
    assert(parse("CFG wifi_pass short", &key, &value) == CFG_BAD_VALUE && key == CFG_WIFI_PASS);
    assert(parse("CFG wifi_pass 12345678", &key, &value) == CFG_SET);
    assert(parse("CFG rw_token a\tb", &key, &value) == CFG_BAD_VALUE);   /* control char */
    assert(parse("CFG rw_token a b", &key, &value) == CFG_BAD_VALUE);    /* tokens: no spaces */
    assert(parse("CFG or_key caf\xc3\xa9", &key, &value) == CFG_BAD_VALUE); /* ASCII only */

    /* Length limits per key, at and past the edge */
    char line[CFG_LINE_MAX + 16], v[CFG_LINE_MAX];
    for (int k = 0; k < CFG_KEY_COUNT; k++) {
        size_t max = cfg_value_max(k);
        assert(max >= 32 && max <= CFG_VALUE_MAX);
        memset(v, 'a', max);
        v[max] = '\0';
        strcpy(line, "CFG ");
        strcat(line, names[k]);
        strcat(line, " ");
        strcat(line, v);
        assert(strlen(line) < CFG_LINE_MAX); /* the longest legal line fits */
        assert(parse(line, &key, &value) == CFG_SET && key == k && strlen(value) == max);
        strcat(line, "a");
        assert(parse(line, &key, &value) == CFG_BAD_VALUE && key == k);
    }
    assert(cfg_value_max(CFG_WIFI_SSID) == 32 && cfg_value_max(CFG_WIFI_PASS) == 64);

    /* A truncated (overlong) line is refused even if what is left looks fine */
    assert(cfg_parse("CFG rw_token abc", true, &key, &value) == CFG_BAD_VALUE && key == CFG_RW_TOKEN);
    assert(cfg_parse("CFG zz abc", true, &key, &value) == CFG_BAD_KEY);
    assert(cfg_parse("CFG wifi_ss", true, &key, &value) == CFG_BAD_KEY);

    /* Replies name the key, never the value */
    char out[64];
    assert(cfg_reply(out, sizeof(out), CFG_SET, CFG_WIFI_PASS) > 0 && strcmp(out, "OK wifi_pass") == 0);
    assert(cfg_reply(out, sizeof(out), CFG_BAD_VALUE, CFG_OR_KEY) > 0 && strcmp(out, "ERR or_key") == 0);
    assert(cfg_reply(out, sizeof(out), CFG_REFUSED, CFG_TZ_IANA) > 0 && strcmp(out, "ERR tz_iana") == 0);
    assert(cfg_reply(out, sizeof(out), CFG_BAD_KEY, -1) > 0 && strcmp(out, "ERR ?") == 0);
    assert(cfg_reply(out, sizeof(out), CFG_NONE, -1) == 0);
    assert(cfg_reply(out, 4, CFG_SET, CFG_WIFI_PASS) == 0); /* too small: nothing */
    {
        char l[80] = "CFG wifi_pass ";
        strcat(l, SECRET);
        cfg_result_t r = parse(l, &key, &value);
        assert(r == CFG_SET);
        assert(cfg_reply(out, sizeof(out), r, key) > 0 && !strstr(out, SECRET));
        strcpy(l, "CFG ");
        strcat(l, SECRET);
        r = parse(l, &key, &value);
        assert(cfg_reply(out, sizeof(out), r, key) > 0 && !strstr(out, "hunter"));
    }

    /* Accepted while KEY stay-awake is on, or idle with a USB host */
    assert(cfg_accepting(true, false, false));
    assert(cfg_accepting(true, true, false));
    assert(cfg_accepting(false, true, true));
    assert(!cfg_accepting(false, true, false)); /* showing, not held */
    assert(!cfg_accepting(false, false, true));
    assert(!cfg_accepting(false, false, false));

    /* Line assembly: CR, LF and CRLF end lines; empty lines are skipped */
    char buf[16];
    linebuf_t lb;
    linebuf_init(&lb, buf, sizeof(buf));
    int lines = 0;
    feed(&lb, "\r\n\n", &lines);
    assert(lines == 0);
    feed(&lb, "STATUS\r\n", &lines);
    assert(lines == 1 && strcmp(lb.buf, "STATUS") == 0 && !lb.overflow);
    feed(&lb, "AB\n", &lines);
    assert(lines == 2 && strcmp(lb.buf, "AB") == 0);
    feed(&lb, "CD\n", &lines);
    assert(lines == 3 && strcmp(lb.buf, "CD") == 0);
    /* Overlong: kept to capacity, flagged, and the next line starts clean */
    feed(&lb, "0123456789abcdefXYZ\n", &lines);
    assert(lines == 4 && lb.overflow && strlen(lb.buf) == sizeof(buf) - 1);
    assert(strncmp(lb.buf, "0123456789abcde", 15) == 0);
    feed(&lb, "ok\n", &lines);
    assert(lines == 5 && !lb.overflow && strcmp(lb.buf, "ok") == 0);
    /* Wipe clears what a secret line left in the buffer */
    feed(&lb, "secret\n", &lines);
    linebuf_wipe(&lb);
    for (size_t i = 0; i < sizeof(buf); i++) assert(buf[i] == 0);
    feed(&lb, "x\n", &lines);
    assert(lines == 7 && strcmp(lb.buf, "x") == 0);
    return 0;
}
