# 200cc speed class

Arcade and VS races can run at **200cc**: every racer, bots included, is about 20% faster. The SPEED screen (CLASSIC / 200CC) follows DIFFICULTY in Arcade and the player count in VS, for single races and cups, then goes to character select. It opens on the last pick, saved in `config.ini` as `[State] speed_class` (0 Classic, 1 200cc).

Time Trial, Adventure (relic times, bosses, Archipelago logic), Battle and the attract demo always run at retail speed. Arcade and VS keep no records, so 200cc never touches the save.

## What changes
| Value | Scale | Code |
|---|---|---|
| Top speed, speed with wumpa, acceleration, reserves acceleration | ×6/5 | `SpeedClass_ApplyConsts` after `VehBirth_SetConsts` |
| Boost sizes (single turbo, sacred fire, mask), damage slowdown | ×6/5 | same |
| Steering rate (`const_TurnRate`) | ×11/10 | same |
| Speed caps written as literals: the player's base-speed cap and kart speed cap (0x6400), the bots' target, boost and air caps | ×6/5 | `SpeedClass_Speed` in `VehPhysGeneral_GetBaseSpeed`, `VehPhysForce_OnApplyForces`, `BOTS.c` |

Rules are in `include/ctr_speed_class.h`, game code in `game/SpeedClass.c`, the screen in `game/230/MM_MenuFlow.c`.

## Decisions
- **6/5, not more.** Speeds are kept in `s16`. The scaled cap (30720) plus one frame of reserves acceleration stays under 32767; at 5/4 it would overflow.
- **Acceleration scales with top speed**, so reaching top speed takes as long as in Classic.
- **Steering scales by 11/10**, about the square root of the speed factor. At the same steering rate a 20% faster kart takes 20% wider corners; the full 20% would make low-speed steering twitchy. Drift timing is in time, not distance, so it is unchanged. Low-speed turn resistance is already relative to the class top speed.
- **Bots** read the player's scaled stats for their speed target. Their catch-up term and their literal caps scale too, so the field keeps its retail order.
- **Records:** nothing to block. Only Time Trial and Relic races offer records (`MainGameEnd_SoloRaceGetReward`).

## Checking
- `tools/test-speed-class.c`: the scaling, the `s16` headroom and which modes take the class.
- An Arcade race with the autopilot, Classic against 200cc (`-Config @{'State.speed_class'=1}`, `CTR_DEV_RACE=3`, `CTR_DEV_AUTOPILOT=1`, `CTR_DEV_NO_ITEMS=1`, `CTR_DEV_RESULTS`): every racer covers about 21% more track in 30 seconds and the ranks match. `CTR_DEV_PLAYERS=2` does the same for VS. A Time Trial trace is byte-identical at both settings.
- The screen: `CTR_DEV_PAD` walks the title menu once it settles (about frame 2000 with `-Track -1`); `CTR_DEV_PLAYERS=2` connects a second pad so the VS player rows unlock.
