#include "core/shelf.h"

#include <stdlib.h>
#include <string.h>

static void reset_book(shelf_builder_t *b)
{
    memset(&b->cur, 0, sizeof(b->cur));
    b->has_progress = false;
    b->progress = 0;
    b->manual_pages = b->product_pages = 0;
}

void shelf_builder_init(shelf_builder_t *b, shelf_t *out)
{
    memset(b, 0, sizeof(*b));
    memset(out, 0, sizeof(*out));
    b->out = out;
}

void shelf_builder_next_page(shelf_builder_t *b)
{
    b->next[0] = '\0';
    reset_book(b);
}

static void copy_fit(char *dst, size_t cap, const char *src)
{
    size_t n = utf8_fit(src, strlen(src), cap - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* A JSON number as a page count: whole pages, 0..65535; null and junk 0 */
static uint32_t page_count(const char *value)
{
    char *end;
    double v = strtod(value, &end);
    if (end == value || !(v > 0)) return 0;
    return v >= 65535 ? 65535 : (uint32_t)v;
}

static void finish_book(shelf_builder_t *b)
{
    book_t *k = &b->cur;
    uint32_t pages = b->manual_pages ? b->manual_pages : b->product_pages;
    k->pages = (uint16_t)pages;
    k->manual_pages = (uint16_t)b->manual_pages;
    k->catalog_pages = (uint16_t)b->product_pages;
    if (b->has_progress && pages) {
        double p = b->progress < 0 ? 0 : b->progress > 1 ? 1 : b->progress;
        k->last_page = (uint16_t)(p * pages + 0.5); /* round half up; p x pages >= 0 */
        if (k->last_page > pages) k->last_page = (uint16_t)pages;
    }
    b->seen++;
    if (b->out->count < SHELF_MAX) b->out->books[b->out->count++] = *k;
    reset_book(b);
}

void shelf_on_value(void *ctx, const char *path, const char *value, bool is_string)
{
    shelf_builder_t *b = ctx;
    if (strncmp(path, "results[]", 9) != 0) {
        if (strcmp(path, "next") == 0) {
            if (value && is_string) copy_fit(b->next, sizeof(b->next), value);
            else b->next[0] = '\0';
        }
        return;
    }
    const char *field = path + 9;
    if (!value) {
        if (!*field) finish_book(b); /* "results[]" closed: one book done */
        return;
    }
    if (strcmp(field, ".id") == 0 && !is_string) {
        b->cur.id = (uint32_t)strtoul(value, NULL, 10);
    } else if (strcmp(field, ".title") == 0 && is_string) {
        copy_fit(b->cur.title, sizeof(b->cur.title), value);
    } else if (strcmp(field, ".author") == 0 && is_string) {
        copy_fit(b->cur.author, sizeof(b->cur.author), value);
    } else if (strcmp(field, ".progress") == 0 && !is_string) {
        char *end;
        double p = strtod(value, &end);
        b->has_progress = end != value;
        b->progress = b->has_progress ? p : 0;
    } else if (strcmp(field, ".manual_total_pages") == 0 && !is_string) {
        b->manual_pages = page_count(value);
    } else if (strcmp(field, ".product.page_count") == 0 && !is_string) {
        b->product_pages = page_count(value);
    }
}

bool shelf_next_url_ok(const char *next)
{
    size_t n = strlen(next), base = strlen(SHELF_API_BASE);
    if (n <= base || n >= JSCAN_VALUE_MAX - 1 || strncmp(next, SHELF_API_BASE, base) != 0)
        return false;
    for (size_t i = 0; i < n; i++)
        if (next[i] <= ' ' || next[i] > '~') return false;
    return true;
}

static void put_le(uint8_t *p, uint64_t v, int bytes)
{
    for (int i = 0; i < bytes; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint64_t get_le(const uint8_t *p, int bytes)
{
    uint64_t v = 0;
    for (int i = 0; i < bytes; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

size_t shelf_pack(const shelf_t *s, uint8_t *buf, size_t cap)
{
    unsigned count = s->count > SHELF_MAX ? SHELF_MAX : s->count;
    size_t n = SHELF_BLOB_HEADER + count * SHELF_BOOK_BYTES;
    if (cap < n) return 0;
    memset(buf, 0, n);
    buf[0] = SHELF_BLOB_VERSION;
    buf[1] = (uint8_t)count;
    put_le(buf + 2, (uint64_t)(int64_t)s->fetched_at, 8);
    uint8_t *p = buf + SHELF_BLOB_HEADER;
    for (unsigned i = 0; i < count; i++, p += SHELF_BOOK_BYTES) {
        const book_t *k = &s->books[i];
        put_le(p, k->id, 4);
        memcpy(p + 4, k->title, 64);
        memcpy(p + 68, k->author, 32);
        put_le(p + 100, k->pages, 2);
        put_le(p + 102, k->last_page, 2);
        put_le(p + 104, k->manual_pages, 2);
        put_le(p + 106, k->catalog_pages, 2);
    }
    return n;
}

bool shelf_unpack(shelf_t *s, const uint8_t *buf, size_t len)
{
    memset(s, 0, sizeof(*s));
    if (!buf || len < SHELF_BLOB_HEADER || buf[0] != SHELF_BLOB_VERSION) return false;
    unsigned count = buf[1];
    if (count > SHELF_MAX || len != SHELF_BLOB_HEADER + count * SHELF_BOOK_BYTES) return false;
    s->count = (uint8_t)count;
    s->fetched_at = (time_t)(int64_t)get_le(buf + 2, 8);
    const uint8_t *p = buf + SHELF_BLOB_HEADER;
    for (unsigned i = 0; i < count; i++, p += SHELF_BOOK_BYTES) {
        book_t *k = &s->books[i];
        k->id = (uint32_t)get_le(p, 4);
        memcpy(k->title, p + 4, 64);
        k->title[63] = '\0';
        memcpy(k->author, p + 68, 32);
        k->author[31] = '\0';
        k->pages = (uint16_t)get_le(p + 100, 2);
        k->last_page = (uint16_t)get_le(p + 102, 2);
        k->manual_pages = (uint16_t)get_le(p + 104, 2);
        k->catalog_pages = (uint16_t)get_le(p + 106, 2);
    }
    return true;
}
