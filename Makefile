# put — print arguments exactly as given.
#
# Everyday targets:
#   make              build build/put and the put.<mode> symlinks
#   make test         unit tests, golden byte-for-byte tests, round-trip fuzzing
#   make check        test + lint: run this before pushing
#   make install      install to $(PREFIX) (default /usr/local); honours DESTDIR
#
# Deeper checks (CI runs all of these):
#   make sanitize     rebuild with AddressSanitizer + UBSan and run every test
#   make valgrind     golden tests under valgrind (Linux)
#   make analyze      GCC's static analyzer (-fanalyzer)
#   make coverage     line coverage report; fails under COVERAGE_MIN percent
#   make fuzz         coverage-guided fuzzing with libFuzzer for FUZZ_TIME seconds
#   make lint         clang-format check, clang-tidy, shellcheck, version check
#
# Releasing:
#   make dist         source tarball from git
#   make bindist      binary tarball of an existing build (see .github/workflows/release.yml)
#
# Options: WERROR=1 makes warnings errors (CI sets it). HARDEN=0 drops the
# hardening flags. STATIC=1 links a static binary (used for Linux releases).

VERSION := 0.1.0

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man/man1

BUILD ?= build
MODES := raw lines nul peek quote json help version
UNAME_S := $(shell uname -s)

# ---------------------------------------------------------------------------
# Compiler flags. `?=` means "use this unless the caller already set it", so
# `make CC=clang` or `make CFLAGS=-O0` both work.

CFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wundef -Wcast-qual \
	-Wwrite-strings -Wvla -Wnull-dereference
ifeq ($(WERROR),1)
WARNINGS += -Werror
endif

# Hardening: defences that make memory bugs harder to exploit if one ever
# slips through. Stack canaries, checked libc calls (FORTIFY), and on Linux a
# position-independent executable with a read-only relocation table.
HARDEN ?= 1
STATIC ?= 0
ifeq ($(HARDEN),1)
HARDEN_CFLAGS := -fstack-protector-strong
HARDEN_CPPFLAGS := -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
ifeq ($(UNAME_S),Linux)
HARDEN_CFLAGS += -fPIE -fstack-clash-protection
ifeq ($(STATIC),1)
HARDEN_LDFLAGS := -static-pie
else
HARDEN_LDFLAGS := -pie
endif
HARDEN_LDFLAGS += -Wl,-z,relro,-z,now,-z,noexecstack
endif
else ifeq ($(STATIC),1)
HARDEN_LDFLAGS := -static
endif

# -std=c11 pins the language version. _POSIX_C_SOURCE unlocks POSIX functions
# like isatty() and poll() that plain C11 doesn't define.
BASE_CPPFLAGS := -Isrc -D_POSIX_C_SOURCE=200809L -DPUT_VERSION='"$(VERSION)"'
ALL_CPPFLAGS := $(BASE_CPPFLAGS) $(HARDEN_CPPFLAGS) $(CPPFLAGS)
ALL_CFLAGS := -std=c11 $(WARNINGS) $(HARDEN_CFLAGS) $(CFLAGS)
ALL_LDFLAGS := $(HARDEN_LDFLAGS) $(LDFLAGS)

SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
OBJ := $(SRC:src/%.c=$(BUILD)/obj/%.o)
DEP := $(OBJ:.o=.d)

.PHONY: all test check sanitize valgrind analyze coverage fuzz fmt fmt-check tidy \
	shellcheck check-version check-hardening lint dist bindist install install-files uninstall clean print-version

# ---------------------------------------------------------------------------
# Build

all: $(BUILD)/put $(MODES:%=$(BUILD)/put.%)

# Link: combine the object files into one executable.
$(BUILD)/put: $(OBJ)
	$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

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

# ---------------------------------------------------------------------------
# Tests

# Unit tests link against the real UTF-8 code, not a copy of it.
$(BUILD)/unit: tests/unit.c $(BUILD)/obj/utf8.o $(BUILD)/obj/out.o
	$(CC) $(ALL_CPPFLAGS) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@ $^

test: all $(BUILD)/unit
	$(BUILD)/unit
	tests/run.sh $(BUILD)
	python3 tests/roundtrip.py $(BUILD)
	python3 tests/io.py $(BUILD)

check: test lint

# Sanitizers add runtime checks for memory errors and undefined behaviour.
# A separate build directory keeps the instrumented objects apart.
SAN := -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
sanitize:
	$(MAKE) BUILD=$(BUILD)/san HARDEN=0 CFLAGS="-O1 -g $(SAN)" LDFLAGS="$(SAN)" test

valgrind: all
	PUT_RUNNER="valgrind -q --error-exitcode=99 --leak-check=full" tests/run.sh $(BUILD)

