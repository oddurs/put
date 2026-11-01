/*
 * mode_basic.c — put, put.raw, put.lines and put.nul.
 *
 * These four are the whole point of the project: print the bytes, nothing
 * else. No argument is ever inspected for flags or escapes.
 */
#include "modes.h"
#include "out.h"

#include <string.h>

/* Arguments joined by single spaces. Shared by put and put.raw. */
static void join_with_spaces(size_t n, char *const args[]) {
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            out_byte(' ');
        }
        out_str(args[i]);
    }
}

int mode_plain(size_t n, char *const args[]) {
    join_with_spaces(n, args);
    out_byte('\n');
    return EXIT_OK;
}

int mode_raw(size_t n, char *const args[]) {
    join_with_spaces(n, args);
    return EXIT_OK;
}

int mode_lines(size_t n, char *const args[]) {
    for (size_t i = 0; i < n; i++) {
        out_str(args[i]);
        out_byte('\n');
    }
    return EXIT_OK;
}

int mode_nul(size_t n, char *const args[]) {
    for (size_t i = 0; i < n; i++) {
        /* C note: every C string already ends with a NUL byte (0) in memory.
         * Writing strlen + 1 bytes sends the string *and* that terminator. */
        out_bytes(args[i], strlen(args[i]) + 1);
    }
    return EXIT_OK;
}
