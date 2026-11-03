# Working on CTR Decrypted

Everything a new contributor needs. [ROADMAP.md](../ROADMAP.md) has what is in progress and planned; [UPSTREAM-ISSUES.md](UPSTREAM-ISSUES.md) summarizes the projects we draw from.

## What this is
A fork of [dowlle/ctr-native-ap](https://github.com/dowlle/ctr-native-ap) (the native CTR port, CTR-native, with an Archipelago client compiled in), plus enhancements ported from [High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane) and fixes for bugs reported upstream. The game reads assets from the player's own NTSC-U disc image (`assets/ctr-u.bin`); nothing copyrighted is in the repo.

## Repository layout (main parts)
| Path | Contents |
|---|---|
| `game/` | Decompiled game code (unity build via `game/game_unity.h`). Archipelago hooks are `#ifdef CTR_AP` blocks |
| `platform/native_*.c` | PC layer: OpenGL renderer, software SPU audio, input, config, disc reading |
| `platform/icon/` | The game's icon (Ripper Roo's portrait, 64x64 pixel art in `icon.png`). `python tools/decrypted/make_icon.py` rebuilds from it the exe icon (`ctr.ico`, via `ctr.rc`), the Linux window icon (`native_icon_pixels.h`) and the README image |
| `ap/` | Archipelago client and game hooks (`ap_hooks_*.c`). Dependencies are fetched and verified by `ap/vendor/fetch-deps.sh` |
| `apworld/ctr/` | The Archipelago world that generates CTR seeds. `python apworld/ctr/tools/build_apworld.py <out>.apworld` packs it; tests are under [Testing](#testing) |
| `apworld/fuzz/` | Fuzz settings for the world: arms (`arms.txt`), option metas, overlays, the Universal Tracker hook |
| `include/` | Shared headers, structs, macros (`include/macros.h` holds the frame-rate macros) |
| `externals/SDL` | SDL 3.4.10, vendored, static |
| `externals/libusb` | libusb 1.0.30 Windows backend, vendored, for Switch 2 controllers |
| `tools/decrypted/` | Our own scripts: upstream sync and status, unattended runs, render diagnostics, save and Archipelago progress editing |
| `tools/racers/`, `tools/tracks/` | Converters for custom racers ([CUSTOM_RACERS.md](CUSTOM_RACERS.md)) and for Mario Kart DS courses ([TRACK-CONVERTER.md](TRACK-CONVERTER.md)) |
| `docs/` | Project docs |
| `site/` | The project site ([Site](#site)) |

The root and `tools/` keep dowlle's layout, including files we don't use (`build.bat`, `tools/authoring-branch/`, `tools/CUSTOM-TRACK-SPIKE.md`): moving or deleting an upstream file would conflict on every sync. `support-bundle.*`, `default_controls.ini`, `ap-config.example.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md` and `SETUP.md` ship in every client package (`tools/ci/package-client.py`). Our additions live in `tools/decrypted/`, `tools/racers/`, `tools/tracks/`, `docs/` and `ROADMAP.md`. Build folders are `build-*/` (ignored); the developer tools write to `%TEMP%\ctrdev`.

## Build (Windows)
1. Install [MSYS2](https://www.msys2.org) at `C:\msys64`, then in a shell:
   `pacman -S --needed git mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja mingw-w64-i686-openssl mingw-w64-i686-zlib mingw-w64-i686-python`
2. From a **MINGW32** shell at the repo root:
   ```
   ap/vendor/fetch-deps.sh
   cmake -S . -B build-ap -G Ninja -DCTR_AP=ON -DCMAKE_BUILD_TYPE=Release
   cmake --build build-ap
   ```
   The output is `build-ap/ctr_native_ap.exe`. Without `-DCTR_AP=ON` you get the plain game (`ctr_native.exe`).
3. To run it, put the exe next to an `assets/ctr-u.bin`.

From PowerShell, set `MSYSTEM=MINGW32` and `CHERE_INVOKING=1` and call `C:\msys64\usr\bin\bash.exe -lc "<command>"`.

### Windows gotchas
- **Line endings.** Text files are stored with LF (`* text=auto`). `.gitattributes` forces LF on `*.sh`, `*.py`, `*.yml`, `*.patch` and `*.lock`, since the vendor patch hashes depend on it. `fetch-deps.sh` clones with your global git config, so if its tree-hash check fails under `core.autocrlf=true`, run it with `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=false`.
- **Path length.** Keep the checkout at a short path. The repo uses `core.longpaths=true`.
- **32-bit only.** The engine still assumes 32-bit pointers.

## Branches
- `main`: stable, always builds.
- `dev`: integration branch. Feature branches merge here first, and test builds come from it. It goes into `main` once a test build has been played (the checks under [Testing](#testing)).
- `feature/<name>`: one feature or fix per branch, merged into `dev` when it builds. Delete it locally and on `origin` once merged; only `main` and `dev` stay.

Upstream refs live outside `refs/remotes/`, so GitHub Desktop and other clients don't list them: `upstream_status.py` fetches into `refs/<remote>/` and the sync script writes `refs/clean/`. Git still resolves `dowlle/main` and `clean/dowlle-main`. Give each upstream remote the fetch line shown below so a plain `git fetch` does the same.

## Syncing upstream
`python tools/decrypted/upstream_status.py` fetches the upstream remotes and lists, for each followed branch (`FOLLOWED` at the top of the script), the newest commit we have and every newer one we lack. It also lists other dowlle and apworld branches with work we don't have, and the High Octane status (below). A commit counts as ours when our history has one with the same author time, email and subject, so rewrites, path filters and cherry-picks are seen through. Add a branch to `FOLLOWED` when we start following it.

Never merge upstream remotes directly. The sync script imports them and strips tool co-author trailers from commit messages. The rewrite is deterministic, so repeated imports give the same hashes and later merges stay clean. It needs `python -m pip install git-filter-repo`.

```
git remote add dowlle https://github.com/dowlle/ctr-native-ap.git      # once
git config remote.dowlle.fetch "+refs/heads/*:refs/dowlle/*"           # once
pwsh tools/decrypted/sync-upstream.ps1                                 # -> clean/dowlle-main
git merge clean/dowlle-main
pwsh tools/decrypted/sync-upstream.ps1 -Remote ctr-native -Branch master
```

The world comes from dowlle's Archipelago fork; we keep only `worlds/ctr`, moved to `apworld/ctr` with its history:

```
git remote add apworld https://github.com/dowlle/ctr-archipelago-apworld.git   # once
git config remote.apworld.fetch "+refs/heads/*:refs/apworld/*"                # once
pwsh tools/decrypted/sync-upstream.ps1 -Remote apworld -Branch dev/custom-pad-0.3.0 -Path worlds/ctr -Prefix apworld/ctr
git merge clean/apworld-dev/custom-pad-0.3.0
pwsh tools/decrypted/sync-upstream.ps1 -Remote apworld -Branch dev/custom-pad-0.3.0 -Path .github/fuzz -Prefix apworld/fuzz -Name apworld-fuzz-dev/custom-pad-0.3.0
git merge clean/apworld-fuzz-dev/custom-pad-0.3.0
```

We follow dowlle's 0.3.0 line: `dev/custom-pad-0.3.0` in both repositories, plus the client branch not merged there yet (`feat/xdelta-track-import`). Import each and merge its `clean/` ref.

**After a merge:**
- Upstream moves functions between files, so a merge can silently drop our edit to a function. Run `python tools/decrypted/merge_audit.py <merge-base> HEAD^1 --moved` (our changes in functions that moved) and again without `--moved` (every added line missing from the merged tree; most hits are renames). Carry each real loss to the function's new home, then build every variant (`tools/ci/build-clients.sh`) and run the harnesses; the `*-production` harnesses check that hooks sit where they must in upstream's code.
- `python tools/decrypted/check_upstream_refs.py` (also in CI) fails on links to dowlle's releases, issues or docs, his repository named in a program or packaging string, and his release key. Point each hit at `Decryptu/ctr-decrypted`, or, when it names something only his repository has, add it to the script's exceptions with the reason.
- His harnesses stub game functions with his headers' signatures, which differ from ours in places. Most are Linux only, so run `gh workflow run ci.yml --ref <branch>` before merging an import into `dev`. On Windows, `cc -m32 -fsyntax-only -Wno-implicit-function-declaration -DCTR_NATIVE -DCTR_AP -I ap -I include -I . <harness>` checks that one compiles against our headers.

High Octane (`highoctane/vita`) is not merged. Cherry-pick its commits with `-x` and resolve conflicts by hand, keeping every `#ifdef CTR_AP` block. `tools/decrypted/highoctane.txt` has one line per High Octane commit not in ctr-native that was ported by hand or in part, already here, done our own way, on the Maybe list, skipped, or `open`; a full cherry-pick needs no line. The status script lists ported commits (`--ho ported`, the default; `--ho open,maybe` for the others) and every new commit with no line yet: review it and add its line.

## Releases and updates
Release steps: [RELEASING.md](../RELEASING.md). Players download the release's `ctr-decrypted-vX.Y.Z-windows-x86.zip` (or the Linux `.tar.gz`), which holds everything except the disc image; the release notes open with a **Download** line naming it. The other assets are for hosts (`ctr.apworld`, the YAML template), crash reports (`.debug`) and box authors.

After that the game updates itself (`ap/ap_update.c`, trust rules in `ap/ap_update_core.h`). At start it reads `https://api.github.com/repos/Decryptu/ctr-decrypted/releases/latest`. A stable `vX.Y.Z` release newer than `CTR_AP_VERSION` (`ap/ap_version.h`), with `manifest.json`, `manifest.json.minisig` and both client archives, is offered in the main menu; nothing downloads until the player accepts. The manifest must verify against the compiled-in key (`AP_UPDATE_PUBLIC_KEY`, same as `ctr-release-minisign.pub`) and the archive must match its hash. Only the client, `ctr.apworld`, `solo-runtime/` and the Cortex Vortex files are installed; settings, saves and the disc image are never touched. So:
- Publishing a release (not a draft or prerelease) offers it to every player. Publish as a prerelease to hold it back.
- A release without the signed manifest, or signed by another key (dowlle's, for example), is never offered.
- Bump `CTR_AP_VERSION` in the release commit; `CTR_AP_COMPAT_VERSION` stays the apworld's line.
- Builds up to v1.2.0 check dowlle's repository and are never offered anything; those players download a newer release by hand once.
- `[Archipelago] update_check = false` in `config.ini` turns the check off.
- The updater is dowlle's (`feat/signed-updater-301`). Ours differs only in the repository paths (`kRepoPath`, `kDownloadPrefix` in `ap_update_core.cpp`), the key (`ap_update_core.h`) and the version passed (`CTR_AP_VERSION` instead of `CTR_AP_COMPAT_VERSION`, in `ap_update.c`). Keep those when a merge conflicts.

Test a release end to end with `CTR_DEV_UPDATE` ([DEV-TOOLS.md](DEV-TOOLS.md)): `.\tools\decrypted\devrun.ps1 -Exe build-ap\ctr_native_ap.exe -RunDir C:\t\updrun -Track -1 -Frames 30000 -Env @{CTR_DEV_UPDATE='v1.1.0'}`, then compare the run folder with the release archive. Host harnesses cover the trust rules (`tools/test-update-core.cpp`, `test-update-menu.c`, `test-update-solo-gate.cpp`, Linux only); `test-update-core.cpp` checks the published v1.2.0 manifest and signature against the compiled-in key.

## Site
[decryptu.github.io/ctr-decrypted](https://decryptu.github.io/ctr-decrypted/) is generated, never edited by hand: `pages.yml` builds it from `main` and publishes it to GitHub Pages.
- Pages are the repository's Markdown files listed in `PAGES` at the top of `site/build.py`; edit the doc, not the site. Links to other listed files become page links, other repository links go to GitHub, images are copied. To give a doc a page, add it to `PAGES` (with a nav title to put it in the header).
- The YAML builder (`site/builder.html`, `site/builder.js`) knows no CTR option itself. `site/dump_options.py` runs inside Archipelago with `apworld/ctr` installed and writes every template option (group, name, help, kind, default, choices or range); the builder turns that into fields. A new option kind fails the dump until `describe()` handles it.
- To build locally, from an Archipelago 0.6.7 checkout with `apworld/ctr` in `worlds/ctr` (the one `apworld-test.ps1` sets up works) and the `markdown` package:

```
cd <archipelago>; python <repo>/site/dump_options.py <tmp>/options.json
cd <repo>; python site/build.py <tmp>/options.json <tmp>/site
python -m http.server -d <tmp>/site
```

## Conventions
- Commit messages are plain and descriptive. No co-author trailers for tools, and no mention of the tools used, in commits, code or docs. Local tool files (`CLAUDE.md`, `AGENTS.md`, `.claude/`) are in `.git/info/exclude` and never committed.
- Credit ported work: `cherry-pick -x` keeps the original commit reference, and README's Credits names each source project. New third-party code gets an entry in `THIRD_PARTY_NOTICES.md`.
- The world in `apworld/ctr` and the client change together: a slot data or item/location change lands in one commit with the client code that reads it, and stays compatible with the shipped `ctr.apworld`. Releases ship the `ctr.apworld` packed from `apworld/ctr` on the release commit (`release-prepare.yml`, [RELEASING.md](../RELEASING.md)).
- Our world's `starting_roster` (default `vanilla_eight`) gives the slot the eight retail Adventure racers as starts, so the Adventure garage runs the retail character select (`AP_CharSwap_GarageRacer` allows it when every retail starter is unlocked) and only the other eight racers are items. The test package pins `starting_character_only` as its default because the upstream suite counts 15 unlock items; `test_starting_roster.py` covers the eight-racer roster.
- Update `ROADMAP.md` in the commit that starts or finishes an item: add its row when work starts, delete it when done. The roadmap and docs describe the current state, never history: no "Done" list, no bug-hunt stories, no dated results. A doc about a fix keeps the rule, why, and how to check it. At the end of a change, grep `ROADMAP.md` and `docs/` for its topic and remove what it made stale.
- Frame counts assume 30 FPS. Wrap a frame-counted timer value in `FPS_DOUBLE`, and per-frame steps or blink phases in `FPS_HALF` (`include/macros.h`), so it keeps its real-time speed at 60 FPS. Anything else a frame does by a fixed amount rather than by `elapsedTimeMS` (a push, a rounding loss) happens twice as often at 60 and needs the same care. Frame-by-frame recordings (ghosts, AI laps) stay in 30 FPS units. See [60FPS.md](60FPS.md).

## Testing
Before `dev` goes into `main`:
- Build the AP and vanilla configurations.
- In game: boot to the menu, one race, the Adventure hub, and connecting to a local Archipelago server (`ArchipelagoServer.exe --port 38281 <seed.zip>` from Archipelago 0.6.7+). Check timing changes with `fps60` on and off (`[Video & QoL]` in `config.ini`, or **OPTIONS → Video → 60 FPS (Next Load)**).
- Reproduce and check visual changes with the [developer tools](DEV-TOOLS.md): `tools/decrypted/devrun.ps1` runs any track's demo hidden, fast-forwarded and bit-exact, and captures frames. Never drive the game with OS keystrokes.

### Workflows
They run on pushes to `dev` and `release/**` and on pull requests, never on `main` (except `pages.yml`): `main` only moves to commits `dev` already tested. A newer push cancels the run on that branch. To run one on another branch: `gh workflow run build.yml --ref <branch>` (same for `ci.yml`). `tools/ci/` has the local runners.
- `ci.yml` builds and runs the host harnesses in parallel, one per CPU (`tools/ci/run-harnesses.py`).
- `build.yml` builds each client variant (ap, vanilla, authoring, authoring-client) on Windows and Linux, one job each; `tools/ci/build-clients.sh <platform> [variant...]` does the same locally. The Linux jobs also run `ctest` (upstream's unit and regression tests). The Windows job checks text out with LF so packaged text files match Linux byte for byte.
  - Release assembly check: packs `ctr.apworld`, builds the solo runtimes and runs `tools/release/ci-assemble.sh` (template `tools/release/ci-template.sh`, the script `release-prepare.yml` runs) on the run's archives as a release of `ap/ap_version.h`'s version. A file added to the client archives that `assemble-release.py`'s member list doesn't know fails here.
  - Cache: ccache per platform and variant, plus the MSYS2 packages. Pull requests read it without saving; tag builds don't use it, so release binaries are compiled from scratch. To keep hits, CI builds without precompiled headers (MinGW GCC writes SDL's differently each time), and the commit hash reaches code only through `platform/native_build_id.c`, never as a define on `main.c` (that changes the whole unity build every commit).
- `apworld.yml` (CTR world) runs on pull requests and pushes that touch `apworld/`, nightly on `dev`, or by hand. It runs the world's pytest suite in Archipelago 0.6.7, packs `ctr.apworld` (the artifact a release ships), and fuzzes that pack (below).
- `pages.yml` (Site) builds the site on pushes to `dev` and pull requests that touch the docs, `site/` or `apworld/ctr/`, and builds and publishes it on such pushes to `main` (Pages source: GitHub Actions).

### Harnesses
- On Windows, run `py -3 tools/ci/run-harnesses.py` with a Python outside the MinGW folder and MinGW's `bin` on `PATH`. MinGW has no UBSan runtime, so the runner builds `tools/ci/nosan-shim.c` as `cc`, `gcc` and `g++`, ahead of the real compilers, to drop `-fsanitize` flags; under MinGW's own Python, Windows finds the real compilers first.
- About 20 harnesses run only on Linux: those that compile with `-fsanitize=undefined` themselves and those using POSIX calls (`symlink`, `mkdir(path, mode)`, `linkat`, `dlfcn`), such as `test-update-core.cpp`, `test-xdelta-import.cpp` and `test-disc-copy.c`. On Windows, compare a branch's failures with its base branch's.
- Harnesses compile game files with `CTR_NATIVE` defined (the default recipe adds it; a harness's own build line must too). Without it the headers take the PS1 branch.
- Each harness compiles real game files on its own, so new code it pulls in needs its declarations included where called (not only through `game/game_unity.h`), and fork-wide globals (`gNative60FpsEnabled`, `gNativePgxpMode`, the `NativeDev_*` switches, `rdata` for MEMPACK names) need a definition in the harness. Linux drops unused code at link time more than MinGW, so a harness can link in CI and fail on Windows.

### World tests and fuzzing
- `pwsh tools/decrypted/apworld-test.ps1` runs the world's pytest suite locally; extra arguments go to pytest (`-k starting_roster`). The first run clones Archipelago and installs its packages into `%LOCALAPPDATA%\ctr-decrypted\archipelago`; later runs copy `apworld/ctr` in and test. On Windows, the symlink tests (`test_launcher_component`, the `TestLinks` cases in `test_solo_runtime`) fail unless Developer Mode is on; they pass in CI.
- `apworld/` is checked out with LF everywhere (`.gitattributes`): its fixtures are pinned by hash and its files go into `ctr.apworld`.
- Fuzz arms are lines in `apworld/fuzz/arms.txt` (Archipelago-index checks plus the content plan, pads, goals, laps and Universal Tracker, metas in `apworld/fuzz/`); `apworld/fuzz/matrix.py` turns it into the job matrix. Each arm has two counts. Pushes and pull requests run the quick set (arms with a push count: about twenty, 5,000 generations); nightly and tier `full` by hand run all arms at full count (about a hundred, 60,000 generations). To add an arm, add a line; give it a push count only if the quick set doesn't already cover its family.
- A failing generation uploads its log as `fuzz-failure-<arm>` next to its YAML. The log starts with `fuzz seed N, PYTHONHASHSEED H` (`apworld/fuzz/log-seed.py` patches the fuzzer to print it). To reproduce in the Archipelago checkout: put the YAML alone in a folder, set `PYTHONHASHSEED=H` and run `python Generate.py --player_files_path <folder> --seed N --skip_output`.
