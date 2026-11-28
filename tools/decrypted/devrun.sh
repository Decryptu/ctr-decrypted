#!/usr/bin/env bash
# Run the game unattended (hidden window, fast clock) and capture frames, on
# Linux or macOS. Same run as tools/decrypted/devrun.ps1 on Windows; see
# docs/DEV-TOOLS.md for the switches the game reads.
#
#   tools/decrypted/devrun.sh -x build-x64/ctr_native -b ~/ctr-u.bin \
#     -t 3 -n 3000 -f 600 -e 300 -E CTR_DEV_TRACE=/tmp/t.csv
#
# Prints the capture folder. On Linux with no display it runs the game under
# xvfb-run, so it works over SSH, in WSL and in containers.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
track=3 frames=2400 every=120 from=0 timeout=300
exe=$repo/build-vanilla/ctr_native
bin=${CTR_DISC:-}
out='' run='' save='' visible=0 realtime=0 wrap=''
cfg=() envs=() gameargs=()
usage() {
	cat >&2 <<'USAGE'
usage: devrun.sh [options] [-- game arguments]
  -t N     LevelID of the attract demo (default 3, -1 = no demo)
  -n N     quit after N presented frames (default 2400)
  -e N     capture every N frames (default 120)
  -f N     first captured frame (default 0)
  -x EXE   the game executable (default build-vanilla/ctr_native)
  -b BIN   the disc image (default $CTR_DISC)
  -o DIR   capture folder (default: new folder under $TMPDIR/ctrdev)
  -r DIR   run folder (default $TMPDIR/ctrdev/run); give parallel runs their own
  -c K=V   config.ini override; 'Section.key=V' goes in that section
  -E K=V   extra environment variable for the game
  -s SAVE  a save (tools/decrypted/ctr_save.py) for memcards/slot0
  -T SEC   timeout (default 300)
  -w CMD   run the game under CMD: qemu-aarch64 (an arm64 build on x86), valgrind...
  -V       show the window       -R  keep wall-clock pacing
USAGE
	exit 2
}
while getopts 't:n:e:f:x:b:o:r:c:E:s:T:w:VRh' opt; do
	case $opt in
		t) track=$OPTARG ;; n) frames=$OPTARG ;; e) every=$OPTARG ;; f) from=$OPTARG ;;
		x) exe=$OPTARG ;; b) bin=$OPTARG ;; o) out=$OPTARG ;; r) run=$OPTARG ;;
		c) cfg+=("$OPTARG") ;; E) envs+=("$OPTARG") ;; s) save=$OPTARG ;;
		T) timeout=$OPTARG ;; w) wrap=$OPTARG ;; V) visible=1 ;; R) realtime=1 ;; *) usage ;;
	esac
done
shift $((OPTIND - 1))
gameargs=("$@")
[ -n "$bin" ] || { echo 'devrun.sh: no disc image (-b or $CTR_DISC)' >&2; exit 2; }
[ -f "$exe" ] || { echo "devrun.sh: no executable at $exe" >&2; exit 2; }
abspath() { (cd "$(dirname "$1")" && echo "$(pwd)/$(basename "$1")"); }
exe=$(abspath "$exe")
bin=$(abspath "$bin")

root=${TMPDIR:-/tmp}/ctrdev
run=${run:-$root/run}
mkdir -p "$run" "$root"
# Default capture folders are scratch: drop the ones older than a day.
find "$root" -maxdepth 1 -type d -name 'shots-*' -mtime +0 -exec rm -rf {} + 2>/dev/null || true
out=${out:-$root/shots-$(date +%H%M%S)}
mkdir -p "$out"
out=$(cd "$out" && pwd)
# Captures from an earlier run would mix into this one's sheet.
find "$out" -maxdepth 1 -type f \( -name 'f[0-9][0-9][0-9][0-9][0-9][0-9].bmp' \
	-o -name 'f[0-9][0-9][0-9][0-9][0-9][0-9].png' -o -name sheet.png \) -delete

# A run folder of its own, so the owner's install and saves are never touched.
cp -f "$exe" "$run/ctr_native"
[ -d "$run/assets" ] || cp -R "$repo/assets" "$run/assets"
# The game only reads the disc: a link does.
[ -e "$run/assets/ctr-u.bin" ] || ln -s "$bin" "$run/assets/ctr-u.bin"
if [ -n "$save" ]; then
	mkdir -p "$run/memcards/slot0"
	cp -f "$save" "$run/memcards/slot0/BASCUS-94426-SLOTS"
