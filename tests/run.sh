#!/usr/bin/env bash
#
# Golden tests: run a put command and compare its stdout byte for byte, plus
# its exit code, against what we expect.
#
#   tests/run.sh BUILD_DIR
#
# Set PUT_RUNNER to run every command under a wrapper, e.g.
#   PUT_RUNNER="valgrind -q --error-exitcode=99" tests/run.sh build
#
# Expected output is written as a printf %b string, so \n, \t, \0, \xHH and
# \\ are escapes. Arguments use bash's $'...' quoting with byte escapes
# (\xHH) so the tests don't depend on the locale.

set -u

BIN=$(cd "${1:?usage: tests/run.sh BUILD_DIR}" && pwd)
RUNNER=${PUT_RUNNER:-}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
unset NO_COLOR

pass=0
fail=0

# expect STATUS EXPECTED COMMAND [ARGS...]
expect() {
    local want_status=$1 want=$2 cmd=$3
    shift 3
    printf '%b' "$want" >"$TMP/want"
    # $RUNNER is deliberately unquoted so it can hold a command plus flags.
    # shellcheck disable=SC2086
    $RUNNER "$BIN/$cmd" "$@" >"$TMP/got" 2>"$TMP/err"
    local status=$?
    if [ "$status" -eq "$want_status" ] && cmp -s "$TMP/want" "$TMP/got"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %s' "$cmd"
        printf ' %q' "$@"
        printf '\n  exit %s (want %s)\n  want: ' "$status" "$want_status"
        od -c "$TMP/want" | sed -n '1,4p' | tr '\n' ' '
        printf '\n  got:  '
        od -c "$TMP/got" | sed -n '1,4p' | tr '\n' ' '
        printf '\n'
        sed 's/^/  stderr: /' "$TMP/err"
    fi
}

# check NAME CONDITION... : for tests that aren't a single comparison.
check() {
    local name=$1
    shift
    if "$@"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %s\n' "$name"
    fi
}

# --- put: arguments are data, always ---------------------------------------
expect 0 'a b\n' put a b
expect 0 '\n' put
expect 0 '-n\n' put -n
expect 0 '-e a\\nb\n' put -e 'a\nb'
expect 0 '-- x\n' put -- x
expect 0 '--help\n' put --help
expect 0 ' \n' put '' ''
expect 0 '\xff\xfe\n' put $'\xff\xfe'
expect 0 '\x1b[31mred\x1b[0m\n' put $'\x1b[31mred\x1b[0m'
expect 0 'two\nlines\n' put $'two\nlines'

# --- put.raw ----------------------------------------------------------------
expect 0 'a b' put.raw a b
expect 0 '' put.raw
expect 0 '-n' put.raw -n

# --- put.lines --------------------------------------------------------------
expect 0 'a\nb c\n' put.lines a 'b c'
expect 0 '' put.lines
expect 0 '\n' put.lines ''

# --- put.nul ----------------------------------------------------------------
expect 0 'a\0b\0' put.nul a b
expect 0 '\0' put.nul ''
expect 0 '' put.nul

# --- put.peek ---------------------------------------------------------------
expect 0 '0 args\n' put.peek
expect 0 '1 arg\n0  x\n' put.peek x
expect 0 '6 args\n0  hello\n1  two  words\xc2\xb7\n2  crlf\\r\n3  \xe2\x88\x85 empty\n4  tab\\there\n5  nb\\u{a0}sp\n' \
    put.peek hello 'two  words ' $'crlf\r' '' $'tab\there' $'nb\xc2\xa0sp'
