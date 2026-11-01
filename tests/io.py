#!/usr/bin/env python3
"""Output edge cases that a shell script can't easily set up.

  - stdout is a non-blocking pipe and the reader is slow (put must wait,
    not fail with "Resource temporarily unavailable")
  - single arguments larger than put's 64 KiB buffer
  - many arguments that cross the buffer boundary repeatedly

Usage: python3 tests/io.py BUILD_DIR
"""

import os
import subprocess
import sys
import threading
import time

failures = 0


def check(name: str, ok: bool, detail: object = "") -> None:
    global failures
    if not ok:
        failures += 1
        print(f"FAIL: {name} {detail!r}"[:500])


def read_all_slowly(fd: int, out: list) -> None:
    """Wait before reading so the pipe fills and put has to wait for us."""
    time.sleep(0.3)
    chunks = []
    while True:
        chunk = os.read(fd, 65536)
        if not chunk:
            break
        chunks.append(chunk)
    out.append(b"".join(chunks))


def run_nonblocking(binary: str, args: list) -> tuple:
    r, w = os.pipe()
    os.set_blocking(w, False)
    result = []
    reader = threading.Thread(target=read_all_slowly, args=(r, result))
    reader.start()
    proc = subprocess.run([binary, *args], stdout=w, stderr=subprocess.PIPE, check=False)
    os.close(w)
    reader.join()
    os.close(r)
    return proc, result[0]


def main() -> int:
    build = sys.argv[1] if len(sys.argv) > 1 else "build"
    lines = os.path.join(build, "put.lines")
    plain = os.path.join(build, "put")
    peek = os.path.join(build, "put.peek")

    # 20,000 short lines is about 220 KB: far more than a pipe holds.
    args = [f"line-{i:05d}".encode() for i in range(20000)]
    proc, got = run_nonblocking(lines, args)
    check("non-blocking stdout: exit 0", proc.returncode == 0, proc.stderr)
    check("non-blocking stdout: all bytes arrive", got == b"".join(a + b"\n" for a in args))

    # One argument bigger than the internal buffer goes straight to write().
    big = b"x" * 100_000
    proc, got = run_nonblocking(plain, [big, b"tail"])
    check("large argument: exit 0", proc.returncode == 0, proc.stderr)
    check("large argument: exact bytes", got == big + b" tail\n")

    # put.peek output for a large argument also crosses the buffer boundary.
    proc = subprocess.run([peek, b"\r" * 40_000], capture_output=True, check=False)
    check("large peek: exit 0", proc.returncode == 0, proc.stderr)
    check("large peek: exact bytes", proc.stdout == b"1 arg\n0  " + b"\\r" * 40_000 + b"\n")

    print(f"io: {failures} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