# GCC's analyzer follows paths through the code looking for bugs such as
# leaks, double frees and NULL dereferences. It needs gcc, not clang.
analyze:
	$(MAKE) BUILD=$(BUILD)/analyze CC=gcc WERROR=1 CFLAGS="-O2 -fanalyzer" all

COVERAGE_MIN ?= 95
GCOV ?= gcov
coverage:
	rm -rf $(BUILD)/cov
	$(MAKE) BUILD=$(BUILD)/cov HARDEN=0 CFLAGS="-O0 -g --coverage" LDFLAGS="--coverage" test
	gcovr --gcov-executable $(GCOV) --root . --filter src/ --object-directory $(BUILD)/cov/obj \
		--print-summary --txt $(BUILD)/cov/coverage.txt --html-details $(BUILD)/cov/index.html \
		--fail-under-line $(COVERAGE_MIN)

# libFuzzer generates millions of inputs, keeps the ones that reach new code,
# and checks invariants in tests/fuzz.c. Found crashes are saved as crash-*.
FUZZ_TIME ?= 60
FUZZ_CC ?= clang
FUZZ_SRC := $(filter-out src/main.c,$(SRC)) tests/fuzz.c
$(BUILD)/fuzzer: $(FUZZ_SRC) $(HDR)
	mkdir -p $(BUILD)
	$(FUZZ_CC) $(BASE_CPPFLAGS) -std=c11 $(WARNINGS) -g -O1 \
		-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o $@ $(FUZZ_SRC)

fuzz: $(BUILD)/fuzzer
	mkdir -p $(BUILD)/corpus
	$(BUILD)/fuzzer -max_total_time=$(FUZZ_TIME) -max_len=4096 -print_final_stats=1 \
		$(BUILD)/corpus tests/corpus

# ---------------------------------------------------------------------------
# Lint

CLANG_FORMAT ?= clang-format
CLANG_TIDY ?= clang-tidy
FORMAT_FILES := $(wildcard src/*.c src/*.h tests/*.c)
SHELL_FILES := tests/run.sh $(wildcard scripts/*.sh)

fmt:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

fmt-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

tidy:
	$(CLANG_TIDY) --quiet $(SRC) tests/unit.c tests/fuzz.c -- -std=c11 $(BASE_CPPFLAGS)

shellcheck:
	shellcheck $(SHELL_FILES)

# The version lives here, in the man page and in the changelog; keep them equal.
check-version:
	@grep -q '"put $(VERSION)"' man/put.1 || { echo "man/put.1: .TH should say put $(VERSION)"; exit 1; }
	@grep -q '^## \[$(VERSION)\]' CHANGELOG.md || { echo "CHANGELOG.md: no section for $(VERSION)"; exit 1; }
	@echo "version $(VERSION) is consistent"

lint: fmt-check tidy shellcheck check-version

print-version:
	@echo $(VERSION)

# Linux only: confirm the hardening flags made it into the binary.
check-hardening: all
	scripts/check-hardening.sh $(BUILD)/put

# ---------------------------------------------------------------------------
# Release

dist:
	mkdir -p $(BUILD)
	git archive --format=tar.gz --prefix=put-$(VERSION)/ -o $(BUILD)/put-$(VERSION).tar.gz HEAD

# Packages an existing $(BUILD)/put; it deliberately doesn't rebuild, so a
# binary made some other way (say, a macOS universal binary from lipo) can be
# packaged too.
TARGET ?= $(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')
STRIP ?= strip
DISTNAME := put-$(VERSION)-$(TARGET)
bindist:
	@test -x $(BUILD)/put || { echo "no $(BUILD)/put: build first"; exit 1; }
	rm -rf $(BUILD)/dist/$(DISTNAME)
	$(MAKE) --no-print-directory install-files DESTDIR=$(BUILD)/dist/$(DISTNAME) PREFIX=
	$(STRIP) $(BUILD)/dist/$(DISTNAME)/bin/put
	cp README.md LICENSE CHANGELOG.md $(BUILD)/dist/$(DISTNAME)/
	tar -C $(BUILD)/dist -czf $(BUILD)/dist/$(DISTNAME).tar.gz $(DISTNAME)
	@echo "wrote $(BUILD)/dist/$(DISTNAME).tar.gz"

# ---------------------------------------------------------------------------
# Install

install: all install-files

install-files:
	install -d $(DESTDIR)$(BINDIR) $(DESTDIR)$(MANDIR)
	install -m 755 $(BUILD)/put $(DESTDIR)$(BINDIR)/put
	for m in $(MODES); do ln -sf put $(DESTDIR)$(BINDIR)/put.$$m; done
	install -m 644 man/put.1 $(DESTDIR)$(MANDIR)/put.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/put $(MODES:%=$(DESTDIR)$(BINDIR)/put.%)
	rm -f $(DESTDIR)$(MANDIR)/put.1

clean:
	rm -rf $(BUILD)
