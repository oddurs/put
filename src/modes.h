/*
 * modes.h — one function per put.<mode>.
 *
 * Every mode has the same shape: it gets the arguments (everything after the
 * program name) and returns an exit code. `char *const args[]` reads as
 * "an array of constant pointers to char": the mode may not repoint them.
 */
#ifndef PUT_MODES_H
#define PUT_MODES_H

#include <stddef.h>

/* Exit codes, shared by every mode. */
enum {
    EXIT_OK = 0,
    EXIT_WRITE_ERROR = 1,
    EXIT_UNKNOWN_MODE = 2,
    EXIT_NOT_UTF8 = 3,
};

int mode_plain(size_t n, char *const args[]);
int mode_raw(size_t n, char *const args[]);
int mode_lines(size_t n, char *const args[]);
int mode_nul(size_t n, char *const args[]);
int mode_peek(size_t n, char *const args[]);
int mode_quote(size_t n, char *const args[]);
int mode_json(size_t n, char *const args[]);

#endif
