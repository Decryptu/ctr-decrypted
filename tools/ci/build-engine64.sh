#!/usr/bin/env bash
# Build the 64-bit engine (docs/64BIT.md) with the host's own 64-bit compiler:
# Linux x86_64, Linux arm64, or MSYS2 UCRT64 on Windows, unpackaged. CI runs it
# for Windows x86_64, whose releases stay 32-bit; Linux releases are built and
# packaged by build-clients.sh. On Linux the CMake tests run too, which starts
# the built executable once (--version). Never runs the game or fetches disc
# assets.
set -euo pipefail
cd "$(dirname "$0")/../.."
usage='usage: build-engine64.sh linux|windows [vanilla|ap ...]'
platform=${1:?$usage}
shift
case "$platform" in
  linux|windows) ;;
  *) echo "$usage" >&2; exit 2 ;;
esac
variants=("$@")
if [ ${#variants[@]} -eq 0 ]; then
  variants=(vanilla ap)
fi
for variant in "${variants[@]}"; do
  case "$variant" in
    vanilla|ap) ;;
    *) echo "Unknown variant: $variant" >&2; exit 2 ;;
  esac
done
bash ap/vendor/fetch-deps.sh
for variant in "${variants[@]}"; do
  ap=OFF
  custom=OFF
  if [ "$variant" = ap ]; then
    ap=ON
    custom=ON
  fi
  build=build-ci64-$variant
  # No precompiled headers, as in build-clients.sh: the compiler cache could
  # not reuse SDL's objects otherwise.
  cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON \
    -DCTR_64BIT=ON -DCTR_AP="$ap" -DCTR_CUSTOM_TRACKS="$custom" \
    -DCTR_AP_VERIFY_VENDOR=ON
  cmake --build "$build" --parallel "${CTR_BUILD_JOBS:-$(nproc)}"
  if [ "$platform" = linux ]; then
    ctest --test-dir "$build" --output-on-failure
  fi
done
