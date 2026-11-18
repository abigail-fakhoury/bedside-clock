#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/reply.h"

static void expect(const char *content, bool ok, const char *title, int page)
{
    heard_t h = reply_parse(content);
    if (h.ok != ok || strcmp(h.title, title) != 0 || h.page != page) {
        fprintf(stderr, "input: %s\ngot  ok=%d title=\"%s\" page=%d\nwant ok=%d title=\"%s\" page=%d\n",
                content ? content : "(NULL)", h.ok, h.title, h.page, ok, title, page);
        assert(0);
    }
}

static void test_shapes(void)
{
    expect("{\"title\": \"Great Circle\", \"page\": 394}", true, "Great Circle", 394);
    expect("```json\n{\n  \"title\": \"Great Circle\",\n  \"page\": 394\n}\n```\n", true,
           "Great Circle", 394);
    expect("```\n{\"title\":\"Bird by Bird\",\"page\":12}\n```", true, "Bird by Bird", 12);
    expect("{\"title\": null, \"page\": null}", true, "", -1);
    expect("{\"page\": 240}", true, "", 240);
    expect("{\"title\":\"Great Circle\",\"page\":\"394\"}", true, "Great Circle", 394);
    expect("Sure! {\"title\": \"Dracula\", \"page\": 7} Hope that helps.", true, "Dracula", 7);
    expect("{}", true, "", -1);
    /* Unknown and nested members are ignored; only the first object counts */
    expect("{\"x\": {\"title\": \"No\", \"page\": 5}, \"page\": 6, \"y\": [1, {\"page\": 8}]}", true,
           "", 6);
    expect("{\"page\": 3} {\"page\": 4}", true, "", 3);
    /* A brace inside a string does not end the object */
    expect("{\"title\": \"a}b{c\", \"page\": 9}", true, "a}b{c", 9);
}

static void test_pages(void)
{
    expect("{\"page\": 0}", true, "", -1);
    expect("{\"page\": 123456}", true, "", -1);
    expect("{\"page\": 1}", true, "", 1);
    expect("{\"page\": 9999}", true, "", 9999);
    expect("{\"page\": 10000}", true, "", -1);
    expect("{\"page\": -5}", true, "", -1);
    expect("{\"page\": 39.5}", true, "", -1);
    expect("{\"page\": 4e2}", true, "", -1);
    expect("{\"page\": \"39x\"}", true, "", -1);
    expect("{\"page\": \"\"}", true, "", -1);
    expect("{\"page\": \" 394\"}", true, "", -1);
    expect("{\"page\": \"0394\"}", true, "", 394);
    expect("{\"page\": \"99999999999999999999\"}", true, "", -1);
    expect("{\"page\": true}", true, "", -1);
    expect("{\"page\": [394]}", true, "", -1);
}

static void test_titles(void)
{
    expect("{\"title\": \"The \\\"Quoted\\\" Book\", \"page\": 12}", true, "The \"Quoted\" Book",
           12);
    expect("{\"title\": \"Caf\\u00e9\", \"page\": 2}", true, "Caf\xc3\xa9", 2);
    expect("{\"title\": 42, \"page\": 2}", true, "", 2);
    expect("{\"title\": \"\", \"page\": 2}", true, "", 2);
    /* 70 bytes: cut to 63; with a 2-byte character across byte 63, to 62 */
    char in[160], want[64];
    memset(want, 'a', 63);
    want[63] = '\0';
    snprintf(in, sizeof(in), "{\"title\": \"%s1234567\", \"page\": 1}", want);
    expect(in, true, want, 1);
    char cut[64];
    memset(cut, 'a', 62);
    cut[62] = '\0';
    snprintf(in, sizeof(in), "{\"title\": \"%s\xc3\xa9zzz\", \"page\": 1}", cut);
    expect(in, true, cut, 1);
}

/* None of these may crash; none gives a title */
static void test_garbage(void)
{
    expect("{\"title\": \"Great Cir", false, "", -1);
    expect("", false, "", -1);
    expect("I heard Great Circle", false, "", -1);
    expect(NULL, false, "", -1);
    expect("{", false, "", -1);
    expect("}", false, "", -1);
    expect("{\"title\": \"x\\", false, "", -1);
    expect("{title: \"Great Circle\", page: 394}", false, "", -1);
    expect("{\"title\": \"Great Circle\" \"page\": 394}", false, "", -1);
    expect("{\"title\": \"Great Circle\", \"page\": 394,}", false, "", -1);
    expect("```json\n{\"title\": \"Great Circle\", \"page\": 3", false, "", -1);
    expect("{\"a\": [[[[{\"b\": 1}]]]]", false, "", -1);

    /* Deep nesting, long strings, every byte value */
    size_t n = 20000;
    char *big = malloc(n + 1);
    assert(big);
    memset(big, '{', n);
    big[n] = '\0';
    expect(big, false, "", -1);
    memset(big, '[', n);
    big[0] = '{';
    expect(big, false, "", -1);
    memset(big, 'x', n);
    memcpy(big, "{\"title\": \"", 11);
    memcpy(big + n - 2, "\"}", 2);
    heard_t h = reply_parse(big);
    assert(h.ok && strlen(h.title) == 63 && h.page == -1);
    for (size_t i = 0; i < n; i++) big[i] = (char)(1 + (i * 131) % 255);
    reply_parse(big);
    free(big);
}

