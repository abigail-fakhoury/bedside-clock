#include "core/match.h"

#include <stdbool.h>
#include <string.h>

typedef struct {
    char w[MATCH_WORDS][MATCH_WORD_MAX];
    uint8_t len[MATCH_WORDS];
    int stored; /* words in w */
    int total;  /* words seen, including those past MATCH_WORDS */
} words_t;

static bool is_word_byte(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c >= 0x80;
}

static bool is_article(const char *w, size_t full_len)
{
    return (full_len == 3 && !memcmp(w, "the", 3)) || (full_len == 1 && w[0] == 'a') ||
           (full_len == 2 && !memcmp(w, "an", 2));
}

/* Splits at most cap bytes of src (stopping at a NUL) into words. */
static void split(const char *src, size_t cap, words_t *out)
{
    out->stored = out->total = 0;
    char cur[MATCH_WORD_MAX];
    size_t cur_len = 0, full_len = 0; /* full_len counts bytes past the cut too */
    bool first = true;

    for (size_t i = 0;; i++) {
        unsigned char c = (i < cap) ? (unsigned char)src[i] : 0;
        /* Apostrophes join: ' and U+2018/U+2019 (E2 80 98/99) are dropped. */
        if (c == '\'') continue;
        if (c == 0xE2 && i + 2 < cap && (unsigned char)src[i + 1] == 0x80 &&
            ((unsigned char)src[i + 2] == 0x98 || (unsigned char)src[i + 2] == 0x99)) {
            i += 2;
            continue;
        }
        if (c && is_word_byte(c)) {
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
            if (cur_len < MATCH_WORD_MAX) cur[cur_len++] = (char)c;
            full_len++;
            continue;
        }
        if (full_len) {
            if (!(first && is_article(cur, full_len))) {
                if (out->stored < MATCH_WORDS) {
                    memcpy(out->w[out->stored], cur, cur_len);
                    out->len[out->stored] = (uint8_t)cur_len;
                    out->stored++;
                }
                out->total++;
            }
            first = false;
            cur_len = full_len = 0;
        }
        if (!c) break;
    }
}

/* Edit distance between x and y is exactly 1. */
static bool one_edit(const char *x, size_t nx, const char *y, size_t ny)
{
    if (nx < ny) {
        const char *t = x; x = y; y = t;
        size_t n = nx; nx = ny; ny = n;
    }
    if (nx - ny > 1) return false;
    size_t i = 0;
    while (i < ny && x[i] == y[i]) i++;
    if (nx == ny) /* one substitution at i, rest equal */
        return i < nx && !memcmp(x + i + 1, y + i + 1, nx - i - 1);
    return !memcmp(x + i + 1, y + i, ny - i); /* one byte of x deleted at i */
}

/* Heard words shared with the title; each title word used at most once,
 * exact pairs first, then near ones. */
static int shared(const words_t *h, const words_t *t)
{
    bool used[MATCH_WORDS] = {false}, done[MATCH_WORDS] = {false};
    int n = 0;
    for (int i = 0; i < h->stored; i++)
        for (int j = 0; j < t->stored; j++)
            if (!used[j] && h->len[i] == t->len[j] && !memcmp(h->w[i], t->w[j], h->len[i])) {
                used[j] = done[i] = true;
                n++;
                break;
            }
    for (int i = 0; i < h->stored; i++) {
        if (done[i]) continue;
        for (int j = 0; j < t->stored; j++) {
            size_t li = h->len[i], lj = t->len[j];
            if (!used[j] && (li >= 5 || lj >= 5) && one_edit(h->w[i], li, t->w[j], lj)) {
                used[j] = done[i] = true;
                n++;
                break;
            }
        }
    }
    return n;
}

match_t match_title(const shelf_t *shelf, const char *heard)
{
    match_t m = {-1, -1};
    if (!shelf || !heard) return m;

    words_t h, t;
    split(heard, MATCH_HEARD_MAX, &h);
    if (!h.total) return m;

    int count = shelf->count < SHELF_MAX ? shelf->count : SHELF_MAX;
    int score[SHELF_MAX];
    int best = -1;
    for (int i = 0; i < count; i++) {
        split(shelf->books[i].title, sizeof(shelf->books[i].title), &t);
        score[i] = shared(&h, &t);
        if (best < 0 || score[i] > score[best]) best = i;
    }
    /* score / total >= 0.6, kept in integers */
    if (best < 0 || 10 * score[best] < 6 * h.total) return m;
    m.a = (int8_t)best;

    int second = -1;
    for (int i = 0; i < count; i++)
        if (i != best && (second < 0 || score[i] > score[second])) second = i;
    /* within 0.1: (best - second) / total <= 0.1 */
    if (second >= 0 && 10 * (score[best] - score[second]) <= h.total) m.b = (int8_t)second;
    return m;
}
