# Changelog

All notable changes to this project are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The output of every mode is part of the public interface: a change to what
any mode prints for the same arguments is a breaking change.

## [Unreleased]

## [0.1.0]

First release.

### Added

- `put`, `put.raw`, `put.lines`, `put.nul`: print arguments literally, with
  no options and no escape processing.
- `put.peek`: show what each argument contains, including invisible and
  control characters, without ever writing a control byte from the input.
- `put.quote`: print arguments as bash/zsh input that recreates them.
- `put.json`: print arguments as a JSON array; refuses invalid UTF-8.
- `put.help` and `put.version`.
- Correct output on non-blocking stdout, partial writes and interrupted
  writes; write errors are reported with exit code 1.
- Man page, static Linux binaries and a universal macOS binary.

[Unreleased]: https://github.com/oddurs/put/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/oddurs/put/releases/tag/v0.1.0
