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
#include "unicode_tables.h"

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

/*
 * Binary search: is cp inside one of the n sorted, non-overlapping ranges?
 * Each step halves the part of the table still to search, so even the
 * 700-range table takes about 10 comparisons.
 */
static bool in_ranges(uint32_t cp, const utf8_range *table, size_t n) {
    size_t lo = 0;
    size_t hi = n; /* the answer, if any, is in table[lo .. hi-1] */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2; /* not (lo + hi) / 2, which can overflow */
        if (cp < table[mid].first) {
            hi = mid;
        } else if (cp > table[mid].last) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

/* sizeof the whole array / sizeof one element = number of elements. */
#define COUNT(table) (sizeof(table) / sizeof((table)[0]))

bool utf8_is_invisible(uint32_t cp) {
    return in_ranges(cp, INVISIBLE_RANGES, COUNT(INVISIBLE_RANGES));
}

bool utf8_is_combining(uint32_t cp) {
    return in_ranges(cp, COMBINING_RANGES, COUNT(COMBINING_RANGES));
}

const char *utf8_unicode_version(void) {
    return PUT_UNICODE_VERSION;
}
