#!/usr/bin/env bash
# Build the static OpenSSL the macOS Archipelago client links, from a pinned
# release, for the game's minimum macOS (CMAKE_OSX_DEPLOYMENT_TARGET in
# CMakeLists.txt). Homebrew's OpenSSL is not used: it is built for the
# runner's own macOS and moves with every Homebrew update.
#
#   tools/ci/build-openssl-macos.sh <prefix> [download cache]
#
# Installs headers and static libraries into <prefix>; pass it to CMake as
# OPENSSL_ROOT_DIR. Does nothing when <prefix> already holds this build.
set -euo pipefail
version=3.5.9
sha256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
minimum=11.0
prefix=${1:?usage: build-openssl-macos.sh <prefix> [download cache]}
cache=${2:-${TMPDIR:-/tmp}/openssl-download}
stamp="openssl $version arm64 macOS $minimum"
if [ "$(cat "$prefix/BUILD-STAMP" 2>/dev/null)" = "$stamp" ]; then
  exit 0
fi
mkdir -p "$cache"
tarball=$cache/openssl-$version.tar.gz
if [ ! -f "$tarball" ] || [ "$(shasum -a 256 "$tarball" | cut -d' ' -f1)" != "$sha256" ]; then
  curl -fsSL -o "$tarball.part" "https://github.com/openssl/openssl/releases/download/openssl-$version/openssl-$version.tar.gz"
  actual=$(shasum -a 256 "$tarball.part" | cut -d' ' -f1)
  if [ "$actual" != "$sha256" ]; then
    echo "openssl-$version.tar.gz: SHA-256 $actual, expected $sha256" >&2
    rm -f "$tarball.part"
    exit 1
  fi
  mv "$tarball.part" "$tarball"
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tar -xzf "$tarball" -C "$work"
cd "$work/openssl-$version"
rm -rf "$prefix"
./Configure darwin64-arm64-cc no-shared no-tests no-docs no-apps \
  "-mmacosx-version-min=$minimum" --prefix="$prefix" --libdir=lib
make -j "$(sysctl -n hw.ncpu)" build_libs
make install_dev
echo "$stamp" > "$prefix/BUILD-STAMP"
