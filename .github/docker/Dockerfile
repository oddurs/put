# Build environment for the Alpine (musl) CI job and the static Linux release
# binaries. The base image is pinned by digest so a rebuilt tag can't change
# what CI and releases are built with; Dependabot proposes updates.
FROM alpine:3.22@sha256:2e0b81d60597f449baa56dc25d355367869d2dca4cd57e866f1e2b52af3eb55f

RUN apk add --no-cache bash build-base py3-pip python3 zsh

# Built with tests/ as the context: docker build -f .github/docker/Dockerfile tests
COPY requirements.txt /tmp/requirements.txt
RUN python3 -m pip install --break-system-packages --no-cache-dir -r /tmp/requirements.txt
