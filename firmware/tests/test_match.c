#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "core/match.h"
#include "core/shelf.h"

/* A sample "currently reading" shelf of 19 books, in shelf order. */
static const char *const REAL[][2] = {
    {"Dracula", "Stoker, Bram"},                                          /* 0 */
    {"House of Earth and Blood", "Sarah J. Maas"},                        /* 1 */
    {"Bird by Bird", "Anne Lamott"},                                      /* 2 */
    {"A Court of Splintered Harmony", "Sarah J. Maas"},                   /* 3 */
    {"East of Eden", "John Steinbeck"},                                   /* 4 */
    {"Lectures on Russian Literature", "Vladimir Nabokov"},               /* 5 */
    {"The Vanishing Family", "Robert Kolker"},                            /* 6 */
    {"Building A Story Brand", "Miller, Donald"},                         /* 7 */
    {"Great Circle", "Shipstead, Maggie"},                                /* 8 */
    {"The Seven Principles of Lighthouse Keeping on a Rocky Northern", "Keeper, Ann"}, /* 9 */
    {"Bird by Bird", "Anne Lamott"},                                      /* 10 */
    {"The Secret Garden", "Frances Hodgson Burnett"},                  /* 11 */
    {"The Blue Castle", "Montgomery, L.M."},                              /* 12 */
    {"Every Tool's a Hammer", "Adam Savage"},                             /* 13 */
    {"Hateship, Friendship, Courtship, Loveship, Marriage", "Alice Munro"}, /* 14 */
    {"The Correspondent", "Virginia Evans"},                              /* 15 */
    {"Meditations", "Marcus Aurelius"},                                  /* 16 */
    {"The Wind in the Willows", "Kenneth Grahame, with illustr"},       /* 17 */
    {"The Perfectionist's Guide to Losing Control", "Katherine Morgan Schafler"}, /* 18 */
};
#define REAL_N ((int)(sizeof(REAL) / sizeof(REAL[0])))

static void add(shelf_t *s, const char *title)
{
    book_t *b = &s->books[s->count];
    memset(b, 0, sizeof(*b));
    b->id = 1000u + s->count;
    snprintf(b->title, sizeof(b->title), "%s", title);
    s->count++;
}

static void real_shelf(shelf_t *s)
{
    memset(s, 0, sizeof(*s));
    for (int i = 0; i < REAL_N; i++) {
        add(s, REAL[i][0]);
        snprintf(s->books[i].author, sizeof(s->books[i].author), "%s", REAL[i][1]);
    }
}

static void expect(const shelf_t *s, const char *heard, int a, int b)
{
    match_t m = match_title(s, heard);
    if (m.a != a || m.b != b) {
        fprintf(stderr, "match_title(\"%s\") = {%d, %d}, want {%d, %d}\n", heard ? heard : "(null)", m.a,
                m.b, a, b);
        assert(0);
    }
}

static void test_brief_cases(void)
{
    shelf_t s;
    real_shelf(&s);
    assert(s.count == 19);
    expect(&s, "Great Circle", 8, -1);
    expect(&s, "the blue castle", 12, -1);
    expect(&s, "Hateship Friendship", 14, -1);
    expect(&s, "east of eaden", 4, -1);
    expect(&s, "Bird by Bird", 2, 10); /* both copies offered */
    expect(&s, "seven principles", 9, -1);
    expect(&s, "Middlemarch", -1, -1);
    expect(&s, "", -1, -1);
    expect(&s, "the", -1, -1);
}

static void test_bad_input(void)
{
    shelf_t s;
    real_shelf(&s);
    expect(NULL, "Great Circle", -1, -1);
    expect(&s, NULL, -1, -1);

    shelf_t empty;
    memset(&empty, 0, sizeof(empty));
    expect(&empty, "Great Circle", -1, -1);

    expect(&s, "!!! ... ,, -- ?", -1, -1);
    expect(&s, "   ", -1, -1);
    expect(&s, "a", -1, -1);
    expect(&s, "an", -1, -1);
    expect(&s, "The.", -1, -1);
}

