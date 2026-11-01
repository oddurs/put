# A tour of put, for people new to C

This codebase is small (about 1,000 lines of C, a good share of it comments, plus a generated Unicode table) and written to be read. This guide gives a reading order and points out the C ideas you'll meet in each file. Every source file also has `C note:` comments at the places where something C-specific is happening.

## 0. How a C program gets built

C is compiled in two stages, and the [Makefile](../Makefile) spells both out:

1. **Compile.** Each `.c` file is compiled on its own into an object file (`build/obj/main.o`). Before compiling, the *preprocessor* pastes in every `#include`d header and expands every `#define` macro. The compiler only sees one file at a time, which is why headers exist: `out.h` tells `main.c` that `out_str()` exists and what it takes, without `main.c` seeing its code.
2. **Link.** The linker joins the object files into one executable (`build/put`) and connects each call to the function it names.

Run `make` and read the commands it prints: each `cc ... -c` line is step 1, and the final `cc ... -o build/put` is step 2. Then `ln -sf put build/put.peek` and friends create the extra names.

The `-W...` flags turn on warnings. C compiles plenty of code that is almost certainly wrong, and these flags make the compiler say so. CI adds `-Werror`, which turns every warning into a build failure.

## 1. src/main.c: where it starts

Things to notice:

- **`int main(int argc, char *argv[])`** is where every C program starts. `argc` counts the arguments and `argv` is an array of them. `argv[0]` is the program's own name, which is how put knows whether it was run as `put.peek` or `put.json`.
- **Strings are pointers.** A C string is just the address of its first byte, and it ends at the first zero byte (the NUL terminator). There is no length stored anywhere. `char *` means "pointer to char".
- **`strrchr` returns a pointer into the same string.** `base_name()` doesn't copy anything; it returns the address one past the last `/`. Pointer arithmetic (`name + 4`) does the same to skip `"put."`.
- **Structs and function pointers.** `struct mode` groups a name, a function and a summary. The `run` field holds the address of a function, so `mode->run(n, args)` calls whichever mode was found. This is how C does what other languages do with interfaces or closures.
- **`sizeof MODES / sizeof MODES[0]`** is the standard trick for counting elements in a fixed array.

## 2. src/mode_basic.c: the core

The four plain modes are a few lines each. The interesting one is `mode_nul`: since every C string already has a NUL byte at the end in memory, writing `strlen + 1` bytes sends the terminator too.

## 3. src/out.c: output and errors

This is the most "systems" file in the project, and the one where production code differs most from tutorial code.

- **`static` at file level** makes a variable or function private to its file. That's C's only encapsulation.
- **Buffering.** Asking the operating system to write is slow compared to copying memory, so output collects in a 64 KiB array and goes out in large chunks. `out_bytes` copies into the buffer; `out_flush` hands it to the OS.
- **`write()` is a system call, and it can come up short.** It may write fewer bytes than asked (so `write_all` loops), be interrupted by a signal (`EINTR`: try again), or refuse because stdout is non-blocking (`EAGAIN`: wait with `select()`, then try again). Tutorial code usually calls `printf` and hopes. `tests/io.py` sets up the non-blocking case, which the first version of put got wrong.
- **Portability bites quietly.** An earlier version waited with `poll()`, the more modern call. It worked everywhere tested, but macOS's `poll()` doesn't support terminals and returns at once, so a full terminal would have made put spin at 100% CPU. `select()` is older and clunkier, and works on every Unix. The man pages' BUGS sections are worth reading.
- **`errno`** is a global set by library calls when they fail. `strerror(errno)` turns it into a message like "No space left on device". Try `build/put hi > /dev/full` on Linux, or `build/put hi >&-` to run with stdout closed.
- **Variadic functions.** `out_printf(const char *fmt, ...)` takes any number of arguments, like `printf` itself, using `va_list`. It formats into a small stack array and only uses `malloc` when the result doesn't fit.
- **Check errors once, at the end.** After the first failure, output stops and `out_finish` reports it. That keeps every call site simple: modes never check for errors themselves.

## 4. src/utf8.c: bits and bytes

- **Bit operations.** `b0 & 0xE0U` keeps the top three bits; `cp << 6` shifts left by 6 bits. UTF-8 is built entirely from these.
- **`char` vs `unsigned char`.** Whether plain `char` is signed is up to the platform. Bytes above 0x7F can come out negative, so code that looks at byte values converts to `unsigned char` first.
- **Fixed-width types.** `uint32_t` is exactly 32 bits on every platform; `size_t` is the type for sizes and lengths.
- **Compound literals.** `(utf8_char){.cp = cp, .len = need, .valid = true}` builds a struct value inline, naming each field.
- **Generated code and binary search.** Which characters count as invisible comes from Unicode's own data, not a hand-written list: `scripts/gen-unicode.py` writes `src/unicode_tables.h`, a sorted array of ranges, and `in_ranges()` binary-searches it. Each step halves the part left to search, so 700 ranges take about 10 comparisons. Note `lo + (hi - lo) / 2` rather than `(lo + hi) / 2`, which can overflow: a bug that sat in Java's standard library for nine years.
- **`static` in a header.** The tables are `static const` arrays in a header, so each `.c` file that includes it gets a private copy. That's fine for data only `utf8.c` uses, and it lets `tests/unit.c` check the tables are sorted.