expect 0 '2 args\n0  \xc2\xb7\xc2\xb7a\n1  \xc2\xb7\xc2\xb7\xc2\xb7\n' put.peek '  a' '   '
expect 0 '1 arg\n0  a\\\\b\n' put.peek 'a\b'
expect 0 '1 arg\n0  \\e[0m\\x07\\x7f\\n\n' put.peek $'\x1b[0m\x07\x7f\n'
expect 0 '1 arg\n0  \\xff\\xc3\n' put.peek $'\xff\xc3'
expect 0 '1 arg\n0  \\xc0\\xaf\n' put.peek $'\xc0\xaf'
expect 0 '1 arg\n0  \\xed\\xa0\\x80\n' put.peek $'\xed\xa0\x80'
expect 0 '1 arg\n0  a\\u{202e}b\n' put.peek $'a\xe2\x80\xaeb'
expect 0 '1 arg\n0  \\u{200b}\\u{feff}\n' put.peek $'\xe2\x80\x8b\xef\xbb\xbf'
expect 0 '1 arg\n0  \\u{e0041}\n' put.peek $'\xf3\xa0\x81\x81'
expect 0 '1 arg\n0  \\u{b7}\\u{2205}\n' put.peek $'\xc2\xb7\xe2\x88\x85'
expect 0 '1 arg\n0  h\xc3\xa9llo \xf0\x9f\x99\x82\n' put.peek $'h\xc3\xa9llo \xf0\x9f\x99\x82'
expect 0 '11 args\n 0  a\n 1  b\n 2  c\n 3  d\n 4  e\n 5  f\n 6  g\n 7  h\n 8  i\n 9  j\n10  k\n' \
    put.peek a b c d e f g h i j k
# Variation selector (emoji style), ZWJ, braille blank, private use, unassigned.
expect 0 '1 arg\n0  \xe2\x9d\xa4\\u{fe0f}\n' put.peek $'\xe2\x9d\xa4\xef\xb8\x8f'
expect 0 '1 arg\n0  \xf0\x9f\x91\xa8\\u{200d}\xf0\x9f\x91\xa9\n' put.peek $'\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9'
expect 0 '3 args\n0  \\u{2800}\n1  \\u{e000}\n2  \\u{378}\n' put.peek $'\xe2\xa0\x80' $'\xee\x80\x80' $'\xcd\xb8'
# A combining mark shows as itself after a letter, and is escaped with nothing
# to attach to: at the start, or after a space marker or an escape.
expect 0 '4 args\n0  e\xcc\x81\n1  \\u{301}x\n2  \xc2\xb7\\u{301}\n3  \\t\\u{301}\n' \
    put.peek $'e\xcc\x81' $'\xcc\x81x' $' \xcc\x81' $'\t\xcc\x81'

# --- put.quote --------------------------------------------------------------
expect 0 "plain 'two words' 'it'\\\\''s' \$'tab\\\\there' '' '=ls' 'a=b'\n" \
    put.quote plain 'two words' "it's" $'tab\there' '' '=ls' 'a=b'
expect 0 '\n' put.quote
expect 0 "\$'\\\\xff'\n" put.quote $'\xff'
expect 0 "\$'nb\\\\xc2\\\\xa0sp'\n" put.quote $'nb\xc2\xa0sp'
# A ' inside $'...' is \x27, never \' (bash 3.2 history expansion).
expect 0 "\$'a\\\\\\\\b\\\\x27c\\\\n'\n" put.quote $'a\\b\'c\n'
expect 0 "\$'x\\\\x27!echo\\\\x01'\n" put.quote $'x\'!echo\x01'
# Any = is quoted (zsh's MAGIC_EQUAL_SUBST expands a==cmd and a:=cmd).
expect 0 "'a==ls' 'b:=ls' -o 'k=v'\n" put.quote 'a==ls' 'b:=ls' -o 'k=v'
expect 0 "'hi!' 'a!b'\n" put.quote 'hi!' 'a!b'
# A combining mark that would draw on a quote mark or an escape is escaped.
expect 0 "'e\xcc\x81' \$'\\\\xcc\\\\x81x' \$'it\\\\x27\\\\xcc\\\\x81s'\n" \
    put.quote $'e\xcc\x81' $'\xcc\x81x' $'it\'\xcc\x81s'
expect 0 "\$'\\\\xef\\\\xb8\\\\x8f'\n" put.quote $'\xef\xb8\x8f'
# shellcheck disable=SC2016 # the literal text $HOME is the point
expect 0 "'~home' '#x' '*' '\$HOME' 'a b'\n" put.quote '~home' '#x' '*' '$HOME' 'a b'
expect 0 "'h\xc3\xa9llo'\n" put.quote $'h\xc3\xa9llo'

# --- put.json ---------------------------------------------------------------
expect 0 '[]\n' put.json
expect 0 '["a","b c","say \\"hi\\"","back\\\\slash","nl\\n\\u0001"]\n' \
    put.json a 'b c' 'say "hi"' 'back\slash' $'nl\n\x01'
