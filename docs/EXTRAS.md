# Extras

OPTIONS → Extras holds optional HUD and effect mods from the CTR-ModSDK modules, in the form High Octane ships them. Each one is its own row, saved in `config.ini` under `[Extras]`. Every row is visual only and applies at once.

| Row | Key | Default | What it does | Code |
|---|---|---|---|---|
| Reserves Meter | `reserves_meter` | On in the Archipelago build, off in vanilla | A 49×3 bar under the powerslide bar showing the stacked boost ("reserves") you hold, in 1P races and crystal challenges. Red, yellow and green by amount; blue once it overruns the bar; purple once the value has wrapped past 0x7FFF and no longer drains ("Saffi fire"). | `UI_DrawReservesMeter` in `game/UI/UI_Meter.c`, maths in `ap/ap_reserves_meter_logic.h` |
| Keep Reserve Fire | `reserve_fire` | Off | While a driver holds reserves, fire earned from a powerslide stays visible. Retail kills it after two frames or hides it for the visibility cooldown, so chained slides with no start boost show no fire. | `VehFire_KeepReserveFire` in `game/Vehicle/VehFire.c` |
| Blue Fire At USF | `blue_fire` | Off | Player 1's exhaust turns blue while they carry Ultimate Speed Fire, as in Nitro-Fueled. | `game/Vehicle/VehBlueFire.c` |
| Slide Meter Gradient | `slide_meter_colours` | Off | The powerslide bar fades from green to yellow as it fills, then through orange to red across the boost window, instead of flat green then red. | `UI_DrawSlideMeter`, maths in `include/ctr_extras.h` |
| Cyan Super Pads (Next Load) | `super_pad_colour` | Off | Super turbo pads are drawn cyan, as in Nitro-Fueled, so they stand apart from ordinary pads. With the all-super-turbo-pads cheat every pad turns cyan. Applied when a track loads. | `game/LevSuperTurboPads.c`, the shader in `platform/native_renderer.c`, rules in `include/ctr_extras.h` |

Every row applies at once except Cyan Super Pads, which applies at the next track load.

## Decisions
- **Options, default off.** The retail look stays the default; each mod changes a familiar cue. The reserves meter defaults on in the Archipelago build only because that build always drew it.
- **One implementation for both builds.** The Archipelago build's reserves meter, fire tweak and blue fire palette swap are the same code as these rows:
  - The reserves meter draws whenever the option is on. The speed readout under it is Archipelago-only.
  - The fire tweak reads the option. With no `config.ini`, the Archipelago build honours `hud_reserves_fx=1` in `ap-config.txt` (`AP_HudReservesFx`, the same precedence as `map_flash`).
  - The blue palette has one writer, `VehBlueFire_Tick`, run every frame from `MainMain`. It turns blue when the option asks for it or when the Archipelago Progressive Boost capstone does (`AP_BlueFireWanted`), so the two never fight over the same VRAM cells.
- **The fire tweak is the ModSDK module's whole tweak:** it stops the visibility-cooldown hide and keeps a powerslide's first flame from dying after two frames, the half that shows fire after a race started with no start boost.
- **USF is detected from the driver's own constants:** "fire speed cap above the sacred-fire cap", the test `VehFire_Increment` itself uses, rather than the ModSDK module's fixed 0x4800.
- **Palettes are only written while a race is loaded.** Hubs and menus put other content in the same VRAM cells, so outside a race the palette state is forgotten and rewritten at the next race (`ap/ap_blue_fire_logic.h`).
- **Cyan pads are found from the level's own data.** Retail draws ordinary and super pads with the same animated arrow texture, so the texture can't simply be recoloured. A pad is built from:
  - an invisible trigger quad whose terrain byte has the turbo or super turbo bit;
  - a visible overlay quad covering at least 98% of the trigger, within 30 units of its height, textured with a 10-frame animation;
  - often a road quad with the same footprint under it, which is not animated.

  So a **pad animation** is an animated texture on a quad covering 90% or more of any pad trigger, and a **super-pad face** is a face showing a pad animation on a quad that overlaps a super trigger at its height. No per-track exceptions are needed (High Octane scores overlap guesses and special-cases Cortex Castle's wooden floor tiles under super triggers; those carry no pad animation here).
- **The colour travels in the palette word's top bit.** Each super-pad face gets a copy of the pad animation whose texture layouts set bit 15 of the palette word. PSX palette words keep that bit clear, so no real data has it. The shader clears it before the palette lookup and recolours the texel from its brightness (High Octane's formula: red 0.3, green 1.25, blue 1.25 plus a little, of the brightest channel). No VRAM is needed, which matters because tracks have almost no free VRAM outside the framebuffers. Every draw path (1P, split screen, clipped quads) carries the word unchanged.
- **The copies live in the level's memory pack**, because the level renderer only accepts texture data from there. They take about 600 bytes; the feature is skipped if the pack would keep less than 32 KB. The copies follow the original animation each frame (`LevSuperTurboPads_Cycle`). A face's low-detail texture is a picture of the whole quad, so it turns cyan only when all four faces are pad.

## Checking
- `tools/test-extras.c`: the gradient's colours at each stage, that it never brightens again inside the window, the retail fallback for a driver with no room set, and the blue-fire rule.
- Turbo Track with the all-super-turbo-pads cheat (`CTR_DEV_CHEATS=1048576`) and the autopilot, which drives over the first pad at USF: Blue Fire turns the exhaust blue there, the reserves meter draws under the powerslide bar, and the race trace is identical with every row on and off (the rows are visual only).
- Cyan Super Pads: `CTR_DEV_QUAD_DUMP` after marking (palette words with 0x8000) lists the faces made cyan. Hot Air Skyway has two super pads and ordinary ones, Oxide Station five super-pad quads, Cortex Castle two pad quads plus wooden tiles under super triggers that must stay as they are, and Turbo Track 30 pads, all super.
- The gradient and the kept fire while powersliding need a player: the autopilot doesn't powerslide.
