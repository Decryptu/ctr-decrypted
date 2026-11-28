# macOS

macOS runs the 64-bit engine ([64BIT.md](64BIT.md)) on Apple Silicon (arm64). SDL3 gives the window, the OpenGL context, audio and controllers as on the other systems; this page is what differs. What is still missing before a release is in [ROADMAP.md](../ROADMAP.md).

## The app installs the game folder
A Windows or Linux release is a folder: the executable, the bundled content (Cortex Vortex, the solo runtime, `default_controls.ini`, the docs) and, next to them, everything the player adds or the game writes (the disc image, `config.ini`, saves, logs, downloaded tracks). The game works from that folder (`NativeAssets_Init`, then `chdir`), and the in-game updater replaces files in it.

A macOS app bundle cannot be that folder: it is signed, `/Applications` may be read-only for the player, and macOS runs an app that is quarantined and not notarized from a random read-only copy until the player moves it. So `CTR Decrypted.app` carries a game folder and installs it:

- `Contents/MacOS/<exe>` is the game executable; `Contents/Resources/game/` is the rest of the release folder.
- At launch, `NativeRelaunch_Startup` (`platform/native_relaunch.c`) asks `NativeMacInstall_Prepare` (`platform/native_mac_install.c`) to copy that folder into `~/Library/Application Support/CTR Decrypted/` when the folder holds no game or was last installed by another build of the app (`.installed-by-app` names the build). The same build leaves the folder alone, so an update the in-game updater installed since is kept; a different build is a new download, installed whether it is older or newer. Files the release does not carry stay; `solo-runtime/` is replaced whole, as the updater replaces it. An install takes `.install.lock`, so two launches never copy at once.
- Every file is written new and renamed into place: rewriting a signed executable or library in place leaves the kernel's cached signature on the old bytes, and macOS kills the next process that maps it. Extended attributes are not copied, so the quarantine flag stays on the app the player approved and never reaches the installed files.
- The process then replaces itself with the installed executable, which finds no bundle around it and runs as the Windows and Linux builds do, with that folder as its game folder.
- If the install fails, a message box says why and the app quits: the bundle's own files are no game folder.

Disc search already looks in Downloads, Desktop and Documents, so a player never has to open the game folder to give the game its disc (macOS asks once for access to each of those folders).

## Fixed load address
Savestates and replays hold raw pointers into the game's static memory and need every launch of a build at the same address ([DEV-TOOLS.md](DEV-TOOLS.md#determinism-and-load-addresses)). arm64 macOS has no non-PIE executables (the linker ignores `-no_pie`), so `NativeRelaunch_Startup` replaces the process with the game launched with `_POSIX_SPAWN_DISABLE_ASLR`, the flag Apple's debugger uses. It loads at the linked address, also under the hardened runtime. The bundle install above and this restart are one exec. `CTR_FIXED_BASE_EXEC` in the environment tells the new image not to restart again; if the exec fails, the game runs where it was loaded and a state then loads only in that launch.

## Platform notes
- **OpenGL.** The core context is created forward-compatible, which macOS requires; it gives OpenGL 4.1.
- **zlib** links as the system library (the SDK has no static archive). OpenSSL, which macOS does not ship, links statically from Homebrew's `openssl@3`.
- **Debug symbols** are not split by `objcopy` (Linux only); the packaged app keeps them in a dSYM.
- **Signing.** The release is ad-hoc signed and not notarized. A player opens it once through System Settings > Privacy & Security > Open Anyway. The in-game updater downloads without the quarantine flag.

## Checking
- CI builds vanilla and ap and runs `ctest` on `macos-15` (`build.yml`, `macos`).
- A 64-bit Linux build with `-DCTR_FIXED_BASE=OFF` places every static above 4 GB at a new address every launch, as macOS does: its runs must match the fixed-base build's ([64BIT.md](64BIT.md#checking)).
- On a Mac, `tools/decrypted/devrun.sh` runs the game hidden and fast-forwarded as on Linux; its traces and results must match the Windows build's.
