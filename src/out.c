/*
 * out.c — buffered output straight to the stdout file descriptor.
 *
 * C's stdio (fwrite, printf) would work, but a tool whose only job is output
 * should get every failure case right, and that's easier without stdio in the
 * way. We keep our own 64 KiB buffer and hand it to the operating system with
 * write(2), handling the cases stdio leaves to chance:
 *
 *   partial writes  write() may accept fewer bytes than asked; loop until done
 *   EINTR           a signal interrupted the call; just try again
 *   EAGAIN          stdout was left non-blocking by another program (common
 *                   with Node.js and some terminals); wait until it's ready
 *                   with select()
 *   late errors     the first failure is remembered and reported once, at the
 *                   end, with a nonzero exit code
 */
#include "out.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#define OUT_BUFFER_SIZE 65536

/*
 * C note: `static` at file level means "private to this file". No other .c
 * file can see these variables, which is how C does encapsulation.
 */
static char buffer[OUT_BUFFER_SIZE];
static size_t used = 0;
static bool failed = false;
static int saved_errno = 0;

static void fail_with(int err) {
    if (!failed) {
        failed = true;
        saved_errno = err;
    }
}

/* EAGAIN and EWOULDBLOCK are the same number on most systems, but not all. */
static bool would_block(int err) {
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    if (err == EWOULDBLOCK) {
        return true;
    }
#endif
    return err == EAGAIN;
}

/*
 * Sleep until stdout can take more data. Returns false on a real error.
 *
 * select() rather than poll(): macOS's poll() doesn't support character
 * devices and returns POLLNVAL at once for a terminal, which would turn this
 * wait into a busy loop. select() works for pipes, sockets and terminals on
 * every Unix. Its fd_set has a size limit (FD_SETSIZE, usually 1024), which
 * file descriptor 1 is comfortably under.
 */
static bool wait_until_writable(void) {
    for (;;) {
        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(STDOUT_FILENO, &writable);
        int ready = select(STDOUT_FILENO + 1, NULL, &writable, NULL, NULL); /* NULL: no timeout */
        if (ready > 0) {
            return true; /* writable, or an error the next write() will report */
        }
        if (ready < 0 && errno != EINTR) {
            return false;
        }
    }
}

/* Write all n bytes at p to stdout, or record why we couldn't. */
static void write_all(const char *p, size_t n) {
    while (n > 0 && !failed) {
        ssize_t written = write(STDOUT_FILENO, p, n);
        if (written > 0) {
            p += written;
            n -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else if (written < 0 && would_block(errno)) {
            if (!wait_until_writable()) {
                fail_with(errno);
            }
        } else {
            /* write() returning 0 for a nonzero request "shouldn't happen",
             * but if it does, looping would spin forever. */
            fail_with(written < 0 ? errno : EIO);
        }
    }
}

void out_flush(void) {
    write_all(buffer, used);
    used = 0;
}

void out_bytes(const char *p, size_t n) {
    if (failed || n == 0) {
        return; /* after one error, stop writing instead of erroring again */
    }
    if (n > sizeof buffer - used) {
        out_flush();
        if (n >= sizeof buffer) {
            write_all(p, n); /* too big to buffer: send it directly */
            return;
        }
    }
    memcpy(buffer + used, p, n);
    used += n;
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
    /*
     * C note: `...` takes a variable number of arguments; a va_list walks
     * them. vsnprintf formats into a buffer we own and returns the length
     * the full result needs, so a long result can get a bigger buffer. A
     * va_list can only be walked once, so va_copy keeps a spare.
     */
    char small[128];
    va_list ap;
    va_list again;
    va_start(ap, fmt);
    va_copy(again, ap);
    /* clang-tidy's analyzer misreads va_start here and reports an
     * uninitialized va_list, a known false positive. */
    int len =
        vsnprintf(small, sizeof small, fmt, ap); // NOLINT(clang-analyzer-valist.Uninitialized)
    va_end(ap);

    if (len < 0) {
        fail_with(errno != 0 ? errno : EINVAL);
    } else if ((size_t)len < sizeof small) {
        out_bytes(small, (size_t)len);
    } else {
        char *big = malloc((size_t)len + 1);
        if (big == NULL) {
            fail_with(ENOMEM);
        } else {
            vsnprintf(big, (size_t)len + 1, fmt, again);
            out_bytes(big, (size_t)len);
            free(big);
        }
    }
    va_end(again);
}

int out_finish(const char *prog) {
    out_flush();
    /*
     * Closing stdout can report an error that write() didn't, for example on
     * network filesystems. EBADF means stdout was never open; if anything had
     * been written, write() would have failed already, so that's fine.
     */
    if (!failed && close(STDOUT_FILENO) != 0 && errno != EINTR && errno != EBADF) {
        fail_with(errno);
    }
    if (!failed) {
        return 0;
    }
    fprintf(stderr, "%s: write error: %s\n", prog, strerror(saved_errno));
    return 1;
}
