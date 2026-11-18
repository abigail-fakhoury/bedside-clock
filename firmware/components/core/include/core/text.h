#pragma once
#include <stddef.h>

/* Book titles are UTF-8; the screen fonts hold printable ASCII only. Copies
 * src into out (cap bytes, always NUL-terminated) with curly quotes made
 * straight, dashes '-', the ellipsis "...", the no-break space a space,
 * Latin-1 letters without their accents (an e with an acute accent -> e,
 * the sharp s -> ss, ae and oe ligatures -> ae, oe), and any other
 * character, or any malformed byte, '?'. Never cuts inside a fold. */
void text_ascii(char *out, size_t cap, const char *src);
