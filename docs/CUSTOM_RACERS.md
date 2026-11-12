# Character select and custom racers

The native character select builds its layout from a roster that can include N. Oxide and custom racers. With only the retail racers and Character Select Stats off, it uses retail's icon positions and animations.

Code: `game/230/MM_CharacterGrid.c` (included from `MM_Characters.c`; the PS1 build keeps the retail functions there). Racers: `platform/native_racers.c` (models, portraits, the players' picks) and `platform/native_racer_library.cpp` (installed packages).

## The roster
In this order:
1. The 15 retail racers, in each layout's reading order (the order retail's tables place them).
2. N. Oxide, once unlocked (`GAME_UNLOCK_BIT_OXIDE`: beat him in Adventure, or the Spyro 2 demo code).
3. The custom racers, in library order.

Custom racers are new slots, not replacements. Each has its own icon, name, model and textures, and borrows a retail character ID (its template) only for systems keyed by that ID (voice, podium, records, Archipelago). Real IDs above 15 would touch about a hundred sites, save formats included (a memory card ghost packs the ID into 4 bits). Portraits go to a second VRAM bank, so the template keeps its own and can race in the same field.

A locked retail racer keeps an empty place, as in retail, except in the last retail row, where gaps would push extra racers off-centre.

## The layout
Each player count has a list of rows in fill order, each with a height and a capacity:

| Players | Rows (fill order) | Centre |
|---|---|---|
| 1P/2P | three rows of 6, top down | x 0xE0 |
| 3P | three rows of 4, then the top row of 4 | x 0x80 |
| 4P | two rows of 6 between the windows, 2 under them, 2 above | x 0xE0 |

Entries flow into the rows, and every row is centred, so 15 racers land exactly on retail's positions and Oxide makes the 1P bottom row four wide. A full page spills onto more pages; arrows beside the grid show another page.

- In 1P/2P, with nothing in the last row (no secret or extra racer), the grid moves down 0x20 under the "Select Character" title: retail's unexpanded layout.
- In 3P/4P the title is drawn when the page's top row is empty.
- Icons slide in with retail's stagger. A turned page slides in from the side it was turned toward.

## Navigation
- Left and right move along the row. Up and down go to the nearest row with an icon at most half a column aside; on a tie, down goes right and up goes left. Retail's own links follow these rules.
- Multiple players can highlight and confirm the same racer. A shared portrait shows each player's number in white while choosing and orange once confirmed. The menu remembers each player's pick, including shared picks.
- At a dead end, the cursor first steps one place across, then moves from there. Across is down then up for left and right, and left then right for up and down, like retail's fallback directions.
- Off the end of a row, left and right turn the page; so do L1 and R1. The page is shared: any player who has not chosen can turn it. A cursor whose racer is on another page is hidden until its next d-pad press brings it onto the shown page.

Players are tracked by roster entry. `GAME_CHARACTER_IDS` gets the entry's character ID, and `NativeRacers_SetPlayer` its racer index (-1 for a retail racer).

## Regional Fake Crash assets

The Japanese release (SCPS-10118) fixes Fake Crash's face and gives him a paler kart. To use it, run `python tools/decrypted/extract_fakecrash_jp.py "<NTSC-U disc.bin>" "<JP disc.bin>" "<game folder>/assets"`. It writes `assets/FAKECRASH-JP/<BIGFILE index>.BIN`, and both clients serve those entries in place of the disc's (`NativeAssets_BigfileOverride` in `platform/native_assets.c`, called from `LOAD_ReadFile_ex`). The game still boots from the NTSC-U disc. The set is all or nothing: with a file missing, the log says so and Fake Crash stays NTSC-U.

