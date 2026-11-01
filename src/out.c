/*
 * out.c — buffered output with error tracking.
 *
 * We use C's standard I/O library (stdio). fwrite() copies bytes into a buffer
 * in memory and only talks to the operating system when the buffer fills or
 * we flush it. That's fast, but it means a write can "succeed" now and fail
 * later. So we remember the first failure and report it in out_finish().
 */
#include "out.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
 * C note: `static` at file level means "private to this file". No other .c
 * file can see these two variables, which is how C does encapsulation.
 */
static bool failed = false;
static int saved_errno = 0;

static void note_failure(void) {
    if (!failed) {
        failed = true;
        saved_errno = errno; /* errno is set by the library call that just failed */
    }
}

void out_bytes(const char *p, size_t n) {
    if (failed || n == 0) {
        return; /* after one error, stop writing instead of erroring again */
    }
    if (fwrite(p, 1, n, stdout) != n) {
        note_failure();
    }
}

void out_str(const char *s) {
    out_bytes(s, strlen(s));
}

void out_byte(char c) {
    out_bytes(&c, 1); /* &c is the address of c: a pointer to one byte */
}

void out_printf(const char *fmt, ...) {
    if (failed) {
        return;
    }
    /* C note: `...` takes a variable number of arguments; va_list walks them. */
    va_list ap;
    va_start(ap, fmt);
    /* clang-tidy's analyzer misreads va_start here and reports an
     * uninitialized va_list, a known false positive. */
    int written = vfprintf(stdout, fmt, ap); // NOLINT(clang-analyzer-valist.Uninitialized)
    va_end(ap);
    if (written < 0) {
        note_failure();
    }
}

int out_finish(const char *prog) {
    /* fclose flushes whatever is still buffered, which is where most errors
     * (like a full disk) actually show up. */
    if (fclose(stdout) != 0) {
        note_failure();
    }
    if (!failed) {
        return 0;
    }
    if (saved_errno != 0) {
        fprintf(stderr, "%s: write error: %s\n", prog, strerror(saved_errno));
    } else {
        fprintf(stderr, "%s: write error\n", prog);
    }
    return 1;
}
