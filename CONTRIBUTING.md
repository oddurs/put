# Contributing

Thanks for helping. put is small on purpose, so the bar for new features is high, while the bar for bug reports, tests and docs is low.

## The rules put lives by

1. **Arguments are data.** No mode may ever read options, flags or escapes from its arguments.
2. **Output is the interface.** What a mode prints for given arguments is a public promise. Changing it is a breaking change, even for "obviously better" output.
3. **The name is the mode.** New behavior means a new `put.<mode>` name, not a switch on an existing one.
4. **No formatting language.** printf already exists.

If a proposal bends one of these, open an issue to discuss it before writing code.

## Building and testing

You need a C11 compiler, make, bash and python3. zsh is optional (the quote round-trip test uses it when present).

```sh
make check        # build, all tests, and lint: run this before pushing
```

Deeper checks, all of which CI runs on every pull request:

| Command | What it does | Needs |
| --- | --- | --- |
| `make sanitize` | every test under AddressSanitizer + UBSan | clang or gcc |
| `make valgrind` | golden tests under valgrind | valgrind (Linux) |
| `make analyze` | GCC static analyzer | gcc 12+ |
| `make coverage` | line coverage, fails under 95% | gcc, gcovr |
| `make fuzz` | libFuzzer for `FUZZ_TIME` seconds (default 60) | clang |
| `make lint` | clang-format, clang-tidy, shellcheck, version check | clang-format, clang-tidy, shellcheck |
| `make check-hardening` | confirms hardening flags reached the binary | Linux, readelf |

## What a change needs

- **Tests.** Output changes need golden tests in `tests/run.sh`. New escaping or quoting rules also need a property in `tests/fuzz.c`.
- **Docs.** New or changed behavior goes in `man/put.1`, `README.md`, and under `## [Unreleased]` in `CHANGELOG.md`.
- **Formatting.** Run `make fmt` before committing.
- **No new warnings.** CI builds with `-Werror` on gcc and clang.

## Releasing

1. Pick the version (see the semver note at the top of `CHANGELOG.md`).
2. Set `VERSION` in the `Makefile`, the date and version in `man/put.1`'s `.TH` line, and rename `## [Unreleased]` in `CHANGELOG.md` to the version, adding a fresh empty `## [Unreleased]` above it and updating the compare links at the bottom.
3. Run `make check`; `check-version` confirms the three agree.
4. Commit, then tag and push:
   ```sh
   git tag -a v0.2.0 -m "put 0.2.0"
   git push origin main v0.2.0
   ```
5. The release workflow builds static Linux binaries (x86_64, aarch64) and a universal macOS binary, runs the tests on each, and publishes a GitHub release with `SHA256SUMS` and signed build provenance. It refuses to publish if the tag doesn't match `VERSION`.

The same workflow runs on every push to `main` without publishing, so release builds are always known to work.
