#!/usr/bin/env bash
# Same recipe locally and in Actions. Never run the game or fetch disc assets.
set -euo pipefail
cd "$(dirname "$0")/../.."
# With no variant named, every variant is built in turn. CI builds each in its
# own job, in parallel.
platform=${1:?usage: build-clients.sh linux|windows [ap|vanilla|authoring|authoring-client ...]}
shift
case "$platform" in
  linux) suffix= ;;
  windows) suffix=.exe ;;
  *) echo 'Expected linux or windows' >&2; exit 2 ;;
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
    -DCMAKE_C_FLAGS='-m32 -msse' -DCMAKE_CXX_FLAGS=-m32 \
    -DCTR_AP="$ap" -DCTR_CUSTOM_TRACKS="$custom" -DCTR_AP_AUTHORING="$authoring" \
    -DCTR_AUTHORING_CLIENT="$client" \
    -DCTR_AP_VERIFY_VENDOR=ON
  cmake --build "$build" --parallel "${CTR_BUILD_JOBS:-$(nproc)}"
  exe="$build/$binary$suffix"
  if [ "$platform" = windows ]; then
    objcopy --only-keep-debug "$exe" "$exe.debug"
    objcopy --strip-all "$exe"
    objcopy --add-gnu-debuglink="$exe.debug" "$exe"
  fi
  python3 tools/ci/package-client.py "$platform" "$variant" "$exe" client-artifacts
done