fi

# Fixed baseline config (as devrun.ps1), then the caller's overrides.
python3 - "$run/config.ini" ${cfg[@]+"${cfg[@]}"} <<'PY'
import sys
cfg = {'fps60': 'true', 'pgxp': '2', 'aspect_ratio': '1', 'render_scale': '0',
       'State.setup_done': 'true'}
for kv in sys.argv[2:]:
    k, v = kv.split('=', 1)
    cfg[k] = v
sections = {'Video & QoL': []}
for k, v in cfg.items():
    sec, key = k.rsplit('.', 1) if '.' in k else ('Video & QoL', k)
    sections.setdefault(sec, []).append(f'{key} = {v}')
with open(sys.argv[1], 'w') as f:
    for sec, lines in sections.items():
        f.write(f'[{sec}]\n' + ''.join(l + '\n' for l in lines))
PY

launch=()
if [ "$(uname)" = Linux ] && [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
	command -v xvfb-run >/dev/null || { echo 'devrun.sh: no display and no xvfb-run' >&2; exit 2; }
	launch=(xvfb-run -a -s '-screen 0 1280x720x24')
fi
# Job control puts the game (and xvfb-run, its X server) and the timeout
# watcher in process groups of their own, so each is stopped as a whole.
set -m
(
	cd "$run"
	export CTR_DEV_DEMO=$track CTR_DEV_EXIT_FRAME=$frames CTR_DEV_SHOT_DIR=$out \
		CTR_DEV_SHOT_EVERY=$every CTR_DEV_SHOT_FROM=$from \
		CTR_DEV_HIDDEN=$((1 - visible)) CTR_DEV_FAST=$((1 - realtime)) \
		SDL_JOYSTICK_HIDAPI=0
	# HIDAPI over libusb claims a USB pad for one process: a test run must
	# never take the owner's controller.
	for kv in ${envs[@]+"${envs[@]}"}; do export "${kv?}"; done
	# shellcheck disable=SC2086 # CMD may carry its own arguments
	exec ${launch[@]+"${launch[@]}"} $wrap ./ctr_native ${gameargs[@]+"${gameargs[@]}"}
) >/dev/null 2>&1 &
pid=$!
( sleep "$timeout"; kill -- "-$pid" 2>/dev/null && echo "devrun.sh: timed out after $timeout s" >&2 ) &
watch=$!
status=0
wait "$pid" || status=$?
# A watcher that is gone has stopped the game (xvfb-run exits 0 on a signal).
if kill -- "-$watch" 2>/dev/null; then
	wait "$watch" 2>/dev/null || true
else
	status=124
fi
set +m
if [ "$status" -eq 124 ]; then
	:
elif [ "$status" -ne 0 ]; then
	echo "devrun.sh: game exited with status $status before frame $frames (a crash; see $run/*.log)" >&2
fi

# PNG captures and a contact sheet, when Pillow is there.
python3 - "$out" <<'PY' || echo 'devrun.sh: no Pillow, captures stay BMP' >&2
import os, sys
from PIL import Image, ImageDraw
out = sys.argv[1]
for n in sorted(os.listdir(out)):
    if n.endswith('.bmp'):
        p = os.path.join(out, n)
        Image.open(p).save(p[:-4] + '.png')
        os.remove(p)
shots = sorted(n for n in os.listdir(out) if n.startswith('f') and n.endswith('.png'))
if shots:
    cols = min(5, len(shots)); rows = -(-len(shots) // cols)
    first = Image.open(os.path.join(out, shots[0]))
    tw = 384; th = tw * first.height // first.width
    sheet = Image.new('RGB', (tw * cols, th * rows))
    d = ImageDraw.Draw(sheet)
    for i, n in enumerate(shots):
        x, y = (i % cols) * tw, (i // cols) * th
        sheet.paste(Image.open(os.path.join(out, n)).convert('RGB').resize((tw, th)), (x, y))
        d.rectangle((x, y, x + 70, y + 16), fill='black')
        d.text((x + 2, y + 2), n[:-4], fill='white')
    sheet.save(os.path.join(out, 'sheet.png'))
PY
echo "$out"
exit "$status"
