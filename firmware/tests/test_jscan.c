#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/jscan.h"

/* Every callback appended as "path=value;" (strings quoted, ends as "path$;") */
static char log_buf[1 << 18];

static void record(void *ctx, const char *path, const char *value, bool is_string)
{
    (void)ctx;
    char line[512];
    if (!value) snprintf(line, sizeof(line), "%s$;", path);
    else if (is_string) snprintf(line, sizeof(line), "%s=\"%s\";", path, value);
    else snprintf(line, sizeof(line), "%s=%s;", path, value);
    assert(strlen(log_buf) + strlen(line) < sizeof(log_buf));
    strcat(log_buf, line);
}

/* Scans doc in pieces of `step` bytes (0 = whole); returns jscan_finish() */
static bool scan(const char *doc, size_t len, size_t step)
{
    jscan_t s;
    log_buf[0] = '\0';
    jscan_init(&s, record, NULL);
    if (!step) step = len ? len : 1;
    bool fed = true;
    for (size_t i = 0; i < len; i += step) {
        size_t n = len - i < step ? len - i : step;
        fed = jscan_feed(&s, doc + i, n);
        if (!fed) break;
    }
    bool ok = jscan_finish(&s);
    assert(!ok || fed);
    return ok;
}

static bool scan_str(const char *doc) { return scan(doc, strlen(doc), 0); }

static void expect(const char *doc, const char *want)
{
    for (size_t step = 0; step < 4; step++) {
        if (!scan(doc, strlen(doc), step)) {
            fprintf(stderr, "scan failed (step %zu): %s\n", step, doc);
            assert(0);
        }
        if (strcmp(log_buf, want) != 0) {
            fprintf(stderr, "doc:  %s\ngot:  %s\nwant: %s\n", doc, log_buf, want);
            assert(0);
        }
    }
}

