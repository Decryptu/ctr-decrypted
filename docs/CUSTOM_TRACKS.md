# How custom tracks load in this fork

This is for CTR Native modders who want to know how CTR Decrypted
(built on `dowlle/ctr-native-ap`) loads community tracks: what a package is, where the
files live, which engine functions the loading path goes through, and what
memory it needs. References are `file: function` in this repository. The
freestanding headers named below carry the full reasoning in their comments,
and most decisions have a host harness under `tools/test-custom-*`.

There are two ways a custom track reaches the engine. Both end in the same
byte-serving hook (section 6).

- **Packages (offline).** A verified, immutable folder per track revision,
  installed from Project Saphi, from the Decrypted track catalogue, from an
  xdelta patch or by hand, and raced from
  extra pages on the normal Arcade and Time Trial track selector. Every build
  with custom tracks has them, the Archipelago player client included (the Mods
  screen), and most of this document describes them.
- **Seed-bound tracks (Archipelago player client only).** An Archipelago seed
  names a track by its LEV and VRM SHA-256 and the Gem Cup it replaces;
  `config.ini [CustomTracks]` says where the two files are.
  `platform/native_custom_tracks.c: CustomTrack_ApplySeedDescriptor` hashes
  them and arms the loader. Without a seed this path never arms. It is
  summarised in section 12.

## 1. Builds and options

All options are in `CMakeLists.txt`.

| Option | What it adds | Needs |
|---|---|---|
| `CTR_CUSTOM_TRACKS` | The loader core: BIGFILE serving, the 8 MiB memory arena, render bounds for larger tracks | nothing |
| `CTR_CUSTOM_PACKAGES` | Package library, the Mods screen, Arcade and Time Trial custom pages, offline loader, Saphi catalogue and install, xdelta import, custom music, custom Time Trial records, 1 MiB SPU memory | `CTR_CUSTOM_TRACKS` |
| `CTR_BOX_AUTHORING` | Box Author Mode, the in-game box placement editor | nothing |
| `CTR_AI_LAP_RECORDER` | The AI lap recorder (always present when `CTR_AP` is on) | nothing |
| `CTR_AUTHORING_CLIENT` | All four of the above, without Archipelago; exe `ctr_native_authoring`, window title `AUTHORING CLIENT (no Archipelago)` | `CTR_AP=OFF` |

With `CTR_AP=ON`, packages follow `CTR_CUSTOM_TRACKS`, so the player client
and the box authoring download (`ctr_native_ap_authoring`, built with
`CTR_AP_AUTHORING`) both have them. Box Author Mode is in both; the box
authoring download keeps it on between starts. Box Author Mode on custom
tracks needs packages (`AP_AUTHOR_CUSTOM` in `ap/ap_author.c`). Every client with packages has the
Mods screen (section 4) and the custom pages for Single Race and Time Trial.
Rooms, Play Solo and Arcade read the same installed copy in the shared track
store.

Every race outside Adventure drives vanilla kart stats, in a room or not:
Arcade (retail and the custom pages), Time Trial, Versus and Battle. Adventure
keeps the room's stats (`ap/ap_race_stats_logic.h`).

An Arcade custom race in a room holds that room (`AP_ArcadeHoldsRoom`): nothing
the race does sends a check or a goal, received items stay queued, and their
effects and a reconnect's reset wait until the race is left. Checks earned before
the race can still go out through their own resend paths.

Linux, from a checkout:

```
bash ap/vendor/fetch-deps.sh
cmake -S . -B build-authoring -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS='-m32 -msse' -DCMAKE_CXX_FLAGS=-m32 -DCTR_AUTHORING_CLIENT=ON
cmake --build build-authoring --parallel 2
```

The package code is C++17 and needs 32-bit OpenSSL and zlib (static
archives are preferred) and, on Linux, standalone asio. JSON is the pinned
nlohmann/json under `ap/vendor/json`, and Unicode handling is the vendored
utf8proc under `vendor/utf8proc`; `cmake/CustomPackageDeps.cmake` refuses to
configure if either differs from its pin. `fetch-deps.sh` fetches the pinned
header-only dependencies. Windows builds with MinGW32 (see
`tools/ci/build-clients.sh windows`) and uses WinHTTP for Saphi.

The engine is one unity translation unit (`main.c` includes
`game/game_unity.h` and the platform files), so every hook below is a guarded
block inside an existing function, not a separate module boundary.

## 2. Where files live

Everything is relative to the assets folder next to the executable
(`platform/native_assets.c: NativeAssets_GetAssetDir`) or to the working
directory, the same place as `config.ini`.