What the tool builds, and why:
- **Standalone models 256, 322, 346** (high, medium, low) are the Japanese files as they are. In the Japanese release both one-based texture slots 100 and 101 match their Arcade layouts (in PAL they share one layout), so a Japanese model always goes with Japanese texels.
- **Driver packs 259, 274, 290, 306, 362** stay NTSC-U except for the `fake` model, which is swapped for the Japanese one. The Japanese packs rename TNT, Nitro and token models, so the rest must not come from them. The model is self-contained (no pointer leaves it, and only the pack's model array points at it); the tool checks this, shifts the pointers after it and keeps the pointer map the same size.
- **Main-menu level 216** gets the same swap; the character select draws this copy. The Japanese menu keeps Fake Crash's texels in its own VRM at places where the NTSC-U menu has other textures, so the tool points each of the menu model's texture slots at the matching slot of the standalone model instead. Those sample the shared VRM, which the menu loads too.
- **Shared VRM 258**: the 192 words the Japanese models sample and that differ from NTSC-U. The models sample the same set of VRAM words on both discs at each detail level. PAL packs these palettes elsewhere, so PAL is no source.

Check: an A/B capture with and without the folder in the run's `assets`. Use `CTR_DEV_RACE=3`, `CTR_DEV_CHARACTER=14`, `CTR_DEV_UNLOCKS=1`, `CTR_DEV_WARP=-657,757,-4658,3070`, `CTR_DEV_CAMERA=-780,812,-4658,2098,3069,0`, `CTR_DEV_MODE=tt` and Skip Intro, frame 1100; and frame 1300 with `CTR_DEV_CHARACTER_SELECT=1` (the select turns him to face the camera). Only Fake Crash's pixels change (about 5,600 and 1,500 at 800x450): his eyes are half-closed and the kart is paler. Don't judge Arcade from that warp: it drops him onto the Crash bot's grid slot, so the two karts are drawn on top of each other. A `CTR_DEV_DUMP_FRAME` dump shows that Arcade draws Fake Crash with the same pages, palettes and UVs as Time Trial.

## Character stats

OPTIONS > Extras > Character Select Stats enables a comparison panel beside each player's model, for one to four players. Player 1 can also toggle it on the select screen with L2 (the prompt shows the bound keyboard or controller button). The choice is saved as `[Extras] character_stats`, off by default.

The panel follows the highlighted roster entry, including while turning pages. Retail racers use their metadata engine class; custom racers use `NativeRacers_Engine`, including a package's explicit engine override. Penta uses the NTSC-U turning class or three full PAL bars according to OPTIONS > Extras > Penta (Next Race); Oxide uses the balanced class. The Penta choice also changes his driving stats ([EXTRAS.md](EXTRAS.md#penta-stats)) and never overrides a custom racer's engine. Speed, acceleration and turning bars are relative class comparisons based on High Octane's viewer, not physical units or the retail garage's ratings. They describe the base engine, before Archipelago progressive stats or race modifiers.

## Racer packages
A racer is a folder under `assets/racers/` (any name) holding `racer.json` and the files it lists. Package builds only (`CTR_CUSTOM_PACKAGES`, with the Mods screen). The library reads every folder once, when the character select first needs it. A folder whose manifest or files don't check out is skipped with a `[Racers]` log line.

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
- `model.ctr`: a racer model as the BIGFILE stores one (a u32 pointer-map offset, the model, the pointer map), at most 512 KiB. The character select draws a racer with wheels at race size, since the menu's wheels sit where the race's do; one without wheels is scaled 5/8 to fit, like N. Oxide's hovercraft.
- Converted models have no animations and can use all 256 vertex slots of a command list. Retail models stay under the 88 that fit the scratchpad's vertex cache, so slots past it live in their own array (`RenderBucket_PackedVertexScratch`, `game/RenderBucket/RenderBucket_QueueExecute.c`). Past the scratchpad lies the first draw buffer, so a slot written there cuts its draw list short (the HUD and loading flag flicker every other frame).
- `portrait.bin`: `CDP1`, u16 width 44, u16 height 26, 16 palette words, then 26 rows of 11 words of 4-bit texels. PS1 15-bit colours, word 0 transparent.
- Every file's size and SHA-256 must match. The id is unique: a second folder with the same id is skipped.

## Portraits: the racer VRAM bank
Portraits go to a second VRAM, a GL texture with the PS1 VRAM's size and format (`NativeRenderer_CreateVramBank`). A primitive whose tpage word has both bits 14 and 15 set (`NATIVE_TPAGE_RACER_BANK`, `include/platform/native_gpu.h`) samples the bank instead of VRAM, with its page, palette, depth and blending unchanged, so the paletted shaders and every icon draw path work as is. Either bit alone keeps its Archipelago meaning (box atlas, trial letters).

Portrait i sits in a 64×32-texel slot, four across and eight down per 4-bit page, from page 0 of the first page row (64 racers fill two pages). Its palette goes in page 15, four per row. Each racer gets an `Icon` whose layout points there (`platform/native_racers.c`).

Other users of the bank, which anything new must stay clear of:
- The Mods screen's track picture, at (512, 256) in the second page row ([CUSTOM_TRACKS.md](CUSTOM_TRACKS.md#4-the-mods-screen-and-the-library)).
- The title screen's Roo (`ap/ap_title_roo.c`, Archipelago build): his podium textures at (512, 0), 384×256. He dances in Crash's place using his podium dance model from the player's disc, with texture layouts tagged to sample the bank, so the main menu's VRAM is untouched.

## Converting PS1 racer mods
Racer mods for the disc are xdelta patches that replace a retail racer. `tools/racers/convert_racer.py` applies each patch to a clean disc and writes one package per racer it replaces:

- The model is the patched `BIGFILE` racer model (high detail).
- The portrait is cut from the patched shared VRM at the template's portrait (44×26, 4-bit; positions in the script).
- The names come from the patched English language file.

A racer whose mod kept the retail name or portrait is refused until `sources.json` names it; nothing from the retail disc goes into a package. `sources.json` also holds authors, credits and the source URL, keyed by patch file name. Two mods with the same model give one racer. The script prints what it wrote, skipped and refused.

```sh
gcc -O2 -Iinclude tools/racers/xdelta_apply.c -o tools/racers/xdelta_apply.exe
python tools/racers/convert_racer.py --disc "CTR (USA).bin" --sources sources.json --out racers/ mods/*.xdelta
```

The catalogue, [Decryptu/ctr-decrypted-racers](https://github.com/Decryptu/ctr-decrypted-racers), keeps the converted racers under `racers/` with its `sources.json`. `tools/racers/build_index.py <catalogue>` checks every racer folder and writes `index.json` (each racer's folder, authors, and its `racer.json` size and SHA-256) and `CREDITS.md`. It refuses a racer with no authors. A racer that reuses a look from another release says so in `credits` (the Japanese Crash credits the Japanese release).

Converted racers are static, vertex-coloured meshes with no textures or animations; a textured racer would use the same VRAM bank. Their kart textures, voices and code patches are not carried over.

## The Racers tab
The Mods screen's Racers tab (`game/230/MM_CustomRacers.c`) lists every racer in the catalogue's `index.json` and every installed racer it doesn't list, by name, with `OWNED` on installed ones. The panel shows the portrait, the authors and the template racer. Cross downloads a racer, or asks to remove an installed one. L2 reloads the catalogue; R2 searches names and authors. The Installed tab lists installed racers after the tracks, with the same panel and Cross. There is no Karts tab: a racer's model is driver and kart in one mesh, and wheels are shared sprites.

A package holds only the mod's own model and portrait, nothing from the player's disc.

- A listed racer's portrait shows before install. When the selection rests on it, `NativeRacerCatalogue_StartPreview` fetches its `racer.json` (checked against the index), then the portrait (checked against `racer.json`), and keeps it in `assets/racers/previews/<id>-<first 16 of the racer.json hash>.bin`, so a seen racer shows offline and an updated one is fetched again. It is drawn from the slot the track pictures use, so one hides the other (`game/230/MM_CustomPicture.c`).
- Downloads come from `raw.githubusercontent.com`, the catalogue's `main` branch, through the same HTTPS code as Project Saphi (`ap/ap_custom_track_download.cpp`, `NativeRacerCatalogue_*`). The index pins each `racer.json` (size and SHA-256) and `racer.json` pins its files, so the catalogue's `.gitattributes` keeps `racer.json` byte for byte (`eol=lf`).
- `NativeRacerLibrary_Install` checks the files against each other, writes them to a hidden `.<folder>.part` folder, removes any installed folder with the same id, then renames the staging folder into place. `NativeRacerLibrary_Remove` deletes the racer's folder.
- After either, `NativeRacers_Reload` rereads the library so the character select has the change at once, and clears the players' racer picks, since indices change.

`CTR_DEV_MODS=2` opens the tab ([DEV-TOOLS.md](DEV-TOOLS.md)); `CTR_DEV_PAD` presses then drive it. The catalogue loads within about 300 frames of a fast-forwarded run.

## In a race
A driver races as a custom racer when it is a human player who picked one or an Arcade/Cup bot assigned one, outside Adventure and the menus, and its character ID is still the racer's template (`NativeRacers_Driver`). Anything that changed the ID since (Archipelago's character swap, the garage) changed the racer too. Then:

- `VehBirth_NonGhost` births it with the racer's model, at every split-screen size. In Time Trial, `MainInit_Drivers` then gives player 1 the template's high-detail model but leaves a custom racer's alone.
- `VehBirth_TireSprites` gives it wheels or not by `wheels`.
- `VehBirth_SetConsts` reads the racer's engine class (`VEH_BIRTH_ENGINE_ID`, `include/common.h`). Engine sound stays the template's.
- The HUD portraits use the racer's (`VEH_DRIVER_ICON`): race rank list, cup standings, race results, VS and battle standings.

Everything else keyed by character ID uses the template: voice, podium, records, ghosts, Archipelago. OPTIONS > Extras > Opponent Roster controls the Arcade/Cup field (see below).

`CTR_DEV_RACER=n` with `CTR_DEV_RACE` puts player 1 in a race as the nth installed racer. Through the menus: `CTR_DEV_CHARACTER_SELECT=1` with `CTR_DEV_MODE=tt` opens the Time Trial character select, and `CTR_DEV_PAD` picks the racer, track and ghost.

## Opponent roster

OPTIONS > Extras > Opponent Roster is saved as `[Extras] ai_racers`:

- **ORIGINAL** (`0`, default): the original opponent roster.
- **UNLOCKED** (`1`): draw opponents from the eight starting racers and the secret racers unlocked in the current save, including N. Oxide when unlocked.
- **INCLUDE CUSTOM** (`2`): also draw from installed custom racers whose race model loads successfully.

This applies to one-player and two-player Arcade and Cup races. Offline custom courses use it in their supported single-player mode. Adventure, Archipelago encounter rosters, bosses, demos, Time Trial and Battle retain their own rules. A cup keeps the same opponents across its legs. A new single race or cup draws a new field.

The draw is without replacement by roster entry, excluding the human players' picks. A custom racer and its retail template are separate entries and may race together, as may different custom racers sharing a template. Each driver keeps its own model, engine, wheels and portrait; voices and records still use the template. N. Oxide's boss head start and damage advantages apply only in boss races.

`game/LOAD/LOAD_NativeBots.c` retains the retail pack for shared assets and loads each opponent's medium-detail template model into an owned buffer through `LT_DRAM`. Driver birth uses that driver's model, then the custom model when assigned. The buffers and bot picks are cleared at every driver-pack load, including return to menus; the library owns custom models. This avoids enlarging the fixed retail mempack or resolving a shared template to another driver's model.

`tools/test-ai-racers.c` exercises the production loader with a disc stub: locked and unlocked pools, human exclusions, distinct custom entries sharing a template, empty libraries, Cup continuity, mode exclusions, the eight-slot load queue and buffer cleanup. It runs as CTest `ctr_ai_racers`.

## Checking
`CTR_DEV_CHARACTER_SELECT=1` opens the screen ([DEV-TOOLS.md](DEV-TOOLS.md)). `CTR_DEV_FAKE_RACERS=n` replaces the installed racers with n placeholders that borrow the first eight retail racers' looks, to fill pages. To check retail is unchanged, A/B the screen against an older build with `CTR_DEV_UNLOCKS=0` and `=1` at 1 to 4 players, with `CTR_DEV_PAD` scripts that hit taken icons and row ends. With Character Select Stats off, every frame should match apart from the bottom stats prompt and the exit slide-out, which older builds did not animate.

For the stats panel, capture one to four players with `[Extras] character_stats` on and off, at 4:3 and 16:9. Page onto an installed racer and check its engine against `racer.json`; change the engine in a throwaway copy to check an override independent of its template. `CTR_DEV_PAD=1500:2,1900:2` toggles the panel twice and leaves `character_stats = false` saved.
