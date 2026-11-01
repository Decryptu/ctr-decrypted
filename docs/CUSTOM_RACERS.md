# Character select and custom racers

The native character select is built from a roster list instead of retail's hand-made icon tables, so it can hold N. Oxide and any number of custom racers. With the retail racers only, it draws, animates and navigates exactly like retail.

Code: `game/230/MM_CharacterGrid.c` (included from `MM_Characters.c`; the PS1 build keeps the retail functions there). The racers behind it: `platform/native_racers.c` (models, portraits, the players' picks) and `platform/native_racer_library.cpp` (the installed packages).

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

## Racer packages
A racer is a folder under `assets/racers/` (any name) holding `racer.json` and the files it lists. Package builds only (`CTR_CUSTOM_PACKAGES`, with the Mods screen). The library reads every folder once, the first time the character select needs it (`platform/native_racer_library.cpp`). A folder whose manifest or files do not check out is left out with a `[Racers]` line in the log.

```json
{
  "schema_version": 1,
  "id": "uuid",
  "name": "CRUNCH BANDICOOT",
  "short_name": "CRUNCH",
  "version": "1",
  "authors": ["robthefryman", "Angel123/Booz"],
  "credits": "",
  "source": {"file": "ctr-u_CrunchKart.xdelta", "sha256": "...", "url": "https://gamebanana.com/mods/575350"},
  "template": "crash",
  "engine": "balanced",
  "wheels": true,
  "files": {
    "model": {"path": "model.ctr", "bytes": 14536, "sha256": "..."},
    "portrait": {"path": "portrait.bin", "bytes": 612, "sha256": "..."}
  }
}
```

- `name` and `short_name`: printable ASCII, drawn with the game's font (under 32 and 16 bytes).
- `template`: the retail racer whose character ID it borrows: `crash cortex tiny coco ngin dingodile polar pura pinstripe papu roo komodo tropy penta fakecrash oxide`.
- `engine`: `balanced`, `accel`, `speed` or `turn`. `wheels`: whether the game draws its tires.
- `model.ctr`: a racer model file as the BIGFILE stores one (a u32 pointer-map offset, the model, the pointer map), at most 512 KiB. The menu draws it scaled 5/8, as N. Oxide's.
- `portrait.bin`: `CDP1`, u16 width 44, u16 height 26, 16 palette words, then 26 rows of 11 words of 4-bit texels. PS1 15-bit colours, word 0 transparent.
- Every file's size and SHA-256 must match. The id is unique: a second folder with the same id is left out.

## Portraits: the racer VRAM bank
Portraits go to a second VRAM, a GL texture with the same size and format as the PS1 VRAM (`NativeRenderer_CreateVramBank`). A primitive whose tpage word has both bits 14 and 15 set (`NATIVE_TPAGE_RACER_BANK`, `include/platform/native_gpu.h`) samples the bank instead of VRAM. Its page, palette, colour depth and blending stay as they are, so the paletted shaders and every icon draw path work unchanged. The two bits alone keep their Archipelago meanings (box atlas, trial letters).

Portrait i sits in a 64×32-texel slot: four across and eight down per 4-bit page, from page 0 of the first page row (64 racers fill two pages). Its palette goes in page 15, four per row. Each racer gets an `Icon` whose layout points there (`platform/native_racers.c`). The Mods screen's track picture also lives in the bank, at (512, 256) in the second page row ([CUSTOM_TRACKS.md](CUSTOM_TRACKS.md#4-the-mods-screen-and-the-library)).

## Converting PS1 racer mods
Racer mods for the disc are xdelta patches that replace a retail racer. `tools/racers/convert_racer.py` applies each patch to a clean disc and writes one package per racer it replaces:

- The model is the patched `BIGFILE` racer model (high detail).
- The portrait is cut from the patched shared VRM at the template's portrait (44×26, 4-bit; the positions are in the script).
- The names come from the patched English language file.

A racer whose mod kept the retail name or portrait is refused until `sources.json` names it; nothing from the retail disc goes into a package. `sources.json` also holds authors, credits and the source URL, keyed by patch file name. Two mods with the same model give one racer. The script prints what it wrote, skipped and refused.

```sh
gcc -O2 -Iinclude tools/racers/xdelta_apply.c -o tools/racers/xdelta_apply.exe
python tools/racers/convert_racer.py --disc "CTR (USA).bin" --sources sources.json --out racers/ mods/*.xdelta
```

The catalogue, [Decryptu/ctr-decrypted-racers](https://github.com/Decryptu/ctr-decrypted-racers), keeps the converted racers under `racers/`, with its `sources.json`. `tools/racers/build_index.py <catalogue>` checks every racer folder and writes `index.json` (each racer's folder, its authors, plus the size and SHA-256 of its `racer.json`) and `CREDITS.md`. It refuses a racer with no authors. A racer that reuses a look from another release says so in its `credits` (the Japanese Crash credits the Japanese release).

The racer mods converted so far are static, vertex-coloured meshes: no textures and no animations, as they are on a PS1. Their kart textures, voices and code patches are not carried over.

## The Racers tab
The Mods screen's Racers tab (`game/230/MM_CustomRacers.c`) lists every racer in the catalogue's `index.json` and every installed racer the catalogue does not list, by name, with `OWNED` on installed ones. The panel shows the portrait (once installed), the authors and the retail racer it is based on. Cross downloads a racer, or asks to remove an installed one. L2 reads the catalogue again; R2 searches names and authors.

- The downloads come from `raw.githubusercontent.com`, the catalogue's `main` branch, through the same HTTPS code as Project Saphi (`ap/ap_custom_track_download.cpp`, `NativeRacerCatalogue_*`). The index pins each `racer.json` (size and SHA-256), and `racer.json` pins its files, so the repository's `.gitattributes` keeps `racer.json` byte for byte (`eol=lf`).
- `NativeRacerLibrary_Install` checks the files against each other, writes them to a hidden `.<folder>.part` folder, removes any installed folder with the same racer id, then renames the staging folder into place. `NativeRacerLibrary_Remove` deletes the racer's folder.
- After either, `NativeRacers_Reload` reads the library again, so the character select has the change at once. It clears the players' racer picks, since racer indices change.

`CTR_DEV_MODS=3` opens the tab ([DEV-TOOLS.md](DEV-TOOLS.md)). `CTR_DEV_PAD` presses then drive it; the catalogue loads within about 300 frames of a fast-forwarded run.

## In a race
A driver races as a custom racer when it is a human player who picked one, outside Adventure and the menus, and its character ID is still the racer's template (`NativeRacers_Driver`). Anything that changed the ID since (Archipelago's character swap, the garage) changed the racer too. Then:

- `VehBirth_NonGhost` births it with the racer's model instead of the template's, at every split-screen size.
- `VehBirth_TireSprites` gives it wheels or not by the racer's `wheels`.
- `VehBirth_SetConsts` reads the racer's engine class (`VEH_BIRTH_ENGINE_ID`, `include/common.h`). Engine sound stays the template's.
- The HUD portraits use the racer's (`VEH_DRIVER_ICON`): the race rank list, cup standings, the race results and the VS and battle standings.

Everything else keyed by character ID uses the template: voice, podium, records, ghosts, Archipelago. Bots never pick a custom racer.

`CTR_DEV_RACER=n` with `CTR_DEV_RACE` puts player 1 in a race as the nth installed racer.

## Checking
`CTR_DEV_CHARACTER_SELECT=1` opens the screen ([DEV-TOOLS.md](DEV-TOOLS.md)). `CTR_DEV_FAKE_RACERS=n` replaces the installed racers with n placeholders that borrow the first eight retail racers' looks, to fill pages. To check that retail is unchanged, A/B the screen against an older build with `CTR_DEV_UNLOCKS=0` and `=1` at 1 to 4 players, with `CTR_DEV_PAD` navigation scripts that hit taken icons and row ends. Every frame should match except the exit slide-out, which older builds did not animate.
