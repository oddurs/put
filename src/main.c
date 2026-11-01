/*
 * main.c — entry point: pick a mode from the program's name and run it.
 *
 * put never reads options from its arguments. Instead, one binary is
 * installed under several names (put, put.raw, put.peek, ...) and checks
 * which name it was started with. That name arrives as argv[0].
 */
#include "modes.h"
#include "out.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

/* Defined below; the table needs to know they exist. */
static int mode_help(size_t n, char *const args[]);
static int mode_version(size_t n, char *const args[]);

/*
 * The mode table. Each row pairs a name with a function pointer: a variable
 * that holds the address of a function, so the right one can be looked up
 * and called at run time. put.help prints this same table, so the help text
 * can never drift out of sync with what actually exists.
 */
struct mode {
    const char *name; /* NULL for plain `put` */
    int (*run)(size_t n, char *const args[]);
    const char *summary;
};

static const struct mode MODES[] = {
    {NULL, mode_plain, "args joined by spaces, then a newline"},
    {"raw", mode_raw, "args joined by spaces, no trailing newline"},
    {"lines", mode_lines, "each arg on its own line"},
    {"nul", mode_nul, "each arg followed by a NUL byte (for xargs -0)"},
    {"peek", mode_peek, "show what each arg really contains"},
    {"quote", mode_quote, "args as shell input that recreates them"},
    {"json", mode_json, "args as a JSON array of strings"},
    {"help", mode_help, "this text"},
    {"version", mode_version, "the version number"},
};

/* sizeof the whole array / sizeof one element = number of elements. */
#define MODE_COUNT (sizeof MODES / sizeof MODES[0])

static int mode_help(size_t n, char *const args[]) {
    (void)n; /* (void) marks a parameter as deliberately unused */
    (void)args;

    out_str("put " PUT_VERSION " - print arguments exactly as given\n"
            "\n"
            "Every argument is printed literally: no flags, no escapes.\n"
            "The command name picks the mode:\n"
            "\n");
    for (size_t i = 0; i < MODE_COUNT; i++) {
        if (MODES[i].name == NULL) {
            out_printf("  %-12s %s\n", "put", MODES[i].summary);
        } else {
            out_printf("  put.%-8s %s\n", MODES[i].name, MODES[i].summary);
        }
    }
    out_str("\n"
            "Exit codes: 0 ok, 1 write error, 2 unknown mode, 3 put.json got invalid UTF-8\n"
            "More: man put, https://github.com/oddurs/put\n");
    return EXIT_OK;
}

static int mode_version(size_t n, char *const args[]) {
    (void)n;
    (void)args;
    out_str("put " PUT_VERSION "\n");
    return EXIT_OK;
}

/* "/usr/local/bin/put.peek" -> "put.peek". Returns a pointer into `path`. */
static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

static const struct mode *find_mode(const char *name) {
    if (strcmp(name, "put") == 0) {
        return &MODES[0];
    }
    if (strncmp(name, "put.", 4) != 0) {
        return NULL;
    }
    const char *suffix = name + 4; /* pointer arithmetic: skip "put." */
    for (size_t i = 1; i < MODE_COUNT; i++) {
        if (strcmp(suffix, MODES[i].name) == 0) {
            return &MODES[i];
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    /*
     * When a reader like `head -1` exits early, the next write raises
     * SIGPIPE. The default action quietly ends the program, which is what
     * Unix tools are expected to do. A parent process can leave SIGPIPE
     * ignored, so restore the default explicitly.
     */
    signal(SIGPIPE, SIG_DFL);

    /* argc can be 0 if put is started with an empty argument list. */
    const char *name = argc > 0 ? base_name(argv[0]) : "put";

    const struct mode *mode = find_mode(name);
    if (mode == NULL) {
        fprintf(stderr, "put: unknown mode '%s'; run put.help for the list\n", name);
        return EXIT_UNKNOWN_MODE;
    }

    size_t n = argc > 0 ? (size_t)argc - 1 : 0;
    char *const *args = argc > 0 ? argv + 1 : argv;

    int status = mode->run(n, args);
    int write_status = out_finish(name);
    return status != EXIT_OK ? status : write_status;
}
