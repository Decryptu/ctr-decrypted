#!/usr/bin/env bash
# Build the solo runtimes, assemble the release assets and verify them before
# signing. Shared by release-prepare.yml (the real release) and the release
# check in build.yml (every push and pull request), so a client archive the
# assembler would refuse fails the build it came from, not the release.
#
#   tools/release/ci-assemble.sh <native artifacts> <ctr.apworld> <template.yaml> <version> <output dir>
#
# Run from the repository root of the exact source the native artifacts were
# built from. Uses `python` (CPython 3.12, for the solo runtime bytecode) and
# `python3` from PATH. The solo runtime downloads are cached in
# solo-runtime-cache/.
set -euo pipefail

artifacts="$1"
apworld="$2"
template="$3"
version="$4"
output="$5"

for platform in windows linux; do
	python tools/solo/build-runtime.py --platform "$platform" \
		--apworld "$apworld" --out "solo-runtime-$platform" --cache solo-runtime-cache
done

python3 tools/release/assemble-release.py \
	--native-source . \
	--windows-artifacts "$artifacts" \
	--linux-artifacts "$artifacts" \
	--apworld "$apworld" \
	--template "$template" \
	--version "$version" \
	--output-dir "$output" \
	--solo-runtime-windows solo-runtime-windows \
	--solo-runtime-linux solo-runtime-linux \
	--authoring

python3 tools/verify-release.py "$output" \
	--version "v${version#v}" \
	--pre-sign --require-solo-runtime
