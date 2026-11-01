/*
 * unit.c — unit tests for the UTF-8 decoder.
 *
 * C has no built-in test framework, and for a project this size we don't
 * need one: a macro that records failures is enough. Build and run with
 * `make test`.
 */
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
}

int main(void) {
    test_ascii();
    test_multibyte();
    test_invalid();
    test_no_overread();
    test_valid_strings();
    test_classification();

    printf("unit: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
