# Security policy

## Supported versions

Security fixes go into the latest release.

## Reporting a vulnerability

Please report privately through GitHub: open the repository's **Security** tab and choose **Report a vulnerability**. Don't open a public issue for a security problem.

You can expect an acknowledgement within a week. Once a fix is ready, it ships in a new release and the advisory is published with credit, unless you'd rather stay anonymous.

## What counts

put's security promises are narrow and specific. These are in scope:

- Any memory-safety bug: out-of-bounds read or write, use of uninitialized memory, crash on any input.
- `put.peek` writing a control byte, an invalid UTF-8 sequence or an invisible character taken from its input. It is meant to be safe to run on untrusted data in a terminal.
- `put.quote` producing output that, pasted into bash or zsh, runs a command or expands to something other than the original arguments.
- `put.json` producing invalid JSON, or JSON that decodes to different strings.

Plain `put`, `put.raw`, `put.lines` and `put.nul` print bytes exactly as given, including terminal escape sequences. That's intended, and documented in the man page.

## Verifying a release

Each release includes `SHA256SUMS` and a signed build provenance attestation, which proves a tarball was built by this repository's release workflow from a specific commit:

```sh
sha256sum -c SHA256SUMS --ignore-missing
gh attestation verify put-0.1.0-x86_64-linux.tar.gz --repo oddurs/put
```
