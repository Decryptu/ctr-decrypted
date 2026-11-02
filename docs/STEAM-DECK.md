# Linux and the Steam Deck

The Linux build is the same game as the Windows one: a 32-bit x86 executable (`ctr_native_ap` in `ctr-decrypted-vX.Y.Z-linux-x86.tar.gz`, built in Debian 12 by `.github/workflows/build.yml`). It links only `libc`, `libm` and `ld-linux.so.2` and needs glibc 2.38 or older (`tools/ci/package-client.py` refuses anything more, and records both in the archive's `BUILD.json`). SDL3 is linked in and loads X11 or Wayland, OpenGL, PulseAudio/ALSA/PipeWire and udev at run time, so a system needs their 32-bit libraries. SteamOS has them, because the Steam client is 32-bit.

Players install it on a Deck as [SETUP.md](../SETUP.md#optional-controllers-and-steam-input) says: extract in Desktop Mode, add the disc image, **Add to Steam**, play in Gaming Mode.

## What the game does on a Deck

- **Detection.** `NativeConfig_IsSteamDeck` (`platform/native_config.c`): the `SteamDeck=1` variable Steam sets for what it launches, or a Valve board named Jupiter (LCD) or Galileo (OLED) in `/sys/class/dmi/id`, for a launch from Desktop Mode outside Steam.
- **First launch.** With no `config.ini`, a Deck starts fullscreen at 16:10 (`NativeConfig_ApplyFirstLaunchDefaults`); elsewhere the defaults stay windowed 4:3. The Archipelago setup screen shows these as its starting values. `tools/test-graphics-options.c` checks both.
- **Fullscreen.** Game Mode's gamescope never reports the window as fullscreen. The fullscreen setting is requested once, not every frame (`NativeConfig_FullscreenNeedsReapply`), and the cursor is hidden when the setting is on, not only when the window says so (`Platform_UpdateCursorVisibility`).
- **Controller.** Steam Input hands the game a virtual Xbox-style pad, which SDL maps like any other. Outside Steam, SDL's own Steam Deck driver reads the built-in controls.
- **Text.** A connection field starts SDL text input with its area set, and SDL opens Steam's floating keyboard over it when `SteamDeck=1`. The pad can confirm or cancel the edit (`NativeText_Resolve`). The log records whether a screen keyboard was available (`*Text input:` lines).
- **Audio.** SDL is asked for PulseAudio, then ALSA, then PipeWire (`NativeAudio_SelectDriverHint`); `SDL_AUDIO_DRIVER` overrides it.

## Testing on a Deck

No Deck is available to the project's own runs, so a Deck release is checked by hand. With the latest release installed as above, in Gaming Mode:

1. A first launch fills the screen at 16:10 with no cursor or black bars, and the Options menu shows Fullscreen on and Aspect Ratio 16:10.
2. The built-in controls drive the menus and a race, including L1/R1 and the triggers; the Controls menu lists the pad.
3. On the Connection screen, every field brings up the Steam keyboard, and Enter or X saves it.
4. Sound plays, a race runs smoothly at 60 FPS, and suspending and resuming the Deck mid-race carries on.
5. Quitting from the game's menu returns to Steam; the in-game updater, when a newer release exists, installs it and restarts.

A problem goes in an issue with the support bundle (`support-bundle.sh` next to the game), which holds `ctr-ap.log`.
