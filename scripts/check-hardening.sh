#!/bin/sh
#
# Check that a Linux build of put carries the hardening the Makefile asks for.
#
#   scripts/check-hardening.sh build/put
#
# Each check reads the ELF headers with readelf; nothing is executed.

set -eu

bin=${1:?usage: scripts/check-hardening.sh BINARY}
fail=0

pass() { printf 'ok    %s\n' "$1"; }
miss() {
    printf 'FAIL  %s\n' "$1"
    fail=1
}

# Position-independent: the kernel can load it at a random address (ASLR).
if readelf -h "$bin" | grep -q 'Type:.*DYN'; then pass "position-independent executable"; else miss "position-independent executable"; fi

# Full RELRO: the relocation table is made read-only before main() runs.
if readelf -lW "$bin" | grep -q 'GNU_RELRO'; then pass "RELRO segment"; else miss "RELRO segment"; fi
if readelf -dW "$bin" | grep -Eq 'BIND_NOW|FLAGS.*NOW'; then pass "bind now (full RELRO)"; else miss "bind now (full RELRO)"; fi

# Non-executable stack: GNU_STACK must not have the E flag.
if readelf -lW "$bin" | grep 'GNU_STACK' | grep -Eq 'RW +0x'; then pass "non-executable stack"; else miss "non-executable stack"; fi

# Stack canaries and FORTIFY show up as calls to checking functions.
syms=$(readelf -sW --dyn-syms "$bin" 2>/dev/null || true)
if printf '%s\n' "$syms" | grep -q '__stack_chk_fail'; then pass "stack protector"; else miss "stack protector"; fi
if ldd --version 2>&1 | grep -qi musl; then
    # musl's FORTIFY comes from fortify-headers, which inlines its checks
    # instead of calling __*_chk functions, so there's no symbol to look for.
    printf 'skip  FORTIFY_SOURCE (musl checks are inlined)\n'
elif printf '%s\n' "$syms" | grep -q '_chk'; then
    pass "FORTIFY_SOURCE"
else
    miss "FORTIFY_SOURCE"
fi

exit "$fail"
