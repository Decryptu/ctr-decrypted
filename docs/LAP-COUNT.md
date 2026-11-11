# Lap count 1 to 9

Arcade and VS races can run from 1 to 9 laps. Retail's LAPS box offers 3, 5 and 7, and only for single races; cups always run 3 laps.

- **Single races** (retail and custom tracks): the LAPS box after the track select lists 1 to 9. It opens on the last count picked, 3 the first time. A custom track's Arcade race takes the count picked over its package's own.
- **Cups** (Arcade and VS): picking a cup opens the same LAPS box. The count holds for every race of the cup. Circle goes back to the cup list.
- The one-lap cheat and Archipelago's one-lap cups (`one_lap_cups`) still force one lap, and the cup's LAPS box is skipped for them.
- Adventure, Time Trial and Battle are unchanged.

High Octane has the same feature (commits `4871e8312` and `875fd120b` on `highoctane/vita`); it was ported by hand, as its menus and clock code differ from ours.

## Code

| Part | Where |
|---|---|
| Rows `1` to `9` (`CTR_LNG_LAP_1`..) and their labels | `include/ctr_menu_strings.h`, `RECTMENU_RowString` in `game/RECTMENU.c` |
| The LAPS box rows, last pick and lap count | `MM_LapSelect_Prepare` and `MM_LapSelect_LapCount` in `game/230/MM_TrackSelect.c` |
| Custom track LAPS box | `game/230/MM_CustomTrackSelect.c` |
| Cup LAPS box | `MM_CupSelect_OpenLapMenu` in `game/230/MM_CupSelect.c` |
| Lap times | `UI_LapTime_*` in `game/UI/UI_Reward.c` |
| Results box height | `AA_EndEvent_DisplayTime` in `game/222.c` |

## Lap times past lap 7

Retail stores lap time digits for two players and seven laps (`sdata->LapTimes`, 14 slots), and `gGT->lapTime` has seven entries. An 8 or 9 lap race would write past both. The native build keeps every lap time of every driver in its own table (`UI_LapTime_Get`), writes the retail slots only where the index fits, and the race clock, the results lap list and the best-lap check read that table. From 7 laps on, the total time shows the tens-of-minutes digit, as retail's 7 lap races do. From 4 laps the results box lists the laps in the small font, one row each, and grows with the count.

## Checking

`CTR_DEV_RACE=<level> CTR_DEV_LAPS=9 CTR_DEV_AUTOPILOT=1` races 9 laps; capture the results screen after the finish to see all nine lap times in the box.
