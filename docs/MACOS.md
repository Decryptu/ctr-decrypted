# macOS

macOS runs the 64-bit engine ([64BIT.md](64BIT.md)) on Apple Silicon (arm64). SDL3 gives the window, the OpenGL context, audio and controllers as on the other systems; this page is what differs. What is still missing before a release is in [ROADMAP.md](../ROADMAP.md).

## The app installs the game folder
A Windows or Linux release is a folder: the executable, the bundled content (Cortex Vortex, the solo runtime, `default_controls.ini`, the docs) and, next to them, everything the player adds or the game writes (the disc image, `config.ini`, saves, logs, downloaded tracks). The game works from that folder (`NativeAssets_Init`, then `chdir`), and the in-game updater replaces files in it.

A macOS app bundle cannot be that folder: it is signed, `/Applications` may be read-only for the player, and macOS runs an app that is quarantined and not notarized from a random read-only copy until the player moves it. So `CTR Decrypted.app` carries a game folder and installs it:

- `Contents/Resources/game/` is the release folder, byte for byte what a Linux archive holds (the game executable included). `Contents/MacOS/CTR Decrypted` is a small launcher (`platform/macos/ctr_launcher.c`, the `ctr_macos_launcher` target), the only code the bundle's signature covers as code. Sealing the bundle therefore never rewrites the game executable: it stays the exact bytes CI built and `BUILD.json` names.
- At launch, the launcher's `NativeMacInstall_Prepare` (`platform/native_mac_install.c`) copies `game/` into `~/Library/Application Support/CTR Decrypted/` when that folder holds no game or was last installed by another build of the app (`.installed-by-app` names the build). The same build leaves the folder alone, so an update the in-game updater installed since is kept; a different build is a new download, installed whether it is older or newer. Files the release does not carry stay; `solo-runtime/` is replaced whole, as the updater replaces it. An install takes `.install.lock`, so two launches never copy at once.
- Every file is written new and renamed into place: rewriting a signed executable or library in place leaves the kernel's cached signature on the old bytes, and macOS kills the next process that maps it. Extended attributes are not copied, so the quarantine flag stays on the app the player approved and never reaches the installed files.
- The launcher then replaces itself with the installed game, launched at its linked address (below). The game runs as the Windows and Linux builds do, with that folder as its game folder.
- If the install fails, the launcher says why in an alert and quits.

Disc search already looks in Downloads, Desktop and Documents, so a player never has to open the game folder to give the game its disc (macOS asks once for access to each of those folders).

## Fixed load address
Savestates and replays hold raw pointers into the game's static memory and need every launch of a build at the same address ([DEV-TOOLS.md](DEV-TOOLS.md#determinism-and-load-addresses)). arm64 macOS has no non-PIE executables (the linker ignores `-no_pie`), so the game is started with `_POSIX_SPAWN_DISABLE_ASLR`, the flag Apple's debugger uses, which loads it at the linked address, also under the hardened runtime. The launcher starts it that way; a game started any other way that finds itself slid replaces itself with such a launch (`NativeRelaunch_Startup`). `CTR_FIXED_BASE_EXEC` in the environment tells the game it was started so; if the exec fails, the game runs where it was loaded and a state then loads only in that launch.

## Platform notes
- **Minimum macOS 11**, the first release on Apple Silicon (`CMAKE_OSX_DEPLOYMENT_TARGET` in `CMakeLists.txt`), so every arm64 Mac runs it. `tools/ci/package-client.py` refuses an executable built for anything else.
- **OpenGL.** The core context is created forward-compatible, which macOS requires; it gives OpenGL 4.1.
- **zlib** links as the system library (the SDK has no static archive). **OpenSSL**, which macOS does not ship, links statically from a pinned release built for macOS 11 (`tools/ci/build-openssl-macos.sh`; Homebrew's is built for the runner's own macOS and moves with every update).
- **Debug symbols.** After linking, `dsymutil` writes `<exe>.dSYM`, `strip -S` removes the debug map, and the executable is signed again ad hoc (stripping invalidates the linker's signature, and arm64 macOS runs no unsigned code). The dSYM ships zipped as its own asset and matches the executable by UUID.
- **Signing.** The release is ad-hoc signed and not notarized. A player opens it once through System Settings > Privacy & Security > Open Anyway. The in-game updater downloads without the quarantine flag.

## Building and releasing
- `tools/ci/build-clients.sh macos <variant>` builds on an Apple Silicon Mac (the `macos` jobs in `build.yml`, `macos-15`) and `package-client.py` makes the app: the release folder as `Contents/Resources/game/`, the launcher, `Info.plist`, an icon from `platform/icon/icon-256.png`, then an ad-hoc `codesign` that seals the bundle. Each variant is its own app with its own Application Support folder ("CTR Decrypted", "CTR Decrypted Vanilla", "CTR Decrypted Box Authoring", "CTR Decrypted Authoring Client"; `CTR_MACOS_APP_NAME` in `CMakeLists.txt`), so an authoring app never installs over a player's game. `tools/ci/smoke-macos-app.sh` then installs the app into a scratch home as a player would and starts the installed game with `--version`.
- The release archive `ctr-decrypted-vX.Y.Z-macos-arm64.tar.gz` holds `ctr-decrypted-vX.Y.Z/CTR Decrypted.app`. The release assembler puts the release folder (solo runtime and `ctr.apworld` included) into the CI app and seals it again, so the assembly runs on macOS ([RELEASING.md](../RELEASING.md)).
- The in-game updater (`TARGET_MACOS_ARM64`) installs from that archive's `CTR Decrypted.app/Contents/Resources/game/` only, into the installed game folder. The next launch of the same app keeps it.

## Checking
- CI builds every variant, runs `ctest` and the install smoke test on `macos-15` (`build.yml`, `macos`).
- A 64-bit Linux build with `-DCTR_FIXED_BASE=OFF` places every static above 4 GB at a new address every launch, as macOS does: its runs must match the fixed-base build's ([64BIT.md](64BIT.md#checking)).
- On a Mac, `tools/decrypted/devrun.sh` runs the game hidden and fast-forwarded as on Linux; its traces and results must match the Windows build's.
