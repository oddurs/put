# put

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

### put.peek

| Content | Shown as |
| --- | --- |
| Leading or trailing space | `·` |
| Tab, CR, LF | `\t`, `\r`, `\n` |
| ESC and other control bytes | `\e`, `\x07`, `\x7f` |
| No-break, zero-width, BOM, tag characters | `\u{a0}`, `\u{200b}`, `\u{feff}`, `\u{e0041}` |
| Bidirectional controls ([Trojan Source](https://trojansource.codes)) | `\u{202e}`, in red |
| Invalid UTF-8 | `\xff` |
| Empty argument | `∅ empty` |

On a terminal, escapes and markers are colored so they can't be confused with real text. When piped (or with `NO_COLOR` set), literal backslashes are doubled and a literal `·` or `∅` is escaped instead. Either way, `put.peek` never writes a control byte from its input, so it's safe to point at untrusted data.

### put.quote

```sh
$ put.quote plain "two words" "it's" $'tab\there' ""
plain 'two words' 'it'\''s' $'tab\there' ''
```

Paste the output into bash or zsh and you get the exact same arguments back. Control bytes, invalid UTF-8 and invisible characters use `$'...'` with byte escapes, so what you copy shows what's really there.

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

Needs a C11 compiler and make.

```sh
make
sudo make install                 # /usr/local/bin and the man page
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
make test        # unit tests, golden byte-for-byte tests, round-trip fuzzing
make sanitize    # everything again under AddressSanitizer + UBSan
make valgrind    # golden tests under valgrind
make fmt         # clang-format
make lint        # format check + clang-tidy
```

CI runs all of these on every push, with gcc and clang on Linux and clang on macOS, and `WERROR=1` so any compiler warning fails the build.

The round-trip fuzzer (`tests/roundtrip.py`) feeds random byte strings through `put.quote` and back through real bash and zsh, through `put.json` and back through a JSON parser, and through `put.nul`, and checks every byte survives.

New to C? [docs/TOUR.md](docs/TOUR.md) walks through the codebase in reading order.

## License

MIT
