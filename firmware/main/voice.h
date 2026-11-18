#pragma once
#include <stdbool.h>

#include "audio.h"
#include "core/reply.h"
#include "core/shelf.h"

/* One OpenRouter chat completions request per clip. */
#define VOICE_MODEL "google/gemini-3.5-flash-lite"

/* Sends the clip with the start prompt (want_title: title and page, the
 * shelf listed for spelling) or the end prompt (page only; shelf may be
 * NULL), streaming the base64 audio into the body in pieces, and parses
 * the reply into *out. The network must already be up (net_up); this
 * neither starts nor stops Wi-Fi. Reads or_key from settings for the call
 * and never logs it. Logs the HTTP status, timings and the reply text.
 * True when the reply was HTTP 200 and out->ok; otherwise *out is
 * {"", -1, false}. Blocks for up to about 30 s. */
bool voice_ask(const clip_t *clip, bool want_title, const shelf_t *shelf, heard_t *out);
