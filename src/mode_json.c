/*
 * mode_json.c — put.json: print the arguments as a JSON array of strings.
 *
 *   $ put.json a "b c" 'say "hi"'
 *   ["a","b c","say \"hi\""]
 *
 * JSON strings must be valid Unicode. Rather than silently replacing bad
 * bytes, put.json checks every argument first and fails with exit code 3
 * before writing anything, so you never get half an array.
 */
#include "modes.h"
#include "out.h"
#include "utf8.h"

#include <stdio.h>
#include <string.h>

static void emit_json_string(const char *arg) {
    const unsigned char *s = (const unsigned char *)arg;
    size_t n = strlen(arg);

    out_byte('"');
    size_t i = 0;
    while (i < n) {
        utf8_char c = utf8_decode(s + i, n - i); /* always valid: checked earlier */
        switch (c.cp) {
        case '"':
            out_str("\\\"");
            break;
        case '\\':
            out_str("\\\\");
            break;
        case '\n':
            out_str("\\n");
            break;
        case '\r':
            out_str("\\r");
            break;
        case '\t':
            out_str("\\t");
            break;
        case '\b':
            out_str("\\b");
            break;
        case '\f':
            out_str("\\f");
            break;
        default:
            /* Other control characters must be escaped. U+2028 and U+2029
             * are legal JSON but break JavaScript string literals, so escape
             * those too. */
            if (c.cp < 0x20 || c.cp == 0x7F || c.cp == 0x2028 || c.cp == 0x2029) {
                out_printf("\\u%04x", c.cp);
            } else {
                out_bytes(arg + i, c.len);
            }
            break;
        }
        i += c.len;
    }
    out_byte('"');
}

int mode_json(size_t n, char *const args[]) {
    for (size_t i = 0; i < n; i++) {
        if (!utf8_valid(args[i], strlen(args[i]))) {
            fprintf(stderr, "put.json: argument %zu is not valid UTF-8 (put.peek shows why)\n", i);
            return EXIT_NOT_UTF8;
        }
    }

    out_byte('[');
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            out_byte(',');
        }
        emit_json_string(args[i]);
    }
    out_str("]\n");
    return EXIT_OK;
}
