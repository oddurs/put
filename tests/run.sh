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

# --- put.quote --------------------------------------------------------------
expect 0 "plain 'two words' 'it'\\\\''s' \$'tab\\\\there' '' '=ls' a=b\n" \
    put.quote plain 'two words' "it's" $'tab\there' '' '=ls' 'a=b'
expect 0 '\n' put.quote
expect 0 "\$'\\\\xff'\n" put.quote $'\xff'
expect 0 "\$'nb\\\\xc2\\\\xa0sp'\n" put.quote $'nb\xc2\xa0sp'
expect 0 "\$'a\\\\\\\\b\\\\'c\\\\n'\n" put.quote $'a\\b\'c\n'
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
    # A reader that stops early should end put quietly via SIGPIPE (128+13).
    "$BIN/put.lines" $(seq 1 100000) | head -n 1 >/dev/null
    check "closed pipe ends put with SIGPIPE" [ "${PIPESTATUS[0]}" -eq 141 ]

    # Color needs a terminal; util-linux `script` provides a fake one.
    if script --version 2>/dev/null | grep -q util-linux; then
        script -qec "'$BIN/put.peek' 'x '" /dev/null >"$TMP/tty"
        check "peek colors markers on a terminal" grep -q $'\x1b\\[2m' "$TMP/tty"
        NO_COLOR=1 script -qec "'$BIN/put.peek' 'x '" /dev/null >"$TMP/tty"
        check "NO_COLOR turns color off" bash -c "! grep -q \$'\\x1b' '$TMP/tty'"
    fi
fi

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
