#include <assert.h>
#include <string.h>

#include "core/text.h"

static void eq(const char *src, const char *want)
{
    char out[64];
    text_ascii(out, sizeof(out), src);
    assert(strcmp(out, want) == 0);
}

int main(void)
{
    eq("Great Circle", "Great Circle");
    eq("Don\xE2\x80\x99t Look", "Don't Look");                   /* ’ */
    eq("\xE2\x80\x9CQuoted\xE2\x80\x9D", "\"Quoted\"");          /* “ ” */
    eq("A\xE2\x80\x94" "B\xE2\x80\x93" "C", "A-B-C");             /* — – */
    eq("Wait\xE2\x80\xA6", "Wait...");                            /* … */
    eq("Bront\xC3\xAB", "Bronte");                                /* ë */
    eq("\xC3\x89mile Zola", "Emile Zola");                        /* É */
    eq("Stra\xC3\x9F" "e", "Strasse");                            /* ß */
    eq("\xC3\x86on \xC5\x93uvre", "AEon oeuvre");                 /* Æ œ */
    eq("a\xC2\xA0" "b", "a b");                                   /* no-break space */
    eq("\xE6\x97\xA5", "?");                                      /* a CJK character */
    eq("x\xF0\x9F\x93\x96y", "x?y");                              /* an emoji */
    eq("bad\xC3", "bad?");                                        /* cut */
    eq("bad\x80z", "bad?z");                                      /* stray continuation */
    eq("", "");
    /* Always terminated, never cut inside a fold */
    char small[4];
    text_ascii(small, sizeof(small), "abcdef");
    assert(strcmp(small, "abc") == 0);
    text_ascii(small, sizeof(small), "ab\xE2\x80\xA6");
    assert(strcmp(small, "ab") == 0);
    text_ascii(small, sizeof(small), "a\xC3\x9F");
    assert(strcmp(small, "ass") == 0);
    text_ascii(small, 1, "abc");
    assert(small[0] == '\0');
    return 0;
}
