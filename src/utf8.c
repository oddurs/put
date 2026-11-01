/*
 * utf8.c — a small, strict UTF-8 decoder.
 *
 * Byte patterns (x = payload bits):
 *   0xxxxxxx                              1 byte,  U+0000..U+007F
 *   110xxxxx 10xxxxxx                     2 bytes, U+0080..U+07FF
 *   1110xxxx 10xxxxxx 10xxxxxx            3 bytes, U+0800..U+FFFF
 *   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx   4 bytes, U+10000..U+10FFFF
 *
 * "Strict" means we also reject sequences that fit the pattern but are
 * forbidden: overlong encodings (a character encoded with more bytes than it
 * needs), UTF-16 surrogates (U+D800..U+DFFF), and anything above U+10FFFF.
 */
#include "utf8.h"

utf8_char utf8_decode(const unsigned char *s, size_t n) {
    /* C note: this is a "compound literal": a struct value written inline. */
    const utf8_char invalid = {.cp = 0, .len = 1, .valid = false};

    unsigned char b0 = s[0];
    if (b0 < 0x80) {
        return (utf8_char){.cp = b0, .len = 1, .valid = true};
    }

    size_t need;  /* total bytes in this sequence */
    uint32_t cp;  /* code point, built up 6 bits at a time */
    uint32_t min; /* smallest code point allowed at this length */

    /* The `U` suffix makes these constants unsigned, so the bit operations
     * stay unsigned and -Wsign-conversion has nothing to complain about. */
    if ((b0 & 0xE0U) == 0xC0U) {
        need = 2;
        cp = b0 & 0x1FU;
        min = 0x80;
    } else if ((b0 & 0xF0U) == 0xE0U) {
        need = 3;
        cp = b0 & 0x0FU;
        min = 0x800;
    } else if ((b0 & 0xF8U) == 0xF0U) {
        need = 4;
        cp = b0 & 0x07U;
        min = 0x10000;
    } else {
        return invalid; /* a stray continuation byte, or 0xF8..0xFF */
    }

    if (n < need) {
        return invalid; /* the string ends in the middle of a character */
    }
    for (size_t i = 1; i < need; i++) {
        if ((s[i] & 0xC0U) != 0x80U) {
            return invalid; /* every following byte must look like 10xxxxxx */
        }
        cp = (cp << 6) | (s[i] & 0x3FU);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return invalid;
    }
    return (utf8_char){.cp = cp, .len = need, .valid = true};
}

bool utf8_valid(const char *s, size_t n) {
    /* C note: `char` may be signed, so bytes >= 0x80 would read as negative
     * numbers. Viewing the same memory as `unsigned char` avoids that. */
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        utf8_char c = utf8_decode(p + i, n - i);
        if (!c.valid) {
            return false;
        }
        i += c.len;
    }
    return true;
}

bool utf8_is_bidi_control(uint32_t cp) {
    return cp == 0x061C                       /* Arabic letter mark */
           || cp == 0x200E || cp == 0x200F    /* left-to-right / right-to-left marks */
           || (cp >= 0x202A && cp <= 0x202E)  /* embeddings and overrides */
           || (cp >= 0x2066 && cp <= 0x2069); /* isolates */
}

bool utf8_is_invisible(uint32_t cp) {
    return (cp >= 0x80 && cp <= 0xA0)          /* C1 controls, no-break space */
           || cp == 0xAD                       /* soft hyphen */
           || cp == 0x034F                     /* combining grapheme joiner */
           || cp == 0x115F || cp == 0x1160     /* Hangul fillers */
           || cp == 0x17B4 || cp == 0x17B5     /* Khmer inherent vowels */
           || cp == 0x180E                     /* Mongolian vowel separator */
           || (cp >= 0x2000 && cp <= 0x200F)   /* typographic spaces, zero-width chars */
           || (cp >= 0x2028 && cp <= 0x202F)   /* line/paragraph separators, narrow nbsp */
           || (cp >= 0x205F && cp <= 0x206F)   /* word joiner, invisible operators */
           || cp == 0x3000                     /* ideographic space */
           || cp == 0x3164 || cp == 0xFFA0     /* Hangul fillers */
           || cp == 0xFEFF                     /* byte order mark */
           || (cp >= 0xFFF9 && cp <= 0xFFFB)   /* interlinear annotation */
           || (cp >= 0xE0000 && cp <= 0xE007F) /* tag characters */
           || utf8_is_bidi_control(cp);
}