static void test_spelling_and_case(void)
{
    shelf_t s;
    real_shelf(&s);
    expect(&s, "GREAT CIRCLE", 8, -1);
    expect(&s, "great circle.", 8, -1);
    expect(&s, "dracla", 0, -1);       /* 6 letters, one deletion */
    expect(&s, "Draculas", 0, -1);     /* one insertion */
    expect(&s, "dracuka", 0, -1);      /* one substitution */
    expect(&s, "drcla", -1, -1);       /* two edits */
    expect(&s, "bard by bard", -1, -1); /* 4-letter words must be exact: 1/3 */
    expect(&s, "Hateship-Friendship", 14, -1);
}

static void test_apostrophes(void)
{
    shelf_t s;
    real_shelf(&s);
    expect(&s, "every tools a hammer", 13, -1);
    expect(&s, "every tool's a hammer", 13, -1);
    expect(&s, "every tool\xe2\x80\x99s a hammer", 13, -1); /* curly apostrophe */
    expect(&s, "the perfectionists guide", 18, -1);

    shelf_t c;
    memset(&c, 0, sizeof(c));
    add(&c, "Dracula");
    add(&c, "Every Tool\xe2\x80\x99s a Hammer");
    expect(&c, "every tools a hammer", 1, -1);
}

static void test_articles(void)
{
    shelf_t s;
    real_shelf(&s);
    /* Leading article stripped from heard: "dracula" is then 1/1. */
    expect(&s, "the dracula", 0, -1);
    expect(&s, "a dracula", 0, -1);
    /* Only a leading one: "dracula the" is 1/2. */
    expect(&s, "dracula the", -1, -1);
    /* Shelf titles with a leading article match without it, and with it. */
    expect(&s, "court of splintered harmony", 3, -1);
    expect(&s, "a court of splintered harmony", 3, -1);
    expect(&s, "vanishing family", 6, -1);
    expect(&s, "the vanishing family", 6, -1);
    /* Only one article is stripped: "the castle blue" left, 2/3. */
    expect(&s, "the the castle blue", 12, -1);
}

/* Pinned choice: one title word is shared with at most one heard word. */
static void test_one_to_one(void)
{
    shelf_t s;
    real_shelf(&s);
    expect(&s, "castle castle castle blue", -1, -1); /* 2/4, not 4/4 */
    expect(&s, "bird bird", 2, 10);                   /* the title has "bird" twice */
    /* Word order does not matter. */
    expect(&s, "eden east", 4, -1);
}

/* b: within 0.1 of a, lower shelf index first on ties, and never a == b. */
static void test_second_choice(void)
{
    shelf_t s;
    memset(&s, 0, sizeof(s));
    add(&s, "Bird by Bird");
    add(&s, "Bird by Bird");
    add(&s, "Bird by Bird");
    expect(&s, "bird by bird", 0, 1);

    /* 10 heard words; X shares 7 (0.7), Y shares 6 (0.6, within 0.1),
     * Z shares 5 (0.5). Y listed after Z. */
    memset(&s, 0, sizeof(s));
    add(&s, "one two three four five");                 /* Z: 5 */
    add(&s, "one two three four five six seven");       /* X: 7 */
    add(&s, "one two three four five six");             /* Y: 6 */
    expect(&s, "one two three four five six seven eight nine ten", 1, 2);

    /* b may score under 0.6 when within 0.1: X 6/10, Z 5/10. */
    memset(&s, 0, sizeof(s));
    add(&s, "one two three four five");
    add(&s, "one two three four five six");
    expect(&s, "one two three four five six seven eight nine ten", 1, 0);

    /* Exactly 0.6 is enough; 0.2 apart is not "within 0.1". */
    memset(&s, 0, sizeof(s));
    add(&s, "alpha bravo charlie");
    add(&s, "alpha");
    expect(&s, "alpha bravo charlie delta echo", 0, -1);

    /* The better book wins wherever it sits; 0.5 behind is no b. */
    memset(&s, 0, sizeof(s));
    add(&s, "Great");
    add(&s, "Great Circle");
    expect(&s, "great circle", 1, -1);
}

