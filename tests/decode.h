/*
 * decode.h — read put.peek, put.quote and put.json output back into the
 * original arguments.
 *
 * The fuzzer runs every mode and then decodes its output: if decoding
 * doesn't give back exactly the arguments that went in, information was lost
 * or mangled. Each decoder is strict and accepts only what put is supposed
 * to write, so it also checks the format: a bare word in put.quote output
 * containing an unsafe character, or a \' inside $'...', is a decode error.
 *
 * The quote and json decoders are themselves checked against real shells
 * and Python's JSON parser by tests/roundtrip.py. The peek decoder *is* the
 * definition of what plain put.peek output means.
 */
#ifndef PUT_TEST_DECODE_H
#define PUT_TEST_DECODE_H

#include <stdbool.h>
#include <stddef.h>

#define DECODE_MAX_ARGS 64

/* Decoded arguments: each one a malloc'd byte string with its length. */
typedef struct {
    size_t count;
    unsigned char *arg[DECODE_MAX_ARGS];
    size_t len[DECODE_MAX_ARGS];
} decoded_args;

/* Each returns false if the output is malformed. On success *out holds the
 * arguments; call decoded_free on it either way. */
bool decode_peek(const unsigned char *s, size_t n, decoded_args *out);
bool decode_quote(const unsigned char *s, size_t n, decoded_args *out);
bool decode_json(const unsigned char *s, size_t n, decoded_args *out);

void decoded_free(decoded_args *d);

/* True if d holds exactly the n NUL-terminated strings in args. */
bool decoded_equal(const decoded_args *d, size_t n, char *const args[]);

#endif
