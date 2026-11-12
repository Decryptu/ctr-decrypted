# Cheats

OPTIONS → Cheats lists the retail title-screen cheats as toggles. Code: `game/230/MM_Cheats.c`, a bespoke page included by `game/230/MM_ConfigMenu.c`.

| Row | Bit (`include/namespace_Main.h`) | Retail code |
|---|---|---|
| Max Wumpa | `CHEAT_WUMPA` | SEEDS |
| Infinite Masks | `CHEAT_MASK` | WARLORDS |
| Infinite Turbos | `CHEAT_TURBO` | AXELWAX |
| Infinite Bombs | `CHEAT_BOMBS` | ARSENAL |
| Super Engine | `CHEAT_ENGINE` | UNLEADED |
| Invisibility | `CHEAT_INVISIBLE` | UNSEEN |
| Super Turbo Pads | `CHEAT_TURBOPAD` | ARROW |
| Icy Tracks | `CHEAT_ICY` | SLEDROAD |
| Adventure AI | `CHEAT_ADV` | ENDNEO |
| Super Hard | `CHEAT_SUPERHARD` | DELUXE |
| One Lap Race | `CHEAT_ONELAP` | SUDDENDEAD |
| Turbo Counter | `CHEAT_TURBOCOUNT` | ADDON |

Retail codes are typed on the title screen while holding L1 and R1 (`MM_ParseCheatCodes`, the table in `game/230/D230.c`).

## Rules
- **A row is the cheat's `gameMode2` bit, nothing else.** The page reads and flips `gGT->gameMode2`, so a typed code shows as ON and the page and the codes never disagree. Nothing is saved: a cheat lasts until the game closes, as in retail.
- **Retail's own clears stay.** Starting Adventure or Time Trial turns off the five item cheats (Max Wumpa, masks, turbos, bombs, Super Engine), so they read OFF afterwards. The row's help line says so.
- **Unlock codes stay codes.** The racer, track and scrapbook unlock codes change the save once; they are not toggles and have no row.
- **Both clients have the page.** Archipelago does not gate retail cheats, typed or toggled.

## Checking
`tools/test-options-pages.c` checks the page order. To see the page: `CTR_DEV_OPTIONS=Cheats` opens OPTIONS on it ([DEV-TOOLS.md](DEV-TOOLS.md)), and `CTR_DEV_PAD` can toggle rows.
