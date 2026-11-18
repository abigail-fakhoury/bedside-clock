#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "core/body.h"
#include "core/jscan.h"

#define T0 1794452645 /* 2026-11-12T03:04:05Z */

static const char EXPECTED[] =
    "{\"items\":[{\"book_id\":5608920,\"format\":\"paper\","
    "\"started_at\":\"2026-11-12T03:04:05Z\",\"ended_at\":\"2026-11-12T03:51:30Z\","
    "\"page_at_start\":394,\"page_at_end\":412,\"timezone\":\"America/Los_Angeles\","
    "\"publish_to_feed\":true,"
    "\"device\":{\"model\":\"Bedside Clock\",\"vendor\":\"Waveshare\"}}]}";

static session_t fixed(void)
{
    session_t s;
    memset(&s, 0, sizeof(s));
    s.phase = S_ENDED;
    s.book_id = 5608920;
    snprintf(s.title, sizeof(s.title), "%s", "Great Circle");
    s.page_start = 394;
    s.page_end = 412;
    s.started_at = T0;
    s.ended_at = T0 + 2845;
    return s;
}

static void test_exact(void)
{
    char out[512];
    session_t s = fixed();
    int n = body_session(out, sizeof(out), &s, "America/Los_Angeles");
    if (n < 0 || strcmp(out, EXPECTED) != 0) {
        fprintf(stderr, "got (%d):  %s\nwant (%d): %s\n", n, n < 0 ? "" : out,
                (int)strlen(EXPECTED), EXPECTED);
        assert(0);
    }
    assert(n == (int)strlen(EXPECTED));
}

/* The title is not in the body, so no title can break the JSON. */
static void test_title_absent(void)
{
    char out[512];
    session_t s = fixed();
    snprintf(s.title, sizeof(s.title), "%s", "The \"Quoted\" Book\\\",\"x\":1");
    int n = body_session(out, sizeof(out), &s, "America/Los_Angeles");
    assert(n == (int)strlen(EXPECTED));
    assert(strcmp(out, EXPECTED) == 0);
    assert(strstr(out, "Quoted") == NULL);
    assert(strstr(out, "title") == NULL);
}

static void test_cap(void)
{
    char out[512];
    session_t s = fixed();
    size_t need = strlen(EXPECTED) + 1;
    memset(out, 'z', sizeof(out));
    assert(body_session(out, need - 1, &s, "America/Los_Angeles") == -1);
    assert(out[need - 1] == 'z'); /* nothing past cap */
    assert(body_session(out, 0, &s, "America/Los_Angeles") == -1);
    assert(body_session(NULL, 0, &s, "America/Los_Angeles") == -1);
    assert(body_session(out, need, &s, "America/Los_Angeles") == (int)need - 1);
    assert(strcmp(out, EXPECTED) == 0);
}

static void test_refused(void)
{
    char out[512];
    session_t s = fixed();
    assert(body_session(out, sizeof(out), NULL, "UTC") == -1);

    s.started_at = 0; /* RTC never set */
    assert(body_session(out, sizeof(out), &s, "UTC") == -1);
    s = fixed();
    s.started_at = SESSION_EARLIEST - 1;
    s.ended_at = SESSION_EARLIEST;
    assert(body_session(out, sizeof(out), &s, "UTC") == -1);
    s = fixed();
    s.ended_at = s.started_at - 1;
    assert(body_session(out, sizeof(out), &s, "UTC") == -1);
    s = fixed();
    s.page_end = s.page_start - 1;
    assert(body_session(out, sizeof(out), &s, "UTC") == -1);

    s = fixed();
    assert(body_session(out, sizeof(out), &s, NULL) == -1);
    assert(body_session(out, sizeof(out), &s, "") == -1);
    assert(body_session(out, sizeof(out), &s, "America/\"x") == -1);
    assert(body_session(out, sizeof(out), &s, "Europe/Paris\n") == -1);
    assert(body_session(out, sizeof(out), &s, "America/Los Angeles") == -1);
    char tz[65];
    memset(tz, 'A', 64);
    tz[64] = '\0';
    assert(body_session(out, sizeof(out), &s, tz) == -1); /* 64 bytes */
    tz[63] = '\0';
    assert(body_session(out, sizeof(out), &s, tz) > 0);    /* 63 bytes */
    assert(body_session(out, sizeof(out), &s, "Etc/GMT+8") > 0);
    assert(body_session(out, sizeof(out), &s, "America/Port-au-Prince") > 0);
}

