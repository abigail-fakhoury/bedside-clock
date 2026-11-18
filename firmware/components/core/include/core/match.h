#pragma once
#include <stdint.h>

#include "core/shelf.h"

/* Matching a spoken book title to a book on the shelf.
 *
 * Both sides are lowercased (ASCII), apostrophes dropped ("tool's" ->
 * "tools"), other ASCII punctuation and spaces split words, bytes >= 0x80
 * kept as part of a word, and a leading "the", "a" or "an" removed.
 * score = heard words shared with the title / heard words. A heard word is
 * shared when it equals a title word, or when either of the two is 5+ bytes
 * long and they are within edit distance 1. Each title word is shared with
 * at most one heard word (exact pairs are taken first).
 *
 * a is the best book when its score is >= 0.6, else -1. b is the next best
 * other book when it scores within 0.1 of a (two copies of one title give
 * a and b), else -1. Equal scores go to the lower shelf index first, so
 * a < b when they tie. a == b never happens; b is -1 whenever a is.
 *
 * Fixed memory: at most MATCH_HEARD_MAX bytes of heard are read, words are
 * cut to MATCH_WORD_MAX bytes, and only the first MATCH_WORDS words of each
 * side are compared (heard words past that still count in the denominator,
 * so they can only lower a score). */
#define MATCH_HEARD_MAX 256
#define MATCH_WORD_MAX 24
#define MATCH_WORDS 16

typedef struct {
    int8_t a; /* index into shelf, -1 = none */
    int8_t b;
} match_t;

match_t match_title(const shelf_t *shelf, const char *heard);
