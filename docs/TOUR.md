# A tour of put, for people new to C

This codebase is small (about 800 lines of C, a good share of it comments) and written to be read. This guide gives a reading order and points out the C ideas you'll meet in each file. Every source file also has `C note:` comments at the places where something C-specific is happening.

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

- **`static` at file level** makes a variable or function private to its file. That's C's only encapsulation.
- **stdio buffering.** `fwrite` copies into a buffer; the operating system only sees the bytes when the buffer fills or is flushed. A write can succeed now and fail at `fclose` time, which is why put checks the result at the very end.
- **`errno`** is a global set by library calls when they fail. `strerror(errno)` turns it into a message like "No space left on device". Try `build/put hi > /dev/full` on Linux.
- **Variadic functions.** `out_printf(const char *fmt, ...)` takes any number of arguments, like `printf` itself, using `va_list`.

## 4. src/utf8.c: bits and bytes

- **Bit operations.** `b0 & 0xE0U` keeps the top three bits; `cp << 6` shifts left by 6 bits. UTF-8 is built entirely from these.
- **`char` vs `unsigned char`.** Whether plain `char` is signed is up to the platform. Bytes above 0x7F can come out negative, so code that looks at byte values converts to `unsigned char` first.
- **Fixed-width types.** `uint32_t` is exactly 32 bits on every platform; `size_t` is the type for sizes and lengths.
- **Compound literals.** `(utf8_char){.cp = cp, .len = need, .valid = true}` builds a struct value inline, naming each field.

## 5. src/mode_peek.c and src/mode_quote.c

- **`#define`** creates a macro, which is plain text substitution. `STYLE_ESCAPE` becomes `"\033[36m"` everywhere before the compiler runs. `"\033"` is the ESC byte written in octal.
- **Adjacent string literals join.** `EMPTY_SET " empty"` becomes one string at compile time.
- **`isatty()` and `getenv()`** come from POSIX and the C library; they decide whether to use color.
- **The `<ctype.h>` trap.** `mode_quote.c` checks character ranges by hand instead of using `isalnum()`, which depends on the locale and is undefined behavior for negative values.

## 6. The tests

- **tests/unit.c** tests the decoder directly. The `CHECK` macro uses `#cond` to turn the checked expression into a string and `__LINE__` to say where it failed.
- **tests/run.sh** runs each command and compares stdout byte for byte with `cmp`, plus the exit code.
- **tests/roundtrip.py** is a property test: random input, a property that must always hold (quoting then unquoting gives back the input), thousands of tries.

## 7. Tools that catch C's sharp edges

C trusts you completely. Reading past the end of an array or using a value you never initialized is *undefined behavior*: the program might work, crash, or quietly do something wrong. These tools exist to catch that:

- **AddressSanitizer (`make sanitize`)** adds checks around every memory access and stops the program with a report on the first bad one.
- **UndefinedBehaviorSanitizer** (same target) catches things like signed integer overflow.
- **valgrind (`make valgrind`)** runs the unmodified program on a simulated CPU and reports bad memory use and leaks.
- **clang-tidy (`make tidy`)** reads the code without running it and flags bug patterns.

### Try it: watch AddressSanitizer catch a bug

1. In `src/utf8.c`, delete the three lines starting with `if (n < need) {`.
2. Run `make sanitize`.
3. The unit test `test_no_overread` hands the decoder a one-byte heap buffer. Without the length check, the decoder reads the second byte, and ASan stops with `heap-buffer-overflow`, the file and line of the bad read, and where the buffer was allocated.
4. Put the lines back.

Without the sanitizer, that same bug would usually pass the tests by luck.

## 8. Next steps to build

Small features that each exercise a different part of C:

- **Byte counts in `put.peek`**: show each argument's length in bytes. (Formatting with `out_printf`.)
- **`put.count`**: print the number of arguments. (Adding a row to the mode table, plus a test.)
- **`put.scan`**: read stdin and show each line the way `put.peek` shows arguments. (Reading input with `getline()`, memory you allocate and must `free()`.)
