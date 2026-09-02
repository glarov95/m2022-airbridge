#!/bin/bash
#
# Build the pinned PAPPL submodule (third_party/pappl, v1.4.12) as a static library into
# build/pappl-install. Idempotent: skips the build when pappl.pc is already there (FORCE=1 to
# rebuild). Dependencies come from Homebrew (libusb, jpeg-turbo, libpng, openssl@3, pkg-config)
# and the macOS SDK (libcups 2.3.4 via cups-config, mDNSResponder).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/third_party/pappl"
PREFIX="${1:-$ROOT/build/pappl-install}"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

export PATH="/opt/homebrew/bin:$PATH"
export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig:/opt/homebrew/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

[[ -f "$SRC/configure" ]] || { echo "PAPPL submodule missing; run: git submodule update --init" >&2; exit 1; }
mkdir -p "$(dirname "$PREFIX")"

if [[ ! -f "$PREFIX/lib/pkgconfig/pappl.pc" || "${FORCE:-0}" == 1 ]]; then
    cd "$SRC"
    echo "configuring PAPPL $(git describe --tags 2>/dev/null || echo '?') ..."
    # Build for the host architecture only: PAPPL's configure defaults to a universal
    # (arm64 + x86_64) build on macOS unless -arch is already present in CFLAGS/LDFLAGS, and
    # the Homebrew libraries are arm64-only.
    export CFLAGS="-arch $(uname -m)${CFLAGS:+ $CFLAGS}" LDFLAGS="-arch $(uname -m)${LDFLAGS:+ $LDFLAGS}"
    ./configure --prefix="$PREFIX" --enable-static --disable-shared \
        --with-dnssd=mdnsresponder --with-tls=openssl \
        --enable-libusb --enable-libjpeg --enable-libpng \
        > "$PREFIX.configure.log" 2>&1 || { cat "$PREFIX.configure.log"; exit 1; }
    echo "building with $JOBS jobs ..."
    make -j"$JOBS" > "$PREFIX.make.log" 2>&1 || { tail -60 "$PREFIX.make.log"; exit 1; }
    make install > "$PREFIX.install.log" 2>&1 || { tail -30 "$PREFIX.install.log"; exit 1; }
fi
echo "PAPPL installed in $PREFIX"
cat "$PREFIX/lib/pkgconfig/pappl.pc"
