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
- **One implementation for both builds.** The Archipelago build already had the reserves meter, the fire tweak and the blue fire palette swap, each tied to an Archipelago session or item. They are now shared:
  - The reserves meter draws whenever the option is on. The speed readout under it stays Archipelago-only.
  - The fire tweak reads the option. With no `config.ini`, the Archipelago build still honours `hud_reserves_fx=1` in `ap-config.txt` (`AP_HudReservesFx`, the same precedence as `map_flash`).
  - The blue palette has one writer, `VehBlueFire_Tick`, run every frame from `MainMain`. It turns blue when the option asks for it or when the Archipelago Progressive Boost capstone does (`AP_BlueFireWanted`), so the two never fight over the same VRAM cells.
- **The fire tweak is the ModSDK module's whole tweak.** The Archipelago version only stopped the visibility-cooldown hide. The module also keeps a powerslide's first flame from dying after two frames, which is the half that shows fire after a race started with no start boost; both halves are in now.
- **USF is detected from the driver's own constants.** The BlueFire module compared the fire speed cap with 0x4800, the value a super turbo pad gives with retail's constants (measured in the run below). Here it is "cap above the sacred-fire cap", the test `VehFire_Increment` itself uses for USF.
- **Palettes are only written while a race is loaded.** Hubs and menus put other content in the same VRAM cells, so outside a race the palette state is forgotten and rewritten at the next race (the Archipelago version's rule, `ap/ap_blue_fire_logic.h`).

- **Cyan pads are found from the level's own data, not by guessing.** Retail draws ordinary and super pads with the same animated arrow texture, so the texture can't simply be recoloured. `CTR_DEV_QUAD_DUMP` showed how a pad is built on Hot Air Skyway, Oxide Station, Cortex Castle and Turbo Track:
  - an invisible trigger quad whose terrain byte has the turbo or super turbo bit;
  - a visible overlay quad covering at least 98% of the trigger, within 30 units of its height, textured with a 10-frame animation;
  - often a road quad with the same footprint under it, which is not animated.

  So a **pad animation** is an animated texture on a quad covering 90% or more of any pad trigger, and a **super-pad face** is a face showing a pad animation on a quad that overlaps a super trigger at its height. High Octane instead scores overlap guesses and needs a Cortex Castle exception for wooden floor tiles under super triggers; here those tiles carry no pad animation, so they are left alone with no special case.
- **The colour travels in the palette word's top bit.** Each super-pad face gets a copy of the pad animation whose texture layouts set bit 15 of the palette word. PSX palette words keep that bit clear, so no real data has it. The shader clears it before the palette lookup and recolours the texel from its brightness (High Octane's formula: red 0.3, green 1.25, blue 1.25 plus a little, of the brightest channel). No VRAM is needed: the four dumped tracks share almost no free VRAM outside the framebuffers, so a cyan palette would have had nowhere safe to go. Every draw path (1P, split screen, clipped quads) carries the word unchanged.
- **The copies live in the level's memory pack**, because the level renderer only accepts texture data from there. They take about 600 bytes; the feature is skipped if the pack would keep less than 32 KB (it keeps 80 KB or more on the tracks measured). The copies follow the original animation each frame (`LevSuperTurboPads_Cycle`). A face's low-detail texture is a picture of the whole quad, so it turns cyan only when all four faces are pad.

## Checked
- `tools/test-extras.c`: the gradient's colours at each stage, that it never brightens again inside the window, the retail fallback for a driver with no room set, and the blue-fire rule.
- Turbo Track with the all-super-turbo-pads cheat (`CTR_DEV_CHEATS=1048576`) and the autopilot, which drives over the first pad at USF (fire cap 0x4800):
  - With Blue Fire on, blue pixels around the kart in the frame over the pad go from 95 to 1646.
  - The reserves meter is drawn under the powerslide bar.
  - The race trace (2086 frames) is identical with all four rows on and all off.
- Cyan Super Pads, from the dump taken after marking:
  - Hot Air Skyway: its two super pads (quads 411, 414) turn cyan; its ordinary pads (1629, 1630) do not. With the cheat, all seven of its pads do.
  - Oxide Station: its five super-pad quads; ordinary pads such as 318, 352 and 457 stay as they were.
  - Cortex Castle: its two pad quads; the wooden floor tiles under its other super triggers stay as they were.
  - Turbo Track, whose 30 pads are all super pads: all 30.
  - On Turbo Track the frame over the first pad changes only within the pad (rows 238 to 342 of 450), green to cyan; a 2-player race shows it in the split-screen view too, and its trace is identical with the row on or off.
- Not checked by a run: the gradient and the kept fire while powersliding, because the autopilot doesn't powerslide. The gradient's colour maths is covered by the test.
