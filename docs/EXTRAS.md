# Extras

OPTIONS → Extras holds optional character, HUD and effect changes. Settings are saved in `config.ini` under `[Extras]`. Japanese Fake Crash is an import action. Visual changes apply at once, except Cyan Super Pads, which applies at the next track load. Penta changes driving stats on the next race or driver birth.

`tools/test-options-pages.c` checks the Extras row order, panel capacity and saved sections, including the character and AI roster options.

| Row | Key | Default | What it does | Code |
|---|---|---|---|---|
| Character Select Stats | `character_stats` | Off | Engine class and relative speed, acceleration and turning beside each player on the character select; L2 also toggles it there. Custom racers use their configured engine. | [CUSTOM_RACERS.md](CUSTOM_RACERS.md#character-stats), `game/230/MM_CharacterGrid.c` |
| Penta (Next Race) | `penta_stats` | NTSC-U (`0`) | NTSC-U uses the turning class; PAL (`1`) combines speed-class top speed, acceleration-class acceleration and turning-class handling. Changes the real Penta, including bots, while custom racers keep their engine. A loaded Archipelago Adventure seed owns its own Penta/stat rules. | `include/ctr_penta.h`, `game/Vehicle/VehBirth.c` |
| Voices | `voices` | US English (`0`) | Language of every voice clip: racer lines, Aku Aku and Uka Uka, bosses, cutscenes and the boot intro. US English comes from the NTSC-U disc; UK English (`1`), French (`2`), German (`3`), Spanish (`4`), Italian (`5`) and Dutch (`6`) are imported from a PAL disc image, Japanese (`7`) from a Japanese one. | [Voices](#voices), `platform/native_voices.c` |
| Japanese Fake Crash | | | Imports the Japanese racer model and textures from a Japanese CTR disc image in `assets`. Takes effect when the game restarts. | [Regional Fake Crash assets](CUSTOM_RACERS.md#regional-fake-crash-assets) |
| Opponent Roster | `ai_racers` | Original (`0`) | Arcade/Cup opponents: Original, Unlocked (`1`), or Include Custom (`2`). Takes effect for the next race or new cup. | [CUSTOM_RACERS.md](CUSTOM_RACERS.md#opponent-roster), `game/LOAD/LOAD_NativeBots.c` |
| Reserves Meter | `reserves_meter` | On in the Archipelago build, off in vanilla | A 49×3 bar under the powerslide bar showing the boost reserves you hold, in 1P races and crystal challenges. Red, yellow, green by amount; blue once it overruns the bar; purple once the value has wrapped past 0x7FFF and no longer drains ("Saffi fire"). | `UI_DrawReservesMeter` in `game/UI/UI_Meter.c`, maths in `ap/ap_reserves_meter_logic.h` |
| Keep Reserve Fire | `reserve_fire` | Off | While a driver holds reserves, powerslide fire stays visible. Retail kills it after two frames or hides it for the visibility cooldown, so chained slides with no start boost show no fire. | `VehFire_KeepReserveFire` in `game/Vehicle/VehFire.c` |
| Blue Fire At USF | `blue_fire` | Off | Player 1's exhaust turns blue at Ultimate Speed Fire, as in Nitro-Fueled. | `game/VehBlueFire.c` |
| Slide Meter Gradient | `slide_meter_colours` | Off | The powerslide bar fades green to yellow as it fills, then orange to red across the boost window, instead of flat green then red. | `UI_DrawSlideMeter`, maths in `include/ctr_extras.h` |
| Colour Filter | `colour_filter` | Off | Greyscale, sepia, negative, vignette, or a protanopia or deuteranopia correction, on top of any screen filter. | [SCREEN-FILTERS.md](SCREEN-FILTERS.md#colour-filter), `platform/native_renderer.c` |
| Cyan Super Pads (Next Load) | `super_pad_colour` | Off | Super turbo pads are drawn cyan, as in Nitro-Fueled. The all-super-turbo-pads cheat turns every pad cyan outside Time Trial. | `game/LevSuperTurboPads.c`, the shader in `platform/native_renderer.c`, rules in `include/ctr_extras.h` |

## Penta stats

The local choice controls Penta's driving stats. The PAL values match High Octane's full 65-row Penta table. The NTSC-U turning class already supplies 62 rows. Acceleration (544), top speed (13900) and accelerated top speed (15400) differ. The loader changes those three halfwords without changing the shared engine metadata or table layout, so another racer using the turning class is unaffected.

Outside a loaded Archipelago Adventure seed, the local option applies to Penta in every mode. In Archipelago Adventure the seed's `penta_stats`, progressive or editable stats and starting-class rules take precedence; the local option does not change slot data. The 200cc modifier is applied after the base Penta choice. Custom racers retain their engine setting even when their template is Penta. The character-select panel shows PAL Penta with three full bars, or the NTSC-U turning-class bars.

## Penta mask and audio

Both clients show Aku Aku in Penta's item HUD and use his PAL kart engine sound with either stats setting. The engine sample is already on the NTSC-U disc. Penta's player channels use separate IDs so the same sample can play for AI karts independently.

Penta's NTSC-U race voice clips are placeholders. When a PAL disc image is in `assets`, the game imports the UK English pack at startup (see [Voices](#voices)) and Penta speaks his finished PAL lines even with US English voices. His two immediate sound effects are identical on the two discs.

## Voices

The game asks for every voice clip by its NTSC-U number (category 1 "extra": Aku Aku and Uka Uka hints, bosses, cutscenes, the boot intro; category 2 "game": racer lines). With another language chosen, `NativeVoices_Map` turns that number into the other disc's number and `NativeAudio_LookupXATrackInfo` (`platform/native_audio.c`) plays the clip from the language's pack. Music never changes.

**Packs.** A pack is one language folder copied from the player's own disc image into `assets/XA/VOICES/<code>`: `<code>.XNF` plus `EXTRA/` and `GAME/` with the XA files as raw 2352-byte sectors (`ENG`, `FRN`, `GRM`, `SPN`, `ITL`, `DCH` from the PAL disc SCES-02105; `JPN` from the Japanese disc SCPS-10118, about 70 MB; a PAL language is about 35 MB). `NativeVoices_Import` finds the source by opening each `.bin` in `assets` and looking for the region's boot executable, reads it through its own file handle (the NTSC-U disc stays mounted) and writes the manifest last, so a pack with a manifest is complete. The NTSC-U disc search already skips PAL and Japanese images, so they can sit in `assets` beside it under any name. Only raw MODE2/2352 `.bin` images are read, not `.chd` or `.iso`.

**When it imports.** At startup (`NativeVoices_Init`, from `main.c`) for the chosen language, and UK English whenever a PAL image is present (for Penta). In OPTIONS, changing the row only checks whether the disc is there; the copy runs when the player leaves the page (`Config_ImportVoices`), so stepping past languages never imports them. A language whose pack is missing plays US English.

**What the player sees.** The value reads the language, or `<LANGUAGE>: NO DISC` / `<LANGUAGE>: IMPORT` while its pack is missing. The two help lines under the page say which disc image is needed and where it goes, that it was found and imports on leaving the page, that it was imported, or that the import failed and to check the log. The player is never left with a silent setting that needs a file they don't know about.

**The clip map.** `python tools/decrypted/voice_map.py <NTSC-U bin> <PAL bin> <JP bin>` generates `platform/native_voice_map.inc` from the retail executable tables and XA manifests, plus the boss script IDs.
- Racer lines come from each executable's voice set table (`data.voiceData`, found through its 16-pointer `voiceSetPtr` array). NTSC-U has 11 sets per racer, PAL and Japanese 8. Lines from NTSC-U sets 8–10 use set 0 instead when the racer's language is not US English (`Voiceline_StartPlay`), as those discs do.
- Extra clips follow each executable's wrong-way clip ids (NTSC-U and PAL 30/61, Japanese 31/63) and N. Tropy line table. PAL drops NTSC-U 62, 63 and 83–86 and moves 64–82 down two. Japanese adds a 31st Aku Aku and Uka Uka hint before the wrong-way clip and keeps 64–82.
- The eight boss lines follow the PLAY_XA opcodes in each disc's overlay 233. The two clips before Ripper Roo's voice sets follow their position in the regional XA manifests. NTSC-U game clips 306 and 307 are outside the boss scripts and racer voice sets and have no confirmed regional match.
- The script checks every PAL English clip has the NTSC-U clip's length to one sector (Penta's placeholders excepted). The PAL map also matches High Octane's hand-written one on every clip a voice table uses. A clip with no counterpart (`0xFFFF`) plays from the NTSC-U disc. Non-US racer playback uses the voice set fallback above for sets 8–10.

**Checking.** With `CTR_DEV_RACE=26 CTR_DEV_MODE=adv CTR_DEV_AUDIO=<raw>` and `Extras.voices` set, Aku Aku's first hint plays around seconds 6–42. Its envelope matches US English at 0.998 correlation with UK English and differs with French and Japanese. `CTR_DEV_OPTIONS=Extras` with `CTR_DEV_PAD=600:D,630:D` shows the row and its help (Archipelago build; the plain build draws no help lines).

## Decisions
- **Retail defaults.** Character-select stats and effect changes default off, and Penta defaults to NTSC-U. The reserves meter defaults on in the Archipelago build only because that build always drew it.
- **One implementation for both builds.** The Archipelago build's reserves meter, fire tweak and blue fire are the same code as these rows:
  - The reserves meter draws whenever the option is on. The Archipelago speed readout sits left of the boost bar, its baseline on the bar's: the bar is at the bottom of the screen, so a readout under it would be cut off.
  - The fire tweak reads the option. With no `config.ini`, the Archipelago build honours `hud_reserves_fx=1` in `ap-config.txt` (`AP_HudReservesFx`, same precedence as `map_flash`).
  - The blue palette has one writer, `VehBlueFire_Tick`, run every frame from `MainMain`. It turns blue when the option or the Archipelago Progressive Boost capstone asks (`AP_BlueFireWanted`), so the two never fight over the same VRAM cells.
- **The fire tweak is the whole ModSDK tweak:** it stops the visibility-cooldown hide and keeps a powerslide's first flame alive past two frames.
- **USF is detected from the driver's constants:** fire speed cap above the sacred-fire cap, the test `VehFire_Increment` uses, not the ModSDK module's fixed 0x4800.
- **Palettes are only written while a race is loaded.** Hubs and menus use the same VRAM cells, so outside a race the palette state is dropped and rewritten at the next race (`ap/ap_blue_fire_logic.h`).
- **Cyan pads are found from the level's data.** Ordinary and super pads share one animated arrow texture, so it can't simply be recoloured. A pad is:
  - an invisible trigger quad whose terrain byte has the turbo or super turbo bit;
  - a visible overlay quad covering at least 98% of the trigger, within 30 units of its height, with a 10-frame animated texture;
  - often a non-animated road quad with the same footprint under it.

  So a **pad animation** is an animated texture on a quad covering 90% or more of any pad trigger, and a **super-pad face** is a face showing a pad animation on a quad that overlaps a super trigger at its height. This needs no per-track exceptions (Cortex Castle's wooden tiles under super triggers carry no pad animation).
- **The colour travels in the palette word's top bit.** Each super-pad face gets a copy of the pad animation whose texture layouts set bit 15 of the palette word, which real PSX data never sets. The shader clears it before the palette lookup and recolours the texel from its brightness (High Octane's formula: red 0.3, green 1.25, blue 1.25 plus a little, of the brightest channel). This needs no VRAM, which tracks barely have outside the framebuffers. Every draw path (1P, split screen, clipped quads) carries the word unchanged.
- **The copies live in the level's memory pack**, since the level renderer only takes texture data from there. They take about 600 bytes; the feature is skipped if the pack would keep less than 32 KB. They follow the original animation each frame (`LevSuperTurboPads_Cycle`). A face's low-detail texture covers the whole quad, so it turns cyan only when all four faces are pad.

## Checking
- `tools/test-penta.c` calls the real driver-constant loader and checks both styles, unaffected racers, custom engines, bots, seed ownership, stat-package order and 200cc order.
- `tools/test-extras.c`: the gradient's colours at each stage, that it never brightens again inside the window, the retail fallback for a driver with no room set, and the blue-fire rule.
- Arcade on Turbo Track with the all-super-turbo-pads cheat (`CTR_DEV_CHEATS=1048576`) and the autopilot, which hits the first pad at USF: Blue Fire turns the exhaust blue, the reserves meter draws, and the race trace is identical with every row on and off.
- Cyan Super Pads: `CTR_DEV_QUAD_DUMP` after marking (palette words with 0x8000) lists the cyan faces. Hot Air Skyway has two super pads and ordinary ones, Oxide Station five super-pad quads, Cortex Castle two pad quads plus wooden tiles under super triggers that must stay unchanged, Turbo Track 30 pads, all super.
- The gradient and the kept fire need a player: the autopilot doesn't powerslide.
