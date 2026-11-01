/*
 * utf8.h — decode UTF-8 text and classify characters.
 *
 * Text arrives as raw bytes. UTF-8 encodes each character (a "code point",
 * a number from 0 to 0x10FFFF) as 1 to 4 bytes. Not every byte sequence is
 * valid UTF-8, and put must handle invalid input without crashing or lying.
 */
#ifndef PUT_UTF8_H
#define PUT_UTF8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The result of decoding the character at the start of a byte sequence. */
typedef struct {
    uint32_t cp; /* the code point; meaningful only when valid is true */
    size_t len;  /* bytes consumed: the sequence length, or 1 if invalid */
    bool valid;  /* false if the bytes are not well-formed UTF-8 */
} utf8_char;

/* Decode one character from the n bytes at s. Requires n >= 1. */
utf8_char utf8_decode(const unsigned char *s, size_t n);

/* True if all n bytes at s are valid UTF-8. */
bool utf8_valid(const char *s, size_t n);

/*
 * True for code points that have no glyph of their own, render as blank
 * space, or depend on the font: C1 controls, format characters (zero-width
 * and bidirectional controls, tags), unassigned and private-use code points,
 * spaces other than U+0020, line and paragraph separators, and every
 * default-ignorable code point (variation selectors, Hangul fillers, ...).
 * Generated from Unicode data; see scripts/gen-unicode.py. C0 controls and
 * DEL are not included: callers handle those first.
 */
bool utf8_is_invisible(uint32_t cp);

/* True for combining marks (Unicode category M), which draw on top of the
 * character before them. */
bool utf8_is_combining(uint32_t cp);

/* True for the bidirectional controls used in "Trojan Source" attacks. */
bool utf8_is_bidi_control(uint32_t cp);

/* The Unicode version the character tables were generated from, e.g. "17.0". */
const char *utf8_unicode_version(void);

#endif
