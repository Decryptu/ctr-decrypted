# Native UI, split screen and movie playback

## Widescreen widgets

Garage bars keep their logical stat lengths. Both `CS_Garage_MenuProc` and the
shared AP garage/hub renderer `AP_StatBar_Draw` compress their drawing widths
with `Widescreen_CompressXAbout`. Each segment's two endpoints use the same
transform; scaling a segment width separately opens rounding gaps. Outlines
and shadows follow the transformed length. Empty bars submit no primitives.

The speedometer background and both needle triangles compress around the
same dial centre. This leaves 4:3 unchanged and keeps the needle inside the
coloured bands at wider aspects. Nested menu boxes leave a 14-pixel native
hierarchy gap so their borders clear the preceding shadow.

Track previews use an opaque RGBA streaming texture, bracketed by texture
override/reset packets in the UI ordering table. Black video pixels remain
opaque. The frame, background and video share an aspect-corrected centre.
The former VRAM preview's tpage side effect is preserved for following sprites.
Do not delete a preview texture while a queued primitive still references it.

Minimap halves share exactly the same join coordinates. Keep the renderer's
UV clamping and texel rounding; do not stretch one half over the other to hide
a sampling error. Levels that request only the bottom half retain that layout.

## Mask music

`Audio_SetMaskSong` selects from live human mask-weapon child threads with
positive duration. Dead threads, mask hints, bots, rescue and engine-revving
states cannot claim the song. Keep the current mask song while that kind of
mask remains active; otherwise choose Aku, then Uka, then the level song.
Every call applies `Music_Adjust` and updates `AKU_SONG`/`UKA_SONG` together.
The action flag alone is insufficient during birth, replacement and expiry.

## Split screen

Native karts render directly rather than through the retail multiplayer decal
cache. Each human's fruit model also renders directly at that player's HUD
position, with its own juiced-up glow and finish-state suppression. The retail
decal/fruit atlas remains in the non-native path.

Scissors intersect the draw area with the display before scaling. Transform
rectangle edges with one rounding rule and derive widths from the transformed
edges; separately scaling an origin and extent loses pixels at fractional
Native scales. Offscreen rendering retains its own coordinate space.

Geometry visibility and retry-list rules are in
[LEVEL-VISIBILITY.md](LEVEL-VISIBILITY.md). Native two-player geometry keeps its
existing emitter: unrestricted geometry PVS and shared draw-distance rules
already cover the upstream PVS workaround. Do not replace it without a
reproduced emitter defect and geometry capture comparisons.

## Scrapbook

The STR decoder uploads top-down RGBA frames to a reusable streaming texture.
Presentation draws an opaque picture into the main target with black borders
and the authored 4:3 aspect, independent of the race FOV. Screen filters,
render scale and direct captures all consume this target. Stop releases the
texture; the presentation pin expires after each frame.

Scrapbook XA sectors are fed from the same reader into a bounded 64-sector
compressed ring. Audio decoding stays out of file I/O and uses the normal
VBlank-driven deterministic mixer in fast runs. A temporary underrun holds
the XA cursor until input resumes. Queue overflow refuses playback instead of
overwriting unread sectors. Normal XA-track playback retains its resident
sector path.

Host streams accept 2048-byte extracted video records, 2336-byte Mode 2
payloads and 2352-byte raw sectors, including leading audio/padding records.
Disc playback reads the Mode 2 payload directly. The reader checks frame
dimensions, chunk counts and payload sizes, skips unrelated records, and
resynchronizes on a new chunk zero after an incomplete frame. Invalid decoded
frames are skipped with bounded recovery. Missing source-frame numbers extend
the previous picture's hold so audio and video keep their timeline.

Playback uses four VBlanks per source frame (15 FPS) at both game frame rates.
Decode time counts toward that deadline. Skip, missing media, natural EOF and
decode refusal return through the normal flag/menu transition and stop XA.

## Checking changes

Use the hidden [developer runner](DEV-TOOLS.md), with explicit scratch folders:

- `CTR_DEV_GARAGE=1` enters the Adventure garage. For the AP hub picker use
  `CTR_DEV_RACE=26`, `CTR_DEV_MODE=adv`, `CTR_DEV_HUB_PICKER=1` and `dev_keys=1`
  in the run folder's `ap-config.txt`; its request waits for any mask hint.
- `CTR_DEV_CHARACTER_SELECT=1` and `CTR_DEV_PAD='2200:X,2500:X'` reach the
  Arcade track preview. Check both icon and video, changing track and leaving.
- `State.hud_speedometer=true` selects the dial for a race capture.
- `CTR_DEV_MASK=1` with `CTR_DEV_RACE`, `CTR_DEV_AUTOPILOT=1` and optionally
  `CTR_DEV_PLAYERS=2` activates masks at 2, 5 and 8 seconds and logs song changes.
- `CTR_DEV_SCRAPBOOK=1` enters the movie directly. Use `CTR_DEV_PAD` to skip,
  or allow all 4,424 source frames to play through EOF.

Compare identical frames at 4:3 and 16:9 in vanilla and AP. Include the AP hub
picker, zero/partial/full bars, resting/moving needle, minimap join and nested
menu shadows. For multiplayer, cover 2P/3P/4P, odd Native window sizes, fixed
scales, juiced-up fruit and results. Geometry changes also require paired
`find_holes.py` counts. For movies, check Original/Native, both game frame
rates, orientation, black borders, filters, audio, skip and natural EOF.

Focused production harnesses are `test-widescreen-ui.c`, `test-mask-song.py`,
`test-render-scale.c`, `test-native-str.c` and `test-interleaved-xa.py` under
`tools/`; the CI harness list runs them. The STR fixtures include missing
chunks, a bad bitstream, interleaved audio, all three host record layouts and
truncated input. The XA harness checks filtering, queue capacity, wrap,
underrun and restart.