/* A chat completions response as OpenRouter sends it (shape from the
 * spike), with the content in a ```json fence. */
static const char RESPONSE[] =
    "\n         \n{\"id\":\"gen-1762900000-abc\",\"provider\":\"Google\","
    "\"model\":\"google/gemini-3.5-flash-lite\",\"object\":\"chat.completion\","
    "\"created\":1762900000,\"choices\":[{\"logprobs\":null,\"finish_reason\":\"stop\","
    "\"native_finish_reason\":\"STOP\",\"index\":0,\"message\":{\"role\":\"assistant\","
    "\"content\":\"```json\\n{\\\"title\\\": \\\"Great Circle\\\", \\\"page\\\": 394}\\n```\","
    "\"refusal\":null,\"reasoning\":null}}],\"usage\":{\"prompt_tokens\":512,"
    "\"completion_tokens\":20,\"total_tokens\":532}}";

static void test_content(void)
{
    char out[256];
    assert(reply_content(RESPONSE, strlen(RESPONSE), out, sizeof(out)));
    assert(strcmp(out, "```json\n{\"title\": \"Great Circle\", \"page\": 394}\n```") == 0);
    heard_t h = reply_parse(out);
    assert(h.ok && strcmp(h.title, "Great Circle") == 0 && h.page == 394);

    /* Spacing, escapes, \u (one, and a surrogate pair), \/ */
    const char *r = "{\"choices\" : [ { \"message\" : { \"content\" : "
                    "\"a\\/b\\\\c\\td\\u00e9\\ud83d\\ude00\\ud800x\" } } ] }";
    assert(reply_content(r, strlen(r), out, sizeof(out)));
    assert(strcmp(out, "a/b\\c\td\xc3\xa9\xf0\x9f\x98\x80\xef\xbf\xbdx") == 0);

    /* A "content" key before "choices" or inside a string is not the reply */
    r = "{\"content\":\"no\",\"note\":\"say \\\"content\\\": \\\"no\\\"\","
        "\"choices\":[{\"message\":{\"content\":\"yes\"}}]}";
    assert(reply_content(r, strlen(r), out, sizeof(out)));
    assert(strcmp(out, "yes") == 0);

    /* No content: null, absent, an error reply, nothing */
    memset(out, 'z', sizeof(out));
    r = "{\"choices\":[{\"message\":{\"content\":null}}]}";
    assert(!reply_content(r, strlen(r), out, sizeof(out)) && out[0] == '\0');
    r = "{\"choices\":[]}";
    assert(!reply_content(r, strlen(r), out, sizeof(out)) && out[0] == '\0');
    r = "{\"error\":{\"message\":\"Key limit exceeded\",\"code\":402}}";
    assert(!reply_content(r, strlen(r), out, sizeof(out)) && out[0] == '\0');
    assert(!reply_content("", 0, out, sizeof(out)) && out[0] == '\0');
    assert(!reply_content(NULL, 0, out, sizeof(out)));
    assert(!reply_content(r, strlen(r), out, 0));

    /* Cut off by len (no terminator needed): unterminated is refused */
    char buf[sizeof(RESPONSE)];
    memcpy(buf, RESPONSE, sizeof(RESPONSE));
    const char *c = strstr(RESPONSE, "Great");
    size_t at = (size_t)(c - RESPONSE);
    buf[at + 3] = 'X'; /* would show if read past len */
    assert(!reply_content(buf, at + 3, out, sizeof(out)) && out[0] == '\0');
    r = "{\"choices\":[{\"message\":{\"content\":\"abc\\";
    assert(!reply_content(r, strlen(r), out, sizeof(out)));
    r = "{\"choices\":[{\"message\":{\"content\":\"\\u00";
    assert(!reply_content(r, strlen(r), out, sizeof(out)));
    r = "{\"choices\":[{\"message\":{\"content\":";
    assert(!reply_content(r, strlen(r), out, sizeof(out)));

    /* Every prefix, in a heap block of exactly that size (no NUL): under
     * ASan any read past len fails */
    size_t rl = strlen(RESPONSE);
    for (size_t k = 0; k <= rl; k++) {
        char *exact = malloc(k ? k : 1);
        assert(exact);
        memcpy(exact, RESPONSE, k);
        bool got = reply_content(exact, k, out, sizeof(out));
        assert(got == (k > (size_t)(strstr(RESPONSE, "```\",") - RESPONSE) + 3));
        free(exact);
    }

    /* Too long for out: cut at a character boundary */
    r = "{\"choices\":[{\"message\":{\"content\":\"abcd\xc3\xa9\\u00e9z\"}}]}";
    assert(reply_content(r, strlen(r), out, 6) && strcmp(out, "abcd") == 0);
    assert(reply_content(r, strlen(r), out, 7) && strcmp(out, "abcd\xc3\xa9") == 0);
    assert(reply_content(r, strlen(r), out, 8) && strcmp(out, "abcd\xc3\xa9") == 0);
    assert(reply_content(r, strlen(r), out, 9) && strcmp(out, "abcd\xc3\xa9\xc3\xa9") == 0);
    assert(reply_content(r, strlen(r), out, 1) && strcmp(out, "") == 0);
}

int main(void)
{
    test_shapes();
    test_pages();
    test_titles();
    test_garbage();
    test_content();
    return 0;
}