static void test_never_same(void)
{
    shelf_t s;
    real_shelf(&s);
    /* Each real title spoken back finds itself as a or b, never a == b. */
    for (int i = 0; i < REAL_N; i++) {
        match_t m = match_title(&s, s.books[i].title);
        assert(m.a >= 0);
        assert(m.a != m.b);
        assert(m.a == i || m.b == i);
    }
    /* Bird by Bird's two copies offer each other. */
    match_t m = match_title(&s, s.books[10].title);
    assert(m.a == 2 && m.b == 10);
}

static void test_non_ascii(void)
{
    shelf_t s;
    memset(&s, 0, sizeof(s));
    add(&s, "Caf\xc3\xa9 Society");
    add(&s, "\xd0\x92\xd0\xbe\xd0\xb9\xd0\xbd\xd0\xb0 \xd0\xb8 \xd0\xbc\xd0\xb8\xd1\x80"); /* Война и мир */
    add(&s, "Great Circle");
    expect(&s, "caf\xc3\xa9 society", 0, -1);
    expect(&s, "\xd0\x92\xd0\xbe\xd0\xb9\xd0\xbd\xd0\xb0 \xd0\xb8 \xd0\xbc\xd0\xb8\xd1\x80", 1, -1);
    expect(&s, "great circle", 2, -1);
    expect(&s, "\xff\xfe\x80 \xc3", -1, -1); /* stray bytes */

    /* A title of 63 non-ASCII bytes, and one with no NUL at all. */
    memset(&s, 0, sizeof(s));
    add(&s, "x");
    memset(s.books[0].title, 0xE9, sizeof(s.books[0].title) - 1);
    s.books[0].title[63] = '\0';
    add(&s, "y");
    memset(s.books[1].title, 'z', sizeof(s.books[1].title)); /* no NUL */
    add(&s, "Great Circle");
    expect(&s, "great circle", 2, -1);
    expect(&s, "\xe9\xe9\xe9", -1, -1);
}

static void test_long_heard(void)
{
    shelf_t s;
    real_shelf(&s);
    static char buf[5000];

    /* Many words: all count, so the score falls to nothing. */
    memset(buf, 0, sizeof(buf));
    strcpy(buf, "great circle");
    for (size_t n = strlen(buf); n + 3 < sizeof(buf) - 1; n += 3) memcpy(buf + n, " zz", 3);
    expect(&s, buf, -1, -1);

    /* Trailing spaces past MATCH_HEARD_MAX are harmless. */
    memset(buf, ' ', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    memcpy(buf, "great circle", 12);
    expect(&s, buf, 8, -1);

    /* One enormous word. */
    memset(buf, 'q', sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    expect(&s, buf, -1, -1);

    /* More than MATCH_WORDS heard words: those past the 16th are not
     * compared but still count in the denominator. 16/26 passes, 16/27
     * does not (ignoring them would give 16/16). */
    shelf_t w;
    memset(&w, 0, sizeof(w));
    add(&w, "b c d e f g h i j k l m n o p q");
    expect(&w, "b c d e f g h i j k l m n o p q", 0, -1);
    expect(&w, "b c d e f g h i j k l m n o p q zz zz zz zz zz zz zz zz zz zz", 0, -1);
    expect(&w, "b c d e f g h i j k l m n o p q zz zz zz zz zz zz zz zz zz zz zz", -1, -1);
    /* Long words cut to MATCH_WORD_MAX still compare equal to themselves. */
    shelf_t l;
    memset(&l, 0, sizeof(l));
    add(&l, "Pneumonoultramicroscopicsilicovolcanoconiosis");
    expect(&l, "pneumonoultramicroscopicsilicovolcanoconiosis", 0, -1);
}

int main(void)
{
    test_brief_cases();
    test_bad_input();
    test_spelling_and_case();
    test_apostrophes();
    test_articles();
    test_one_to_one();
    test_second_choice();
    test_never_same();
    test_non_ascii();
    test_long_heard();
    return 0;
}
