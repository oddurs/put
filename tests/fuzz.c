/*
 * fuzz.c — libFuzzer harness. Build and run with `make fuzz`.
 *
 * libFuzzer calls LLVMFuzzerTestOneInput millions of times with generated
 * bytes, keeping inputs that reach new code. We split each input on NUL bytes
 * into an argument list (arguments can't contain NUL, just as in real life),
 * run every mode on it, and check properties that must hold for any input:
 *
 *   put, put.raw, put.lines, put.nul   output is exactly the expected bytes
 *   put.peek    valid UTF-8, no control bytes besides line ends, no invisible
 *               characters, one line per argument plus the header
 *   put.quote   valid UTF-8, one line, no control bytes or invisible characters
 *   put.json    valid UTF-8, one line, no raw control bytes
 *
 * Breaking a property calls abort(), which libFuzzer reports as a crash and
 * saves the input that caused it. AddressSanitizer and UBSan are on too.
 */
#include "modes.h"
#include "out.h"
#include "utf8.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_ARGS 16

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* Fail loudly with a reason; libFuzzer saves the input. */
static void fail(const char *what) {
    fprintf(stderr, "fuzz property broken: %s\n", what);
    abort();
}

/* Point stdout at a temporary file once, so output can be read back. */
static void redirect_stdout(void) {
    static bool done = false;
    if (done) {
        return;
    }
    FILE *tmp = tmpfile();
    if (tmp == NULL || dup2(fileno(tmp), STDOUT_FILENO) < 0) {
        fail("could not redirect stdout");
    }
    done = true;
}

typedef int (*mode_fn)(size_t n, char *const args[]);

/* Run one mode and return its output (malloc'd; caller frees) and length. */
static unsigned char *capture(mode_fn mode, size_t n, char *const args[], size_t *len) {
    if (ftruncate(STDOUT_FILENO, 0) != 0 || lseek(STDOUT_FILENO, 0, SEEK_SET) != 0) {
        fail("could not reset capture file");
    }
    mode(n, args);
    out_flush();

    off_t end = lseek(STDOUT_FILENO, 0, SEEK_END);
    if (end < 0) {
        fail("could not size capture file");
    }
    *len = (size_t)end;
    unsigned char *buf = malloc(*len + 1);
    if (buf == NULL || pread(STDOUT_FILENO, buf, *len, 0) != (ssize_t)*len) {
        fail("could not read capture file");
    }
    return buf;
}

/* Count bytes equal to b. */
static size_t count_byte(const unsigned char *s, size_t n, unsigned char b) {
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        count += (s[i] == b);
    }
    return count;
}

/* True if every byte is printable except line ends, and (if asked) no
 * decoded character is one of the invisible ones. */
static bool clean_text(const unsigned char *s, size_t n, bool forbid_invisible) {
    size_t i = 0;
    while (i < n) {
        utf8_char c = utf8_decode(s + i, n - i);
        if (!c.valid) {
            return false;
        }
        if ((c.cp < 0x20 && c.cp != '\n') || c.cp == 0x7F) {
            return false;
        }
        if (forbid_invisible && utf8_is_invisible(c.cp)) {
            return false;
        }
        i += c.len;
    }
    return true;
}

/* Build what put / put.raw / put.lines / put.nul must print. */
static void expected_simple(size_t n, char *const args[], char sep, char end_each,
                            bool newline_at_end, unsigned char *out, size_t *len) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (sep != 0 && i > 0) {
            out[k++] = (unsigned char)sep;
        }
        size_t arg_len = strlen(args[i]);
        memcpy(out + k, args[i], arg_len);
        k += arg_len;
        if (sep == 0) {
            out[k++] = (unsigned char)end_each;
        }
    }
    if (newline_at_end) {
        out[k++] = '\n';
    }
    *len = k;
}

static void check_exact(mode_fn mode, size_t n, char *const args[], char sep, char end_each,
                        bool newline_at_end, size_t total, const char *what) {
    unsigned char *want = malloc(total + n + 2);
    if (want == NULL) {
        fail("out of memory");
    }
    size_t want_len = 0;
    expected_simple(n, args, sep, end_each, newline_at_end, want, &want_len);

    size_t got_len = 0;
    unsigned char *got = capture(mode, n, args, &got_len);
    if (got_len != want_len || memcmp(got, want, want_len) != 0) {
        fail(what);
    }
    free(got);
    free(want);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    redirect_stdout();

    /* Copy the input with a NUL after it, then cut it into arguments at
     * every NUL byte. */
    char *copy = malloc(size + 1);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, data, size);
    copy[size] = '\0';

    char *args[MAX_ARGS];
    size_t n = 0;
    size_t start = 0;
    for (size_t i = 0; i <= size && n < MAX_ARGS; i++) {
        if (copy[i] == '\0') {
            args[n++] = copy + start;
            start = i + 1;
        }
    }

    check_exact(mode_plain, n, args, ' ', 0, true, size, "put output differs");
    check_exact(mode_raw, n, args, ' ', 0, false, size, "put.raw output differs");
    check_exact(mode_lines, n, args, 0, '\n', false, size, "put.lines output differs");
    check_exact(mode_nul, n, args, 0, '\0', false, size, "put.nul output differs");

    size_t len = 0;
    unsigned char *out = capture(mode_peek, n, args, &len);
    if (!clean_text(out, len, true)) {
        fail("put.peek wrote a control byte, invalid UTF-8 or an invisible character");
    }
    if (count_byte(out, len, '\n') != n + 1) {
        fail("put.peek did not write exactly one line per argument");
    }
    free(out);

    out = capture(mode_quote, n, args, &len);
    if (!clean_text(out, len, true) || count_byte(out, len, '\n') != 1 || out[len - 1] != '\n') {
        fail("put.quote output is not one clean line");
    }
    free(out);

    bool all_utf8 = true;
    for (size_t i = 0; i < n; i++) {
        all_utf8 = all_utf8 && utf8_valid(args[i], strlen(args[i]));
    }
    if (all_utf8) {
        out = capture(mode_json, n, args, &len);
        if (!clean_text(out, len, false) || count_byte(out, len, '\n') != 1 || out[0] != '[' ||
            len < 3 || out[len - 2] != ']') {
            fail("put.json output is not one clean line");
        }
        free(out);
    }

    free(copy);
    return 0;
}
