#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Reading the voice model's reply. */

typedef struct {
    char title[64]; /* UTF-8, cut at a character boundary; "" when null or absent */
    int page;       /* 1-9999, or -1 when null, absent or out of range */
    bool ok;        /* a whole JSON object was found and read */
} heard_t;

/* Parses the model's message content, e.g. {"title": "Great Circle",
 * "page": 394}. Anything outside the first balanced {...} (a ```json
 * fence, a sentence) is ignored. "title" is used when it is a string.
 * "page" is used when it is an integer, or a string of digits, in 1-9999.
 * ok is false, with title "" and page -1, when there is no complete
 * object or it is not valid JSON. Never reads past the terminator. */
heard_t reply_parse(const char *content);

/* Copies the decoded string value of the first "content" member found
 * after "choices" in a chat completions response (resp[0..len), which
 * need not be NUL-terminated) into out, cut at a UTF-8 boundary to fit
 * cap - 1 bytes. False, with out "", when there is none, it is not a
 * string (null), or the string is unterminated within len. */
bool reply_content(const char *resp, size_t len, char *out, size_t cap);