## 5. src/mode_peek.c and src/mode_quote.c

- **`#define`** creates a macro, which is plain text substitution. `STYLE_ESCAPE` becomes `"\033[36m"` everywhere before the compiler runs. `"\033"` is the ESC byte written in octal.
- **Adjacent string literals join.** `EMPTY_SET " empty"` becomes one string at compile time.
- **`isatty()` and `getenv()`** come from POSIX and the C library; they decide whether to use color.
- **The `<ctype.h>` trap.** `mode_quote.c` checks character ranges by hand instead of using `isalnum()`, which depends on the locale and is undefined behavior for negative values.

## 6. The tests

- **tests/unit.c** tests the decoder and output layer directly. The `CHECK` macro uses `#cond` to turn the checked expression into a string and `__LINE__` to say where it failed.
- **tests/run.sh** runs each command and compares stdout byte for byte with `cmp`, plus the exit code.
- **tests/roundtrip.py** is a property test: random input, a property that must always hold (quoting then unquoting gives back the input), thousands of tries. It pastes `put.quote` output into real shells, including interactive ones, because a review found that macOS's bash 3.2 expands `!` in a place newer shells don't.
- **tests/io.py** sets up output conditions a shell can't easily create, like a non-blocking pipe with a slow reader.
- **tests/unicode.py** runs every Unicode code point through the real binary. With only 1.1 million of them, there's no need to sample: exhaustive testing is a few seconds.
- **tests/fuzz.c** is a *coverage-guided fuzzer*. libFuzzer watches which branches each input reaches and mutates the inputs that found new ones, so it digs into corners random testing misses. Each run checks properties like "`put.peek` never writes a control byte", and the strongest kind of property there is: *decode the output and get the input back*. `tests/decode.c` holds strict decoders for `put.peek`, `put.quote` and `put.json`. If two different inputs ever gave the same output, one of them would decode wrong. `make fuzz` runs it for a minute; it typically gets through a few hundred thousand inputs.
- **`make coverage`** shows which lines the tests never run. Open `build/cov/index.html` to see them highlighted. CI fails if line coverage drops below 95%.

## 7. Tools that catch C's sharp edges

C trusts you completely. Reading past the end of an array or using a value you never initialized is *undefined behavior*: the program might work, crash, or quietly do something wrong. These tools exist to catch that:

- **AddressSanitizer (`make sanitize`)** adds checks around every memory access and stops the program with a report on the first bad one.
- **UndefinedBehaviorSanitizer** (same target) catches things like signed integer overflow.
- **valgrind (`make valgrind`)** runs the unmodified program on a simulated CPU and reports bad memory use and leaks.
- **clang-tidy (`make tidy`)** and **GCC's analyzer (`make analyze`)** read the code without running it and follow paths through it looking for bugs. CodeQL does the same on GitHub.
- **Hardening flags** (in the Makefile) don't find bugs; they make a bug that slips through harder to exploit. Stack canaries detect a smashed stack, `_FORTIFY_SOURCE` swaps `memcpy` and friends for versions that check sizes, and a position-independent executable lets the OS load it at a random address. `make check-hardening` confirms they made it into the binary.

### Try it: watch AddressSanitizer catch a bug

1. In `src/utf8.c`, delete the three lines starting with `if (n < need) {`.
2. Run `make sanitize`.
3. The unit test `test_no_overread` hands the decoder a one-byte heap buffer. Without the length check, the decoder reads the second byte, and ASan stops with `heap-buffer-overflow`, the file and line of the bad read, and where the buffer was allocated.
4. Put the lines back.

Without the sanitizer, that same bug would usually pass the tests by luck.

## 8. Next steps to build

Small features that each exercise a different part of C:

- **Byte counts in `put.peek`**: show each argument's length in bytes. (Formatting with `out_printf`.)
- **`put.count`**: print the number of arguments. (Adding a row to the mode table, plus a test, a man page entry and a changelog line: CONTRIBUTING.md lists what a change needs.)
- **`put.scan`**: read stdin and show each line the way `put.peek` shows arguments. (Reading input with `getline()`, memory you allocate and must `free()`.)
