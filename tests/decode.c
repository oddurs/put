/*
 * decode.c — strict decoders for put.peek, put.quote and put.json output.
 * See decode.h for why they exist.
 */
#include "decode.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- a growable byte string ------------------------------------------- */

typedef struct {
    unsigned char *p;
    size_t len;
    size_t cap;
} bytes;

static bool push(bytes *b, unsigned char c) {
    if (b->len == b->cap) {
        size_t cap = b->cap == 0 ? 32 : b->cap * 2;
        unsigned char *p = realloc(b->p, cap);
        if (p == NULL) {
            return false;
        }
        b->p = p;
        b->cap = cap;
    }
    b->p[b->len++] = c;
    return true;
}

static bool push_utf8(bytes *b, uint32_t cp) {
    if (cp < 0x80) {
        return push(b, (unsigned char)cp);
    }
    if (cp < 0x800) {
        return push(b, (unsigned char)(0xC0U | (cp >> 6))) &&
               push(b, (unsigned char)(0x80U | (cp & 0x3FU)));
    }
    if (cp < 0x10000) {
        return push(b, (unsigned char)(0xE0U | (cp >> 12))) &&
               push(b, (unsigned char)(0x80U | ((cp >> 6) & 0x3FU))) &&
               push(b, (unsigned char)(0x80U | (cp & 0x3FU)));
    }
    return push(b, (unsigned char)(0xF0U | (cp >> 18))) &&
           push(b, (unsigned char)(0x80U | ((cp >> 12) & 0x3FU))) &&
           push(b, (unsigned char)(0x80U | ((cp >> 6) & 0x3FU))) &&
           push(b, (unsigned char)(0x80U | (cp & 0x3FU)));
}

/* Move b into the next slot of out. */
static bool take(decoded_args *out, bytes *b) {
    if (out->count == DECODE_MAX_ARGS) {
        free(b->p);
        return false;
    }
    out->arg[out->count] = b->p;
    out->len[out->count] = b->len;
    out->count++;
    *b = (bytes){NULL, 0, 0};
    return true;
}

void decoded_free(decoded_args *d) {
    for (size_t i = 0; i < d->count; i++) {
        free(d->arg[i]);
    }
    d->count = 0;
}

bool decoded_equal(const decoded_args *d, size_t n, char *const args[]) {
    if (d->count != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(args[i]);
        if (d->len[i] != len || (len > 0 && memcmp(d->arg[i], args[i], len) != 0)) {
            return false;
        }
    }
    return true;
}

static int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1; /* put always writes lowercase hex */
}

/* Read exactly `digits` hex digits at s[*i]. */
static bool read_hex(const unsigned char *s, size_t n, size_t *i, int digits, uint32_t *value) {
    uint32_t v = 0;
    for (int k = 0; k < digits; k++) {
        if (*i >= n || hex_value(s[*i]) < 0) {
            return false;
        }
        v = v * 16 + (uint32_t)hex_value(s[*i]);
        (*i)++;
    }
    *value = v;
    return true;
}

static bool starts_with(const unsigned char *s, size_t n, size_t i, const char *prefix) {
    size_t len = strlen(prefix);
    return n - i >= len && memcmp(s + i, prefix, len) == 0;
}

/* ---- put.peek ----------------------------------------------------------- */

#define MIDDLE_DOT "\xC2\xB7"
#define EMPTY_LINE "\xE2\x88\x85 empty"

/* Decode one argument's rendering, s[i..end). */
static bool decode_peek_body(const unsigned char *s, size_t i, size_t end, bytes *b) {
    if (end - i == strlen(EMPTY_LINE) && memcmp(s + i, EMPTY_LINE, end - i) == 0) {
        return true; /* the empty argument */
    }
    while (i < end) {
        if (starts_with(s, end, i, MIDDLE_DOT)) {
            if (!push(b, ' ')) {
                return false;
            }
            i += 2;
        } else if (s[i] == '\\') {
            if (i + 1 >= end) {
                return false;
            }
            unsigned char e = s[i + 1];
            i += 2;
            uint32_t v = 0;
            bool ok = true;
            switch (e) {
            case '\\':
                ok = push(b, '\\');
                break;
            case 't':
                ok = push(b, '\t');
                break;
            case 'n':
                ok = push(b, '\n');
                break;
            case 'r':
                ok = push(b, '\r');
                break;
            case 'e':
                ok = push(b, 0x1B);
                break;
            case 'x':
                ok = read_hex(s, end, &i, 2, &v) && push(b, (unsigned char)v);
                break;
            case 'u': {
                if (i >= end || s[i] != '{') {
                    return false;
                }
                i++;
                size_t digits_start = i;
                v = 0;
                while (i < end && s[i] != '}' && i - digits_start < 6) {
                    int h = hex_value(s[i]);
                    if (h < 0) {
                        return false;
                    }
                    v = v * 16 + (uint32_t)h;
                    i++;
                }
                if (i >= end || s[i] != '}' || i == digits_start || v > 0x10FFFF) {
                    return false;
                }
                i++;
                ok = push_utf8(b, v);
                break;
            }
            default:
                return false;
            }
            if (!ok) {
                return false;
            }
        } else {
            if (!push(b, s[i])) {
                return false;
            }
            i++;
        }
    }
    return true;
}

