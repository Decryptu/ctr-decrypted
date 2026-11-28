#!/usr/bin/env bash
# Build the solo runtimes, assemble the release assets and verify them before
# signing. Shared by release-prepare.yml (the real release) and the release
# check in build.yml (every push and pull request), so a client archive the
# assembler would refuse fails the build it came from, not the release.
#
#   tools/release/ci-assemble.sh <native artifacts> <ctr.apworld> <template.yaml> <version> <output dir>
#
# Run from the repository root of the exact source the native artifacts were
# built from, on macOS: the macOS app is sealed with Apple's codesign, and GNU
# binutils (Homebrew's, all targets) must be on PATH for the other builds. Uses `python` (CPython 3.12, for the solo runtime bytecode) and
# `python3` from PATH. The solo runtime downloads are cached in
# solo-runtime-cache/.
set -euo pipefail

artifacts="$1"
apworld="$2"
template="$3"
version="$4"
output="$5"

# One solo runtime per client archive (tools/release/release_policy.py TARGETS).
for target in windows-x86 linux-x86_64 linux-arm64 macos-arm64; do
	python tools/solo/build-runtime.py --platform "$target" \
		--apworld "$apworld" --out "solo-runtimes/$target" --cache solo-runtime-cache
done

python3 tools/release/assemble-release.py \
	--native-source . \
	--artifacts "$artifacts" \
	--apworld "$apworld" \
	--template "$template" \
	--version "$version" \
	--output-dir "$output" \
	--solo-runtimes solo-runtimes \
	--authoring

python3 tools/verify-release.py "$output" \
	--version "v${version#v}" \
	--pre-sign --require-solo-runtime

# The macOS app was sealed again with the release folder inside: install and
# start it as a player would.
bash tools/ci/smoke-macos-app.sh "$output"