static void test_edges(void)
{
    char out[512];
    session_t s = fixed();
    /* No pages read, zero length, a leap day, the RTC's last second. */
    s.page_end = s.page_start;
    s.started_at = 1835481599; /* 2028-02-29T23:59:59Z */
    s.ended_at = 1835481599;
    assert(body_session(out, sizeof(out), &s, "UTC") > 0);
    assert(strstr(out, "\"started_at\":\"2028-02-29T23:59:59Z\",\"ended_at\":\"2028-02-29T23:59:59Z\""));
    assert(strstr(out, "\"page_at_start\":394,\"page_at_end\":394,"));
    s.started_at = 1767225600;
    s.ended_at = 4102444799;
    assert(body_session(out, sizeof(out), &s, "UTC") > 0);
    assert(strstr(out, "\"started_at\":\"2026-01-01T00:00:00Z\",\"ended_at\":\"2099-12-31T23:59:59Z\""));
    /* Largest ids and pages. */
    s = fixed();
    s.book_id = 4294967295u;
    s.page_start = 65535;
    s.page_end = 65535;
    assert(body_session(out, sizeof(out), &s, "UTC") > 0);
    assert(strstr(out, "{\"book_id\":4294967295,"));
    assert(strstr(out, "\"page_at_start\":65535,\"page_at_end\":65535,"));
}


#define MODEL "google/gemini-3.5-flash-lite"

static const char HEAD_START[] =
    "{\"model\":\"google/gemini-3.5-flash-lite\",\"max_tokens\":300,\"messages\":[{\"role\":\"user\","
    "\"content\":[{\"type\":\"text\",\"text\":\"The audio is someone saying which book they are "
    "about to read and the page they are on. Their shelf, to help with spelling:\\n"
    "Great Circle - Maggie Shipstead\\nBird by Bird\\n"
    "Reply with only JSON: {\\\"title\\\": \\\"<book title as said, or null>\\\", \\\"page\\\": "
    "<integer or null>}. Use null for anything not clearly said.\"},"
    "{\"type\":\"input_audio\",\"input_audio\":{\"format\":\"wav\",\"data\":\"";

static const char HEAD_END[] =
    "{\"model\":\"google/gemini-3.5-flash-lite\",\"max_tokens\":300,\"messages\":[{\"role\":\"user\","
    "\"content\":[{\"type\":\"text\",\"text\":\"The audio is someone saying the page number they "
    "stopped reading on. Reply with only JSON: {\\\"page\\\": <integer or null>}. Use null if no "
    "number is clearly said.\"},"
    "{\"type\":\"input_audio\",\"input_audio\":{\"format\":\"wav\",\"data\":\"";

static shelf_t two_books(void)
{
    shelf_t s;
    memset(&s, 0, sizeof(s));
    s.count = 2;
    s.books[0].id = 1;
    snprintf(s.books[0].title, sizeof(s.books[0].title), "%s", "Great Circle");
    snprintf(s.books[0].author, sizeof(s.books[0].author), "%s", "Maggie Shipstead");
    s.books[1].id = 2;
    snprintf(s.books[1].title, sizeof(s.books[1].title), "%s", "Bird by Bird");
    return s; /* no author: the line is just the title */
}

static void same(const char *got, int n, const char *want)
{
    if (n < 0 || strcmp(got, want) != 0) {
        fprintf(stderr, "got (%d):  %s\nwant (%d): %s\n", n, n < 0 ? "" : got, (int)strlen(want),
                want);
        assert(0);
    }
    assert(n == (int)strlen(want));
}

/* The whole request (head, some base64, tail) is one valid JSON document */
typedef struct {
    int model, tokens, text, audio, wav, data;
} seen_t;

static void on_value(void *ctx, const char *path, const char *v, bool is_string)
{
    seen_t *s = ctx;
    if (!v) return;
    if (strcmp(path, "model") == 0 && is_string && strcmp(v, MODEL) == 0) s->model++;
    if (strcmp(path, "max_tokens") == 0 && !is_string && strcmp(v, "300") == 0) s->tokens++;
    if (strcmp(path, "messages[].content[].type") == 0 && strcmp(v, "text") == 0) s->text++;
    if (strcmp(path, "messages[].content[].type") == 0 && strcmp(v, "input_audio") == 0) s->audio++;
    if (strcmp(path, "messages[].content[].input_audio.format") == 0 && strcmp(v, "wav") == 0)
        s->wav++;
    if (strcmp(path, "messages[].content[].input_audio.data") == 0 && strcmp(v, "UklGRg==") == 0)
        s->data++;
}

static void valid_request(const char *head)
{
    jscan_t j;
    seen_t s = {0};
    jscan_init(&j, on_value, &s);
    assert(jscan_feed(&j, head, strlen(head)));
    assert(jscan_feed(&j, "UklGRg==", 8));
    assert(jscan_feed(&j, BODY_VOICE_TAIL, strlen(BODY_VOICE_TAIL)));
    assert(jscan_finish(&j));
    assert(s.model == 1 && s.tokens == 1 && s.text == 1 && s.audio == 1 && s.wav == 1 &&
           s.data == 1);
}

