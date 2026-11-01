# put — print arguments exactly as given.
#
# Common targets:
#   make              build build/put and the put.<mode> symlinks
#   make test         build, then run unit tests, golden tests and round-trip fuzzing
#   make sanitize     rebuild with AddressSanitizer + UBSan and run every test
#   make valgrind     run the golden tests under valgrind (Linux)
#   make fmt          reformat the C sources with clang-format
#   make lint         format check + clang-tidy static analysis
#   make install      install to $(PREFIX) (default /usr/local); honours DESTDIR
#
# Pass WERROR=1 to turn every compiler warning into an error (CI does this).

VERSION := 0.1.0

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1

BUILD ?= build
MODES := raw lines nul peek quote json help

# Compiler settings. `?=` means "use this unless the caller already set it",
# so `make CC=clang` or `make CFLAGS=-O0` both work.
CFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef -Wcast-qual \
	-Wwrite-strings -Wvla -Wnull-dereference
ifeq ($(WERROR),1)
WARNINGS += -Werror
endif

# -std=c11 pins the language version. _POSIX_C_SOURCE unlocks POSIX functions
# like isatty() that plain C11 doesn't define.
ALL_CFLAGS := -std=c11 $(WARNINGS) $(CFLAGS)
ALL_CPPFLAGS := -Isrc -D_POSIX_C_SOURCE=200809L -DPUT_VERSION='"$(VERSION)"' $(CPPFLAGS)

SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=$(BUILD)/obj/%.o)
DEP := $(OBJ:.o=.d)
FORMAT_FILES := $(wildcard src/*.c src/*.h tests/*.c)
CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy

.PHONY: all test sanitize valgrind fmt fmt-check tidy lint install uninstall clean

all: $(BUILD)/put $(MODES:%=$(BUILD)/put.%)

# Link: combine the object files into one executable.
$(BUILD)/put: $(OBJ)
	$(CC) $(ALL_CFLAGS) $(LDFLAGS) -o $@ $^

# Compile: one .c file becomes one .o file. -MMD -MP also writes a .d file
# listing the headers it included, so editing a header rebuilds what uses it.
$(BUILD)/obj/%.o: src/%.c | $(BUILD)/obj
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<

# Every mode is the same binary under another name.
$(BUILD)/put.%: $(BUILD)/put
	ln -sf put $@

$(BUILD)/obj:
	mkdir -p $@

-include $(DEP)

# Unit tests link against the real UTF-8 code, not a copy of it.
$(BUILD)/unit: tests/unit.c $(BUILD)/obj/utf8.o
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) -o $@ $^

test: all $(BUILD)/unit
	$(BUILD)/unit
	tests/run.sh $(BUILD)
	python3 tests/roundtrip.py $(BUILD)

# Sanitizers add runtime checks for memory errors and undefined behaviour.
# A separate build directory keeps the instrumented objects apart.
SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
sanitize:
	$(MAKE) BUILD=$(BUILD)/san CFLAGS="-O1 -g $(SAN)" LDFLAGS="$(SAN)" test

valgrind: all
	PUT_RUNNER="valgrind -q --error-exitcode=99 --leak-check=full" tests/run.sh $(BUILD)

fmt:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

tidy:
	$(CLANG_TIDY) --quiet $(SRC) tests/unit.c -- -std=c11 $(ALL_CPPFLAGS)

lint: fmt-check tidy

install: all
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	install -m 755 $(BUILD)/put $(DESTDIR)$(BINDIR)/put
	for m in $(MODES); do ln -sf put $(DESTDIR)$(BINDIR)/put.$$m; done
	install -m 644 man/put.1 $(DESTDIR)$(MANDIR)/put.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/put $(MODES:%=$(DESTDIR)$(BINDIR)/put.%)
	rm -f $(DESTDIR)$(MANDIR)/put.1

clean:
	rm -rf $(BUILD)
