#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/jscan.h"
#include "core/shelf.h"

/* Builds a shelf from doc fed in pieces of `step` bytes (0 = whole);
 * returns jscan_finish() */
static bool build(const char *doc, size_t len, size_t step, shelf_t *out, shelf_builder_t *b)
{
    jscan_t s;
    shelf_builder_init(b, out);
    jscan_init(&s, shelf_on_value, b);
    if (!step) step = len ? len : 1;
    for (size_t i = 0; i < len; i += step)
        jscan_feed(&s, doc + i, len - i < step ? len - i : step);
    return jscan_finish(&s);
}

static bool build_str(const char *doc, shelf_t *out)
{
    shelf_builder_t b;
    return build(doc, strlen(doc), 0, out, &b);
}

static bool same_shelf(const shelf_t *a, const shelf_t *b)
{
    if (a->count != b->count || a->fetched_at != b->fetched_at) return false;
    for (int i = 0; i < a->count; i++) {
        const book_t *x = &a->books[i], *y = &b->books[i];
        if (x->id != y->id || strcmp(x->title, y->title) || strcmp(x->author, y->author) ||
            x->pages != y->pages || x->last_page != y->last_page ||
            x->manual_pages != y->manual_pages || x->catalog_pages != y->catalog_pages)
            return false;
    }
    return true;
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

/* One synthetic book object with id i */
static void add_book(char *doc, size_t cap, int i)
{
    char one[256];
    snprintf(one, sizeof(one),
             "%s{\"id\":%d,\"title\":\"Book %d\",\"author\":\"A\",\"progress\":0.5,"
             "\"manual_total_pages\":null,\"product\":{\"page_count\":%d}}",
             strchr(doc, '{') && doc[strlen(doc) - 1] != '[' ? "," : "", i, i, 100 + i);
    assert(strlen(doc) + strlen(one) < cap);
    strcat(doc, one);
}

static shelf_t sa, sb, sc; /* big: keep off the stack */

int main(void)
{
    /* The captured response, in any chunk size */
    {
        size_t len;
        char *doc = read_file("tests/fixtures/books.json", &len);
        shelf_builder_t b;
        assert(build(doc, len, 0, &sa, &b));
        assert(sa.count == 19 && b.seen == 19);
        assert(b.next[0] == '\0'); /* "next": null */
        assert(build(doc, len, 1, &sb, &b));
        assert(same_shelf(&sa, &sb));
        assert(build(doc, len, 7, &sb, &b));
        assert(same_shelf(&sa, &sb));
        assert(sa.fetched_at == 0);

        /* "product": null, no progress */
        assert(sa.books[0].id == 7124117 && strcmp(sa.books[0].title, "Dracula") == 0);
        assert(strcmp(sa.books[0].author, "Stoker, Bram") == 0);
        assert(sa.books[0].pages == 0 && sa.books[0].last_page == 0);
        /* product.page_count, round(0.0210 x 648) = 14 */
        assert(sa.books[1].id == 5990760 && sa.books[1].pages == 648 &&
               sa.books[1].last_page == 14);
        /* catalog only: the total is not set on Readwise */
        assert(sa.books[1].manual_pages == 0 && sa.books[1].catalog_pages == 648);
        assert(sa.books[0].manual_pages == 0 && sa.books[0].catalog_pages == 0);
        assert(strcmp(sa.books[1].title, "House of Earth and Blood") == 0);
        /* product.title and product.author never replace the book's own */
        assert(strcmp(sa.books[1].author, "Sarah J. Maas") == 0);
        /* page count but progress null */
        assert(sa.books[2].pages == 272 && sa.books[2].last_page == 0);
        /* manual_total_pages with "product": null: 0.1789 x 218 = 39 */
        assert(sa.books[12].pages == 218 && sa.books[12].last_page == 39);
        assert(sa.books[12].manual_pages == 218 && sa.books[12].catalog_pages == 0);
        /* progress but no page count anywhere */
        assert(sa.books[16].pages == 0 && sa.books[16].last_page == 0);
        assert(sa.books[18].id == 2847 && sa.books[18].pages == 320 &&
               sa.books[18].last_page == 35);
        /* a long title cut to fit 63 bytes; an 86-byte author to 31 */
        assert(strlen(sa.books[9].title) == 63);
        assert(strncmp(sa.books[9].title, "The Seven Principles of Lighthouse Keeping", 42) == 0);
        assert(strlen(sa.books[17].author) == 31);

        /* A truncated document is refused */
        assert(!build(doc, len - 1, 7, &sb, &b));
        assert(!build(doc, len / 2, 0, &sb, &b));
        free(doc);
    }

    /* Titles: an escaped quote, non-ASCII (kept as UTF-8, raw or \u) */
    assert(build_str("{\"results\":["
                     "{\"id\":1,\"title\":\"The \\\"Real\\\" Thing\",\"author\":\"Stoppard\"},"
                     "{\"id\":2,\"title\":\"Les Mis\xc3\xa9rables\",\"author\":\"Hugo\"},"
                     "{\"id\":3,\"title\":\"Caf\\u00e9 \\u2014 \\ud83d\\udcda\",\"author\":"
                     "\"Bront\\u00eb\"}"
                     "],\"next\":null}",
                     &sa));
    assert(sa.count == 3);
    assert(strcmp(sa.books[0].title, "The \"Real\" Thing") == 0);
    assert(strcmp(sa.books[1].title, "Les Mis\xc3\xa9rables") == 0);
    assert(strcmp(sa.books[2].title, "Caf\xc3\xa9 \xe2\x80\x94 \xf0\x9f\x93\x9a") == 0);
    assert(strcmp(sa.books[2].author, "Bront\xc3\xab") == 0);

    /* A 300-character title is cut at a character boundary */
    {
        static char doc[4096];
        strcpy(doc, "{\"results\":[{\"id\":1,\"title\":\"");
        for (int i = 0; i < 300; i++) strcat(doc, "a");
        strcat(doc, "\"},{\"id\":2,\"title\":\"");
        for (int i = 0; i < 300; i++) strcat(doc, "\xc3\xa9"); /* 2 bytes each */
        strcat(doc, "\"},{\"id\":3,\"title\":\"");
        for (int i = 0; i < 300; i++) strcat(doc, "\xe2\x82\xac"); /* 3 bytes each */
        strcat(doc, "\"}]}");
        assert(build_str(doc, &sa));
        assert(sa.count == 3);
        assert(strlen(sa.books[0].title) == 63);
        assert(strlen(sa.books[1].title) == 62); /* 31 whole characters */
        for (int i = 0; i < 62; i += 2) assert(memcmp(sa.books[1].title + i, "\xc3\xa9", 2) == 0);
        assert(strlen(sa.books[2].title) == 63); /* 21 whole characters */
        for (int i = 0; i < 63; i += 3)
            assert(memcmp(sa.books[2].title + i, "\xe2\x82\xac", 3) == 0);
    }

    /* Pages: manual wins over the product; null product; rounding; clamps */
    assert(build_str("{\"results\":["
                     "{\"id\":1,\"progress\":0.5,\"manual_total_pages\":101,"
                     "\"product\":{\"page_count\":300}},"
                     "{\"id\":2,\"progress\":0.25,\"manual_total_pages\":null,\"product\":null},"
                     "{\"id\":3,\"progress\":0.5,\"product\":{\"page_count\":3}},"
                     "{\"id\":4,\"progress\":1.5,\"product\":{\"page_count\":200}},"
                     "{\"id\":5,\"progress\":-1,\"product\":{\"page_count\":200}},"
                     "{\"id\":6,\"progress\":1e-3,\"product\":{\"page_count\":999999}},"
                     "{\"id\":7,\"progress\":0.5,\"manual_total_pages\":0,"
                     "\"product\":{\"page_count\":80}},"
                     "{\"id\":8,\"product\":{\"page_count\":12.7}}"
                     "]}",
                     &sa));
    assert(sa.count == 8);
    assert(sa.books[0].pages == 101 && sa.books[0].last_page == 51); /* 50.5 rounds up */
    assert(sa.books[1].pages == 0 && sa.books[1].last_page == 0);
    assert(sa.books[2].pages == 3 && sa.books[2].last_page == 2);    /* 1.5 rounds up */
    assert(sa.books[3].pages == 200 && sa.books[3].last_page == 200);/* progress clamped */
    assert(sa.books[4].pages == 200 && sa.books[4].last_page == 0);
    assert(sa.books[5].pages == 65535 && sa.books[5].last_page == 66);
    assert(sa.books[6].pages == 80 && sa.books[6].last_page == 40);  /* manual 0 = unset */
    assert(sa.books[7].pages == 12 && sa.books[7].last_page == 0);
    assert(sa.books[0].title[0] == '\0' && sa.books[0].author[0] == '\0');
    /* both numbers kept apart, as sent (clamped the same way) */
    assert(sa.books[0].manual_pages == 101 && sa.books[0].catalog_pages == 300);
    assert(sa.books[1].manual_pages == 0 && sa.books[1].catalog_pages == 0);
    assert(sa.books[2].manual_pages == 0 && sa.books[2].catalog_pages == 3);
    assert(sa.books[5].catalog_pages == 65535);
    assert(sa.books[6].manual_pages == 0 && sa.books[6].catalog_pages == 80);

    /* 45 books keep the first 40 */
    {
        static char doc[16384];
        strcpy(doc, "{\"results\":[");
        for (int i = 1; i <= 45; i++) add_book(doc, sizeof(doc), i);
        strcat(doc, "],\"next\":null}");
        shelf_builder_t b;
        assert(build(doc, strlen(doc), 3, &sa, &b));
        assert(sa.count == SHELF_MAX && b.seen == 45);
        assert(sa.books[0].id == 1 && sa.books[39].id == 40);
        assert(strcmp(sa.books[39].title, "Book 40") == 0);
        assert(sa.books[39].pages == 140 && sa.books[39].last_page == 70);
    }

    /* Two pages through one builder; "next" captured per page */
    {
        static char p1[8192], p2[8192];
        strcpy(p1, "{\"results\":[");
        for (int i = 1; i <= 30; i++) add_book(p1, sizeof(p1), i);
        strcat(p1, "],\"next\":\"https://readwise.io/books/api/v1/books?cursor=abc123&limit=100"
                   "&status=currently_reading\"}");
        strcpy(p2, "{\"next\":null,\"results\":[");
        for (int i = 31; i <= 50; i++) add_book(p2, sizeof(p2), i);
        strcat(p2, "]}");

        shelf_builder_t b;
        jscan_t s;
        shelf_builder_init(&b, &sa);
        jscan_init(&s, shelf_on_value, &b);
        assert(jscan_feed(&s, p1, strlen(p1)) && jscan_finish(&s));
        assert(sa.count == 30);
        assert(strcmp(b.next, "https://readwise.io/books/api/v1/books?cursor=abc123&limit=100"
                              "&status=currently_reading") == 0);
        assert(shelf_next_url_ok(b.next));
        shelf_builder_next_page(&b);
        assert(b.next[0] == '\0');
        jscan_init(&s, shelf_on_value, &b);
        assert(jscan_feed(&s, p2, strlen(p2)) && jscan_finish(&s));
        assert(sa.count == 40 && b.seen == 50 && sa.books[39].id == 40);
        assert(b.next[0] == '\0');
    }

    /* Which "next" URLs may be followed */
    assert(shelf_next_url_ok("https://readwise.io/books/api/v1/books?cursor=x"));
    assert(!shelf_next_url_ok(""));
    assert(!shelf_next_url_ok("https://evil.example/books/api/v1/books?cursor=x"));
    assert(!shelf_next_url_ok("https://readwise.io.evil.example/books/api/v1/x"));
    assert(!shelf_next_url_ok("http://readwise.io/books/api/v1/books?cursor=x"));
    assert(!shelf_next_url_ok("https://readwise.io/books/api/v1/books?cursor=a b"));
    assert(!shelf_next_url_ok("https://readwise.io/books/api/v1/books?cursor=a\r\nX: y"));
    {
        char longurl[JSCAN_VALUE_MAX];
        strcpy(longurl, SHELF_API_BASE);
        while (strlen(longurl) < JSCAN_VALUE_MAX - 1) strcat(longurl, "x");
        assert(!shelf_next_url_ok(longurl)); /* 127 bytes: may have been truncated */
        longurl[JSCAN_VALUE_MAX - 2] = '\0';
        assert(shelf_next_url_ok(longurl));
    }

    /* A "next" that is not a string is ignored */
    {
        shelf_builder_t b;
        const char *doc = "{\"results\":[],\"next\":42}";
        assert(build(doc, strlen(doc), 0, &sa, &b));
        assert(sa.count == 0 && b.next[0] == '\0');
    }

    /* Cache blob: round trip */
    {
        static uint8_t blob[SHELF_BLOB_HEADER + (SHELF_MAX + 1) * SHELF_BOOK_BYTES];
        size_t len;
        char *doc = read_file("tests/fixtures/books.json", &len);
        shelf_builder_t b;
        assert(build(doc, len, 0, &sa, &b));
        free(doc);
        sa.fetched_at = 1794000000;
        size_t n = shelf_pack(&sa, blob, sizeof(blob));
        assert(n == SHELF_BLOB_HEADER + 19 * SHELF_BOOK_BYTES);
        assert(blob[0] == SHELF_BLOB_VERSION && blob[1] == 19);
        memset(&sb, 0x5a, sizeof(sb));
        assert(shelf_unpack(&sb, blob, n));
        assert(same_shelf(&sa, &sb));

        /* the two page numbers travel too */
        sa.books[0].manual_pages = 1234;
        sa.books[0].catalog_pages = 65535;
        n = shelf_pack(&sa, blob, sizeof(blob));
        assert(shelf_unpack(&sb, blob, n) && same_shelf(&sa, &sb));
        assert(sb.books[0].manual_pages == 1234 && sb.books[0].catalog_pages == 65535);

        /* too small a buffer */
        assert(shelf_pack(&sa, blob, n - 1) == 0);
        /* unknown version, wrong size: no cache, shelf emptied */
        blob[0] = SHELF_BLOB_VERSION + 1;
        memset(&sb, 0x5a, sizeof(sb));
        assert(!shelf_unpack(&sb, blob, n));
        assert(sb.count == 0);
        blob[0] = SHELF_BLOB_VERSION;
        assert(!shelf_unpack(&sb, blob, n - 1));
        assert(!shelf_unpack(&sb, blob, n + 1));
        assert(!shelf_unpack(&sb, blob, 0));
        assert(!shelf_unpack(&sb, NULL, 0));
        assert(!shelf_unpack(&sb, blob, 5));
        blob[1] = 18;
        assert(!shelf_unpack(&sb, blob, n)); /* count and size disagree */
        blob[1] = 19;
        assert(shelf_unpack(&sb, blob, n));

        /* a title with no terminator in the blob is still terminated */
        memset(blob + SHELF_BLOB_HEADER + 4, 'x', 64);
        assert(shelf_unpack(&sb, blob, n));
        assert(strlen(sb.books[0].title) == 63);

        /* the full shelf; the empty shelf; a count past SHELF_MAX */
        memset(&sc, 0, sizeof(sc));
        sc.count = SHELF_MAX;
        for (int i = 0; i < SHELF_MAX; i++) {
            sc.books[i].id = 4000000000u + (uint32_t)i;
            snprintf(sc.books[i].title, sizeof(sc.books[i].title), "T%d", i);
            sc.books[i].pages = 65535;
            sc.books[i].last_page = (uint16_t)(i * 1000);
        }
        sc.fetched_at = -5;
        n = shelf_pack(&sc, blob, sizeof(blob));
        assert(n == SHELF_BLOB_MAX);
        assert(shelf_unpack(&sb, blob, n) && same_shelf(&sc, &sb));
        sc.count = 0;
        n = shelf_pack(&sc, blob, sizeof(blob));
        assert(n == SHELF_BLOB_HEADER);
        assert(shelf_unpack(&sb, blob, n) && sb.count == 0 && sb.fetched_at == -5);
        /* a version 1 blob (one merged page count per book, 104 bytes a
         * book) is refused, so the shelf is fetched again */
        {
            enum { V1_BOOK = 4 + 64 + 32 + 2 + 2 };
            static uint8_t v1[SHELF_BLOB_HEADER + 19 * V1_BOOK];
            memset(v1, 0, sizeof(v1));
            v1[0] = 1;
            v1[1] = 19;
            assert(SHELF_BLOB_VERSION != 1 && SHELF_BOOK_BYTES != V1_BOOK);
            memset(&sb, 0x5a, sizeof(sb));
            assert(!shelf_unpack(&sb, v1, sizeof(v1)));
            assert(sb.count == 0);
        }
        blob[1] = SHELF_MAX + 1;
        memset(blob + SHELF_BLOB_HEADER, 0, (SHELF_MAX + 1) * SHELF_BOOK_BYTES);
        assert(!shelf_unpack(&sb, blob, SHELF_BLOB_HEADER + (SHELF_MAX + 1) * SHELF_BOOK_BYTES));
    }
    return 0;
}
