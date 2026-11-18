#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "core/jscan.h"

/* The owner's "currently reading" shelf, built from a Readwise Books
 * GET books?status=currently_reading response as it streams through jscan. */

#define SHELF_MAX 40

typedef struct {
    uint32_t id;
    char title[64];     /* UTF-8, cut at a character boundary */
    char author[32];    /* likewise */
    uint16_t pages;     /* manual_total_pages if set, else product.page_count, else 0 */
    uint16_t last_page; /* round(progress x pages) when both are known, else 0 */
    /* The two numbers pages comes from, kept apart so the clock can tell a
     * total set on Readwise from a catalog count only (core/total.h) */
    uint16_t manual_pages;  /* manual_total_pages, 0 when null (not set) */
    uint16_t catalog_pages; /* product.page_count, 0 when unknown */
} book_t;

typedef struct {
    book_t books[SHELF_MAX];
    uint8_t count;
    time_t fetched_at; /* set by the fetcher; 0 when unknown */
} shelf_t;

typedef struct {
    shelf_t *out;
    book_t cur;
    bool has_progress;
    double progress;
    uint32_t manual_pages, product_pages;
    unsigned seen;                /* books in the responses, including any past SHELF_MAX */
    char next[JSCAN_VALUE_MAX];   /* this page's "next" URL, "" when none */
} shelf_builder_t;

/* Empties *out and starts collecting into it. One builder may take several
 * pages in turn (one jscan per page); books past SHELF_MAX are dropped. */
void shelf_builder_init(shelf_builder_t *b, shelf_t *out);

/* Call before feeding each further page: forgets the last page's "next". */
void shelf_builder_next_page(shelf_builder_t *b);

/* A jscan_cb; ctx is the shelf_builder_t. */
void shelf_on_value(void *ctx, const char *path, const char *value, bool is_string);

/* "next" is safe to request: it starts with SHELF_API_BASE, is shorter
 * than JSCAN_VALUE_MAX - 1 bytes (so jscan did not truncate it), and holds
 * only printable ASCII without spaces. */
#define SHELF_API_BASE "https://readwise.io/books/api/v1/"
bool shelf_next_url_ok(const char *next);

/* The cached form kept in NVS: a version byte, the count, fetched_at (int64
 * little-endian), then count books of SHELF_BOOK_BYTES each (id LE, title,
 * author, pages LE, last_page LE, manual_pages LE, catalog_pages LE). A
 * blob of another version or of the wrong size for its count is refused,
 * not misread. Version 1 had no manual_pages or catalog_pages. */
#define SHELF_BLOB_VERSION 2
#define SHELF_BLOB_HEADER 10
#define SHELF_BOOK_BYTES (4 + 64 + 32 + 2 + 2 + 2 + 2)
#define SHELF_BLOB_MAX (SHELF_BLOB_HEADER + SHELF_MAX * SHELF_BOOK_BYTES)

/* Bytes written, 0 when cap is too small. */
size_t shelf_pack(const shelf_t *s, uint8_t *buf, size_t cap);

/* True and *s filled when buf holds a valid blob; false (*s emptied)
 * otherwise. */
bool shelf_unpack(shelf_t *s, const uint8_t *buf, size_t len);
