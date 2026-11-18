#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/shelf.h"

/* Setting a book's paper total before its session is posted.
 *
 * Readwise Books gives a paper session progress from the book's
 * manual_total_pages only, never from the catalog page count, and a
 * session keeps the progress it was posted with (https://readwise.io/
 * books_api, Record sessions). So before posting, a book whose total is
 * not set gets the catalog count the clock knows for it, with
 * PATCH /books {"items":[{"id":…,"manual_total_pages":…}]} (Update books).
 */

/* The total to set before posting a session of book_id that reached
 * page_end, or 0 to post as before without one: 0 when there is no shelf,
 * the book is not on it (or is id 0), its manual total is already set, it
 * has no catalog count, or the catalog count is below page_end (the API
 * refuses a session whose page_at_end is above the total, so a shorter
 * catalog edition would turn a session without progress into a failed
 * one). */
uint16_t total_to_set(const shelf_t *sh, uint32_t book_id, uint16_t page_end);

/* After a PATCH that took: the shelf's book_id counts as having its manual
 * total, `pages`, so it is not PATCHed again (pages itself is unchanged:
 * it was already the catalog count). False, nothing changed, when the book
 * is not on the shelf or pages is 0. */
bool total_mark_set(shelf_t *sh, uint32_t book_id, uint16_t pages);

/* The PATCH /books body for one book. Bytes written (terminator not
 * counted), or -1 when book_id or pages is 0 or it does not fit. */
enum { TOTAL_BODY_MAX = 80 };
int total_body(char *buf, size_t cap, uint32_t book_id, uint16_t pages);

/* What a PATCH /books reply means for the session waiting on it.
 * TOTAL_SET: 200 whose body is one whole JSON document with results[0]
 * .status "updated" and no other result. TOTAL_RATE_LIMITED: 429; the
 * session is not posted this wake (it stays O_PENDING, as after a 429 on
 * the session POST). TOTAL_FAILED: anything else (0 for no reply, any other
 * code, an item error in a 200, a malformed, empty or truncated body); the
 * session is posted as before, without a total. At most TOTAL_REPLY_MAX
 * bytes are looked at, and a reply that filled them counts as truncated. */
enum { TOTAL_REPLY_MAX = 512 };
typedef enum { TOTAL_SET, TOTAL_FAILED, TOTAL_RATE_LIMITED } total_result_t;
total_result_t total_reply(int http_status, const char *body, size_t len);
