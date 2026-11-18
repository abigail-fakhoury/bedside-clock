#pragma once
#include <stdbool.h>
#include <stddef.h>

#include "core/session.h"
#include "core/shelf.h"

/* The POST sessions body for one finished paper session:
 * {"items":[{"book_id":…,"format":"paper","started_at":"…Z","ended_at":"…Z",
 * "page_at_start":…,"page_at_end":…,"timezone":"…","publish_to_feed":true,
 * "device":{"model":"Bedside Clock","vendor":"Waveshare"}}]}
 * Field names as https://readwise.io/books_api documents CreateSessionItem.
 * device.type is left out: the docs allow only Handset, Tablet and E-Reader,
 * none of which a bedside clock is. The title is never sent. Returns the length written (NUL not counted), or
 * -1 when cap is too small, the times are not set (started_at before
 * SESSION_EARLIEST, ended_at before started_at), page_end < page_start, or
 * tz_iana is not 1-63 bytes of [A-Za-z0-9/_+-]. */
int body_session(char *out, size_t cap, const session_t *s, const char *tz_iana);

/* The chat completions request for one voice clip is the head body_voice_head writes, the
 * clip's WAV as base64, then BODY_VOICE_TAIL:
 * {"model":"<model>","max_tokens":300,"messages":[{"role":"user","content":
 * [{"type":"text","text":"<prompt>"},{"type":"input_audio","input_audio":
 * {"format":"wav","data":"   <base64>   "}}]}]}
 * With want_title the prompt asks for {"title","page"} and lists the shelf,
 * one "title - author" line per book (just the title when the author is
 * empty); otherwise it asks for {"page"} and shelf may be NULL. The model
 * and the prompt are JSON-escaped. Returns the head's length (NUL not
 * counted), or -1 when cap is too small, model is NULL, or want_title is
 * set with no shelf. */
int body_voice_head(char *out, size_t cap, const char *model, bool want_title,
                    const shelf_t *shelf);
extern const char BODY_VOICE_TAIL[];
