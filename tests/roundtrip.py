#!/usr/bin/env python3
"""Round-trip fuzzing for put.quote, put.json and put.nul.

Generates random byte-string arguments (biased towards the characters that
cause trouble) and checks:

  put.quote  pasting its output into a shell recreates the exact arguments:
             bash and zsh, non-interactive and interactive (where `!` means
             history expansion), zsh with each option that changes how
             words expand, and macOS's bash 3.2 at /bin/bash when present
  put.json   the output parses as JSON to the same strings, or the command
             exits 3 if any argument isn't valid UTF-8
  put.nul    splitting the output on NUL gives back the arguments

Usage: python3 tests/roundtrip.py BUILD_DIR [ITERATIONS]

Set PUT_STRICT=1 to fail instead of skipping when zsh is missing (CI does).
Set PUT_EXTRA_BASH=/path/to/bash to test one more bash build as well.
"""

import json
import os
import random
import shutil
import subprocess
import sys
import tempfile

TRICKY = [
    b" ", b"'", b'"', b"\\", b"$", b"`", b"!", b"!!", b"!-1", b"^", b"*", b"?", b"~", b"#",
    b"=", b"=ls", b"==ls", b":=ls", b"ls", b"%", b"!ls", b"'!!", b"\\'",
    b"\n", b"\t", b"\r", b"\x1b", b"\x01", b"\x7f", b"-n", b"--",
    b"{", b"}", b"[", b"(", b")", b"<", b">", b"|", b"&", b";",
    b"\xc3\xa9", b"\xf0\x9f\x99\x82", b"\xc2\xa0", b"\xe2\x80\x8b", b"\xe2\x80\xae",
    b"\xef\xb8\x8f", b"\xcc\x81", b"\xff", b"\xc3", b"\xc0\xaf", b"\xed\xa0\x80",
]

# zsh options that change how an unquoted or quoted word expands.
ZSH_OPTIONS = [
    "extendedglob", "kshglob", "magicequalsubst", "rcquotes", "globsubst",
    "shwordsplit", "cshjunkiequotes", "braceccl", "nomatch", "interactivecomments",
]

END = b"__put_case_end__"  # can't be produced by random_arg


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


def shells() -> list:
    """(label, argv) for each shell to test; -c or -i is appended later."""
    found = []
    if shutil.which("bash"):
        found.append(("bash", ["bash", "--norc", "--noprofile"]))
    # macOS ships bash 3.2 as /bin/bash; it quotes differently from bash 5.
    # PUT_EXTRA_BASH names one more bash binary to test, e.g. a local 3.2.
    extra = [b for b in ("/bin/bash", os.environ.get("PUT_EXTRA_BASH")) if b]
    for path in extra:
        if os.path.exists(path) and os.path.realpath(path) != os.path.realpath(
                shutil.which("bash") or ""):
            found.append((path, [path, "--norc", "--noprofile"]))
    if shutil.which("zsh"):
        found.append(("zsh", ["zsh", "-f"]))
        with_options = ["zsh", "-f"]
        for opt in ZSH_OPTIONS:
            with_options += ["-o", opt]
        found.append(("zsh+options", with_options))
    return found


def decode_cases(stdout: bytes) -> list:
    """Split interactive output (NUL-terminated args, END between cases)."""
    cases, current = [], []
    for item in stdout.split(b"\0")[:-1]:
        if item.endswith(END):
            # Interactive shells may echo a history-expanded line to stdout;
            # anything before the marker on the same item belongs to it.
            cases.append(current)
            current = []
        else:
            current.append(item)
    return cases


def main() -> int:
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    iterations = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    rng = random.Random(int(os.environ.get("PUT_SEED", "1")))
    strict = os.environ.get("PUT_STRICT") == "1"

    tested = shells()
    labels = [label for label, _ in tested]
    if "bash" not in labels:
        print("roundtrip: bash is required")
        return 1
    if "zsh" not in labels:
        print("roundtrip: zsh not found" + (", failing (PUT_STRICT=1)" if strict else ", skipping it"))
        if strict:
            return 1

    failures = 0

    def fail(what: str, args: list, detail: object) -> None:
        nonlocal failures
        failures += 1
        if failures <= 10:
            print(f"FAIL {what}: args={args!r}\n  {detail!r}"[:2000])

    all_cases = []
    for _ in range(iterations):
        args = [random_arg(rng) for _ in range(rng.randint(1, 4))]
        all_cases.append(args)

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

    # put.quote: quote every case once.
    quoted = []
    for args in all_cases:
        result = run(os.path.join(build, "put.quote"), args)
        if result.returncode != 0 or result.stdout.count(b"\n") != 1:
            fail("put.quote exit", args, result)
            quoted.append(None)
        else:
            quoted.append(result.stdout.removesuffix(b"\n"))

    # Shells run in an empty scratch directory: if put.quote ever got quoting
    # wrong, a stray > or glob would act there, not in the source tree.
    scratch_dir = tempfile.TemporaryDirectory(prefix="put-roundtrip-")
    scratch = scratch_dir.name

    for label, argv in tested:
        # Non-interactive: one shell per case, so a failure is isolated.
        for args, q in zip(all_cases, quoted):
            if q is None:
                continue
            back = subprocess.run([*argv, "-c", b"printf '%s\\0' " + q],
                                  capture_output=True, check=False, cwd=scratch)
            got = back.stdout.split(b"\0")[:-1]
            if back.returncode != 0 or got != args:
                fail(f"put.quote via {label} -c", args, (q, got, back.stderr))

        # Interactive: all cases in one session, typed on stdin. A command
        # that ran earlier gives history expansion something to expand.
        script = b"echo history-bait\n"
        for q in quoted:
            if q is not None:
                script += b"printf '%s\\0' " + q + b" " + END + b"\n"
        script += b"exit\n"
        back = subprocess.run([*argv, "-i"], input=script, capture_output=True, check=False,
                              cwd=scratch,
                              env={**os.environ, "HISTFILE": os.devnull, "PS1": "", "PS2": ""})
        out = back.stdout.split(b"\n", 1)[1] if back.stdout.startswith(b"history-bait\n") else back.stdout
        got_cases = decode_cases(out)
        want_cases = [args for args, q in zip(all_cases, quoted) if q is not None]
        if len(got_cases) != len(want_cases):
            fail(f"put.quote via {label} -i: case count", [], (len(got_cases), len(want_cases),
                                                              back.stderr[-500:]))
        else:
            for want, got in zip(want_cases, got_cases):
                if want != got:
                    fail(f"put.quote via {label} -i", want, got)

    leftovers = os.listdir(scratch)
    if leftovers:
        fail("shells created files (quoting let a redirect through)", [], leftovers[:5])
    scratch_dir.cleanup()

    print(f"roundtrip: {iterations} cases, shells: {', '.join(labels)} "
          f"(each with -c and -i), {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
