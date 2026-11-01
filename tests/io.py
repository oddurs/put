#!/usr/bin/env python3
"""Output edge cases that a shell script can't easily set up.

  - stdout is a non-blocking pipe and the reader is slow (put must wait,
    not fail with "Resource temporarily unavailable")
  - single arguments larger than put's 64 KiB buffer
  - many arguments that cross the buffer boundary repeatedly
  - a reader that has gone away, with SIGPIPE ignored or blocked by the parent
  - a full, non-blocking terminal (put must wait without spinning)

Usage: python3 tests/io.py BUILD_DIR
"""

import os
import pty
import select
import signal
import subprocess
import sys
import threading
import time
import tty

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

    check_sigpipe(plain)
    check_full_terminal(lines)

    print(f"io: {failures} failed")
    return 1 if failures else 0


def check_sigpipe(binary: str) -> None:
    """Writing into a pipe nobody reads must end put by SIGPIPE, quietly.

    The read end is closed before put starts, so there's no timing involved.
    Python ignores SIGPIPE itself; restore_signals=False passes that on to
    the child, so put has to undo it. The second run also blocks SIGPIPE.
    """
    def block_sigpipe() -> None:
        signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGPIPE})

    for label, preexec in (("ignored", None), ("blocked", block_sigpipe)):
        r, w = os.pipe()
        os.close(r)
        proc = subprocess.run([binary, "x"], stdout=w, stderr=subprocess.PIPE, check=False,
                              restore_signals=False, preexec_fn=preexec)
        os.close(w)
        check(f"closed pipe, SIGPIPE {label}: killed by SIGPIPE",
              proc.returncode == -signal.SIGPIPE, proc.returncode)
        check(f"closed pipe, SIGPIPE {label}: no message", proc.stderr == b"", proc.stderr)


def check_full_terminal(binary: str) -> None:
    """A full non-blocking terminal makes put wait without using the CPU.

    macOS's poll() returns at once for terminals, so waiting with poll()
    there turned into a busy loop. The reader sleeps for a second before
    draining the terminal; put should spend almost none of it on the CPU.
    """
    master, slave = pty.openpty()
    tty.setraw(slave)  # no newline translation: bytes in, same bytes out
    os.set_blocking(slave, False)

    args = [f"tty-line-{i:05d}".encode() for i in range(20000)]
    want = b"".join(a + b"\n" for a in args)
    proc = subprocess.Popen([binary, *args], stdout=slave, stderr=subprocess.PIPE)
    os.close(slave)

    time.sleep(1.0)
    got = bytearray()
    while len(got) < len(want):
        ready, _, _ = select.select([master], [], [], 5)
        if not ready:
            break
        try:
            chunk = os.read(master, 65536)
        except OSError:  # Linux reports EIO once the other side is closed
            break
        if not chunk:
            break
        got += chunk
    _, status, usage = os.wait4(proc.pid, 0)
    stderr = proc.stderr.read()
    proc.stderr.close()
    os.close(master)

    check("full terminal: exit 0", os.waitstatus_to_exitcode(status) == 0, stderr)
    check("full terminal: all bytes arrive", bytes(got) == want, (len(got), len(want)))
    cpu = usage.ru_utime + usage.ru_stime
    check("full terminal: put waits without spinning", cpu < 0.5, f"{cpu:.2f}s of CPU")


if __name__ == "__main__":
    sys.exit(main())