bool decode_peek(const unsigned char *s, size_t n, decoded_args *out) {
    out->count = 0;
    /* Header: "1 arg" or "<count> args". */
    size_t i = 0;
    size_t count = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        count = count * 10 + (size_t)(s[i] - '0');
        i++;
    }
    const char *noun = count == 1 ? " arg\n" : " args\n";
    if (i == 0 || !starts_with(s, n, i, noun)) {
        return false;
    }
    i += strlen(noun);

    /* The index column is right-aligned to the width of the largest index. */
    int width = 1;
    for (size_t v = count > 0 ? count - 1 : 0; v >= 10; v /= 10) {
        width++;
    }

    for (size_t k = 0; k < count; k++) {
        size_t line_end = i;
        while (line_end < n && s[line_end] != '\n') {
            line_end++;
        }
        if (line_end >= n) {
            return false;
        }
        /* "<padded index>  " */
        char prefix[32];
        int len = snprintf(prefix, sizeof prefix, "%*zu  ", width, k);
        if (len < 0 || !starts_with(s, line_end, i, prefix)) {
            return false;
        }
        bytes b = {NULL, 0, 0};
        if (!decode_peek_body(s, i + (size_t)len, line_end, &b) || !take(out, &b)) {
            free(b.p);
            return false;
        }
        i = line_end + 1;
    }
    return i == n;
}

/* ---- put.quote ------------------------------------------------------------ */

/* The characters put.quote may leave unquoted (mode_quote.c, is_bare_safe). */
static bool bare_safe(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           strchr("_-./,:+@%", c) != NULL;
}

/* $'...' body up to the closing quote; *i is just past the opening $'. */
static bool decode_dollar(const unsigned char *s, size_t n, size_t *i, bytes *b) {
    while (*i < n && s[*i] != '\'') {
        if (s[*i] != '\\') {
            if (!push(b, s[*i])) {
                return false;
            }
            (*i)++;
            continue;
        }
        if (*i + 1 >= n) {
            return false;
        }
        unsigned char e = s[*i + 1];
        *i += 2;
        uint32_t v = 0;
        bool ok;
        switch (e) {
        case '\\':
            ok = push(b, '\\');
            break;
        case 'n':
            ok = push(b, '\n');
            break;
        case 't':
            ok = push(b, '\t');
            break;
        case 'r':
            ok = push(b, '\r');
            break;
        case 'x':
            ok = read_hex(s, n, i, 2, &v) && push(b, (unsigned char)v);
            break;
        default:
            return false; /* includes \' : bash 3.2 mis-parses it */
        }
        if (!ok) {
            return false;
        }
    }
    if (*i >= n) {
        return false;
    }
    (*i)++; /* closing quote */
    return true;
}

bool decode_quote(const unsigned char *s, size_t n, decoded_args *out) {
    out->count = 0;
    if (n == 0 || s[n - 1] != '\n') {
        return false;
    }
    n--; /* drop the newline */
    size_t i = 0;
    if (n == 0) {
        return true; /* no arguments */
    }
    for (;;) {
        bytes b = {NULL, 0, 0};
        bool any = false; /* has this word started? */
        bool ok = true;
        while (ok && i < n && s[i] != ' ') {
            any = true;
            if (s[i] == '\'') {
                i++;
                while (i < n && s[i] != '\'') {
                    ok = ok && push(&b, s[i]);
                    i++;
                }
                ok = ok && i < n;
                i++;
            } else if (starts_with(s, n, i, "\\'")) {
                ok = push(&b, '\'');
                i += 2;
            } else if (starts_with(s, n, i, "$'")) {
                i += 2;
                ok = decode_dollar(s, n, &i, &b);
            } else if (bare_safe(s[i])) {
                ok = push(&b, s[i]);
                i++;
            } else {
                ok = false; /* an unsafe character outside quotes */
            }
        }
        if (!ok || !any || !take(out, &b)) {
            free(b.p);
            return false;
        }
        if (i == n) {
            return true;
        }
        i++; /* the single space between words */
        if (i == n) {
            return false; /* trailing space */
        }
    }
}

/* ---- put.json ------------------------------------------------------------- */

static bool decode_json_string(const unsigned char *s, size_t n, size_t *i, bytes *b) {
    if (*i >= n || s[*i] != '"') {
        return false;
    }
    (*i)++;
    while (*i < n && s[*i] != '"') {
        unsigned char c = s[*i];
        if (c < 0x20) {
            return false; /* RFC 8259: control characters must be escaped */
        }
        if (c != '\\') {
            if (!push(b, c)) {
                return false;
            }
            (*i)++;
            continue;
        }
        if (*i + 1 >= n) {
            return false;
        }
        unsigned char e = s[*i + 1];
        *i += 2;
        uint32_t v = 0;
        bool ok;
        switch (e) {
        case '"':
        case '\\':
        case '/':
            ok = push(b, e);
            break;
        case 'b':
            ok = push(b, '\b');
            break;
        case 'f':
            ok = push(b, '\f');
            break;
        case 'n':
            ok = push(b, '\n');
            break;
        case 'r':
            ok = push(b, '\r');
            break;
        case 't':
            ok = push(b, '\t');
            break;
        case 'u':
            /* put only escapes BMP characters this way, never surrogates. */
            ok = read_hex(s, n, i, 4, &v) && !(v >= 0xD800 && v <= 0xDFFF) && push_utf8(b, v);
            break;
        default:
            return false;
        }
        if (!ok) {
            return false;
        }
    }
    if (*i >= n) {
        return false;
    }
    (*i)++;
    return true;
}

bool decode_json(const unsigned char *s, size_t n, decoded_args *out) {
    out->count = 0;
    if (n < 3 || s[0] != '[' || s[n - 2] != ']' || s[n - 1] != '\n') {
        return false;
    }
    size_t end = n - 2; /* index of ']' */
    size_t i = 1;
    if (i == end) {
        return true; /* [] */
    }
    for (;;) {
        bytes b = {NULL, 0, 0};
        if (!decode_json_string(s, end, &i, &b) || !take(out, &b)) {
            free(b.p);
            return false;
        }
        if (i == end) {
            return true;
        }
        if (s[i] != ',') {
            return false;
        }
        i++;
    }
}
