#!/usr/bin/env python3
"""Write a reproducible .tar.gz.

  scripts/mktar.py BASE_DIR NAME SOURCE_DATE_EPOCH OUTPUT

Archives BASE_DIR/NAME as NAME/... with entries sorted by path, every
timestamp set to SOURCE_DATE_EPOCH, owner and group 0 with no names, and
permissions normalized to 755 (directories and executables) or 644. The gzip
header carries no timestamp or file name. The same input files always give
byte-identical output, on any machine.

Plain `tar` can't do this portably: GNU tar and the BSD tar on macOS spell
these options differently, and some versions don't have them at all.
"""

import gzip
import io
import os
import stat
import sys
import tarfile


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    base, name, epoch, output = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    root = os.path.join(base, name)

    paths = [name]
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        rel = os.path.relpath(dirpath, base)
        paths += [os.path.join(rel, d) for d in dirnames]
        paths += [os.path.join(rel, f) for f in sorted(filenames)]
    paths.sort()

    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for path in paths:
            info = tar.gettarinfo(os.path.join(base, path), arcname=path)
            info.mtime = epoch
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            if info.isdir() or (info.isfile() and info.mode & stat.S_IXUSR):
                info.mode = 0o755
            elif info.issym():
                info.mode = 0o777
            else:
                info.mode = 0o644
            if info.isfile():
                with open(os.path.join(base, path), "rb") as f:
                    tar.addfile(info, f)
            else:
                tar.addfile(info)

    with open(output, "wb") as out:
        with gzip.GzipFile(filename="", mode="wb", fileobj=out, mtime=0, compresslevel=9) as gz:
            gz.write(raw.getvalue())
    return 0


if __name__ == "__main__":
    sys.exit(main())