| Path | Written by | Contents |
|---|---|---|
| `assets/tracks/packages/<pin>/` | Saphi install, inbox import | One installed package revision. Never modified after install |
| `assets/tracks/packages/.pending-<pin>-…` | an install in progress | Staging; a failed one is kept for review and never treated as installed |
| `assets/tracks/inbox/<pin>/` | you | A package to import by hand |
| `assets/tracks/mods-off.txt` | the Mods screen | Tracks turned off: one package UUID (or pin, for a package without one) per line. Off tracks stay installed and leave the Arcade and Time Trial pages; seeds ignore the list |
| `assets/tracks/library.catalog` | an external tool, optional | Metadata-only cache (`CTRLB001`), never treated as installed or verified |
| `custom-records/` | Time Trial on a custom track | Best times and ghost per track identity (section 9) |
| `ap-box-placements-authoring.json` | Box Author Mode | Placements on the 18 retail tracks and 7 arenas |
| `ap-box-placements-custom-authoring.json` | Box Author Mode | Placements on custom tracks, keyed by package identity |
| `ap-navpaths/navpath-<level>-<nnn>.navlap` | AI lap recorder | Recorded laps |
| `ctr-authoring.log` | authoring client | Log of the authoring modules (the Archipelago builds write `ctr-ap.log`) |
| `config.ini` | Options menu | `[Authoring]` rows, and `[CustomTracks]` paths for the seed-bound path |

## 3. The package format

A package is a directory named by its **pin**, holding `manifest.json` and the
files it lists. The parser is `platform/native_custom_package.cpp:
CustomPackage_ParseManifest`.

```json
{
  "schema_version": 1,
  "package_uuid": "8-4-4-4-12 lowercase hex, not all zeros",
  "version": "the author's version label",
  "title": "1..160 Unicode codepoints, NFC, no control characters or edge whitespace",
  "compatibility": {"native_contract": 1, "apworld_contract": 1},
  "files": [
    {"role": "lev", "path": "original/lev-123.lev", "sha256": "…", "bytes": 2470836},
    {"role": "vrm", "path": "original/vrm-124.vrm", "sha256": "…", "bytes": 1048576}
  ]
}
```

Rules the parser enforces:

- Exactly these keys, no duplicates, every integer in range.
- Roles and size limits: `lev` (16 MiB), `vrm` (4 MiB), `navigation` (16 MiB),
  `ap_boxes` (4 MiB), `ctr_letters` (1 MiB), `relic_targets` (1 MiB),
  `presentation` (4 MiB), `race_settings` (4096 bytes), `sca` (1 MiB). Each role
  at most once; `lev` and `vrm` are required; 2 to 32 files.
- Paths are relative, compared case-insensitively, and may not collide or nest
  inside each other. Symlinks, reparse points and non-regular files are refused
  when a directory is read (`platform/native_custom_package_files.cpp:
  CustomPackage_AcquireDirectory`).
- Every file's SHA-256 and byte count must match.

**The pin.** The manifest is re-serialised canonically (nlohmann/json compact
dump, keys in sorted order, `files` sorted by role then path) and hashed with
SHA-256. That 64-character lowercase hex digest is the package's pin and its
directory name. A package is only ever asked for by pin, and a pin that does
not match the manifest refuses the whole package. Changing any byte of any
file, the title or the version gives a new pin, so an installed revision is
immutable.

**Identity.** Three different identities are used for three jobs:

- The pin: which exact revision is installed and raced.
- The package UUID plus the LEV and VRM SHA-256: which track is on screen. Box
  placements, Time Trial records and savestate checks use this, so a new
  revision with different geometry never inherits old placements or ghosts.
- `CTR_CUSTOM_LEVEL_ID` (`0x80`, `include/platform/native_custom_identity.h`):
  the level id the engine logs and tests during a custom race, above all 65
  retail level ids. The track never borrows a retail track's identity.

**Sidecars.** `race_settings` is a flat JSON object
`{"schema_version": 1, "lev_sha256", "vrm_sha256", "laps"}` bound to the
package's own LEV and VRM; without it a race runs 3 laps, and more than 7 is
refused (`CustomPackage_GetRaceLaps`, `CustomIdentity_RaceLaps`). A Time Trial
runs these laps, as its records are keyed by them; an Arcade race runs the
count picked in the track select's lap box (`CustomOffline_SetRuntimeLaps`).
`relic_targets` carries sapphire, gold and platinum times in 1/960 s
(`CustomPackage_GetRelicTargets`). `presentation` holds display metadata such
as `metadata/saphi-source.json`. `sca` is the track's music (section 10).

