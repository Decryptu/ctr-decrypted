# Working on CTR Decrypted

Everything a new contributor needs, with no other context. Keep it current. [ROADMAP.md](../ROADMAP.md) lists what is in progress and planned, and [UPSTREAM-ISSUES.md](UPSTREAM-ISSUES.md) summarizes the projects we draw from.

## What this is
- A fork of [dowlle/ctr-native-ap](https://github.com/dowlle/ctr-native-ap): the native Crash Team Racing port (CTR-native) with an Archipelago client compiled in.
- On top of that we port enhancements from [High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane) and fix bugs reported upstream.
- The game reads assets from the player's own NTSC-U disc image (`assets/ctr-u.bin`). Nothing copyrighted is in the repo.

## Repository layout (main parts)
| Path | Contents |
|---|---|
| `game/` | Decompiled game code (unity build via `game/game_unity.h`). Archipelago hooks are `#ifdef CTR_AP` blocks |
| `platform/native_*.c` | PC layer: OpenGL renderer, software SPU audio, input, config, disc reading |
| `platform/icon/` | The game's icon: Ripper Roo's character-select portrait on its select tile, 64x64 pixel art in `icon.png`. `python tools/decrypted/make_icon.py` rebuilds the exe icon (`ctr.ico`, linked through `ctr.rc` on Windows, where SDL gives it to the window too), the Linux window icon (`native_icon_pixels.h`) and the README image from it |
| `ap/` | Archipelago client and game hooks (`ap_hooks_*.c`). Dependencies are fetched and verified by `ap/vendor/fetch-deps.sh` |
| `apworld/ctr/` | The Archipelago world (generator) that makes CTR seeds. `python apworld/ctr/tools/build_apworld.py <out>.apworld` packs it; its tests (`apworld/ctr/test/`) run inside an Archipelago 0.6.7 source checkout (`tools/decrypted/apworld-test.ps1`; [Testing](#testing)) |
| `apworld/fuzz/` | The fuzz settings for the world: the arms (`arms.txt`), option metas, overlays and the Universal Tracker hook that `.github/workflows/apworld.yml` runs |
| `include/` | Shared headers, structs, macros (`include/macros.h` holds the frame-rate macros) |
| `externals/SDL` | SDL 3.4.10, vendored, static |
| `externals/libusb` | libusb 1.0.30 Windows backend, vendored, for Switch 2 controllers |
| `tools/decrypted/` | Our own scripts (upstream sync and status, unattended runs and render diagnostics) |
| `docs/` | Project docs |

The root and `tools/` keep dowlle's layout, including files we don't use ourselves (`build.bat`, `tools/authoring-branch/`, `tools/CUSTOM-TRACK-SPIKE.md`). We merge dowlle regularly, and every upstream file we moved or deleted would conflict on each sync and break upstream's own references to it. The root files are not strays: `support-bundle.*`, `default_controls.ini`, `ap-config.example.txt`, `LICENSE`, `THIRD_PARTY_NOTICES.md` and `SETUP.md` ship in every client package (`tools/ci/package-client.py`). Our own additions live in `tools/decrypted/`, `docs/` and `ROADMAP.md`. Build and run output stays out of the tree: build folders are `build-*/` (ignored), and the developer tools write to `%TEMP%\ctrdev`.

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
3. To run it, copy the exe next to an `assets/ctr-u.bin`.

From PowerShell, set `MSYSTEM=MINGW32` and `CHERE_INVOKING=1` and call `C:\msys64\usr\bin\bash.exe -lc "<command>"`.

### Windows gotchas
- **Line endings.** Every text file is stored with LF (`* text=auto` in `.gitattributes`), so any git, MSYS2's included, compares a CRLF checkout cleanly. With `core.autocrlf=true`, checked-out files get CRLF. `.gitattributes` forces LF on `*.sh`, `*.py`, `*.yml`, `*.patch` and `*.lock`; the vendor patch hashes depend on it. `fetch-deps.sh` clones dependencies with your global git config, so run it with `GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=core.autocrlf GIT_CONFIG_VALUE_0=false` if the tree-hash check fails.
- **Path length.** Keep the checkout at a short path. The repo uses `core.longpaths=true`.
- **32-bit only.** The engine still assumes 32-bit pointers.

## Branches
- `main`: stable, always builds.
- `dev`: integration branch. Finished feature branches are merged here first, and test builds for in-game testing come from it. It goes into `main` once a test build has been played in game (the checks under [Testing](#testing)).
- `feature/<name>`: one feature or fix per branch, merged into `dev` when it builds, then to `main` with `dev` after in-game testing. Delete it locally and on `origin` once merged; only `main` and `dev` stay.

Upstream branches never show up as branches: `upstream_status.py` fetches them into `refs/<remote>/` and the sync script writes `refs/clean/`, both outside `refs/remotes/`, which is all GitHub Desktop and other clients list. Git still resolves `dowlle/main` and `clean/dowlle-main`. Give each upstream remote the same fetch line, so a plain `git fetch` keeps them there too (below).

## Syncing upstream
`python tools/decrypted/upstream_status.py` shows where we stand. It fetches the upstream remotes, then gives, for each branch we follow (the `FOLLOWED` list at the top of the script), the newest commit we have and every newer one we lack. It also lists the other dowlle and apworld branches that hold work we don't have, and the High Octane summary below. A commit counts as ours when our history has one with the same author time, author email and subject, so it sees through the sync script's rewrite, the apworld path filter and cherry-picks. Add a branch to `FOLLOWED` when we start following it.

Never merge upstream remotes directly. Import them through the sync script, which rewrites commit messages to strip AI co-author trailers. The rewrite is deterministic, so repeated imports produce the same hashes and later merges stay clean.

```
git remote add dowlle https://github.com/dowlle/ctr-native-ap.git      # once
git config remote.dowlle.fetch "+refs/heads/*:refs/dowlle/*"           # once
pwsh tools/decrypted/sync-upstream.ps1                                 # -> clean/dowlle-main
git merge clean/dowlle-main
pwsh tools/decrypted/sync-upstream.ps1 -Remote ctr-native -Branch master
```

The world comes from dowlle's Archipelago fork, of which we keep only `worlds/ctr`, moved to `apworld/ctr` with its history:

```
git remote add apworld https://github.com/dowlle/ctr-archipelago-apworld.git   # once
git config remote.apworld.fetch "+refs/heads/*:refs/apworld/*"                # once
pwsh tools/decrypted/sync-upstream.ps1 -Remote apworld -Branch dev/custom-pad-0.3.0 -Path worlds/ctr -Prefix apworld/ctr
git merge clean/apworld-dev/custom-pad-0.3.0
pwsh tools/decrypted/sync-upstream.ps1 -Remote apworld -Branch dev/custom-pad-0.3.0 -Path .github/fuzz -Prefix apworld/fuzz -Name apworld-fuzz-dev/custom-pad-0.3.0
git merge clean/apworld-fuzz-dev/custom-pad-0.3.0
```

We follow dowlle's 0.3.0 line: `dev/custom-pad-0.3.0` in both repositories, plus the client feature branch not merged there yet (`feat/xdelta-track-import`). Import each and merge its `clean/` ref.

The script needs `python -m pip install git-filter-repo`.

Upstream moves functions between files, so a merge can drop our edit to a function without any conflict. After resolving, run `python tools/decrypted/merge_audit.py <merge-base> HEAD^1 --moved` (our changes in functions that now live in another file) and without `--moved` (every line we added that is missing from the merged tree; most hits are renames). Carry each real loss into the function's new home, then build every variant (`tools/ci/build-clients.sh`) and run the harnesses: the `*-production` harnesses check that hooks sit where they must in upstream's code.

A merge from dowlle brings his identity back with it. Two checks catch that:
- `python tools/decrypted/check_upstream_refs.py` (also a harness line in CI) fails on links to his releases, issue tracker or docs, on his repository named in a string in the program or the packaging scripts, and on his release key. Point each hit at `Decryptu/ctr-decrypted`, or, when it names something only his repository has (an issue, his own note), add it to the script's exceptions with the reason.
- A harness he adds stubs game functions with the signatures in his headers, and ours differ in places. CI's host harnesses catch it as a build failure. Most of his new harnesses are Linux only, so before merging an import into `dev`, run `gh workflow run ci.yml --ref <branch>`. On Windows, `cc -m32 -fsyntax-only -Wno-implicit-function-declaration -DCTR_NATIVE -DCTR_AP -I ap -I include -I . <harness>` checks that one compiles against our headers.

High Octane (`highoctane/vita`) is not merged. Port its commits by cherry-picking with `-x` and resolve conflicts by hand, keeping every `#ifdef CTR_AP` block. `tools/decrypted/highoctane.txt` records what we did with each of its commits that is not in ctr-native: ported by hand, already here, our own version, on the roadmap's Maybe list, skipped, or not decided (`open`). A full cherry-pick needs no line; a hand port, or a cherry-pick of only part of a commit, does. The status script lists the commits we ported (`--ho ported`, the default; `--ho open,maybe` for the others) and every new High Octane commit that has no line yet: review it and add its line.

## Releases and updates
Players download the Windows archive of the [latest release](https://github.com/Decryptu/ctr-decrypted/releases/latest) (`ctr-decrypted-vX.Y.Z-windows-x86.zip`; Linux has the `.tar.gz`). It holds everything the game needs except the disc image. The other assets are for hosts (`ctr.apworld`, the YAML template), crash reports (`.debug`) and box authors. The release notes open with a **Download** line naming that archive. The release steps are in [RELEASING.md](../RELEASING.md).

After that, the game updates itself (`ap/ap_update.c`, trust rules in `ap/ap_update_core.h`). At start it reads `https://api.github.com/repos/Decryptu/ctr-decrypted/releases/latest`. When that is a stable `vX.Y.Z` release newer than `CTR_AP_VERSION` (`ap/ap_version.h`), with `manifest.json`, `manifest.json.minisig` and both client archives, the main menu offers it. Nothing downloads until the player accepts. The manifest must verify against the key compiled in (`AP_UPDATE_PUBLIC_KEY`, the same key as `ctr-release-minisign.pub`), and the archive must match the manifest's hash. Only the client, `ctr.apworld`, `solo-runtime/` and the Cortex Vortex files are installed; settings, saves and the disc image are never touched. So:
- Publishing a release (not a draft, not a prerelease) offers it to every player. Publish as a prerelease to hold it back.
- A release without the signed manifest is never offered, and neither is one signed by another key (dowlle's releases, for example).
- `CTR_AP_VERSION` is what the updater compares, so bump it in the release commit; `CTR_AP_COMPAT_VERSION` stays the apworld's line.
- Builds up to v1.2.0 check dowlle's repository, which never offers anything to them. Those players download the first release with this updater by hand once.
- `[Archipelago] update_check = false` in `config.ini` turns the check off.
- The updater is dowlle's (`feat/signed-updater-301`). Ours differs only in the repository paths (`kRepoPath`, `kDownloadPrefix` in `ap_update_core.cpp`), the key (`ap_update_core.h`) and the version it passes (`CTR_AP_VERSION` instead of `CTR_AP_COMPAT_VERSION`, in `ap_update.c`). Keep those when a merge of dowlle's updater conflicts.

Test a release end to end with `CTR_DEV_UPDATE` ([DEV-TOOLS.md](DEV-TOOLS.md)): `.\tools\decrypted\devrun.ps1 -Exe build-ap\ctr_native_ap.exe -RunDir C:\t\updrun -Track -1 -Frames 30000 -Env @{CTR_DEV_UPDATE='v1.1.0'}`, then compare the run folder's files with the release archive. The trust rules have host harnesses (`tools/test-update-core.cpp`, `test-update-menu.c`, `test-update-solo-gate.cpp`, Linux only); `test-update-core.cpp` checks the published v1.2.0 manifest and signature against the compiled-in key.

## Conventions
- Commit messages are plain and descriptive. No co-author trailers for tools, and no mention of AI assistants anywhere in commits, code or docs. Local assistant files (`CLAUDE.md`, `AGENTS.md`, `.claude/`) are listed in `.git/info/exclude` and never committed.
- Credit ported work: `cherry-pick -x` keeps the original commit reference, and README's Credits names each source project.
- New third-party code gets an entry in `THIRD_PARTY_NOTICES.md`.
- Keep the Archipelago client compatible with the shipped `ctr.apworld` version. If slot data or item/location IDs change, the apworld must change in the same release.
- The world in `apworld/ctr` and the client change together: a slot data or item/location change lands in one commit with the client code that reads it. Releases ship the `ctr.apworld` packed from `apworld/ctr` by the CTR world workflow on the release commit (`release-prepare.yml`, [RELEASING.md](../RELEASING.md)).
- Our world changes: `starting_roster` (default `vanilla_eight`) gives the slot the eight retail Adventure racers as from-pool starts, so the Adventure garage runs the retail character select (`AP_CharSwap_GarageRacer` lets it run when every retail starter is unlocked) and only the other eight racers are items. The test package pins `starting_character_only` as its default because the upstream suite counts 15 unlock items; `test_starting_roster.py` covers the eight-racer roster.
- Update `ROADMAP.md` in the same commit that starts or finishes an item: add its row when work starts, delete it when the item is done. The roadmap and docs describe the current state, never history: no "Done" list, no story of how a bug was found, no dated result tables. A doc that explains a fix keeps what someone working on that code needs now (the rule, why it is that way, how to check it); git history and commit messages hold the rest. At the end of a change, grep `ROADMAP.md` and `docs/` for its topic and remove anything it made stale.
- Frame counts assume 30 FPS. When code sets or compares a frame-counted timer, wrap the value in `FPS_DOUBLE`, and wrap per-frame steps or blink phases in `FPS_HALF` (`include/macros.h`), so it keeps the same real-time speed in 60 FPS mode. Data recorded frame by frame (ghosts, AI lap recordings) stays in 30 FPS units so recordings work in both modes. See [60FPS.md](60FPS.md).

## Testing
- Build the AP and vanilla configurations.
- In game, check at least: boot to the menu, one race, the Adventure hub, and connecting to a local Archipelago server. Check timing changes with `fps60` both on and off (`config.ini`, `[Video & QoL]`, or **OPTIONS → Video → 60 FPS (Next Load)**). To host a test seed: `ArchipelagoServer.exe --port 38281 <seed.zip>` from an Archipelago 0.6.7+ install.
- Reproduce and check visual changes with the [developer tools](DEV-TOOLS.md): `tools/decrypted/devrun.ps1` runs any track's demo hidden and fast-forwarded and captures frames, bit-exact between runs. Never drive the game with OS keystrokes.
- The workflows run on pushes to `dev` and `release/**` and on pull requests, never on `main`: `main` only moves to a commit `dev` already tested, so a run there would repeat the same work. A newer push to a branch cancels the run still going on it. To try another branch, start them by hand: `gh workflow run build.yml --ref <branch>` (same for `ci.yml`). `tools/ci/` has the local runners.
- `ci.yml` builds and runs the host harnesses in parallel, one per CPU (`tools/ci/run-harnesses.py`). On Windows, run it with a Python outside the MinGW folder (`py -3 tools/ci/run-harnesses.py`, MinGW's `bin` on `PATH`): MinGW has no UBSan runtime, so the runner builds `tools/ci/nosan-shim.c` as `cc`, `gcc` and `g++` ahead of the real compilers, dropping `-fsanitize` flags. Under MinGW's own Python, Windows would find the real compilers next to it first.
- `build.yml` builds each client variant (ap, vanilla, authoring, authoring-client) on Windows and Linux in its own job, all side by side. `tools/ci/build-clients.sh <platform> [variant...]` is the same recipe locally. The Linux jobs also run the CMake tests (`ctest`: upstream's unit and regression tests, built with the game). Compiles go through ccache, cached per platform and variant; the MSYS2 packages are cached too. Pull requests read the compiler cache but don't save to it, and tag builds don't use it at all, so a release binary is always compiled from scratch. Two things keep the cache effective: CI builds without precompiled headers (MinGW GCC writes SDL's with different bytes every time, which made every SDL object miss), and the commit hash reaches the code only through `platform/native_build_id.c`, never as a definition on `main.c` (that changed the whole unity build with every commit). The Windows job checks text files out with LF so the packaged text files are the same bytes as on Linux. After the builds, its release assembly check packs `ctr.apworld` from the commit, builds the solo runtimes and runs `tools/release/ci-assemble.sh` on that run's archives as a release of the version in `ap/ap_version.h`, the same script `release-prepare.yml` runs (template from `tools/release/ci-template.sh`). A file a build adds to the client archives that the assembler does not know (`assemble-release.py`'s member list) fails there, on the push that added it.
- `apworld.yml` (CTR world) runs on pull requests and pushes that touch `apworld/`, every night on `dev`, or by hand. It runs the world's pytest suite inside Archipelago 0.6.7, packs `ctr.apworld` (the artifact a release ships), and fuzzes that pack with the arms in `apworld/fuzz/arms.txt`: the Archipelago-index checks plus arms for the content plan, pads, goals, laps and Universal Tracker, with metas in `apworld/fuzz/`. Each arm has two counts. A push or pull request runs the quick set, the arms with a push count (about twenty, 5,000 generations, one wave of runners); the nightly run and a run by hand with tier `full` run all of them at their full count (about a hundred, 60,000 generations). `apworld/fuzz/matrix.py` turns the file into the job matrix. To add an arm, add a line; give it a push count only when the quick set does not already cover its family. A failing generation's log, uploaded as the `fuzz-failure-<arm>` artifact next to its YAML, starts with `fuzz seed N, PYTHONHASHSEED H` (`apworld/fuzz/log-seed.py` patches the pinned fuzzer to print it), so it can be generated again from the Archipelago checkout below: put the YAML alone in a folder, set `PYTHONHASHSEED=H` and run `python Generate.py --player_files_path <folder> --seed N --skip_output`. Locally, `pwsh tools/decrypted/apworld-test.ps1` runs the same pytest suite (extra arguments go to pytest, such as `-k starting_roster`). The first run clones Archipelago and installs its Python packages into `%LOCALAPPDATA%\ctr-decrypted\archipelago`; later runs copy `apworld/ctr` in and test. On Windows, the tests that make symbolic links (`test_launcher_component` and the `TestLinks` cases in `test_solo_runtime`) fail unless Developer Mode allows them; they pass in CI. `apworld/` is checked out with LF everywhere (`.gitattributes`), since its fixtures are pinned by hash and its files go into `ctr.apworld`.
- About 20 harnesses only run on Linux (CI): the Python-driven ones that compile with `-fsanitize=undefined` themselves, and the ones that use POSIX calls (`symlink`, `mkdir(path, mode)`, `linkat`, `dlfcn`), such as `test-update-core.cpp`, `test-xdelta-import.cpp` and `test-disc-copy.c`. On Windows, compare a branch's failures with its base branch's rather than expecting zero.
- Harnesses compile game code as the game does, with `CTR_NATIVE` defined (the default recipe adds it; a harness's own build line must too). Without it the headers take the PS1 branch, which only happens to compile on a 32-bit host.
- Harnesses compile real game files on their own, so new code they pull in needs its declarations included where it is called (not only through `game/game_unity.h`), and fork-wide globals (`gNative60FpsEnabled`, `gNativePgxpMode`, the `NativeDev_*` switches, `rdata` for MEMPACK allocation names) need a definition in the harness. Linux drops unused code at link time more aggressively than MinGW, so a harness can link in CI and still fail locally on Windows.
