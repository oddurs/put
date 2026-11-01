/*
 * mode_quote.c — put.quote: print arguments as shell input that recreates them.
 *
 *   $ put.quote plain "two words" "it's" $'tab\there' ""
 *   plain 'two words' 'it'\''s' $'tab\there' ''
 *
 * Three forms, picked per argument:
 *   bare     only characters no shell treats specially:   plain
 *   '...'    anything printable; a ' inside becomes '\'': 'it'\''s'
 *   $'...'   anything with control bytes, invalid UTF-8 or invisible
 *            characters, so the line you copy shows what's really there.
 *            Understood by bash and zsh.
 */
#include "modes.h"
#include "out.h"
#include "utf8.h"

#include <stdbool.h>
#include <string.h>

/*
 * C note: <ctype.h> has isalnum(), but it depends on the locale and is
 * undefined for negative char values, a classic C trap. Checking ranges by
 * hand is simpler and always correct.
 */
static bool is_bare_safe(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == '/' || c == ',' || c == ':' || c == '+' || c == '=' ||
           c == '@' || c == '%';
}

static bool can_be_bare(const unsigned char *s, size_t n) {
    if (n > 0 && s[0] == '=') {
        return false; /* zsh expands a leading = into a command's path */
    }
    for (size_t i = 0; i < n; i++) {
        if (!is_bare_safe(s[i])) {
            return false;
        }
    }
    return n > 0; /* the empty string needs quotes: '' */
}

/* True if the argument contains anything that would be invisible or
 * mangled inside plain single quotes. */
static bool needs_dollar_quotes(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        utf8_char c = utf8_decode(s + i, n - i);
        if (!c.valid || c.cp < 0x20 || c.cp == 0x7F || utf8_is_invisible(c.cp)) {
            return true;
        }
        i += c.len;
    }
    return false;
}

static void emit_single_quoted(const char *arg) {
    out_byte('\'');
    for (const char *p = arg; *p != '\0'; p++) {
        if (*p == '\'') {
            out_str("'\\''"); /* close the quote, add an escaped ', reopen */
        } else {
            out_byte(*p);
        }
    }
    out_byte('\'');
}

static void emit_hex_byte(unsigned char b) {
    out_printf("\\x%02x", b);
}

static void emit_dollar_quoted(const unsigned char *s, size_t n) {
    out_str("$'");
    size_t i = 0;
    while (i < n) {
        utf8_char c = utf8_decode(s + i, n - i);
        if (!c.valid) {
            emit_hex_byte(s[i]);
        } else if (c.cp == '\\' || c.cp == '\'') {
            out_byte('\\');
            out_byte((char)s[i]);
        } else if (c.cp == '\n') {
            out_str("\\n");
        } else if (c.cp == '\t') {
            out_str("\\t");
        } else if (c.cp == '\r') {
            out_str("\\r");
        } else if (c.cp < 0x20 || c.cp == 0x7F || utf8_is_invisible(c.cp)) {
            /* Byte escapes, not \u: they mean the same in every locale. */
            for (size_t k = 0; k < c.len; k++) {
                emit_hex_byte(s[i + k]);
            }
        } else {
            out_bytes((const char *)(s + i), c.len);
        }
        i += c.len;
    }
    out_byte('\'');
}

int mode_quote(size_t n, char *const args[]) {
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            out_byte(' ');
        }
        const unsigned char *s = (const unsigned char *)args[i];
        size_t len = strlen(args[i]);
        if (can_be_bare(s, len)) {
            out_str(args[i]);
        } else if (needs_dollar_quotes(s, len)) {
            emit_dollar_quoted(s, len);
        } else {
            emit_single_quoted(args[i]);
        }
    }
    out_byte('\n');
    return EXIT_OK;
}