**Structural checks.** Before a package can race, the LEV and VRM are checked
structurally from the owned bytes (`platform/native_custom_content_verify.c`):
an Arcade race needs detected Arcade structures and eight measured spawns, a
Time Trial needs a lap checkpoint graph
(`platform/native_custom_offline.c: CustomOffline_CheckStructure`). A track
that cannot run in a mode is listed dimmed with the reason. Passing these
checks is not a promise that the track plays well; it only rules out what is
known to crash or misbehave.

## 4. The Mods screen and the library

`MODS` on the title screen, or `Options > Mods`, opens the Mods screen
(`game/230/MM_CustomManager.c`, included by `game/230/MM_ConfigMenu.c:
MM_ConfigProc_CustomContent`; the title row is `CTR_LNG_MODS`,
`include/ctr_menu_strings.h`). L1 and R1 switch tabs:

- **Seed** (Archipelago player client, in a room whose content plan names
  custom tracks; the screen opens on it there): `game/230/MM_CurrentSeed.c`,
  the room's tracks and Install missing content (section 12). On connecting
  to such a room with tracks missing, the settled title menu also asks once
  per room: it says how many are missing and, when Project Saphi has them,
  Cross runs the same Install missing content (`ap/ap_seed_prompt.c`, wording
  in `ap_seed_prompt_text`, `ap/ap_seed_content_logic.h`). Triangle puts it
  off until the next room.
- **Tracks**: the Saphi and Decrypted catalogues in one list (section 5),
  `OWNED` on installed tracks.
  Search on R2.
