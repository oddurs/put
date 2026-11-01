/*
 * unit.c — unit tests for the UTF-8 decoder and the output layer.
 *
 * C has no built-in test framework, and for a project this size we don't
 * need one: a macro that records failures is enough. Build and run with
 * `make test`.
 */
#include "out.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;
static int checks = 0;

/*
 * C note: a macro is text substitution done before compiling. #cond turns
 * the expression into a string, and __FILE__/__LINE__ say where it was
 * written, so a failure message points straight at the broken check. The
 * do { } while (0) wrapper makes the macro behave like a single statement.
 */
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                                          \
    } while (0)

/* Decode the first character of a string literal. */
static utf8_char decode(const char *s) {
    return utf8_decode((const unsigned char *)s, strlen(s));
}

static void test_ascii(void) {
    utf8_char c = decode("A");
    CHECK(c.valid && c.cp == 'A' && c.len == 1);
}

static void test_multibyte(void) {
    utf8_char c = decode("\xC3\xA9"); /* é U+00E9 */
    CHECK(c.valid && c.cp == 0xE9 && c.len == 2);

    c = decode("\xE2\x82\xAC"); /* € U+20AC */
    CHECK(c.valid && c.cp == 0x20AC && c.len == 3);

    c = decode("\xF0\x9F\x99\x82"); /* 🙂 U+1F642 */
    CHECK(c.valid && c.cp == 0x1F642 && c.len == 4);

    c = decode("\xF4\x8F\xBF\xBF"); /* U+10FFFF, the last code point */
    CHECK(c.valid && c.cp == 0x10FFFF);
}

static void test_invalid(void) {
    CHECK(!decode("\x80").valid);             /* continuation byte on its own */
    CHECK(!decode("\xFF").valid);             /* never valid in UTF-8 */
    CHECK(!decode("\xC0\xAF").valid);         /* overlong encoding of '/' */
    CHECK(!decode("\xE0\x80\xAF").valid);     /* overlong, 3 bytes */
    CHECK(!decode("\xED\xA0\x80").valid);     /* UTF-16 surrogate U+D800 */
    CHECK(!decode("\xF4\x90\x80\x80").valid); /* U+110000, past the end */
    CHECK(!decode("\xC3").valid);             /* truncated */
    CHECK(!decode("\xE2\x82").valid);         /* truncated */
    CHECK(!decode("\xC3\x41").valid);         /* bad continuation byte */
    CHECK(decode("\xFF").len == 1);           /* invalid input consumes one byte */
}

/*
 * The decoder must never read past the n bytes it was given. A string
 * literal always has a NUL after it, which can hide an over-read, so use a
 * heap buffer of exactly one byte. Under `make sanitize`, AddressSanitizer
 * turns any read of buf[1] into an immediate crash with a report.
 */
static void test_no_overread(void) {
    unsigned char *buf = malloc(1);
    if (buf == NULL) {
        CHECK(buf != NULL);
        return;
    }
    buf[0] = 0xE2; /* first byte of a 3-byte sequence, cut short */
    CHECK(!utf8_decode(buf, 1).valid);
    free(buf);
}

static void test_valid_strings(void) {
    CHECK(utf8_valid("", 0));
    CHECK(utf8_valid("plain ascii", 11));
    CHECK(utf8_valid("h\xC3\xA9llo", 6));
    CHECK(!utf8_valid("ok\xFF", 3));
    CHECK(!utf8_valid("cut \xE2\x82", 6));
}

static void test_classification(void) {
    CHECK(utf8_is_invisible(0xA0));    /* no-break space */
    CHECK(utf8_is_invisible(0x200B));  /* zero-width space */
    CHECK(utf8_is_invisible(0xFEFF));  /* byte order mark */
    CHECK(utf8_is_invisible(0xE0041)); /* tag latin capital A */
    CHECK(utf8_is_invisible(0x202E));  /* right-to-left override */
    CHECK(utf8_is_bidi_control(0x202E));
    CHECK(utf8_is_bidi_control(0x2066));
    CHECK(!utf8_is_bidi_control(0x200B));
    CHECK(!utf8_is_invisible('a'));
    CHECK(!utf8_is_invisible(0xE9));
    CHECK(!utf8_is_invisible(0x1F642));

    CHECK(utf8_is_invisible(0xFE0F));   /* variation selector 16 (emoji style) */
    CHECK(utf8_is_invisible(0xE0100));  /* variation selector 17 */
    CHECK(utf8_is_invisible(0x2800));   /* braille pattern blank */
    CHECK(utf8_is_invisible(0xE000));   /* private use */
    CHECK(utf8_is_invisible(0x0378));   /* unassigned */
    CHECK(utf8_is_invisible(0x10FFFF)); /* noncharacter, last code point */
    CHECK(utf8_is_combining(0x0301));   /* combining acute accent */
    CHECK(utf8_is_combining(0x093F));   /* Devanagari vowel sign i (spacing mark) */
    CHECK(!utf8_is_combining('e'));
    CHECK(!utf8_is_combining(0xE9)); /* precomposed é is not a mark */
}

/* The binary search needs sorted, non-overlapping ranges; check the
 * generated tables really are. Including the generated header here gives
 * this file its own copy of the static tables. */
#include "unicode_tables.h"

static void check_table(const utf8_range *table, size_t n) {
    for (size_t i = 0; i < n; i++) {
        CHECK(table[i].first <= table[i].last);
        CHECK(table[i].last <= 0x10FFFF);
        if (i > 0) {
            CHECK(table[i - 1].last + 1 < table[i].first); /* sorted, and not adjacent */
        }
    }
}

static void test_tables(void) {
    check_table(INVISIBLE_RANGES, sizeof INVISIBLE_RANGES / sizeof INVISIBLE_RANGES[0]);
    check_table(COMBINING_RANGES, sizeof COMBINING_RANGES / sizeof COMBINING_RANGES[0]);
    /* The first and last entries are found: the search's edge cases. */
    CHECK(utf8_is_invisible(INVISIBLE_RANGES[0].first));
    CHECK(utf8_is_combining(COMBINING_RANGES[0].first));
    CHECK(utf8_is_invisible(0x10FFFF));
    CHECK(!utf8_is_invisible(INVISIBLE_RANGES[0].first - 1));
}

/*
 * out_printf formats into a 128-byte stack buffer and falls back to the heap
 * for longer results. Point stdout at a temporary file, print something long,
 * and read it back.
 */
static void test_out_printf_long(void) {
    fflush(stdout);
    int saved = dup(STDOUT_FILENO); /* keep the real stdout to restore later */
    FILE *tmp = tmpfile();
    int redirected = saved >= 0 && tmp != NULL && dup2(fileno(tmp), STDOUT_FILENO) >= 0;
    CHECK(redirected);
    if (!redirected) {
        return;
    }

    char big[300];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    out_printf("<%s>", big);
    out_flush();

    dup2(saved, STDOUT_FILENO);
    close(saved);

    char got[400];
    size_t n = fseek(tmp, 0, SEEK_SET) == 0 ? fread(got, 1, sizeof got, tmp) : 0;
    fclose(tmp);
    CHECK(n == 301);
    if (n != 301) {
        return;
    }
    CHECK(got[0] == '<' && got[1] == 'x' && got[299] == 'x' && got[300] == '>');
}

int main(void) {
    test_ascii();
    test_multibyte();
    test_invalid();
    test_no_overread();
    test_valid_strings();
    test_classification();
    test_tables();
    test_out_printf_long();

    printf("unit: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
