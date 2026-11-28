#!/bin/bash
# CTR-AP support bundle (Linux, Steam Deck and macOS).
#
# Run this from the game folder (double-click or `./support-bundle.sh`; on a Mac,
# the game folder is ~/Library/Application Support/<app name>) after a
# crash or a stuck seed. It packs the logs + state needed for a bug report into
# one archive you can drag into Discord. No game data and no passwords are
# included: the config copies have their password lines stripped.
set -e
cd "$(dirname "$0")"

stamp=$(date +%Y%m%d-%H%M%S)
out="ctr-ap-support-$stamp.tar.gz"
tmp=$(mktemp -d)

# Logs + state. "Crash Team Racing*.log" = the engine log (name carries the
# release); D*ap-read.log = the pre-v0.1.2 AP log's broken literal filename,
# collected so older installs still hand over their history.
for f in ctr-ap.log ctr-ap.log.old ctr-ap-crash.txt ap-state.json \
         "Crash Team Racing"*.log D*ap-read.log; do
	[ -f "$f" ] && cp "$f" "$tmp/" 2>/dev/null
done

# Configs, passwords stripped.
for c in config.ini ap-config.txt; do
	[ -f "$c" ] && grep -vi '^[[:space:]]*password' "$c" > "$tmp/$c"
done

{
	echo "CTR-AP support bundle $stamp"
	uname -a
	# md5sum on Linux, md5 on macOS: the same hash support-bundle.ps1 writes.
	for exe in ctr_native_ap ctr_native_ap_authoring ctr_native ctr_native_authoring; do
		[ -f "$exe" ] || continue
		if command -v md5sum >/dev/null; then md5sum "$exe"; else md5 -r "$exe"; fi
	done
} > "$tmp/bundle-info.txt"

tar -czf "$out" -C "$tmp" .
rm -rf "$tmp"
echo ""
echo "Created: $out"
echo "Attach that file to your bug report (Discord or GitHub). Thanks!"