- **Racers**: the custom racer catalogue and the installed racers
  ([CUSTOM_RACERS.md](CUSTOM_RACERS.md#the-racers-tab)).
- **Installed**: one row per track (its newest revision), then the installed
  racers, tagged `RACER`, with the Racers tab's panel and Cross. Cross on a
  track opens its actions: turn it off or on (`assets/tracks/mods-off.txt`),
  Remove (`CustomPackage_RemoveInstalled`: every revision of the track; each pin
  folder is renamed out of the store, then deleted, so a removal cut short is
  never read as a package), and Check files.

The tabs are drawn by the shared tab bar (`game/230/MM_TabBar.c`, layout in
`include/ctr_tab_bar.h`): each tab in its own box, the active one filled. When
the bar is too narrow it shrinks the padding, then uses the short names
(Installed is "Owned"), then shows only the active tab with its place;
`tools/test-tab-bar.c` checks every width. A new tabbed screen uses it too.

Select imports an xdelta patch, also when one is dropped on the window
(`game/230/MM_CustomImport.c`, `platform/native_xdelta_import.cpp`).

- **Track pictures.** Tracks and Installed show the selected track's picture
  at the panel's top left (`game/230/MM_CustomPicture.c`): a package's own
  `metadata/picture.ctp` (role `picture`, 112x42 PS1 texels) if it has one,
  else its Saphi thumbnail. The
  catalogue keeps each track's one current `track_thumbnail`
  (`CustomSaphiRevision.picture`); a track with none, or with two, has no
  picture. A Decrypted catalogue track not installed shows its package's
  `picture.ctp` from the catalogue, whose index lists its size and CRC32
  (`NativeTrackPicture_StartCatalogue`): the bytes must match both, and are
  cached as `assets/tracks/pictures/catalogue-<crc32>.bin`. Once the selection
  has rested on a track for 12 frames, a worker
  (`NativeTrackPicture_Start`, `ap/ap_custom_track_download.cpp`) fetches the
  JPEG, and `platform/native_track_picture.c` decodes it with stb_image
  (`vendor/stb`, JPEG only, at most 1 MB and 2048 pixels a side, checked from
  the header first), crops it to 3:2 and box-filters it to 112x42 PS1 15-bit
  texels. 112x42 looks 3:2 because the 512x216 frame fills a 4:3 screen. The
  texels are cached as `assets/tracks/pictures/<track id>.bin` (`CTP1`, the
  picture's media id, the texels), so a picture seen once shows offline; a
  new media id fetches it again. Saphi serves a smaller copy than the size
  and CRC it lists, so the picture is not checked against the listing; it is
  only ever drawn. The picture goes to the racer VRAM bank at (512, 256),
  page 8 of the second page row (`NativeRacers_TrackPicture`,
  `platform/native_racers.c`). `tools/test-track-picture.c` checks the crop,
  the colours and the refusals on two fixtures of plain colour blocks.

The Mods screen is the only custom content screen; upstream's Options >
Custom Tracks page (`MM_CustomTracks.c`) is not built. The Mods screen also
installs racers, lists the Decrypted track catalogue beside Saphi, and opens
from the title screen, none of which that page does. Fixes to upstream's page
are carried to the Mods screen by hand.

**Import.** The folder list tags each patch file Done, Part or Fail from
`assets/tracks/import-record.json` (found by the patch's SHA-256, so a renamed
copy counts) and the detail panel shows the full file name and what the patch
did before. Confirm opens review; its last row is Import all valid tracks.
Track names come from the pack descriptor, then the curated table of names from
tester feedback (`curated_track_name`, keyed by the patch's SHA-256 and the
track's BIGFILE index), then the known-track table (`known_track_name` in
`platform/native_xdelta_import.cpp`), then a label such as "Lego Adventure
track 3"; Select in the review list types a name. While naming, Left / Right
on the pad (or the arrow keys) move a text cursor through the name. In the
player client an imported track with laps goes into Play Solo's track list at
once (`MM_CustomImport_KeepSoloDescriptors`); one without asks for its laps
there first.

- **Store scan.** `CustomPackage_StartStoreScan` walks
  `assets/tracks/packages/` on a worker thread, re-acquires and re-verifies
  every directory whose name is a lowercase 64-hex pin, and ignores staging and
  unrelated names. Damaged entries stay visible with their reason. The result
  replaces the library in one step (`platform/native_custom_track_library.c:
  CustomTrackLibrary_ApplyStore`).
- **Library.** `struct CustomTrackLibrary` is a UI projection only: title,
  version, author, supported modes and the reason a mode is refused. It never
  authorises a load; the offline loader re-acquires the pinned directory
  itself.
- **Inbox.** A package copied to `assets/tracks/inbox/<pin>/` is verified and
  then published to `packages/<pin>/` (`CustomPackage_InstallInbox`). An
  install never overwrites an existing revision; it re-verifies it instead.

## 5. Project Saphi catalogue and install

The Saphi code is in `ap/ap_custom_track_download.cpp` (it sits under `ap/`
for history; it has no Archipelago dependency) and
`platform/native_saphi_catalogue.cpp`.

- **Refresh.** `CustomSaphi_StartRefresh` pages through
  `https://www.projectsaphi.com/api/v3/tracks?per_page=100&include_standards=0&include_events=0&include_downloads=1&page=N`
  on a worker thread. `CustomSaphi_ParseCatalogue` builds one row per track
  version and requires exactly one LEV and one VRM per version and mode
  signature; ambiguous rows stay visible but disabled. HTTPS only: WinHTTP on
  Windows, asio with OpenSSL on Linux, which looks for the system CA file
  itself because a static OpenSSL carries the build host's default paths.
- **Install.** `CustomSaphi_StartInstall` downloads the selected revision's
  LEV, VRM and, when Saphi lists exactly one current one, its `.sca`, from
  `/api/v3/tracks/<id>/downloads/<media id>` (8 MiB cap per file). Each
  download must match the byte count and CRC32 the catalogue listed. The
  installer then builds a package: SHA-256 for every file, a
  `presentation` file `metadata/saphi-source.json` recording track id, media
  ids, version and author, a `race_settings` file when the track lists a lap
  count (current revisions only), and a manifest whose UUID is a UUIDv5 of
  `https://www.projectsaphi.com/tracks/<track id>`, so every revision of one
  Saphi track shares a UUID. The package goes through the same acquisition and
  structural checks as any other and is published under its pin.
- Older versions of a track can be listed and installed side by side; the
  manager marks a newer Saphi version as "Update available" and never replaces
  an installed revision.

### The Decrypted track catalogue

[Decryptu/ctr-decrypted-tracks](https://github.com/Decryptu/ctr-decrypted-tracks)
is a second source, so a track can be played without waiting for Saphi to list
it. It works like the racer catalogue ([CUSTOM_RACERS.md](CUSTOM_RACERS.md)).

- **The repository.** `tracks/<slug>-<first 8 of the UUID>/` holds one package
  exactly as installed, plus `listing.json` (`authors`, `source`, `credits`,
  `modes`, `saphi_track_id`). A folder is the track's newest version; git keeps
  older ones. `tools/tracks/build_index.py <catalogue> [<package> <authors>
  <source> ...]` adds packages, checks every folder and writes `index.json` and
  `CREDITS.md`. It refuses a track with no authors. `.gitattributes` keeps
  `manifest.json` and the metadata byte for byte, as `manifest.json`'s hash is
  the pin.
- **Refresh.** After Saphi's pages, the same worker reads `index.json` from
  `raw.githubusercontent.com`, the repository's `main` branch.
  `CustomSaphi_AddDecrypted` adds its rows to the catalogue, all or nothing.
  Either source alone is a usable list: one being down hides only its tracks.
- **Install.** `manifest.json` is checked against the pin in the index, then
  each file it lists goes through `CustomPackage_AcquireBuffers` like any other
  package. The package's `metadata/decrypted-source.json` names the author.
- **The same track on both.** A catalogue track is Saphi's when its
  `saphi_track_id` names a listed track, or when a Saphi revision has the same
  LEV and VRM size and CRC32 (an upload of the same files needs no index
  change). Set `saphi_track_id` when an upload's bytes differ. A title is never
  enough: anyone may upload their own Figure-8 Circuit.
  - Same version on both: the catalogue row is dropped, unless Saphi's cannot
    be downloaded. Saphi's row has the picture and the version list.
  - Different versions: the rows pair as versions of one track, and the newest
    is what the list shows.
  - Installed: a copy from either source counts for the other's row. The same
    version is `OWNED`; a newer one is "Update available"
    (`MM_CustomSourceInstalled`). A seed package with a different room UUID
    also counts as owned when both LEV and VRM SHA-256 hashes match. The index
    builder includes these hashes; older indexes without them retain ID and
    version matching. Hashes pass to a paired Saphi row only when both files
    have matching sizes and CRC32. Existing source-ID matches keep their music
    update checks.
  - The two sources have different package UUIDs, so after an update from one
    source the other's copy is removed, or the track would be listed twice
    (`MM_CustomSourceRemoveTwins`). Time Trial records follow the UUID, so that
    track's records start over. A seed's own install from Saphi does not do
    this: a Decrypted copy stays beside it until it is removed by hand.
- A catalogue track's picture is its package's `metadata/picture.ctp`,
  listed in `index.json` (`picture`: size and CRC32), unless it pairs with a
  Saphi track that has one.

To test without publishing, `CTR_DEV_TRACK_CATALOGUE=<folder>` reads the
catalogue from a local copy, with that folder's `saphi.json` (if any) standing
in for Saphi's pages ([DEV-TOOLS.md](DEV-TOOLS.md)).

## 6. Racing a package: the loading path

Custom tracks race only as a one-player Arcade single race or a one-kart Time
Trial. The package's LEV and VRM are served through the BIGFILE subfile group
of one arcade slot, the **host slot** (level 6, Roo's Tubes,
`game/230/MM_CustomTrackSelect.c: MM_CUSTOM_ARCADE_HOST_LEVEL`). The BIGFILE
index cannot grow and every race-mode rule in the engine is a range test on
`levelID`, so borrowing a slot is the loading mechanism. Everything
identity-related goes through `CTR_CUSTOM_LEVEL_ID` instead (section 3).

In order:

1. **Pick.** `game/230/MM_TrackSelect.c: MM_TrackSelect_Init,
   MM_TrackSelect_MenuProc` add custom pages after the retail page
   (`game/230/MM_CustomTrackSelect.c`). Left and right change pages. A custom
   page is laid out like the retail one, with nothing added: the curved rows,
   SELECT LEVEL over the track's picture (the Mods screen's, stretched over the
   video frame), the track's minimap under it, and the page name and number
   under the map, queued before it so they draw on top. A track that cannot
   run in this mode is dimmed; Cross on it says why in the picture frame.
   The minimap is the race HUD's, read from the package before it loads
   (`platform/native_track_minimap.c`): the LEV's icons 3 and 4 through
   `levTexLookup`, their texels from the VRM through the palette, uploaded to
   the racer VRAM bank at (640, 256) (`NativeRacers_TrackMinimap`). A converted
   track points both icons at its whole map; it is kept once.
   `tools/test-track-minimap.c` checks the reading.
2. **Prepare.** Cross starts `CustomOffline_StartPrepare`
   (`platform/native_custom_offline.c`): a worker re-acquires
   `packages/<pin>/` into memory and verifies it. The game never reads the
   package directory again after this, so a file changed on disk mid-race
   cannot reach the engine. `CustomOffline_CheckStructure` then checks the
   mode.
3. **Begin.** In Arcade the retail lap box opens once the track is prepared,
   and its Cross begins; Back closes it and drops the prepared track. A Time
   Trial begins as soon as the track is prepared. `CustomOffline_BeginRuntime`
   takes the host slot and mode, the Arcade lap count replaces the package's,
   and the normal race load for the host slot starts. In the Archipelago box
   authoring download this is refused while a seed is loaded
   (`AP_CustomOfflineLaunchAllowed`), in the player client only while a pad
   race owns the loader, and in the authoring client it is always allowed
   (`ap/ap_authoring_host.c`).
4. **Load request.** `game/MAIN/MainRaceTrack.c: MainRaceTrack_RequestLoad`
   tells the runtime a load was requested; a retry of the host slot keeps it.
5. **Load start.** `MainRaceTrack_StartLoad` refuses the load (back to the
   main menu, with a log line) if the mode or files do not fit, applies the
   runtime's laps, and ends the custom runtime if the level being loaded is
   not the host slot. The custom identity therefore lasts exactly as long as
   the custom geometry is resident.
6. **Bytes.** `game/LOAD/LOAD_File.c: LOAD_ReadFile_ex` asks
   `MainRaceTrack_OfflineRuntimeFile` for every subfile read. For the host
   slot's group it returns the VRM for even and the LEV for odd subfiles, takes
   the size from the package, and fills the sector-rounded buffer with
   `CustomOffline_ReadRuntimeFile`, zero-padding the tail and raising the same
   completion callback a CD read would. A track's single LEV and VRM pair is
   served for all four mode pairs of the group
   (`include/platform/native_custom_tracks_policy.h`, decision 1). There is no
   LEV parsing: the file's own pointer map goes through the normal
   `LOAD_RunPtrMap` fixup.
7. **Level setup.** `game/LOAD/LOAD_TenStages.c: LOAD_TenStages` names the
   level `custom` instead of the host slot's debug name.
   `game/MAIN/MainInit.c: MainInit_PrimMem` raises the per-frame primitive
   arena for a custom load, and `MainInit_JitPoolsNew` reserves the LEV's own
   instance count on top of the retail instance pool
   (`ap/ap_instance_pool_logic.h`), because authored instances leave the free
   list without entering the taken list.
8. **Race.** Identity-keyed reads use the custom identity: AI difficulty is the
   median of the 18 retail rows (`CTR_CUSTOM_DIFFICULTY_PARAMS1/2`), the start
   banner shows the package title (`game/UI/UI_RaceFlow.c`), Roo's Tubes
   bubbles and ambience are off (`game/231/RB_Bubbles.c: RB_Bubbles_RoosTubes`,
   `game/HOWL/HOWL_LevelAudio.c: Level_AmbientSound`), and the high-score
   pointer is a private table (`game/GAMEPROG.c: GAMEPROG_GetPtrHighScoreTrack`).
9. **Finish.** `game/PlayLevel.c: PlayLevel_UpdateLapStats` reports the finish
   (`MainRaceTrack_OfflineRaceFinished`). Time Trial results go to
   `custom-records/` (`game/MAIN/MainFrame.c: MainFrame_GameLogic`,
   `game/MAIN/MainGameEnd.c`), never to the memory card.

## 7. Memory: what a custom track needs

Retail CTR fits a track into a PlayStation memory budget that community
tracks often exceed. `CTR_CUSTOM_TRACKS` changes these, and only these, for
every load in that build:

- **8 MiB MEMPACK arena.** The build sets
  `CTR_NATIVE_MEMPACK_RETAIL_PRESSURE=0`, which selects an 8 MiB arena in
  `platform/native_memory.c` instead of the retail-pressure window of
  `0x144e10` bytes (about 1.27 MiB) inside a 2 MiB store. The first event
  track's resident payload alone was 2,470,836 bytes. The allocator, the
  sector arithmetic in `LOAD_ReadFile_ex` and the pointer-map fixup were
  audited for 32-bit headroom (`tools/CUSTOM-TRACK-SPIKE.md`). Upstream
  `CTR-tools/ctr-native` removed this expanded arena in commit `56326417f`
  ("refactor(memory): enforce retail mempack budget"), so this loader does not
  work on upstream as it stands; a port would have to bring the arena back.
- **Primitive arena.** Retail sizes each frame's primitive arena by level id,
  as `u8 << 10`, at most 261,120 bytes. A custom load gets a floor above that
  (`game/MAIN/MainInit.c`, decision 8 in the policy header).
- **Bounded emitters.** `game/DrawSky.c: DrawSky_Piece, DrawSky_Full` and
  `game/RenderStars.c: RenderStars` take their counts from the level file and
  had no bound, so they now stop when the arena is full instead of writing
  past it (decision 7). `game/226/226_00_DrawLevelOvr1P.c:
  DrawLevelOvr1P_AppendRenderedQuadBlock` stops at the end of the 256-entry
  rendered-quadblock array (decision 9), and `game/RenderLevel/RenderLists.c:
  RenderLists_PushChild` counts dropped BSP records. Each custom level's run
  logs a `[CustomTracks] level ...` report of these at the next level load (and
  at the end of a dev run), with the list's peak use.
- **SPU memory** (packages only). `include/platform/native_spu_memory.h`
  doubles the emulated SPU RAM to 1 MiB so a track's own sound bank fits next
  to the retail banks. The engine's u16 SPU addresses switch from 8-byte to
  16-byte units; every retail bank lands at the same byte address.

Known side effects of the larger arena, both recorded in
`tools/CUSTOM-TRACK-SPIKE.md`: quads whose retail PSX address cannot be
reconstructed get a fallback z-sort index (visual only), and savestate pointer
registration slows down on very large LEVs.

## 8. Savestates

A savestate stores memory, VRAM and engine state, but not the offline
runtime. Each checkpoint therefore records which package was loaded (its UUID,
LEV and VRM SHA-256 and mode, `include/platform/native_custom_state_identity.h`)
and a restore is refused unless it matches the live one
(`platform/native_checkpoint.c: NativeCheckpoint_Capture,
NativeCheckpoint_Restore`). A refused restore shows a message in the race HUD.

## 9. Time Trial records

`platform/native_custom_records.c` keeps a custom track's best lap, five best
times and one ghost in `custom-records/<uuid>-<lev 16 hex>-<vrm 16 hex>-<laps>l.times`
and `.ghost`, with `-200cc` before the extension for a 200cc run
([SPEED-CLASS.md](SPEED-CLASS.md)). Both files repeat the full identity and
class and are refused if they differ, so a new revision, lap count or class
starts a fresh table. `game/TimeTrialRecords.c` holds the game side, shared
with 200cc retail Time Trials: the custom Time Trial page loads the ghost when
the race starts (`TimeTrialRecords_BeginFromTrackSelect`), and
`game/MAIN/MainGameEnd.c: MainGameEnd_CheckTimeTrialGhost` saves a new one
(`TimeTrialRecords_SaveGhost`).

## 10. Custom music

Saphi ships a track's audio as a separate `.sca` file: a container with a
retail level sound bank (`BANK`), a retail CSEQ song pack (`CSEQ`), per-sample
SPU sizes (`SIZE`) and display metadata (`META`). The layout was derived from
Saphi's files; there is no public specification
(`include/platform/native_custom_music.h`). During a custom race
`game/LOAD/LOAD_Howl.c: LOAD_HowlSectorChainStart` serves the level bank
sectors from the package (`game/HOWL/HOWL_CustomMusic.c:
HOWL_CustomMusic_ServeSectors`), and `game/HOWL/HOWL_Music.c:
Music_AsyncParseBanks` takes the level bank and song from the `.sca`
(`HOWL_CustomMusic_LoadBank`, `HOWL_CustomMusic_SetSong`). A missing or refused `.sca` keeps the
default: Crash Cove's song and sound bank. A package imported from an xdelta
patch has no `.sca`; it falls back to the retail bank and song of the arcade
track whose BIGFILE group holds the patched LEV
(`CustomMusic_ReplacedLevel`, set by the custom page), or Crash Cove's when
that is not one of the 18 arcade tracks.

**Where the `.sca` comes from.** The Track Manager install stores it with the
package (role `sca`, section 5). The Seed tab's install into the
shared package store (`ap_custom_track_download_start_shared`) also fetches the
one current `.sca` Saphi lists for the same version as the LEV, up to 1 MiB, and
pins it in the package manifest as `original/sca-<id>.sca`. Music is optional:
a missing, ambiguous, oversized or failed `.sca` installs the track without it.
A seed does not pin the `.sca`; the package manifest does, and the race reads
the file only when its size and SHA-256 match that pin
(`CustomTrack_UseContentMusic`). Tracks installed before this change have no
`.sca` in the store; install them again to add it.

**Which races play it.** The Arcade and Time Trial custom pages, and a
content-plan race (a room pad or cup leg) on the package's own host
slot. Cortex Vortex keeps its bundled music. A content-plan package without a
pinned `.sca` plays its host slot's own music, as before.

**Sound memory.** Builds with `CTR_CUSTOM_PACKAGES`, which includes the
player client built with `CTR_CUSTOM_TRACKS`, have 1 MiB of SPU memory
(`include/platform/native_spu_memory.h`). Before a `.sca` replaces the level
bank, `CustomMusic_Admit` checks that it fits next to bank 0, bank 54 and the
character banks; if not, the race keeps the default music and logs
`[CUSTOM MUSIC] track music refused, default music`.

## 11. Box Author Mode and the AI lap recorder

**Box Author Mode** (`ap/ap_author.c`) places Archipelago item box locations.
Turn it on at `Options > Authoring > Box Author Mode`, then race:

- Numpad 9 or Select drops a placement at the kart, Numpad 0 or Select + L1
  removes the newest one on this track, Numpad . or Select + R1 saves and lists
  them in the log.
- Retail tracks and arenas write `ap-box-placements-authoring.json`: one line
  per placement with `level_id`, `level`, `pos` and `rot_y`, in LEV InstDef
  units (signed 16-bit world units; `0x1000` is a full turn). It starts from the
  241 placements built into the client (`ap/ap_placements_data.h`).
- Custom tracks write `ap-box-placements-custom-authoring.json`
  (`ap/ap_author_custom.h`), keyed by package UUID plus LEV and VRM SHA-256.
- Markers use the level's weapon crate model, or the Archipelago logo mesh on
  levels without one (`ap/ap_marker_model.c`). Their height comes from the same
  crate measurement the Archipelago client uses (`ap/ap_box_measure.c`,
  `ap/ap_box_spawn_pos.c`), so a marker stands where the real box will.

Nothing in the authoring client reads these files back for gameplay. The
Archipelago client turns placement N on a track into that track's Nth item box
location; a fork can use the same files for its own purposes.
`docs/BOX_AUTHORING.md` covers the format and the editor in more detail.

**AI lap recorder** (`ap/ap_navrec.c`). `Save AI Lap Recordings` banks the
laps you drive and writes the best ones to `ap-navpaths/` when the race ends; `Use
Recorded AI Laps` hands recorded laps to the bots in place of the level's own
paths. The file format is in `tools/navrec/FORMAT.md`. Recording and playback
are blocked on custom-page races, because those have no recording identity
yet.

In the authoring client these modules run from
`ap/ap_authoring_host.c: Authoring_OnFrame`, called from
`game/MAIN/MainMain.c: main` in the same order the Archipelago client uses.

## 12. The seed-bound path (Archipelago only)

For completeness: in the Archipelago player client a seed's slot_data carries
a custom track descriptor (laps, host slot, the cup it replaces, the LEV and
VRM SHA-256). `ap/ap_hooks.c` passes it to `CustomTrack_ApplySeedDescriptor`,
which hashes the two files named in `config.ini [CustomTracks]` and arms
`CustomTrack_GetOverride`, the older sibling of the offline hook in
`LOAD_ReadFile_ex`. A refusal is total: no custom bytes and the cup keeps its
four retail legs. That path is not in the authoring client.

Content-plan seeds (0.3.0 development) admit any complete package descriptor:
track key, LEV and VRM pins, authored laps and measured capability flags
(`time_trial`, `arcade`, `relic`, `ctr_tokens`, `wumpa`). The two compiled Baby
T Park revisions are looked for in their manager folders first. Every package
is found by SHA-256 and size in `assets/tracks/<track key>/original/`,
whatever the file names, so several revisions of one track can share the
folder. When that folder has no LEV or VRM at all, the package store is
searched too: a `packages/<pin>/` whose `manifest.json` names both digests is
hashed, and the first whose bytes and sizes match is used
(`platform/native_custom_track_manager.c: Manager_FindInPackages`), so a track
installed from Tracks needs no second download. The Seed tab's Install
missing content downloads into the package store too
(`ap/ap_custom_track_download.cpp: ap_custom_track_download_start_shared`),
under the room descriptor's package UUID, revision, title, author, laps and
relic times, so Installed lists it with Off, Remove and Check files. It
downloads the compiled profiles (Baby T Park) and any track the live track
registry names by Saphi download ids; any other track of a room is added from
Tracks or an xdelta patch. On connect and
again at pad entry the client runs the structural verifier on those exact
bytes (`ap/ap_content_capability.h`) and refuses the package when a flag the
descriptor sets to true is not there; the loader then hashes the files once
more before it arms. These loads carry no navigation identity, so recorded AI
lanes are blocked for them.

### Where a missing track comes from

`include/platform/native_track_sources.inc` lists, by LEV SHA-256, the Project Saphi download or the xdelta patch (file name, patch SHA-256, slot, public page) each known track was published in. `include/platform/native_track_sources.h` reads it. The Seed tab and the seed preflight use it to say "Download from Project Saphi" (and to offer the existing download when the live registry does not list the track) or "Import <patch file> (slot N) from <page>". The first row for a LEV wins: Saphi rows come first, and a LEV that two patches share names the pack players most likely have, with the other pack after "Also in". The table holds no track bytes, rehosts nothing, and never decides what installs: the room's pinned hashes still do. A LEV that is not listed keeps the plain "put the LEV and VRM in tracks/<key>/original" text.

## 13. The authoring-client branch

`tools/authoring-branch/make-branch.py <release tag>` produces a branch that
is that release plus one commit: the Archipelago-only sources under `ap/` are
removed, the authoring client is the default build, and harnesses that need
the removed files are skipped. It is regenerated per release and never edited
by hand. `#ifdef CTR_AP` blocks remain in shared engine files; they compile to
nothing.

## 14. License

The code is GPL-3.0, the same as upstream CTR Native. If you copy this loader
into your fork, your fork's distribution must also follow GPL-3.0. The
Archipelago logo used as a fallback marker is CC BY-NC 4.0; see
`THIRD_PARTY_NOTICES.md` for the attributions that must stay with it.
