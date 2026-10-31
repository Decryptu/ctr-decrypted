# Working on CTR Decrypted

Everything a new contributor needs, with no other context. Keep it current. [ROADMAP.md](../ROADMAP.md) has the status of every item, and [UPSTREAM-ISSUES.md](UPSTREAM-ISSUES.md) summarizes the projects we draw from.

## What this is
- A fork of [dowlle/ctr-native-ap](https://github.com/dowlle/ctr-native-ap): the native Crash Team Racing port (CTR-native) with an Archipelago client compiled in.
- On top of that we port enhancements from [High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane) and fix bugs reported upstream.
- The game reads assets from the player's own NTSC-U disc image (`assets/ctr-u.bin`). Nothing copyrighted is in the repo.

## Repository layout (main parts)
| Path | Contents |
|---|---|
| `game/` | Decompiled game code (unity build via `game/game_unity.h`). Archipelago hooks are `#ifdef CTR_AP` blocks |
| `platform/native_*.c` | PC layer: OpenGL renderer, software SPU audio, input, config, disc reading |
| `ap/` | Archipelago client and game hooks (`ap_hooks_*.c`). Dependencies are fetched and verified by `ap/vendor/fetch-deps.sh` |
| `include/` | Shared headers, structs, macros (`include/macros.h` holds the frame-rate macros) |
| `externals/SDL` | SDL 3.4.10, vendored, static |
| `externals/libusb` | libusb 1.0.30 Windows backend, vendored, for Switch 2 controllers |
| `tools/decrypted/` | Our own scripts (upstream sync, unattended runs and render diagnostics) |
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
- `dev`: integration branch. Finished feature branches are merged here first, and test builds for in-game testing come from it. It goes into `main` once the in-game checklists pass.
- `feature/<name>`: one feature or fix per branch, merged into `dev` when it builds, then to `main` with `dev` after in-game testing. Delete it locally and on `origin` once merged; only `main`, `dev` and the `clean/*` import refs stay.

## Syncing upstream
Never merge upstream remotes directly. Import them through the sync script, which rewrites commit messages to strip AI co-author trailers. The rewrite is deterministic, so repeated imports produce the same hashes and later merges stay clean.

```
git remote add dowlle https://github.com/dowlle/ctr-native-ap.git      # once
pwsh tools/decrypted/sync-upstream.ps1                                 # -> clean/dowlle-main
git merge clean/dowlle-main
pwsh tools/decrypted/sync-upstream.ps1 -Remote ctr-native -Branch master
```

The script needs `python -m pip install git-filter-repo`. High Octane (`highoctane/vita`) is not merged. Port its commits by cherry-picking with `-x` and resolve conflicts by hand, keeping every `#ifdef CTR_AP` block.

## Conventions
- Commit messages are plain and descriptive. No co-author trailers for tools, and no mention of AI assistants anywhere in commits, code or docs. Local assistant files (`CLAUDE.md`, `AGENTS.md`, `.claude/`) are listed in `.git/info/exclude` and never committed.
- Credit ported work: `cherry-pick -x` keeps the original commit reference, and README's Credits names each source project.
- New third-party code gets an entry in `THIRD_PARTY_NOTICES.md`.
- Keep the Archipelago client compatible with the shipped `ctr.apworld` version. If slot data or item/location IDs change, the apworld must change in the same release.
- Releases ship the apworld built from [dowlle/ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld) `main` at b775595a4 or later (`release-prepare.yml` takes the run and commit). That is v0.2.4 plus generator-only fixes, with the same slot data; it includes the vanilla-mode stage 2 fix (ctr-native-ap #499: a track's relic races and CTR Challenge need that hub's keys, as in the original game), which the client already enforces.
- Update `ROADMAP.md` in the same commit that finishes or starts an item.
- Frame counts assume 30 FPS. When code sets or compares a frame-counted timer, wrap the value in `FPS_DOUBLE`, and wrap per-frame steps or blink phases in `FPS_HALF` (`include/macros.h`), so it keeps the same real-time speed in 60 FPS mode. Data recorded frame by frame (ghosts, AI lap recordings) stays in 30 FPS units so recordings work in both modes. See [60FPS.md](60FPS.md).

## Testing
- Build the AP and vanilla configurations.
- In game, check at least: boot to the menu, one race, the Adventure hub, and connecting to a local Archipelago server. Check timing changes with `fps60` both on and off (`config.ini`, `[Video & QoL]`, or **OPTIONS → Video → 60 FPS (Next Load)**). To host a test seed: `ArchipelagoServer.exe --port 38281 <seed.zip>` from an Archipelago 0.6.7+ install.
- Reproduce and check visual changes with the [developer tools](DEV-TOOLS.md): `tools/decrypted/devrun.ps1` runs any track's demo hidden and fast-forwarded and captures frames, bit-exact between runs. Never drive the game with OS keystrokes.
- Both workflows run on pushes to `main`, `dev` and `release/**` and on pull requests, so `dev` is tested before it goes into `main`. To try another branch, start them by hand: `gh workflow run build.yml --ref <branch>` (same for `ci.yml`). `tools/ci/` has the local runners.
- `ci.yml` builds and runs the host harnesses in parallel, one per CPU (`tools/ci/run-harnesses.py`).
- `build.yml` builds each client variant (ap, vanilla, authoring, authoring-client) on Windows and Linux in its own job, all side by side. `tools/ci/build-clients.sh <platform> [variant...]` is the same recipe locally. Compiles go through ccache, cached per platform and variant; the MSYS2 packages are cached too. Pull requests read the compiler cache but don't save to it, and tag builds don't use it at all, so a release binary is always compiled from scratch. Two things keep the cache effective: CI builds without precompiled headers (MinGW GCC writes SDL's with different bytes every time, which made every SDL object miss), and the commit hash reaches the code only through `platform/native_build_id.c`, never as a definition on `main.c` (that changed the whole unity build with every commit). The Windows job checks text files out with LF so the packaged text files are the same bytes as on Linux.
- Harnesses compile real game files on their own, so new code they pull in needs its declarations included where it is called (not only through `game/game_unity.h`), and fork-wide globals (`gNative60FpsEnabled`, `gNativePgxpMode`, the `NativeDev_*` switches) need a definition in the harness. Linux drops unused code at link time more aggressively than MinGW, so a harness can link in CI and still fail locally on Windows.
