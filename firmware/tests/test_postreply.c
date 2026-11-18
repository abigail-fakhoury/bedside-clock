#include <assert.h>
#include <string.h>

#include "core/postreply.h"

static outbox_state_t r200(const char *body)
{
    return post_reply_state(200, true, body, strlen(body));
}

/* The documented reply: https://readwise.io/books_api, Record sessions */
static const char CREATED[] =
    "{\n  \"results\": [\n    {\n      \"status\": \"created\",\n"
    "      \"session_id\": 90211876,\n      \"error\": null\n    }\n  ]\n}";

static void test_created(void)
{
    assert(r200(CREATED) == O_EMPTY);
    assert(r200("{\"results\":[{\"status\":\"created\",\"session_id\":1,\"error\":null}]}") ==
           O_EMPTY);
    /* the order of the members does not matter */
    assert(r200("{\"results\":[{\"error\":null,\"session_id\":1,\"status\":\"created\"}]}") ==
           O_EMPTY);
    /* other top-level members are fine */
    assert(r200("{\"x\":{\"status\":\"error\"},\"results\":[{\"status\":\"created\"}]}") ==
           O_EMPTY);
}

static void test_other_status(void)
{
    assert(r200("{\"results\":[{\"status\":\"existing\",\"session_id\":1,\"error\":null}]}") ==
           O_CHECK);
    assert(r200("{\"results\":[{\"status\":\"error\",\"session_id\":null,\"error\":"
                "{\"code\":\"not_found\",\"message\":\"No such book\",\"details\":[]}}]}") ==
           O_CHECK);
    assert(r200("{\"results\":[{\"status\":\"CREATED\"}]}") == O_CHECK);
    assert(r200("{\"results\":[{\"status\":\"created \"}]}") == O_CHECK);
    assert(r200("{\"results\":[{\"status\":true}]}") == O_CHECK); /* not a string */
    /* only the first result counts (one item is sent) */
    assert(r200("{\"results\":[{\"status\":\"error\"},{\"status\":\"created\"}]}") == O_CHECK);
    /* "created" anywhere but results[0].status */
    assert(r200("{\"status\":\"created\"}") == O_CHECK);
    assert(r200("{\"results\":[{\"error\":{\"status\":\"created\"}}]}") == O_CHECK);
    assert(r200("{\"results\":{\"status\":\"created\"}}") == O_CHECK);
    assert(r200("{\"results\":[[{\"status\":\"created\"}]]}") == O_CHECK);
    assert(r200("{\"results\":[]}") == O_CHECK);
    assert(r200("{}") == O_CHECK);
    assert(r200("[]") == O_CHECK);
    assert(r200("\"created\"") == O_CHECK);
}

static void test_bad_body(void)
{
    assert(post_reply_state(200, true, NULL, 0) == O_CHECK);
    assert(post_reply_state(200, true, "", 0) == O_CHECK);
    /* truncated anywhere: never created */
    size_t n = strlen(CREATED);
    for (size_t i = 0; i < n; i++) {
        assert(post_reply_state(200, true, CREATED, i) == O_CHECK);
    }
    assert(r200("{\"results\":[{\"status\":\"created\"}]}x") == O_CHECK); /* junk after */
    assert(r200("{\"results\":[{\"status\":\"created\"}]}{}") == O_CHECK);
    assert(r200("{\"results\":[{\"status\":\"created\",}]}") == O_CHECK);
    assert(r200("<html>OK</html>") == O_CHECK);
    /* a reply that did not fit in POST_REPLY_MAX counts as truncated */
    static const char ok[] = "{\"results\":[{\"status\":\"created\"}]}";
    const size_t k = sizeof(ok) - 1;
    char big[POST_REPLY_MAX + 64];
    memset(big, ' ', sizeof(big));
    memcpy(big, ok, k);
    assert(post_reply_state(200, true, big, k) == O_EMPTY);
    assert(post_reply_state(200, true, big, POST_REPLY_MAX - 1) == O_EMPTY); /* spaces after */
    assert(post_reply_state(200, true, big, POST_REPLY_MAX) == O_CHECK); /* may have been cut */
    assert(post_reply_state(200, true, big, sizeof(big)) == O_CHECK);
    /* the reply ending on the last byte that fits is whole */
    memset(big, ' ', sizeof(big));
    memcpy(big + POST_REPLY_MAX - 1 - k, ok, k);
    assert(post_reply_state(200, true, big, POST_REPLY_MAX - 1) == O_EMPTY);
    assert(post_reply_state(200, true, big, POST_REPLY_MAX) == O_CHECK);
}

static void test_codes(void)
{
    /* not sent: kept to send later, whatever else is said */
    assert(post_reply_state(0, false, NULL, 0) == O_PENDING);
    assert(post_reply_state(200, false, CREATED, strlen(CREATED)) == O_PENDING);
    /* refused before anything was recorded: send again later */
    assert(post_reply_state(401, true, NULL, 0) == O_PENDING);
    assert(post_reply_state(403, true, "{\"error\":{}}", 12) == O_PENDING);
    assert(post_reply_state(429, true, NULL, 0) == O_PENDING);
    /* the rest: unknown, never resent, even with "created" in the body */
    assert(post_reply_state(0, true, NULL, 0) == O_CHECK); /* sent, no reply */
    assert(post_reply_state(201, true, CREATED, strlen(CREATED)) == O_CHECK);
    assert(post_reply_state(400, true, NULL, 0) == O_CHECK);
    assert(post_reply_state(404, true, NULL, 0) == O_CHECK);
    assert(post_reply_state(413, true, NULL, 0) == O_CHECK);
    assert(post_reply_state(500, true, CREATED, strlen(CREATED)) == O_CHECK);
    assert(post_reply_state(502, true, NULL, 0) == O_CHECK);
    assert(post_reply_state(-1, true, NULL, 0) == O_CHECK);
}

/* The body could not be built (bad times or tz): nothing left the clock,
 * but it never can, so it is parked for "Check Readwise", not kept */
static void test_unpostable(void)
{
    assert(post_reply_state(POST_UNPOSTABLE, false, NULL, 0) == O_CHECK);
    assert(post_reply_state(POST_UNPOSTABLE, true, NULL, 0) == O_CHECK);
    assert(POST_UNPOSTABLE != 0 && POST_UNPOSTABLE != 200);
}

int main(void)
{
    test_created();
    test_other_status();
    test_bad_body();
    test_codes();
    test_unpostable();
    return 0;
}
