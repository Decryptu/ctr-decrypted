# Character select and custom racers

The native character select is built from a roster list instead of retail's hand-made icon tables, so it can hold N. Oxide and any number of custom racers. With the retail racers only, it draws, animates and navigates exactly like retail.

Code: `game/230/MM_CharacterGrid.c` (included from `MM_Characters.c`; the PS1 build keeps the retail functions there). The racer library behind it: `platform/native_racers.c`, `include/platform/native_racers.h`.

## The roster
In this order:
1. The 15 retail racers, in each layout's reading order (the order retail's tables place them).
2. N. Oxide, once unlocked (`GAME_UNLOCK_BIT_OXIDE`: beat him in Adventure, or the Spyro 2 demo code).
3. The custom racers, in library order.

A locked retail racer keeps an empty place, as in retail, except in the last retail row. Extra racers join that row, so empty places there would push them off-centre.

## The layout
Each player count has a list of rows in fill order, each with a height and a capacity:

| Players | Rows (fill order) | Centre |
|---|---|---|
| 1P/2P | three rows of 6, top down | x 0xE0 |
| 3P | three rows of 4, then the top row of 4 | x 0x80 |
| 4P | two rows of 6 between the windows, 2 under them, 2 above | x 0xE0 |

Entries flow into the rows. Every row is centred, so 15 racers land exactly on retail's positions, and Oxide makes the 1P bottom row four racers wide, still centred. When a page is full, the rest go on more pages. Arrows beside the grid show that there is another page.

- In 1P/2P, when nothing is in the last row (no secret racer and no extra racer), the grid moves down 0x20 under the "Select Character" title. That is retail's unexpanded layout.
- In 3P/4P the title is drawn when the page's top row is empty.
- Icons slide in with retail's stagger. A turned page slides in from the side it was turned toward.

## Navigation
- Left and right move along the row. Up and down go to the nearest row with an icon at most half a column aside. When two icons tie, down goes right and up goes left. Retail's own links follow these rules too.
- Icons taken by another player are passed over.
- At a dead end (every icon that way taken, or none at all), the cursor first steps one place across, then makes the move from there. Across means down then up for left and right, and left then right for up and down. Retail does the same with its fallback directions.
- Off the end of a row, left and right turn to the previous or next page. L1 and R1 turn the page too. The page is shared: any player who has not chosen yet can turn it. A cursor whose racer is on another page is hidden, and comes onto the shown page at its next d-pad press.

Players are tracked by roster entry. `GAME_CHARACTER_IDS` gets the entry's character ID, and `NativeRacers_SetPlayer` gets its racer index (-1 for a retail racer).

## Checking
`CTR_DEV_CHARACTER_SELECT=1` opens the screen ([DEV-TOOLS.md](DEV-TOOLS.md)). `CTR_DEV_FAKE_RACERS=n` adds n placeholder racers that borrow the first eight retail racers' looks, to fill pages. To check that retail is unchanged, A/B the screen against an older build with `CTR_DEV_UNLOCKS=0` and `=1` at 1 to 4 players, with `CTR_DEV_PAD` navigation scripts that hit taken icons and row ends. Every frame should match except the exit slide-out, which older builds did not animate.
