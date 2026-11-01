#!/usr/bin/env bash
# Generate the Crash Team Racing YAML template a release ships from a packed
# ctr.apworld, with an Archipelago checkout. Shared by release-prepare.yml and
# the release check in build.yml, so both build it the same way.
#
#   tools/release/ci-template.sh <ctr.apworld> <archipelago checkout> <output.yaml>
#
# Uses `python` from PATH (the workflow's setup-python) and installs
# Archipelago's requirements into it.
set -euo pipefail

apworld="$(realpath "$1")"
archipelago="$2"
output="$3"

python -m pip install --disable-pip-version-check -r "$archipelago/requirements.txt"
mkdir -p "$archipelago/custom_worlds"
cp "$apworld" "$archipelago/custom_worlds/ctr.apworld"

templates="$(mktemp -d)"
(cd "$archipelago" && SKIP_REQUIREMENTS_UPDATE=1 python -c \
	"import sys; from Options import generate_yaml_templates; generate_yaml_templates(sys.argv[1], False)" "$templates")
template="$(find "$templates" -type f -name 'Crash Team Racing.yaml' -print -quit)"
test -n "$template" -a -s "$template"
mkdir -p "$(dirname "$output")"
cp "$template" "$output"
