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
 *
 * The output must survive being pasted into an *interactive* shell too,
 * where `!` triggers history expansion. Single quotes protect `!`, and so
 * does $'...' as long as it contains no \' sequence: bash 3.2 (still the
 * /bin/bash on macOS) treats that ' as the end of the quotes and expands a
 * later `!`. So inside $'...' a ' is always written as \x27.
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
/*
 * `=` is deliberately missing: zsh expands a leading =cmd into the path of
 * cmd, and with the MAGIC_EQUAL_SUBST option also expands a==cmd and a:=cmd.
 * `~`, `#`, `^`, `!`, `*`, `?`, `[`, `{` and friends are missing for the
 * usual reasons: tilde, comment, glob, history and brace expansion.
 */
static bool is_bare_safe(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '-' || c == '.' || c == '/' || c == ',' || c == ':' || c == '+' || c == '@' ||
           c == '%';
}

static bool can_be_bare(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!is_bare_safe(s[i])) {
            return false;
        }
    }
    return n > 0; /* the empty string needs quotes: '' */
}

/* True if the argument contains anything that would be invisible or
 * mangled inside plain single quotes. That includes a combining mark with no
 * character of its own before it, which would draw on top of a quote mark. */
static bool needs_dollar_quotes(const unsigned char *s, size_t n) {
    size_t i = 0;
    uint32_t prev = '\'';
    while (i < n) {
        utf8_char c = utf8_decode(s + i, n - i);
        if (!c.valid || c.cp < 0x20 || c.cp == 0x7F || utf8_is_invisible(c.cp)) {
            return true;
        }
        if (prev == '\'' && utf8_is_combining(c.cp)) {
            return true;
        }
        prev = c.cp;
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
    bool after_base = false; /* was the last thing written a plain character? */
    while (i < n) {
        utf8_char c = utf8_decode(s + i, n - i);
        bool base = false;
        if (!c.valid) {
            emit_hex_byte(s[i]);
        } else if (c.cp == '\\') {
            out_str("\\\\");
        } else if (c.cp == '\'') {
            out_str("\\x27"); /* never \' : see the comment at the top */
        } else if (c.cp == '\n') {
            out_str("\\n");
        } else if (c.cp == '\t') {
            out_str("\\t");
        } else if (c.cp == '\r') {
            out_str("\\r");
        } else if (c.cp < 0x20 || c.cp == 0x7F || utf8_is_invisible(c.cp) ||
                   (!after_base && utf8_is_combining(c.cp))) {
            /* Byte escapes, not \u: they mean the same in every locale. */
            for (size_t k = 0; k < c.len; k++) {
                emit_hex_byte(s[i + k]);
            }
        } else {
            out_bytes((const char *)(s + i), c.len);
            base = true;
        }
        after_base = base;
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