static char *read_file(const char *name, size_t *len)
{
    FILE *f = fopen(name, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    assert(buf && fread(buf, 1, (size_t)n, f) == (size_t)n);
    buf[n] = '\0';
    fclose(f);
    *len = (size_t)n;
    return buf;
}

int main(void)
{
    /* Paths */
    expect("{\"results\":[{\"id\":7,\"product\":{\"page_count\":320}}],\"next\":null}",
           "results[].id=7;results[].product.page_count=320;results[].product$;results[]$;"
           "results$;next=null;$;");
    expect(" 42 ", "=42;");
    expect("\"x\"", "=\"x\";");
    expect("[1,[2,3],{}]", "[]=1;[][]=2;[][]=3;[]$;[]$;$;");
    expect("{\"a\":true,\"b\":false,\"c\":null}", "a=true;b=false;c=null;$;");
    expect("{ \"a\" :\n [ ] ,\t\"b\" : { } }", "a$;b$;$;");
    expect("[-0,1.5e+3,-2E-2,0.25]", "[]=-0;[]=1.5e+3;[]=-2E-2;[]=0.25;$;");

    /* Escapes: quote, backslash, slash, controls, \u as UTF-8, surrogates */
    expect("[\"say \\\"hi\\\"\"]", "[]=\"say \"hi\"\";$;");
    expect("[\"a\\\\b\\/c\\n\\t\"]", "[]=\"a\\b/c\n\t\";$;");
    expect("[\"caf\\u00e9 \\u20ac\"]", "[]=\"caf\xc3\xa9 \xe2\x82\xac\";$;");
    expect("[\"\\ud83d\\ude00\"]", "[]=\"\xf0\x9f\x98\x80\";$;");
    expect("[\"\\ud83dx\"]", "[]=\"\xef\xbf\xbdx\";$;");     /* lone high */
    expect("[\"\\ude00\"]", "[]=\"\xef\xbf\xbd\";$;");       /* lone low */
    expect("[\"\\ud83d\\n\"]", "[]=\"\xef\xbf\xbd\n\";$;");  /* high then other escape */
    expect("[\"\xc3\xa9t\xc3\xa9\"]", "[]=\"\xc3\xa9t\xc3\xa9\";$;"); /* raw UTF-8 kept */
    expect("{\"k\\\"ey\":1}", "k\"ey=1;$;");

    /* Long values: truncated to 127 bytes, at a character boundary */
    {
        char doc[1024] = "[\"", want[1024];
        for (int i = 0; i < 300; i++) strcat(doc, "a");
        strcat(doc, "\"]");
        assert(scan_str(doc));
        assert(strlen(log_buf) == strlen("[]=\"\";$;") + 127);
        /* two-byte characters: 126 bytes fit, 127 would split one */
        strcpy(doc, "[\"");
        for (int i = 0; i < 150; i++) strcat(doc, "\xc3\xa9");
        strcat(doc, "\"]");
        strcpy(want, "[]=\"");
        for (int i = 0; i < 63; i++) strcat(want, "\xc3\xa9");
        strcat(want, "\";$;");
        expect(doc, want);
        /* long numbers too (still validated to the end) */
        strcpy(doc, "[1");
        for (int i = 0; i < 200; i++) strcat(doc, "0");
        strcat(doc, "]");
        assert(scan_str(doc));
        assert(strlen(log_buf) == strlen("[]=;$;") + 127);
        strcat(doc, "x");
        assert(!scan_str(doc));
    }

    /* Deeper than 8: skipped (no callbacks inside or for it), not an error.
     * {"a": n nested arrays around 9, "b":1}: the object is depth 1, the
     * arrays depths 2..n+1. */
    for (int n = 7; n <= 9; n++) {
        char doc[128] = "{\"a\":", want[512] = "", p[64];
        for (int i = 0; i < n; i++) strcat(doc, "[");
        strcat(doc, "9");
        for (int i = 0; i < n; i++) strcat(doc, "]");
        strcat(doc, ",\"b\":1}");
        if (n == 7) strcat(want, "a[][][][][][][]=9;"); /* inside depth 8: reported */
        for (int d = n + 1 < 8 ? n + 1 : 8; d >= 2; d--) {
            strcpy(p, "a");
            for (int i = 2; i < d; i++) strcat(p, "[]");
            strcat(want, p);
            strcat(want, "$;");
        }
        strcat(want, "b=1;$;");
        expect(doc, want);
    }
    /* skipped subtrees are still syntax-checked */
    assert(!scan_str("[[[[[[[[[[1,}]]]]]]]]]]"));
    expect("[[[[[[[[[{\"x\":[2]}]]]]]]]],3]",
           "[][][][][][][]$;[][][][][][]$;[][][][][]$;[][][][]$;[][][]$;[][]$;[]$;"
           "[]=3;$;");
    /* A key too long for the key buffer: that member is skipped */
    {
        char doc[256] = "{\"";
        for (int i = 0; i < 80; i++) strcat(doc, "k");
        strcat(doc, "\":{\"x\":1},\"y\":2}");
        expect(doc, "y=2;$;");
    }
    /* A path too long for the path buffer: skipped */
    {
        char doc[512] = "{\"", key[61];
        memset(key, 'k', 60);
        key[60] = '\0';
        strcat(doc, key);
        strcat(doc, "\":{\"");
        strcat(doc, key);
        strcat(doc, "\":{\"");
        strcat(doc, key);
        strcat(doc, "\":{\"q\":1},\"r\":3},\"z\":2}}");
        /* K.K.K would be 182 bytes: that object and all inside it are skipped */
        char want[512];
        snprintf(want, sizeof(want), "%s.%s.r=3;%s.%s$;%s.z=2;%s$;$;", key, key, key, key, key,
                 key);
        expect(doc, want);
    }

    /* Malformed or truncated input gives false */
    const char *bad[] = {
        "", "   ", "{", "[1,2", "{\"a\":1", "{\"a\"", "{\"a\":", "\"abc", "[1,]", "[,1]",
        "{\"a\":1,}", "{,}", "{\"a\" 1}", "{1:2}", "[01]", "[1.]", "[.5]", "[1e]", "[-]",
        "[tru]", "[nul]", "[truex]", "[True]", "1 2", "{} {}", "[1]]", "]", "[\"\\x\"]",
        "[\"\\u12g4\"]", "[\"a\nb\"]", "{\"a\":1]", "[1}", "nan", "[+1]",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        for (size_t step = 0; step < 3; step++) {
            if (scan(bad[i], strlen(bad[i]), step)) {
                fprintf(stderr, "accepted malformed: %s\n", bad[i]);
                assert(0);
            }
        }
    }
    /* Nesting past JSCAN_STACK is malformed */
    {
        char doc[200] = "";
        for (int i = 0; i < 70; i++) strcat(doc, "[");
        for (int i = 0; i < 70; i++) strcat(doc, "]");
        assert(!scan_str(doc));
    }
    /* Once false, stays false */
    {
        jscan_t s;
        jscan_init(&s, record, NULL);
        assert(!jscan_feed(&s, "[1,]", 4));
        assert(!jscan_feed(&s, "", 0));
        assert(!jscan_finish(&s));
    }

    /* The captured response: identical callbacks in any chunk size */
    {
        size_t len;
        char *doc = read_file("tests/fixtures/books.json", &len);
        assert(scan(doc, len, 0));
        char *whole = strdup(log_buf);
        assert(strstr(whole, "results[].title=\"Dracula\";"));
        assert(strstr(whole, "results[].product.page_count=648;"));
        size_t steps[] = {1, 7, 1024};
        for (size_t i = 0; i < 3; i++) {
            assert(scan(doc, len, steps[i]));
            assert(strcmp(whole, log_buf) == 0);
        }
        /* cut anywhere: never complete */
        for (size_t cut = 0; cut < len - 1; cut += 997) assert(!scan(doc, cut, 7));
        assert(!scan(doc, len - 2, 0)); /* ends "...null" without the "}" */
        free(whole);
        free(doc);
    }

    /* utf8_fit */
    assert(utf8_fit("abc", 3, 5) == 3);
    assert(utf8_fit("abc", 3, 2) == 2);
    assert(utf8_fit("a\xc3\xa9", 3, 2) == 1);
    assert(utf8_fit("a\xe2\x82\xac", 4, 3) == 1);
    assert(utf8_fit("a\xe2\x82\xac", 4, 4) == 4);
    assert(utf8_fit("\xf0\x9f\x98\x80z", 5, 4) == 4);
    assert(utf8_fit("\xf0\x9f\x98\x80z", 5, 3) == 0);
    return 0;
}