static void test_voice_head(void)
{
    char out[8192];
    shelf_t s = two_books();
    int n = body_voice_head(out, sizeof(out), MODEL, true, &s);
    same(out, n, HEAD_START);
    valid_request(out);

    n = body_voice_head(out, sizeof(out), MODEL, false, NULL);
    same(out, n, HEAD_END);
    valid_request(out);
    n = body_voice_head(out, sizeof(out), MODEL, false, &s); /* the shelf is not listed */
    same(out, n, HEAD_END);

    assert(strcmp(BODY_VOICE_TAIL, "\"}}]}]}") == 0);

    /* An empty shelf lists nothing */
    shelf_t empty;
    memset(&empty, 0, sizeof(empty));
    n = body_voice_head(out, sizeof(out), MODEL, true, &empty);
    assert(n > 0 && strstr(out, "spelling:\\nReply with only JSON"));
    valid_request(out);
}

/* Titles, authors and the model are escaped as JSON strings */
static void test_voice_escaping(void)
{
    char out[8192];
    shelf_t s;
    memset(&s, 0, sizeof(s));
    s.count = 3;
    snprintf(s.books[0].title, sizeof(s.books[0].title), "%s", "The \"Quoted\" Book");
    snprintf(s.books[0].author, sizeof(s.books[0].author), "%s", "A\\B");
    snprintf(s.books[1].title, sizeof(s.books[1].title), "%s", "Tab\there\nnew\rline\x01\x1f\x7f");
    snprintf(s.books[1].author, sizeof(s.books[1].author), "%s", "Caf\xc3\xa9 </script>");
    snprintf(s.books[2].title, sizeof(s.books[2].title), "%s", "\",\"model\":\"x");
    int n = body_voice_head(out, sizeof(out), MODEL, true, &s);
    assert(n > 0);
    assert(strstr(out, "\\nThe \\\"Quoted\\\" Book - A\\\\B\\n"));
    assert(strstr(out, "\\nTab\\there\\nnew\\rline\\u0001\\u001f\x7f - Caf\xc3\xa9 </script>\\n"));
    assert(strstr(out, "\\n\\\",\\\"model\\\":\\\"x\\n"));
    for (int i = 0; i < n; i++) assert((unsigned char)out[i] >= 0x20); /* no raw control bytes */
    valid_request(out);

    /* A model name with a quote cannot break out either */
    n = body_voice_head(out, sizeof(out), "a\"b", false, NULL);
    assert(n > 0 && strstr(out, "{\"model\":\"a\\\"b\",\"max_tokens\":300,"));

    /* A full shelf of long titles fits in 6 KB */
    shelf_t full;
    memset(&full, 0, sizeof(full));
    full.count = SHELF_MAX;
    for (int i = 0; i < SHELF_MAX; i++) {
        memset(full.books[i].title, 'T', 63);
        memset(full.books[i].author, 'A', 31);
    }
    n = body_voice_head(out, 6144, MODEL, true, &full);
    assert(n > 0 && n < 6144);
    valid_request(out);
}

static void test_voice_refused(void)
{
    char out[8192];
    shelf_t s = two_books();
    size_t need = strlen(HEAD_START) + 1;
    memset(out, 'z', sizeof(out));
    assert(body_voice_head(out, need - 1, MODEL, true, &s) == -1);
    assert(out[need - 1] == 'z'); /* nothing past cap */
    assert(body_voice_head(out, 1, MODEL, true, &s) == -1);
    assert(body_voice_head(out, 0, MODEL, true, &s) == -1);
    assert(body_voice_head(NULL, 0, MODEL, true, &s) == -1);
    assert(body_voice_head(out, need, MODEL, true, &s) == (int)need - 1);
    assert(strcmp(out, HEAD_START) == 0);

    need = strlen(HEAD_END) + 1;
    assert(body_voice_head(out, need - 1, MODEL, false, NULL) == -1);
    assert(body_voice_head(out, need, MODEL, false, NULL) == (int)need - 1);

    assert(body_voice_head(out, sizeof(out), NULL, false, NULL) == -1);
    assert(body_voice_head(out, sizeof(out), MODEL, true, NULL) == -1);
    s.count = SHELF_MAX + 1; /* a corrupt count reads no further than the shelf */
    assert(body_voice_head(out, sizeof(out), MODEL, true, &s) > 0);
}

int main(void)
{
    test_exact();
    test_title_absent();
    test_cap();
    test_refused();
    test_edges();
    test_voice_head();
    test_voice_escaping();
    test_voice_refused();
    return 0;
}
