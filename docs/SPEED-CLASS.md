# 200cc speed class

Arcade, VS and Time Trial races can run at **200cc**: every racer, bots included, is about 20% faster. The SPEED screen (CLASSIC / 200CC) follows DIFFICULTY in Arcade, the player count in VS (single races and cups) and the Time Trial row, then goes to character select. It opens on the last pick, saved in `config.ini` as `[State] speed_class` (0 Classic, 1 200cc).

Adventure (relic races included), Battle and the attract demo always run at retail speed.

## Time Trial records
A 200cc Time Trial never reads or writes the memory card's Time Trial data. Its best lap, five best times and ghost live in files next to the exe (`include/platform/native_custom_records.h`):

| Track | Files |
|---|---|
| Retail | `records/track<NN>-3l-200cc.times` and `.ghost` (NN is the level id) |
| Custom | `custom-records/<identity>-200cc.times` and `.ghost`, next to its Classic files |

- **Classic stays on the memory card**, exactly as retail, so old saves need no migration and a Classic record is never overwritten by a 200cc one.
- **No staff ghosts at 200cc.** N. Tropy and Oxide were recorded at retail speed, so they are not raced, their stars are hidden on the track select, and their flags and the scrapbook unlock are never touched.
- **The ghost loads from the file.** Leaving the track select skips the memory card's ghost selection and races the saved 200cc ghost, if any; a run that beats it (or runs without one) replaces it. There is no Save Ghost row and the track select shows no memory card ghost count.
- **SAVE writes the file.** The name entry after a record stores it in the table and its `.times` file.

`TimeTrialRecords_OffCard` (no memory card data: custom track or 200cc) and `TimeTrialRecords_Active` (records in files) in `game/TimeTrialRecords.c` are the two checks every call site uses.

**High Scores screen.** The title menu's High Scores show CLASSIC or 200CC at the top right of the Time Trial page; L1 or R1 switches it, and it opens on the class last picked on the SPEED screen. The 200cc page reads `records/` (`TimeTrialRecords_RetailTable200cc`, the same table a race uses) and shows no staff ghost stars. The Relic Race page has no class. The switch is native code in `game/230/MM_HighScore.c`.

## What changes
| Value | Scale | Code |
|---|---|---|
| Top speed, speed with wumpa, acceleration, reserves acceleration | ×6/5 | `SpeedClass_ApplyConsts` after `VehBirth_SetConsts` |
| Boost sizes (single turbo, sacred fire, mask), damage slowdown | ×6/5 | same |
| Steering rate (`const_TurnRate`) | ×11/10 | same |
| Speed caps written as literals: the player's base-speed cap and kart speed cap (0x6400), the bots' target, boost and air caps | ×6/5 | `SpeedClass_Speed` in `VehPhysGeneral_GetBaseSpeed`, `VehPhysForce_OnApplyForces`, `BOTS.c` |

Rules are in `include/ctr_speed_class.h`, game code in `game/SpeedClass.c` (`SpeedClass_TimeTrial` for a 200cc Time Trial, menus included), records in `game/TimeTrialRecords.c`, the screen in `game/230/MM_MenuFlow.c`.

## Decisions
- **6/5, not more.** Speeds are kept in `s16`. A super turbo pad pushes about 3950 past the speed cap in one frame: retail peaks at 29552 against its 25600 cap, and at the 200cc cap (30720) the push passes 32767. `VehPhysCrash_ConvertVecToSpeed` saturates the stored speeds at 32767 instead of letting them wrap negative (a no-op in retail), and the next frame's cap brings the kart back to 30720. At 5/4 the bots' target cap (0x6900) would no longer fit either.
- **Acceleration scales with top speed**, so reaching top speed takes as long as in Classic.
- **Steering scales by 11/10**, about the square root of the speed factor. At the same steering rate a 20% faster kart takes 20% wider corners; the full 20% would make low-speed steering twitchy. Drift timing is in time, not distance, so it is unchanged. Low-speed turn resistance is already relative to the class top speed.
- **Bots** read the player's scaled stats for their speed target. Their catch-up term and their literal caps scale too, so the field keeps its retail order.
- **Records per class, in files.** 200cc Time Trial records and ghosts sit beside the memory card rather than in a new save layout: the card stays byte-compatible with retail and every existing save. Relic races stay retail, so the only other record table is untouched.
- **No Archipelago.** Archipelago plays Adventure, whose relic times, bosses and logic are tuned for retail speed; rescaling them is a separate feature, and an item unlocking 200cc would do nothing in a seed, since only Arcade, VS and Time Trial take it.

## Checking
- `tools/test-speed-class.c`: the scaling, the `s16` headroom and which modes take the class.
- `tools/test-custom-records.c`: retail and 200cc keys, file names, the `class` line and the keyed ghost layout, never read as one another. `tools/test-custom-time-trial.c`: a 200cc retail Time Trial on the real end-of-race code keeps its table and ghost in `records/` and leaves the save untouched; a Relic Race and a Classic Time Trial still use the card.
- A Time Trial with the autopilot (`CTR_DEV_RACE=3`, `CTR_DEV_MODE=tt`, `-Config @{'State.speed_class'=1}`): at 20 s the kart has covered about 19% more track than in Classic. Two runs in the same `-RunDir`: the first logs `[Records] ghost saved to records`, the second `[Records] saved ghost loaded` (the dev switch loads it as the track select does).
- An Arcade race with the autopilot, Classic against 200cc (`-Config @{'State.speed_class'=1}`, `CTR_DEV_RACE=3`, `CTR_DEV_AUTOPILOT=1`, `CTR_DEV_NO_ITEMS=1`, `CTR_DEV_RESULTS`): every racer covers about 21% more track in 30 seconds and the ranks match. `CTR_DEV_PLAYERS=2` does the same for VS.
- Ultimate Speed Fire: Turbo Track (`CTR_DEV_RACE=17`) with every pad a super turbo pad (`CTR_DEV_CHEATS=1048576`) and the autopilot, traced. At 200cc the speed peaks at 32767 and never goes below normal reversing; without the saturation it wrapped to -31000. The Classic trace is byte-identical with and without it.
- High Scores: with a `records/track03-3l-200cc.times` in the run folder, `-Track -1` and `CTR_DEV_PAD=2000:D,2030:D,2060:D,2090:D,2120:D,2150:X,2400:J` open Crash Cove's page in Classic, then switch it to 200cc.
- The SPEED screen: `CTR_DEV_PAD` walks the title menu once it settles (about frame 2000 with `-Track -1`); `CTR_DEV_PLAYERS=2` connects a second pad so the VS player rows unlock. Go through it twice, with a race and Quit between: `MM_SpeedClass_Open` resets the menu because it lives outside overlay 230's data, which loading the menu level restores, and `MM_ResetAllMenus` cannot reach it once that data has dropped the link.
