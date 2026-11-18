#include <assert.h>
#include <string.h>

#include "core/shelf.h"
#include "core/total.h"

static shelf_t sh; /* big: keep off the stack */

static book_t *add(uint32_t id, uint16_t manual, uint16_t catalog)
{
    book_t *k = &sh.books[sh.count++];
    memset(k, 0, sizeof(*k));
    k->id = id;
    k->manual_pages = manual;
    k->catalog_pages = catalog;
    k->pages = manual ? manual : catalog;
    return k;
}

static total_result_t r(int status, const char *body)
{
    return total_reply(status, body, body ? strlen(body) : 0);
}

/* Which books get their total set before a session is posted */
static void test_decision(void)
{
    memset(&sh, 0, sizeof(sh));
    add(218218, 218, 0);    /* manual total set, no catalog */
    add(111, 300, 280);     /* manual total set, catalog differs */
    add(798662, 0, 528);    /* catalog only: The Pirate Queen */
    add(5990760, 0, 648);   /* catalog only */
    add(42, 0, 0);          /* neither */

    /* manual set: no PATCH, whatever the catalog says */
    assert(total_to_set(&sh, 218218, 11) == 0);
    assert(total_to_set(&sh, 111, 11) == 0);
    /* catalog only: PATCH with the catalog count */
    assert(total_to_set(&sh, 798662, 11) == 528);
    assert(total_to_set(&sh, 5990760, 30) == 648);
    /* neither: no PATCH */
    assert(total_to_set(&sh, 42, 11) == 0);
    /* not on the shelf, or no shelf at all: no PATCH */
    assert(total_to_set(&sh, 999, 11) == 0);
    assert(total_to_set(NULL, 798662, 11) == 0);
    sh.count = 0;
    assert(total_to_set(&sh, 798662, 11) == 0);
    sh.count = 5;
    /* the debug book (id 0) never */
    sh.books[4].id = 0;
    sh.books[4].catalog_pages = 100;
    assert(total_to_set(&sh, 0, 11) == 0);

    /* The API refuses a session whose page_at_end is above the total: a
     * catalog edition shorter than the page reached is not set (the
     * session posts without progress, as before, rather than failing) */
    assert(total_to_set(&sh, 798662, 528) == 528);
    assert(total_to_set(&sh, 798662, 529) == 0);
    assert(total_to_set(&sh, 798662, 0) == 528);

    /* only books within count are looked at */
    sh.books[10].id = 777;
    sh.books[10].catalog_pages = 100;
    assert(total_to_set(&sh, 777, 1) == 0);
}

/* After a PATCH that took: the cached book counts as set */
static void test_mark(void)
{
    memset(&sh, 0, sizeof(sh));
    add(1, 0, 400);
    book_t *k = add(798662, 0, 528);
    k->last_page = 7;
    assert(total_mark_set(&sh, 798662, 528));
    assert(k->manual_pages == 528 && k->catalog_pages == 528);
    assert(k->pages == 528 && k->last_page == 7);
    assert(total_to_set(&sh, 798662, 11) == 0); /* never PATCHed again */
    assert(total_to_set(&sh, 1, 11) == 400);    /* the others untouched */
    assert(!total_mark_set(&sh, 999, 100));
    assert(!total_mark_set(NULL, 1, 100));
    assert(!total_mark_set(&sh, 1, 0));
    assert(sh.books[0].manual_pages == 0);
}

/* The documented request: PATCH /books {"items":[{"id":…,"manual_total_pages":…}]} */
static void test_body(void)
{
    char buf[TOTAL_BODY_MAX];
    int n = total_body(buf, sizeof(buf), 798662, 528);
    assert(n > 0 && (size_t)n == strlen(buf));
    assert(strcmp(buf, "{\"items\":[{\"id\":798662,\"manual_total_pages\":528}]}") == 0);
    n = total_body(buf, sizeof(buf), 4294967295u, 65535);
    assert(strcmp(buf, "{\"items\":[{\"id\":4294967295,\"manual_total_pages\":65535}]}") == 0);
    assert(n < TOTAL_BODY_MAX);
    /* nothing to send: no id, no pages; too small a buffer */
    assert(total_body(buf, sizeof(buf), 0, 528) == -1);
    assert(total_body(buf, sizeof(buf), 798662, 0) == -1);
    assert(total_body(buf, 20, 798662, 528) == -1);
}

/* What the reply means (https://readwise.io/books_api, Update books:
 * 200 with {"results":[{"id":…,"status":"updated","error":null}]}) */
static void test_reply(void)
{
    assert(r(200, "{\"results\":[{\"id\":798662,\"status\":\"updated\",\"error\":null}]}") ==
           TOTAL_SET);
    assert(r(200, "{\n \"results\": [\n  {\n   \"id\": 4815162,\n   \"status\": \"updated\",\n"
                  "   \"error\": null\n  }\n ]\n}") == TOTAL_SET);
    assert(r(200, "{\"results\":[{\"error\":null,\"status\":\"updated\",\"id\":1}]}") == TOTAL_SET);

    /* a 200 batch can still carry an item error (an open app session gives
     * invalid_value, a book not yours not_found) */
    assert(r(200, "{\"results\":[{\"id\":1,\"status\":\"error\",\"error\":{\"code\":"
                  "\"invalid_value\",\"message\":\"m\",\"details\":[]}}]}") == TOTAL_FAILED);
    /* no results, empty, malformed, truncated */
    assert(r(200, "{\"results\":[]}") == TOTAL_FAILED);
    assert(r(200, "") == TOTAL_FAILED);
    assert(r(200, NULL) == TOTAL_FAILED);
    assert(r(200, "{\"results\":[{\"status\":\"updated\"}") == TOTAL_FAILED);
    assert(r(200, "{\"results\":[{\"status\":\"updated\"}]}x") == TOTAL_FAILED);
    assert(r(200, "{\"results\":[{\"status\":true}]}") == TOTAL_FAILED);
    /* only the first result counts, and a second one saying otherwise fails it */
    assert(r(200, "{\"results\":[{\"status\":\"updated\"},{\"status\":\"error\"}]}") ==
           TOTAL_FAILED);
    /* a reply that did not fit in TOTAL_REPLY_MAX is truncated */
    {
        static char big[TOTAL_REPLY_MAX + 8];
        const char *head = "{\"results\":[{\"status\":\"updated\"}],\"pad\":\"";
        strcpy(big, head);
        memset(big + strlen(head), 'x', TOTAL_REPLY_MAX - strlen(head));
        assert(total_reply(200, big, TOTAL_REPLY_MAX) == TOTAL_FAILED);
    }

    /* rate limited: the session waits for the next wake */
    assert(r(429, "{\"error\":{\"code\":\"rate_limited\",\"message\":\"m\",\"details\":[]}}") ==
           TOTAL_RATE_LIMITED);
    assert(r(429, NULL) == TOTAL_RATE_LIMITED);
    /* everything else fails: no reply (0), refused, server error, a redirect */
    assert(r(0, NULL) == TOTAL_FAILED);
    assert(r(400, "{}") == TOTAL_FAILED);
    assert(r(401, NULL) == TOTAL_FAILED);
    assert(r(403, NULL) == TOTAL_FAILED);
    assert(r(500, NULL) == TOTAL_FAILED);
    assert(r(301, NULL) == TOTAL_FAILED);
}

int main(void)
{
    test_decision();
    test_mark();
    test_body();
    test_reply();
    return 0;
}
