#include "core/text.h"

#include <string.h>

/* U+00C0-U+00FF without accents; "" for the two non-letters (x, /) */
static const char *const LATIN1[64] = {
    "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
    "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
    "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
    "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y",
};

/* Decodes one UTF-8 character at s; returns its length (0 = malformed,
 * consume one byte) */
static int decode(const unsigned char *s, unsigned *cp)
{
    int n;
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    if ((s[0] & 0xE0) == 0xC0) { n = 2; *cp = s[0] & 0x1F; }
    else if ((s[0] & 0xF0) == 0xE0) { n = 3; *cp = s[0] & 0x0F; }
    else if ((s[0] & 0xF8) == 0xF0) { n = 4; *cp = s[0] & 0x07; }
    else return 0;
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
        *cp = (*cp << 6) | (s[i] & 0x3F);
    }
    return n;
}

static const char *fold(unsigned cp)
{
    static char one[2];
    if (cp >= 0x20 && cp < 0x7F) { one[0] = (char)cp; one[1] = 0; return one; }
    if (cp >= 0xC0 && cp <= 0xFF) return LATIN1[cp - 0xC0];
    switch (cp) {
    case 0xA0: return " ";
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
    case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: return "-";
    case 0x2026: return "...";
    case 0x0152: return "OE";
    case 0x0153: return "oe";
    default: return "?";
    }
}

void text_ascii(char *out, size_t cap, const char *src)
{
    if (cap == 0) return;
    size_t o = 0;
    const unsigned char *s = (const unsigned char *)src;
    while (*s) {
        unsigned cp = 0;
        int n = decode(s, &cp);
        const char *f = n ? fold(cp) : "?";
        s += n ? n : 1;
        size_t len = strlen(f);
        if (o + len > cap - 1) break;
        memcpy(out + o, f, len);
        o += len;
    }
    out[o] = '\0';
}
