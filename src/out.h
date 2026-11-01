/*
 * out.h — everything put writes to stdout goes through here.
 *
 * C note: a header (.h) declares what a module offers; the matching .c file
 * defines it. Other files #include the header so the compiler can check calls
 * against these declarations. The #ifndef "include guard" below stops the
 * declarations being pasted in twice if two headers both include this one.
 */
#ifndef PUT_OUT_H
#define PUT_OUT_H

#include <stddef.h>

/* Write n bytes starting at p. The bytes may contain anything, even NUL. */
void out_bytes(const char *p, size_t n);

/* Write a NUL-terminated string (not including the terminating NUL). */
void out_str(const char *s);

/* Write one byte. */
void out_byte(char c);

/*
 * printf-style formatted output. The attribute (a GCC/Clang extension) lets
 * the compiler check the format string against the arguments, like it does
 * for printf itself.
 */
void out_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/*
 * Flush and close stdout. Returns 0 if every byte was written, or prints an
 * error naming `prog` to stderr and returns 1. Call this exactly once, last.
 */
int out_finish(const char *prog);

#endif
