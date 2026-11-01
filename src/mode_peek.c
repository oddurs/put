/*
 * mode_peek.c — put.peek: show exactly what each argument contains.
 *
 *   $ put.peek hello "two  words " $'crlf\r' ""
 *   4 args
 *   0  hello
 *   1  two  words·
 *   2  crlf\r
 *   3  ∅ empty
 *
 * Two output styles:
 *   styled  (stdout is a terminal, NO_COLOR unset): rendered escapes and
 *           markers are colored, so they can't be confused with real text.
 *   plain   (piped, or NO_COLOR set): no color, so anything that could be
 *           confused with a marker is escaped instead: a literal backslash
 *           becomes \\, a literal · becomes \u{b7}, a literal ∅ \u{2205}.
 * Either way, put.peek never writes a control byte from its input, nor a
 * character that renders as nothing (see utf8_is_invisible). In plain style
 * the output is also lossless: every argument can be recovered exactly from
 * its line, which tests/fuzz.c checks by decoding it.
 */
#include "modes.h"
#include "out.h"
#include "utf8.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ANSI terminal color codes. "\033" is the ESC byte, written in octal. */
#define STYLE_ESCAPE "\033[36m" /* cyan: rendered escapes like \r */
#define STYLE_MARK   "\033[2m"  /* dim: markers and indices */
#define STYLE_WARN   "\033[31m" /* red: bidirectional controls */
#define STYLE_RESET  "\033[0m"

#define MIDDLE_DOT    "\xC2\xB7"     /* · U+00B7, marks leading/trailing spaces */
#define EMPTY_SET     "\xE2\x88\x85" /* ∅ U+2205, marks an empty argument */
#define CP_MIDDLE_DOT 0xB7U
#define CP_EMPTY_SET  0x2205U

static bool styled = false;

/* Color follows https://no-color.org: any non-empty NO_COLOR turns it off. */
static bool want_color(void) {
    const char *no_color = getenv("NO_COLOR");
    if (no_color != NULL && no_color[0] != '\0') {
        return false;
    }
    const char *term = getenv("TERM");
    if (term != NULL && strcmp(term, "dumb") == 0) {
        return false;
    }
    return isatty(STDOUT_FILENO) == 1;
}

/* Write text wrapped in a color when styled, or as-is when plain. */
static void emit(const char *style, const char *text) {
    if (styled) {
        out_str(style);
        out_str(text);
        out_str(STYLE_RESET);
    } else {
        out_str(text);
    }
}

/*
 * Write a hex escape such as \x1b or \u{a0}: prefix, value in lowercase hex
 * padded to at least `min_digits`, suffix.
 */
static void emit_hex(const char *style, const char *prefix, unsigned value, int min_digits,
                     const char *suffix) {
    if (styled) {
        out_str(style);
    }
    out_printf("%s%0*x%s", prefix, min_digits, value, suffix);
    if (styled) {
        out_str(STYLE_RESET);
    }
}

/* Render one control byte (< 0x20 or 0x7F) as a readable escape. */
static void render_control(unsigned char b) {
    switch (b) {
    case '\t':
        emit(STYLE_ESCAPE, "\\t");
        break;
    case '\n':
        emit(STYLE_ESCAPE, "\\n");
        break;
    case '\r':
        emit(STYLE_ESCAPE, "\\r");
        break;
    case 0x1B:
        emit(STYLE_ESCAPE, "\\e");
        break;
    default:
        emit_hex(STYLE_ESCAPE, "\\x", b, 2, "");
        break;
    }
}

/*
 * Render the character starting at s (n bytes remain) and return how many
 * bytes it used.
 *
 * *after_base says whether the last thing written was an ordinary character
 * from the input. A combining mark (like the accent in a decomposed é) is
 * only written as itself then; anywhere else it would land on a marker, an
 * escape or the index column, so it's escaped instead.
 */
static size_t render_char(const unsigned char *s, size_t n, bool *after_base) {
    utf8_char c = utf8_decode(s, n);
    bool base = false;
    if (!c.valid) {
        emit_hex(STYLE_ESCAPE, "\\x", s[0], 2, "");
    } else if (c.cp < 0x20 || c.cp == 0x7F) {
        render_control(s[0]);
    } else if (c.cp == '\\') {
        /* Styled: escapes are colored, so a plain \ is unambiguous.
         * Plain: double it, so \r can only mean a carriage return. */
        out_str(styled ? "\\" : "\\\\");
    } else if (utf8_is_invisible(c.cp)) {
        const char *style = utf8_is_bidi_control(c.cp) ? STYLE_WARN : STYLE_ESCAPE;
        emit_hex(style, "\\u{", c.cp, 1, "}");
    } else if ((!styled && (c.cp == CP_MIDDLE_DOT || c.cp == CP_EMPTY_SET)) ||
               (!*after_base && utf8_is_combining(c.cp))) {
        /* Plain output: · or ∅ would look like a marker. Either way: a
         * combining mark with nothing of its own to attach to. */
        emit_hex(STYLE_ESCAPE, "\\u{", c.cp, 1, "}");
    } else {
        out_bytes((const char *)s, c.len); /* an ordinary visible character */
        base = true;
    }
    *after_base = base;
    return c.len;
}

static void render_arg(const char *arg) {
    const unsigned char *s = (const unsigned char *)arg;
    size_t n = strlen(arg);

    if (n == 0) {
        emit(STYLE_MARK, EMPTY_SET " empty");
        return;
    }

    /* Find where the leading run of spaces ends and the trailing run starts.
     * Spaces in between are shown as ordinary spaces. */
    size_t lead_end = 0;
    while (lead_end < n && s[lead_end] == ' ') {
        lead_end++;
    }
    size_t trail_start = n;
    while (trail_start > lead_end && s[trail_start - 1] == ' ') {
        trail_start--;
    }

    size_t i = 0;
    bool after_base = false;
    while (i < n) {
        if (s[i] == ' ' && (i < lead_end || i >= trail_start)) {
            emit(STYLE_MARK, MIDDLE_DOT);
            after_base = false;
            i++;
        } else {
            i += render_char(s + i, n - i, &after_base);
        }
    }
}

/* How many decimal digits it takes to print v. */
static int digits(size_t v) {
    int d = 1;
    while (v >= 10) {
        v /= 10;
        d++;
    }
    return d;
}

int mode_peek(size_t n, char *const args[]) {
    styled = want_color();

    out_printf("%zu %s\n", n, n == 1 ? "arg" : "args");

    int width = digits(n > 0 ? n - 1 : 0);
    for (size_t i = 0; i < n; i++) {
        if (styled) {
            out_str(STYLE_MARK);
        }
        /* %*zu: the * takes the field width from the argument list. */
        out_printf("%*zu", width, i);
        if (styled) {
            out_str(STYLE_RESET);
        }
        out_str("  ");
        render_arg(args[i]);
        out_byte('\n');
    }
    return EXIT_OK;
}