expect 0 '["\\u2028\\u2029","h\xc3\xa9"]\n' put.json $'\xe2\x80\xa8\xe2\x80\xa9' $'h\xc3\xa9'
expect 3 '' put.json ok $'\xff'

# --- put.help ---------------------------------------------------------------
"$BIN/put.help" >"$TMP/help" 2>&1
check "put.help exits 0" [ $? -eq 0 ]
check "put.help lists put.peek" grep -q 'put.peek' "$TMP/help"

# --- put.version ------------------------------------------------------------
VERSION=$(sed -n 's/^VERSION := //p' "$(dirname "$0")/../Makefile")
UNICODE=$(sed -n 's/^#define PUT_UNICODE_VERSION "\(.*\)"$/\1/p' "$(dirname "$0")/../src/unicode_tables.h")
expect 0 "put $VERSION (Unicode $UNICODE)\n" put.version
expect 0 "put $VERSION (Unicode $UNICODE)\n" put.version --help

# --- errors -----------------------------------------------------------------
ln -s "$BIN/put" "$TMP/put.bogus"
ln -s "$BIN/put" "$TMP/echo2"
$RUNNER "$TMP/put.bogus" x >"$TMP/out" 2>/dev/null
check "unknown mode exits 2" [ $? -eq 2 ]
check "unknown mode prints nothing" [ ! -s "$TMP/out" ]
$RUNNER "$TMP/echo2" x >/dev/null 2>&1
check "unrelated name exits 2" [ $? -eq 2 ]

if [ -w /dev/full ]; then
    $RUNNER "$BIN/put" hi >/dev/full 2>"$TMP/err"
    check "write error exits 1" [ $? -eq 1 ]
    check "write error is reported" grep -q 'write error' "$TMP/err"
fi

if [ -z "$RUNNER" ]; then
    # stdout closed by the caller: writing fails, and put says why.
    "$BIN/put" hi >&- 2>"$TMP/err"
    check "closed stdout exits 1" [ $? -eq 1 ]
    check "closed stdout is reported" grep -q 'Bad file descriptor' "$TMP/err"
    # Nothing to write, so a closed stdout is not an error.
    "$BIN/put.raw" >&- 2>"$TMP/err"
    check "closed stdout with no output exits 0" [ $? -eq 0 ]

    # SIGPIPE behavior is tested in tests/io.py, where the pipe's read end can
    # be closed before put starts instead of racing a reader.

    # Color needs a terminal; util-linux `script` provides a fake one. TERM
    # is set explicitly so the result doesn't depend on the caller's terminal.
    if script --version 2>/dev/null | grep -q util-linux; then
        export TERM=xterm-256color
        script -qec "'$BIN/put.peek' 'x '" /dev/null >"$TMP/tty"
        check "peek colors markers on a terminal" grep -q $'\x1b\\[2m' "$TMP/tty"
        NO_COLOR='' script -qec "'$BIN/put.peek' 'x '" /dev/null >"$TMP/tty"
        check "empty NO_COLOR keeps color" grep -q $'\x1b\\[2m' "$TMP/tty"
        NO_COLOR=1 script -qec "'$BIN/put.peek' 'x '" /dev/null >"$TMP/tty"
        check "NO_COLOR turns color off" bash -c "! grep -q \$'\\x1b' '$TMP/tty'"
        TERM=dumb script -qec "'$BIN/put.peek' 'x '" /dev/null >"$TMP/tty"
        check "TERM=dumb turns color off" bash -c "! grep -q \$'\\x1b' '$TMP/tty'"

        # A control byte and a backslash, styled: the escape is colored and
        # the literal backslash is not doubled.
        cat >"$TMP/styled.sh" <<EOF
#!/bin/sh
exec "$BIN/put.peek" "\$(printf 'a\\001b\\\\c')"
EOF
        chmod +x "$TMP/styled.sh"
        script -qec "$TMP/styled.sh" /dev/null >"$TMP/tty"
        check "styled escapes are colored" grep -qF $'\x1b[36m\\x01\x1b[0mb\\c' "$TMP/tty"
    fi
fi

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
