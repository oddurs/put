#!/usr/bin/env python3
"""Round-trip fuzzing for put.quote, put.json and put.nul.

Generates random byte-string arguments (biased towards the characters that
cause trouble) and checks:

  put.quote  pasting its output back into bash and zsh recreates the exact
             arguments
  put.json   the output parses as JSON to the same strings, or the command
             exits 3 if any argument isn't valid UTF-8
  put.nul    splitting the output on NUL gives back the arguments

Usage: python3 tests/roundtrip.py BUILD_DIR [ITERATIONS]
"""

import json
import os
import random
import shutil
import subprocess
import sys

TRICKY = [
    b" ", b"'", b'"', b"\\", b"$", b"`", b"!", b"*", b"?", b"~", b"#", b"=", b"%",
    b"\n", b"\t", b"\r", b"\x1b", b"\x01", b"\x7f", b"-n", b"--", b"{", b"}",
    b"\xc3\xa9", b"\xf0\x9f\x99\x82", b"\xc2\xa0", b"\xe2\x80\x8b", b"\xe2\x80\xae",
    b"\xff", b"\xc3", b"\xc0\xaf", b"\xed\xa0\x80",
]


def random_arg(rng: random.Random) -> bytes:
    parts = []
    for _ in range(rng.randint(0, 6)):
        if rng.random() < 0.5:
            parts.append(rng.choice(TRICKY))
        else:
            # Any byte except NUL, which can't appear in an argument.
            parts.append(bytes([rng.randint(1, 255)]))
    return b"".join(parts)


def run(binary: str, args: list) -> subprocess.CompletedProcess:
    return subprocess.run([binary, *args], capture_output=True, check=False)


def main() -> int:
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    iterations = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    rng = random.Random(int(os.environ.get("PUT_SEED", "1")))

    shells = [s for s in ("bash", "zsh") if shutil.which(s)]
    failures = 0

    def fail(what: str, args: list, detail: object) -> None:
        nonlocal failures
        failures += 1
        if failures <= 10:
            print(f"FAIL {what}: args={args!r}\n  {detail!r}")

    for _ in range(iterations):
        args = [random_arg(rng) for _ in range(rng.randint(1, 4))]

        # put.quote -> shell -> printf '%s\0' must give back the same bytes.
        result = run(os.path.join(build, "put.quote"), args)
        if result.returncode != 0:
            fail("put.quote exit", args, result.stderr)
            continue
        quoted = result.stdout.removesuffix(b"\n")
        for shell in shells:
            flags = ["-f"] if shell == "zsh" else ["--norc", "--noprofile"]
            script = b"printf '%s\\0' " + quoted
            back = subprocess.run([shell, *flags, "-c", script], capture_output=True, check=False)
            got = back.stdout.split(b"\0")[:-1]
            if back.returncode != 0 or got != args:
                fail(f"put.quote via {shell}", args, (quoted, got, back.stderr))

        # put.json: valid UTF-8 parses back exactly; anything else exits 3.
        result = run(os.path.join(build, "put.json"), args)
        try:
            expected = [a.decode("utf-8") for a in args]
        except UnicodeDecodeError:
            expected = None
        if expected is None:
            if result.returncode != 3 or result.stdout:
                fail("put.json invalid input", args, result)
        elif result.returncode != 0 or json.loads(result.stdout) != expected:
            fail("put.json", args, result.stdout)

        # put.nul: split on NUL.
        result = run(os.path.join(build, "put.nul"), args)
        if result.returncode != 0 or result.stdout.split(b"\0")[:-1] != args:
            fail("put.nul", args, result.stdout)

    print(f"roundtrip: {iterations} cases, shells: {', '.join(shells) or 'none'}, "
          f"{failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
