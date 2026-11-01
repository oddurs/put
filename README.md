# put

[![ci](https://github.com/oddurs/put/actions/workflows/ci.yml/badge.svg)](https://github.com/oddurs/put/actions/workflows/ci.yml)
[![codeql](https://github.com/oddurs/put/actions/workflows/codeql.yml/badge.svg)](https://github.com/oddurs/put/actions/workflows/codeql.yml)

`echo`, reinvented. Every argument is printed exactly as given: no flags, no escape sequences, no surprises.

```sh
$ x=-n
$ echo "$x"        # prints nothing in bash
$ put "$x"
-n
```

And when you're debugging, `put.peek` shows what a value *actually* contains:

```sh
$ put.peek hello "two  words " $'crlf\r' "" $'nb\xc2\xa0sp'
5 args
0  hello
1  two  words·
2  crlf\r
3  ∅ empty
4  nb\u{a0}sp
```

## Why

`echo` mixes control and data in one channel. `echo "$x"` breaks when `$x` is `-n` or `-e`, shells disagree about whether `\n` is an escape, and POSIX gave up and called the output implementation-defined. Worse, echo hides the things you reach for it to find: trailing spaces, carriage returns from Windows files, non-breaking spaces pasted from docs, and where one argument ends and the next begins.

`put` follows one rule: **arguments are data, always.** Behavior is chosen by the command's *name*, never by flags, so no value can ever change what the tool does.

## Modes

One binary, installed under several names (like `mkfs.ext4`). Type `put.` and press Tab to list them.

| Command | Output | With no args | Replaces |
| --- | --- | --- | --- |
| `put` | args joined by spaces, then a newline | a newline | `echo "$x"` |
| `put.raw` | args joined by spaces, no newline | nothing | `echo -n` |
| `put.lines` | each arg followed by a newline | nothing | `printf '%s\n' "$@"` |
| `put.nul` | each arg followed by a NUL byte | nothing | `printf '%s\0' "$@"` |
| `put.peek` | what each arg really contains | `0 args` | `echo "[$x]"` |
| `put.quote` | shell input that recreates the args | a newline | bash `printf '%q '` |
| `put.json` | a JSON array of strings | `[]` | hand-rolled JSON |
| `put.help` | the list of modes | | `--help` |
| `put.version` | the version, and the Unicode version its tables use | | `--version` |

### put.peek

| Content | Shown as |
| --- | --- |
| Leading or trailing space | `·` |
| Tab, CR, LF, ESC | `\t`, `\r`, `\n`, `\e` |
| Other control bytes | `\x07`, `\x7f` |
| Invalid UTF-8 | `\xff` |
| Anything that renders as nothing, as blank space, or depending on the font: no-break and other non-ASCII spaces, zero-width characters, BOM, variation selectors, tag characters, private-use and unassigned code points | `\u{a0}`, `\u{200b}`, `\u{feff}`, `\u{fe0f}`, `\u{e0041}` |
| Bidirectional controls ([Trojan Source](https://trojansource.codes)) | `\u{202e}`, in red |
| A combining mark with nothing to attach to | `\u{301}` |
| Empty argument | `∅ empty` |

The character classes are generated from Unicode 17.0 data and checked against every one of the 1.1 million code points in CI.

On a terminal, escapes and markers are colored so they can't be confused with real text. When piped (or with `NO_COLOR` set, or `TERM=dumb`), literal backslashes are doubled and a literal `·` or `∅` is escaped instead, which makes the output lossless: every argument can be recovered exactly from its line. Either way, `put.peek` never writes a control byte or an invisible character from its input, so it's safe to point at untrusted data.

### put.quote

```sh
$ put.quote plain "two words" "it's" $'tab\there' "" a=b
plain 'two words' 'it'\''s' $'tab\there' '' 'a=b'
```

Paste the output into bash or zsh, in a script or at an interactive prompt, and you get the exact same arguments back. That includes macOS's bash 3.2, history expansion (`!`), and zsh options such as `EXTENDED_GLOB` and `MAGIC_EQUAL_SUBST` (the reason `a=b` is quoted). Control bytes, invalid UTF-8 and invisible characters use `$'...'` with byte escapes, so what you copy shows what's really there.

### put.json

```sh
$ put.json a "b c" 'say "hi"'
["a","b c","say \"hi\""]
```

If any argument isn't valid UTF-8, `put.json` exits with status 3 and writes nothing, instead of silently replacing bytes.

## Migrating from echo

| Before | After |
| --- | --- |
| `echo "$x"` | `put "$x"` |
| `echo -n "$x"` | `put.raw "$x"` |
| `echo -e "a\nb"` | `put.lines a b` or `put $'a\nb'` |
| `printf '%s\n' "$@"` | `put.lines "$@"` |
| `printf '%s\0' "$@" \| xargs -0 cmd` | `put.nul "$@" \| xargs -0 cmd` |
| `echo "[$x]"` | `put.peek "$x"` |
| `printf '%5.2f' "$n"` | stays `printf`: formatting is out of scope |

`put` can't fix unquoted expansion: `put $x` splits words just like `echo $x` does. `put.peek $x` shows the split, which is often the fastest way to see why quoting matters.

## Install

**Prebuilt binaries.** Each [release](https://github.com/oddurs/put/releases) has static Linux binaries (x86_64, aarch64; no dependencies) and a universal macOS binary, plus `SHA256SUMS` and signed build provenance:

```sh
v=0.1.0 target=x86_64-linux   # or aarch64-linux, universal-macos
curl -fsSLO "https://github.com/oddurs/put/releases/download/v$v/put-$v-$target.tar.gz"
curl -fsSLO "https://github.com/oddurs/put/releases/download/v$v/SHA256SUMS"
sha256sum -c SHA256SUMS --ignore-missing      # macOS: shasum -a 256 -c
tar xzf "put-$v-$target.tar.gz"
cp -P "put-$v-$target"/bin/* ~/.local/bin/  # -P keeps the put.* links as links
```

**From source.** Needs a C11 compiler and make; builds in a couple of seconds with no dependencies.

```sh
make
sudo make install                  # /usr/local/bin and the man page
make install PREFIX="$HOME/.local" # or somewhere you own
```

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Writing to stdout failed (disk full, closed descriptor) |
| 2 | Run under an unknown name, like `put.nope` |
| 3 | `put.json` got invalid UTF-8 |

If the reader closes the pipe early (`put.lines ... | head -1`), `put` ends quietly via SIGPIPE, like other Unix tools.

## Development

```sh
make check       # build, all tests, lint: run before pushing
make fmt         # clang-format
```

How put is checked:

- **Tests.** Byte-for-byte golden tests of every mode and exit code, unit tests, and I/O edge cases: closed stdout, full disk, non-blocking pipes, a full non-blocking terminal, and SIGPIPE ignored or blocked by the parent.
- **Every code point.** All 1,111,936 Unicode scalar values go through `put.peek` and `put.quote`, checked against Unicode data computed independently of the generated tables.
- **Round-trip fuzzing.** Random byte strings go through `put.quote` and back through real shells (bash 5, bash 3.2 on macOS, zsh with and without its risky options; each both as a script and typed at an interactive prompt), through `put.json` and back through a JSON parser, and through `put.nul`, and every byte must survive.
- **Coverage-guided fuzzing.** libFuzzer runs every mode on generated input and decodes `put.peek`, `put.quote` and `put.json` output back into arguments, which must match exactly. It also checks properties such as "`put.peek` never writes a control byte".
- **Memory and analysis.** AddressSanitizer + UBSan, valgrind, clang-tidy, GCC's analyzer and CodeQL, plus line coverage of at least 95%.
- **Hardened, reproducible builds.** Stack protector, `_FORTIFY_SOURCE`, PIE and full RELRO, verified in CI. Release tarballs are byte-for-byte reproducible and CI rebuilds them to prove it; the golden tests run against the exact binaries that ship.
- **Platforms.** gcc and clang on Linux x86_64 and arm64, Alpine (musl), FreeBSD and macOS, all with warnings as errors.

[CONTRIBUTING.md](CONTRIBUTING.md) covers the rest, including how releases are cut. New to C? [docs/TOUR.md](docs/TOUR.md) walks through the codebase in reading order.

## License

MIT
