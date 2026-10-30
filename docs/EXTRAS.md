# Extras

OPTIONS → Extras holds optional HUD and effect mods from the CTR-ModSDK modules, in the form High Octane ships them. Each one is its own row, saved in `config.ini` under `[Extras]`. Every row is visual only and applies at once.

| Row | Key | Default | What it does | Code |
|---|---|---|---|---|
| Reserves Meter | `reserves_meter` | On in the Archipelago build, off in vanilla | A 49×3 bar under the powerslide bar showing the stacked boost ("reserves") you hold, in 1P races and crystal challenges. Red, yellow and green by amount; blue once it overruns the bar; purple once the value has wrapped past 0x7FFF and no longer drains ("Saffi fire"). | `UI_DrawReservesMeter` in `game/UI/UI_Meter.c`, maths in `ap/ap_reserves_meter_logic.h` |
| Keep Reserve Fire | `reserve_fire` | Off | While a driver holds reserves, fire earned from a powerslide stays visible. Retail kills it after two frames or hides it for the visibility cooldown, so chained slides with no start boost show no fire. | `VehFire_KeepReserveFire` in `game/Vehicle/VehFire.c` |
| Blue Fire At USF | `blue_fire` | Off | Player 1's exhaust turns blue while they carry Ultimate Speed Fire, as in Nitro-Fueled. | `game/Vehicle/VehBlueFire.c` |
| Slide Meter Gradient | `slide_meter_colours` | Off | The powerslide bar fades from green to yellow as it fills, then through orange to red across the boost window, instead of flat green then red. | `UI_DrawSlideMeter`, maths in `include/ctr_extras.h` |

Cyan super turbo pads, the last mod in this set, are not done yet (see [ROADMAP.md](../ROADMAP.md)).

## Decisions
- **Options, default off.** The retail look stays the default; each mod changes a familiar cue. The reserves meter defaults on in the Archipelago build only because that build always drew it.
- **One implementation for both builds.** The Archipelago build already had the reserves meter, the fire tweak and the blue fire palette swap, each tied to an Archipelago session or item. They are now shared:
  - The reserves meter draws whenever the option is on. The speed readout under it stays Archipelago-only.
  - The fire tweak reads the option. With no `config.ini`, the Archipelago build still honours `hud_reserves_fx=1` in `ap-config.txt` (`AP_HudReservesFx`, the same precedence as `map_flash`).
  - The blue palette has one writer, `VehBlueFire_Tick`, run every frame from `MainMain`. It turns blue when the option asks for it or when the Archipelago Progressive Boost capstone does (`AP_BlueFireWanted`), so the two never fight over the same VRAM cells.
- **The fire tweak is the ModSDK module's whole tweak.** The Archipelago version only stopped the visibility-cooldown hide. The module also keeps a powerslide's first flame from dying after two frames, which is the half that shows fire after a race started with no start boost; both halves are in now.
- **USF is detected from the driver's own constants.** The BlueFire module compared the fire speed cap with 0x4800, the value a super turbo pad gives with retail's constants (measured in the run below). Here it is "cap above the sacred-fire cap", the test `VehFire_Increment` itself uses for USF.
- **Palettes are only written while a race is loaded.** Hubs and menus put other content in the same VRAM cells, so outside a race the palette state is forgotten and rewritten at the next race (the Archipelago version's rule, `ap/ap_blue_fire_logic.h`).

## Checked
- `tools/test-extras.c`: the gradient's colours at each stage, that it never brightens again inside the window, the retail fallback for a driver with no room set, and the blue-fire rule.
- Turbo Track with the all-super-turbo-pads cheat (`CTR_DEV_CHEATS=1048576`) and the autopilot, which drives over the first pad at USF (fire cap 0x4800):
  - With Blue Fire on, blue pixels around the kart in the frame over the pad go from 95 to 1646.
  - The reserves meter is drawn under the powerslide bar.
  - The race trace (2086 frames) is identical with all four rows on and all off.
- Not checked by a run: the gradient and the kept fire while powersliding, because the autopilot doesn't powerslide. The gradient's colour maths is covered by the test.
