#!/usr/bin/env bash
# Install and start a packaged macOS app as a player would (docs/MACOS.md),
# without a display or a disc: the launcher installs the bundle's game folder
# into a scratch home's Application Support and starts the game there with
# --version, which prints and exits before anything else.
#
#   tools/ci/smoke-macos-app.sh <folder with one player or CI macOS archive>
#
# Runs on a CI build's archive (build.yml) and on the assembled release
# archive, sealed again with the solo runtime inside (tools/release/ci-assemble.sh).
set -euo pipefail
folder=${1:?usage: smoke-macos-app.sh <archive folder>}
# A release folder also holds the box authoring archive: test the player's.
archives=$(ls "$folder"/ctr-decrypted*-macos-arm64*.tar.gz)
if [ "$(echo "$archives" | wc -l)" -gt 1 ]; then
	archives=$(echo "$archives" | grep -v box-authoring)
fi
archive=$archives
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
tar -xzf "$archive" -C "$work"
app=$(ls -d "$work"/*/*.app)
name=$(basename "$app" .app)
launcher="$app/Contents/MacOS/$name"
codesign --verify --strict "$app"
export HOME="$work/home"
mkdir -p "$HOME"
folder="$HOME/Library/Application Support/$name"

expect_version() {
	local out
	out=$("$launcher" --version)
	echo "$out"
	[[ "$out" == "CTR Native "* ]] || { echo "smoke: the game did not start from $folder" >&2; exit 1; }
}

# First launch: installs, then runs the installed game.
expect_version
game=$(cd "$app/Contents/Resources/game" && ls ctr_native*)
cmp "$app/Contents/Resources/game/$game" "$folder/$game"
diff -r -x .installed-by-app -x .install.lock "$app/Contents/Resources/game" "$folder"
[ -s "$folder/.installed-by-app" ]
if xattr -r "$folder" | grep -q com.apple.quarantine; then
	echo 'smoke: the installed files carry the quarantine flag' >&2
	exit 1
fi
# The support bundle runs from the installed folder with macOS's own tools.
if [ -f "$folder/support-bundle.sh" ]; then
	"$folder/support-bundle.sh" >/dev/null
	bundle=$(ls "$folder"/ctr-ap-support-*.tar.gz)
	tar -xzOf "$bundle" ./bundle-info.txt | grep -q "$game" || { echo 'smoke: the support bundle has no executable hash' >&2; exit 1; }
	rm -f "$bundle"
fi

# Second launch of the same app: the folder stays as it is (an update the
# in-game updater installed would be kept).
echo 'kept' > "$folder/marker-from-the-player.txt"
before=$(stat -f %i "$folder/$game")
expect_version
[ "$(stat -f %i "$folder/$game")" = "$before" ] || { echo 'smoke: the same app installed again' >&2; exit 1; }

# Another build of the app installs again, and leaves the player's files.
echo 'an older build' > "$folder/.installed-by-app"
expect_version
[ "$(stat -f %i "$folder/$game")" != "$before" ] || { echo 'smoke: a different build did not install' >&2; exit 1; }
[ "$(cat "$folder/marker-from-the-player.txt")" = kept ]
echo "smoke: $name installs into Application Support and starts"
