#!/usr/bin/env bash
# Same recipe locally and in Actions. Never run the game or fetch disc assets.
set -euo pipefail
cd "$(dirname "$0")/../.."
# With no variant named, every variant is built in turn. CI builds each in its
# own job, in parallel.
platform=${1:?usage: build-clients.sh linux|macos|windows [ap|vanilla|authoring|authoring-client ...]}
shift
# Windows releases are the 32-bit x86 build (MSYS2 MINGW32). Linux releases are
# the 64-bit build for the host's architecture (docs/64BIT.md): x86_64 or arm64.
# macOS releases are the 64-bit build for Apple Silicon, as an app bundle
# (docs/MACOS.md).
case "$platform" in
  linux)
    suffix=
    case "$(uname -m)" in
      x86_64) target=linux-x86_64 ;;
      aarch64|arm64) target=linux-arm64 ;;
      *) echo "No Linux release target for $(uname -m)" >&2; exit 2 ;;
    esac
    arch_flags=(-DCTR_64BIT=ON)
    ;;
  macos)
    suffix=
    [ "$(uname -m)" = arm64 ] || { echo "No macOS release target for $(uname -m)" >&2; exit 2; }
    target=macos-arm64
    # OpenSSL built for the game's minimum macOS, outside the checkout.
    openssl=${CTR_OPENSSL_PREFIX:-${RUNNER_TEMP:-$HOME/.cache}/ctr-openssl-macos}
    bash tools/ci/build-openssl-macos.sh "$openssl"
    arch_flags=(-DCTR_64BIT=ON "-DOPENSSL_ROOT_DIR=$openssl")
    ;;
  windows)
    suffix=.exe
    target=windows-x86
    arch_flags=(-DCMAKE_C_FLAGS='-m32 -msse' -DCMAKE_CXX_FLAGS=-m32)
    ;;
  *) echo 'Expected linux, macos or windows' >&2; exit 2 ;;
esac
for requested in "$@"; do
  case "$requested" in
    ap|vanilla|authoring|authoring-client) ;;
    *) echo "Unknown variant: $requested" >&2; exit 2 ;;
  esac
done
if [ -e client-artifacts ] || [ -e build-ci-ap ] || [ -e build-ci-vanilla ] || [ -e build-ci-authoring ] \
   || [ -e build-ci-authoring-client ]; then
  echo 'Use a fresh checkout/build directory; preserve prior artifacts.' >&2
  exit 1
fi
source_status=$(git status --porcelain --untracked-files=normal)
if [ -n "$source_status" ]; then
  echo 'Build provenance requires a clean checkout, including untracked files.' >&2
  printf '%s\n' "$source_status" >&2
  exit 1
fi
mkdir client-artifacts
bash ap/vendor/fetch-deps.sh
# authoring = the separate box authoring download: the AP client plus
# CTR_AP_AUTHORING (own exe name, own placement file, never connects to a room).
# It is its own archive and never goes into the player client archive.
# authoring-client = the authoring client without Archipelago (CTR_AP=OFF,
# CTR_AUTHORING_CLIENT): packages, Saphi, Box Author Mode, AI lap recorder.
for variant in ap vanilla authoring authoring-client; do
  if [ $# -gt 0 ] && [[ " $* " != *" $variant "* ]]; then
    continue
  fi
  ap=OFF
  custom=OFF
  authoring=OFF
  client=OFF
  binary=ctr_native
  if [ "$variant" = ap ]; then
    ap=ON
    custom=ON
    binary=ctr_native_ap
  elif [ "$variant" = authoring ]; then
    ap=ON
    custom=ON
    authoring=ON
    binary=ctr_native_ap_authoring
  elif [ "$variant" = authoring-client ]; then
    custom=ON
    client=ON
    binary=ctr_native_authoring
  fi
  build=build-ci-$variant
  # No precompiled headers: MinGW GCC writes SDL's precompiled header with
  # different bytes on every build, so each SDL object (all include it) would
  # miss the compiler cache, and the build would not be byte-reproducible.
  cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    "${arch_flags[@]}" \
    -DCTR_AP="$ap" -DCTR_CUSTOM_TRACKS="$custom" -DCTR_AP_AUTHORING="$authoring" \
    -DCTR_AUTHORING_CLIENT="$client" \
    -DCTR_AP_VERIFY_VENDOR=ON
  cmake --build "$build" --parallel "${CTR_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN)}"
  # The CMake tests (upstream's unit and regression tests) build with the game;
  # run them where they can run, so they cannot break unnoticed.
  if [ "$platform" != windows ]; then
    ctest --test-dir "$build" --output-on-failure
  fi
  exe="$build/$binary$suffix"
  if [ "$platform" = windows ]; then
    objcopy --only-keep-debug "$exe" "$exe.debug"
    objcopy --strip-all "$exe"
    objcopy --add-gnu-debuglink="$exe.debug" "$exe"
  fi
  python3 tools/ci/package-client.py "$target" "$variant" "$exe" client-artifacts
done
