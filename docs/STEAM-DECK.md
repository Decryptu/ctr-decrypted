# Linux and the Steam Deck

The Linux build is the same game as the Windows one: a 32-bit x86 executable (`ctr_native_ap` in `ctr-decrypted-vX.Y.Z-linux-x86.tar.gz`, built in Debian 12 by `.github/workflows/build.yml`). It links only `libc`, `libm` and `ld-linux.so.2` and needs glibc 2.38 or older (`tools/ci/package-client.py` refuses anything newer and records both in the archive's `BUILD.json`). SDL3 is linked in and loads X11 or Wayland, OpenGL, PulseAudio/ALSA/PipeWire and udev at run time, so the system needs their 32-bit libraries. SteamOS has them, since the Steam client is 32-bit.

To install on a Deck, follow [SETUP.md](../SETUP.md#optional-controllers-and-steam-input): extract in Desktop Mode, add the disc image, **Add to Steam**, play in Gaming Mode.

## What the game does on a Deck

- **Detection.** `NativeConfig_IsSteamDeck` (`platform/native_config.c`): the `SteamDeck=1` variable Steam sets, or, for a launch outside Steam, a Valve board named Jupiter (LCD) or Galileo (OLED) in `/sys/class/dmi/id`.
- **First launch.** With no `config.ini`, a Deck starts fullscreen at 16:10 (`NativeConfig_ApplyFirstLaunchDefaults`); elsewhere it starts windowed 4:3. The Archipelago setup screen starts from these values. `tools/test-graphics-options.c` checks both.
- **Fullscreen.** Gaming Mode's gamescope never reports the window as fullscreen, so the setting is requested once, not every frame (`NativeConfig_FullscreenNeedsReapply`), and the cursor is hidden whenever the setting is on (`Platform_UpdateCursorVisibility`).
- **Controller.** Steam Input gives the game a virtual Xbox-style pad, which SDL maps like any other. Outside Steam, SDL's Steam Deck driver reads the built-in controls.
- **Text.** A connection field starts SDL text input with its area set, and SDL opens Steam's floating keyboard over it when `SteamDeck=1`. The pad can confirm or cancel the edit (`NativeText_Resolve`). The log's `*Text input:` lines say whether a screen keyboard was available.
- **Audio.** SDL tries PulseAudio, then ALSA, then PipeWire (`NativeAudio_SelectDriverHint`); `SDL_AUDIO_DRIVER` overrides it.

## Testing on a Deck

CI has no Deck, so check a release by hand in Gaming Mode:

1. A first launch fills the screen at 16:10 with no cursor or black bars; Options shows Fullscreen on and Aspect Ratio 16:10.
2. The built-in controls drive the menus and a race, including L1/R1 and the triggers; the Controls menu lists the pad.
3. On the Connection screen, every field brings up the Steam keyboard, and Enter or X saves it.
4. Sound plays, a race runs smoothly at 60 FPS, and suspend and resume mid-race carries on.
5. Quitting from the menu returns to Steam; when a newer release exists, the updater installs it and restarts.

Report a problem in an issue with the support bundle (`support-bundle.sh` next to the game), which holds `ctr-ap.log`.
