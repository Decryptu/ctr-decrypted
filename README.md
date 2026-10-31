# CTR Decrypted

A native PC version of **Crash Team Racing (PS1, 1999)** with the [Archipelago](https://archipelago.gg) multiworld randomizer built in and High Octane-style enhancements.

- No emulator and no ROM patching: the game is compiled from the [CTR-native](https://github.com/CTR-tools/ctr-native) decompilation.
- You supply your own NTSC-U disc image; the game reads its assets from it.

CTR Decrypted is built on [dowlle/ctr-native-ap](https://github.com/dowlle/ctr-native-ap) and stays compatible with its seeds and `ctr.apworld`. It adds enhancements ported from [Crash Team Racing - High Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane), plus fixes of its own. [ROADMAP.md](ROADMAP.md) has what is planned.

## Features

On top of the Archipelago client, each in OPTIONS:

- **60 FPS**, on by default, in every mode ([docs/60FPS.md](docs/60FPS.md)).
- **PGXP anti-wobble** and perspective-correct textures, with a depth test that fixes the original's level sorting errors ([docs/PGXP.md](docs/PGXP.md)).
- **Screen filters**: Sharp Bilinear, FXAA, Scanlines, CRT ([docs/SCREEN-FILTERS.md](docs/SCREEN-FILTERS.md)).
- **Extras**: reserves meter, kept reserve fire, blue fire at Ultimate Speed Fire, slide meter gradient, cyan super pads ([docs/EXTRAS.md](docs/EXTRAS.md)).
- **Playable N. Oxide**, unlocked by beating him in Adventure.
- Widescreen, render scale, increased draw distance, remappable controls, and native Switch 2 Pro Controller support (with Steam closed).

## Getting started

**1. Download a release** from the [Releases](https://github.com/Decryptu/ctr-decrypted/releases) page and extract it.

**2. Add your disc image.** This project contains no game assets. You must own a retail **NTSC-U** copy of Crash Team Racing, dumped as a raw single-track `.bin` (MODE2/2352). A 2048-byte `.iso` won't work: it loses the audio and video sectors. Put the image in an `assets` folder next to the executable. Launching the game once creates that folder.

```
CTR-Decrypted/
  ctr_native_ap.exe
  assets/
    ctr-u.bin
```

**3. Connect to your Archipelago room.**
1. Go to **OPTIONS → Connection**.
2. Enter the server address (for example `archipelago.gg:38281`), your slot name and the password.
3. Select **Connect**.

The settings are saved to `config.ini`.

## Making a multiworld

- Each player makes a YAML options file, for example with the [CTR YAML Builder](https://ap-pie.com/apworlds?build=ctr).
- The person generating the room installs the `ctr.apworld` shipped with the release into Archipelago's `custom_worlds` folder, then generates with every player's YAML.
- Host the result on [archipelago.gg](https://archipelago.gg/uploads) or with your own Archipelago server.

[SETUP.md](SETUP.md) has the full walkthrough and troubleshooting.

## Building from source

See [BUILDING.md](BUILDING.md).
- Windows builds use MSYS2 MinGW32.
- The game is 32-bit only for now.

## Keeping up with upstream

Upstream branches are imported with `tools/decrypted/sync-upstream.ps1`. It writes `clean/<remote>-<branch>`, which you then merge.

## License

GPL-3.0, inherited from CTR-native. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The `ctr.apworld` is MIT-licensed. This repository contains no game assets.

## Credits

- [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native) and [CTR-ModSDK](https://github.com/CTR-tools/CTR-ModSDK): the decompilation and native port everything here is built on.
- [dowlle/ctr-native-ap](https://github.com/dowlle/ctr-native-ap) and [ctr-archipelago-apworld](https://github.com/dowlle/ctr-archipelago-apworld): the Archipelago client and world this project is based on.
- [Rinnegatamante/Crash-Team-Racing-High-Octane](https://github.com/Rinnegatamante/Crash-Team-Racing-High-Octane): the source of the 60 FPS mode and other enhancements ported here.
- [thecodingbob/ctr-native](https://github.com/thecodingbob/ctr-native): the config menu, widescreen, borderless fullscreen and dithering options.
- [CTR Turbocharged](https://github.com/CameronRedmore/Crash-Team-Racing-Turbocharged), DuckStation (Stenzek) and iCatButler: the PGXP precision geometry, via High Octane.
- Icebound777 and Taor: the [CTR randomizer](https://github.com/icebound777/CTR-Randomizer-Standalone) whose design the Archipelago integration builds on.
- [PsyCross](https://github.com/OpenDriver2/PsyCross), [SDL3](https://github.com/libsdl-org/SDL), [apclientpp](https://github.com/black-sliver/apclientpp).

Crash Team Racing is a trademark of Sony Interactive Entertainment. This project is not affiliated with Sony or Naughty Dog.
