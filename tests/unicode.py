#!/usr/bin/env python3
"""Run every Unicode code point through put.peek and put.quote.

For each code point from U+0080 to U+10FFFF (surrogates excluded), checks
the real binaries against Unicode data computed here, independently of the
generated table in src/unicode_tables.h:

  put.peek  escapes it as \\u{...} exactly when it should: always for
            invisible code points, and for combining marks only when there
            is no character before them to attach to
  put.quote uses $'...' with byte escapes for exactly the same cases

Needs the `regex` package at the version in tests/requirements.txt. Without
it the test is skipped, unless PUT_STRICT=1 is set (CI sets it), in which
case it fails.

Usage: python3 tests/unicode.py BUILD_DIR
"""

import os
import subprocess
import sys

try:
    import regex
except ImportError:
    regex = None

BATCH = 20000

# The same definition as scripts/gen-unicode.py, stated again on purpose: the
# point of this test is to check the binary, not to trust the generated table.
INVISIBLE = r"[\p{Cc}\p{Cf}\p{Cn}\p{Co}\p{Zs}\p{Zl}\p{Zp}\p{Default_Ignorable_Code_Point}⠀\U0001D159]"
COMBINING = r"\p{M}"
MARKER_LOOKALIKES = {0xB7, 0x2205}  # · and ∅ are escaped in plain output


def code_points():
    return [cp for cp in range(0x80, 0x110000) if not 0xD800 <= cp <= 0xDFFF]


def peek_escape(cp: int) -> bytes:
    return b"\\u{%x}" % cp


def quote_bytes(text: str) -> bytes:
    return b"".join(b"\\x%02x" % b for b in text.encode())


def main() -> int:
    if regex is None:
        if os.environ.get("PUT_STRICT") == "1":
            print("unicode: the regex package is required (PUT_STRICT=1)")
            return 1
        print("unicode: skipped (pip install regex to run it)")
        return 0

    build = sys.argv[1] if len(sys.argv) > 1 else "build"

    # The binary's tables come from one Unicode version; checking it against
    # another would report every newly assigned character as a failure.
    pinned = open(os.path.join(os.path.dirname(__file__), "requirements.txt")).read()
    if f"regex=={regex.__version__}" not in pinned:
        msg = (f"unicode: regex {regex.__version__} installed, tests/requirements.txt pins "
               f"another version")
        if os.environ.get("PUT_STRICT") == "1":
            print(msg + " (PUT_STRICT=1)")
            return 1
        print(msg + "; skipped (pip install -r tests/requirements.txt)")
        return 0

    invisible = regex.compile(INVISIBLE)
    combining = regex.compile(COMBINING)
    cps = code_points()
    failures = []

    for start in range(0, len(cps), BATCH):
        batch = cps[start:start + BATCH]
        chars = [chr(cp) for cp in batch]
        is_inv = [invisible.match(ch) is not None for ch in chars]
        is_comb = [combining.match(ch) is not None for ch in chars]

        # put.peek, with a base character before ("a" + ch) and alone.
        for with_base in (True, False):
            args = [("a" + ch if with_base else ch).encode() for ch in chars]
            out = subprocess.run([os.path.join(build, "put.peek"), *args],
                                 capture_output=True, check=True).stdout
            lines = out.split(b"\n")[1:-1]
            width = len(str(len(args) - 1))
            for cp, ch, inv, comb, line in zip(batch, chars, is_inv, is_comb, lines):
                escaped = inv or cp in MARKER_LOOKALIKES or (comb and not with_base)
                body = peek_escape(cp) if escaped else ch.encode()
                want = (b"a" if with_base else b"") + body
                got = line[width + 2:]
                if got != want:
                    failures.append(f"put.peek U+{cp:04X} {'after a' if with_base else 'alone'}: "
                                    f"got {got!r}, want {want!r}")

        # put.quote, one argument per call would be slow; quote them all at
        # once and split on the single spaces between words.
        for with_base in (True, False):
            args = [("a" + ch if with_base else ch).encode() for ch in chars]
            out = subprocess.run([os.path.join(build, "put.quote"), *args],
                                 capture_output=True, check=True).stdout
            words = out.removesuffix(b"\n").split(b" ")
            if len(words) != len(args):
                failures.append(f"put.quote batch at U+{batch[0]:04X}: {len(words)} words "
                                f"for {len(args)} args (a quoted word contains a space?)")
                continue
            for cp, ch, inv, comb, word in zip(batch, chars, is_inv, is_comb, words):
                dollar = inv or (comb and not with_base)
                if dollar:
                    want = b"$'" + (b"a" if with_base else b"") + quote_bytes(ch) + b"'"
                else:
                    want = b"'" + (b"a" if with_base else b"") + ch.encode() + b"'"
                if word != want:
                    failures.append(f"put.quote U+{cp:04X} {'after a' if with_base else 'alone'}: "
                                    f"got {word!r}, want {want!r}")

    for f in failures[:20]:
        print("FAIL:", f)
    print(f"unicode: {len(cps)} code points x 4 checks, {len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
