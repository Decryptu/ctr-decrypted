# Setup guide

This is the full setup guide for CTR Decrypted: Crash Team Racing as a native PC
game with the Archipelago multiworld randomizer built in. For a first setup, the
[Start here](https://decryptu.github.io/ctr-decrypted/start.html) page on the
project site asks whether you are joining a room, hosting one, playing solo or
just racing, and gives you a checklist for exactly that. This page is the
reference behind it.

## What you need first

The game ships no game data. You supply the game files from your own copy of the
disc. You will need:

1. A disc image of your own NTSC-U (North American) Crash Team Racing
   PlayStation disc. A `.cue` plus `.bin`, a single `.bin`, or a `.chd` all
   work. Only the North American release is supported right now. PAL (European)
   and Japanese discs are detected and refused.
2. The release archive for your platform from the
   [releases page](https://github.com/Decryptu/ctr-decrypted/releases): the Windows
   `.zip`, or the Linux and Steam Deck `.tar.gz`.

That is everything you need on the game side when you are joining an existing
room. With a raw `.bin` image you do not need Python and you do not need to
extract the disc. A `.chd` image is unpacked first (Step 2). If you are
generating the multiworld yourself, you also need Archipelago and the matching
`ctr.apworld`; see [Generating or hosting a multiworld](#generating-or-hosting-a-multiworld).

**Choose your next step:**

- **Joining a room that is already generated:** do Steps 1 to 3 with the
  details the host gives you.
- **The host is still collecting players:** make your YAML (your options file)
  with the [YAML builder](https://decryptu.github.io/ctr-decrypted/builder.html)
  and send it to them, then do Steps 1 to 3 once the room is up.
- **Generating or hosting the room:** follow
  [Generating or hosting a multiworld](#generating-or-hosting-a-multiworld).
- **Playing alone:** do Steps 1 and 2, then [Playing solo](#playing-solo-without-a-server).

## New to Archipelago?

Archipelago shuffles the things you unlock across several games played by
several people at once: a race you win in CTR can hold another player's item,
and their game can hold your next key. Every game runs through the same stages:
each player writes a **YAML** (an options file naming their **slot**, the
player name they connect with); the host **generates** a **seed** from all of
them with Archipelago and the **APWorld** of each game that Archipelago does not
ship with (`ctr.apworld` here); the seed runs in a **room** on a server
(archipelago.gg or the host's own computer); everyone **connects** their game to
the room's address with their slot name. Completing a **location** (a race, a
relic, a token, an item box) sends the **item** it holds to whoever owns it.
**Play Solo** does all of this inside the game for one player.

## Step 1: get the game executable

Download and extract the archive for your platform from the
[releases page](https://github.com/Decryptu/ctr-decrypted/releases):
`ctr-decrypted-vX.Y.Z-windows-x86.zip` on Windows, or
`ctr-decrypted-vX.Y.Z-linux-x86.tar.gz` on Linux and Steam Deck. Keep the
extracted files together in their own folder. The rest of these steps add the
game data and your server settings there.

## Step 2: your disc image

The game reads the raw disc image directly. On the first launch without one, it
looks in the game folder, the folder above it, and Downloads, Desktop, Documents
and Games in your home folder (three folders deep). If it finds your disc it
asks whether to use it; otherwise a file picker opens. Either way it then offers
to copy the image into the game folder, or to remember where it is.

You can also put it there yourself: copy your NTSC-U `.bin` into an `assets`
folder next to `ctr_native_ap.exe` (launching the game once creates that
folder):

```
CTR-Decrypted/
  ctr_native_ap.exe
  assets/
    ctr-u.bin
```

The filename does not matter: the game scans the `.bin` files in `assets` and
uses the one whose boot id matches the NTSC-U disc (`SCUS_944.26`).
`ctr-u.bin` is just the conventional name. That's it: no Python, no extraction
step. The image must be the common single-track raw PlayStation BIN layout
(MODE2/2352 sectors). A cooked 2048-byte `.iso` does not carry the audio and
video sector data the game needs, so it will not work. PAL (European) and
Japanese images are detected and refused with a message naming their boot id.

Go straight to Step 3.

A `.chd` image is compressed: unpack it once to `.bin` and `.cue` with
`chdman`, which comes with the MAME tools, and use the `.bin`:

```sh
chdman extractcd -i "CTR.chd" -o "CTR.cue" -ob "CTR.bin"
```

Open a terminal in the folder containing your image and `chdman` first. On
Windows, open that folder in File Explorer, type `powershell` in its address
bar and press Enter. If `chdman.exe` is in that folder, use `./chdman.exe` in
place of `chdman`. On Linux or Steam Deck, right-click the folder and choose
**Open Terminal Here** (on Deck, **Open Terminal** opens Konsole). Replace
`CTR.chd` with your image's filename; keep the quotes if it contains spaces.
The website's **Copy command** button copies the example without running it.

To keep only the files the game uses instead, see the appendix at the end of
this guide.

## Step 3: connect to your room

Launch `ctr_native_ap.exe`. On the first start a setup screen opens on the
title screen: it asks for your room details, then a few options (fullscreen,
aspect ratio, render scale, 60 FPS, skipping the intros, Discord status). Skip
it, or come back later, in **OPTIONS → Connection**. The room details are:

- **Server**: the address of your room, for example `archipelago.gg:38281`
  (the port is on your room page). Secure connections (`wss://`) are used
  automatically for hosted rooms; for a server on your own machine use
  `ws://localhost:38281` or just `localhost:38281`.
- **Slot**: your player name in the room, spelled exactly as it appears there.
  Slot names are case-sensitive, so copy the capitalization exactly. The in-game
  font marks uppercase letters with a short outlined white line underneath.
- **Password**: the room password, or leave it blank if there is none.

Select **Connect** (in the setup, **Next** connects too). The status line on
the same screen shows the connection
state (Connecting… / Connected / an error message). Your settings are saved to
`config.ini` next to the executable, and the game dials automatically on later
launches. A startup dial that cannot reach the room stops after a few automatic
attempts instead of retrying forever: the status line then says that automatic
attempts stopped, and selecting **Connect** retries with the same settings.

## Your first five minutes

Once the status says **Connected**, return to the main menu and start Adventure
mode. Use the normal save station in the hub to save your local Adventure
progress. The Archipelago server remains authoritative for received items and
completed locations, and rebuilds your received-item counts when you reconnect.

The locations in your seed depend on its YAML options. They can include Trophy
Races, CTR Token Challenges, relic Time Trials, boss races, Gem Cups, Crystal
Bonus Rounds, Oxide races, and additional finishing-position checks. Complete
an event and continue past its results or award screen to send its check. Items
you receive, and the ones your checks send to other players, appear in the feed
during the race as well as in the Adventure hub. Progression items update your
available gates; traps arm silently and fire during a later race.

If the connection drops, checks completed offline are retained and sent after
you reconnect to the same seed and slot. The Connection screen shows the
current state and any error message. A drop after you have connected once keeps
the client recovering in the background, so a player mid-seed is not stranded
away from the main-menu Connection screen; a startup or manual dial that never
reached a room stops after a few automatic attempts and waits for you to select
Connect.

The client verifies the seed after connecting. If the hub shows a red **SEED NOT
COMPLETABLE** warning, stop and report it with your YAML and spoiler log. For a
crash or a seed that appears stuck, follow
[Reporting a crash or a stuck seed](#reporting-a-crash-or-a-stuck-seed). Setup
questions and reproducible bugs can go to the
[GitHub issue tracker](https://github.com/Decryptu/ctr-decrypted/issues); if you
are unsure which repository is responsible, file it there anyway.

### Alternative: config file

If you prefer a text file, copy `ap-config.example.txt` to `ap-config.txt` in
the same folder as `ctr_native_ap.exe` and set `uri`, `slot`, and `password`
there; the game reads it at startup. Values saved from the in-game Connection
screen (`config.ini`) take precedence when both exist. The example file also
documents a few optional quality of life toggles (`skip_hints`, `map_flash`),
which can equally be changed in the in-game options menu.

## Item boxes

If your YAML turned on `box_locations`, the tracks carry AP item boxes: crates
placed around the course that send a location check when you break them. This
is separate from `itemsanity`, which controls received weapon items and weapon-
use checks. A broken AP box stays broken for the rest of the seed, across
reconnects and relaunches, because the client asks the server what has already
been checked rather than keeping its own tally.

You do not have to install anything for this. The placement set, meaning where
every box stands on every track, is compiled into the client, so the boxes are
simply there once your seed includes them.

Two things decide whether a box appears:

- **Your seed.** A box stands only if your slot actually has that location. If
  your options created fewer box locations than the placement set covers, or none
  at all, you get exactly the ones your seed created and no more. That is
  correct, not a missing download.
- **Whether you already broke it.** Checked boxes do not come back.

If a track looks empty and you expected boxes, open `ctr-ap.log`. Every level
load writes one line saying how many boxes are standing out of how many
placements the track holds and which placement set is live, plus, when nothing
stands, which reason applies.

By default an AP box shows the Archipelago colour of the item inside it:
purple for progression, blue for useful, cyan for filler and salmon for a trap.
A box is pink until the client has looked up what is inside, a moment after
connecting. To see every box pink instead, turn off **OPTIONS → Archipelago →
Item Box Colours**; the setting is saved in `config.ini`. The seed can also
turn colours off for everyone with the YAML option `color_boxes_by_item`, for
example for a race. Then the row shows **OFF (SEED)** and cannot be turned on.

AP boxes break when your kart touches their collection area, when your moving
bomb, missile or thrown shield hits them, or when your weapon explodes close
enough. Direct projectile hits check the movement between frames. Opponent
karts and weapons cannot collect your AP boxes. Kart and direct-projectile
contact have extra tolerance; weapon explosion ranges are unchanged.

## Experimental recorded AI laps

Recording your laps and using recorded lines are separate options under
**OPTIONS → Authoring**. Both default to off. You can collect laps without
changing how the opponents drive.

### Save your laps

1. Enable **Save AI Lap Recordings** before starting a race. To set the name
   saved in your recordings, close the game and edit `nav_driver_name` under
   `[Authoring]` in `config.ini`. If empty, it uses your configured AP slot name.
   The **Driver Name** row displays the value but cannot edit it.
2. Complete a multi-lap race and continue to its results. Only eligible clean,
   complete laps are saved, up to three per recording. The standing-start lap
   is excluded because it starts on the grid rather than at the finish line;
   a one-lap race therefore does not provide a usable recording.
3. Look in `ap-navpaths`, beside the `config.ini` used by your game. This is
   relative to the game's working directory, which may differ from the
   executable's folder. `ctr-ap.log` records each saved file and explains when
   no eligible lap was saved.

Files are named `navpath-<track-number>-<recording-number>.navlap`. New recordings
use an unused number and preserve earlier files. Nothing is uploaded. Turning
recording off during a race discards its unsaved laps.

### Use recordings

Close the game before adding `.navlap` files to `ap-navpaths`. Keep the track
number in the filename. If a filename already exists, give the incoming file
an unused three-digit recording number from `001` to `999`; do not overwrite
the existing file. Custom-track recordings must match the package's navigation
identity and revision as well as the expected filename. Changing a filename
does not make a recording compatible with another track.

Enable **Use Recorded AI Laps**, then start a race on the matching track.
You can leave **Save AI Lap Recordings** off. To return to the track's normal
AI lines, turn **Use Recorded AI Laps** off and start a new race. Your recordings
remain on disk.

**Current playback is experimental.** Every recording for the track is in the
pool. Each race picks up to three different contributors at random, one
recording and one lap each. The engine still has three AI lines shared by the
opponents: each picked recording replaces one of them, and the lines without a
recording keep the track's own AI. Only the first opponent on a recorded line
shows its contributor's name, so a name never appears twice. The finish line
of each lap is blended so the line does not jump there, and recorded lines
break crates where the track's own lines do. If no usable recording loads, the
track's own navigation remains in use.

Recorded lines do not guarantee the original lap time. Crate contact and the
finish blend still need gameplay testing. A separate recorded line for every
opponent is planned for a later release. In 2P only player 1's laps are saved,
and no names are shown.

For troubleshooting, search `ctr-ap.log` for `[AP NAVREC]`: it lists the pool,
the random seed, the picked file and lap for each line, and reasons recordings
were skipped. Include that log, your client version, track and relevant
recording when reporting a problem
through [Reporting a crash or a stuck seed](#reporting-a-crash-or-a-stuck-seed).

## Generating or hosting a multiworld

### Every player: make a YAML

Create your player file with the [YAML builder](https://decryptu.github.io/ctr-decrypted/builder.html). Its
options come from the `ctr.apworld` of the latest release. Pick a player name
(your slot name: unique in the room, at most 16 characters, and typed with the
same capitals when you connect), download the YAML and give it to the person
generating the room. The builder only writes your player file; it does not
generate the seed or host the server.

Without the website, use **Generate Template Options** in the Archipelago
Launcher after installing the matching `ctr.apworld`, then edit the generated
Crash Team Racing YAML in a text editor.

### The host: generate the seed

1. Install Archipelago from its
   [releases page](https://github.com/ArchipelagoMW/Archipelago/releases) (the
   Windows installer, or the Linux AppImage). It installs the Archipelago
   Launcher, which generates and hosts. On Windows it goes into
   `C:\ProgramData\Archipelago` by default; the Launcher's **Browse Files**
   opens the folder wherever it is.
2. Install `ctr.apworld` from the same release as the players' game: double-click
   it, use **Install APWorld** in the Launcher, or copy it into the
   `custom_worlds` folder. Restart the Launcher. Only the person generating needs
   it, and other games that Archipelago does not ship with need their own
   APWorld the same way.
3. Collect one YAML per slot, every slot name different. Agree on the release
   first: the game, the YAMLs and the `ctr.apworld` must come from the same one.
4. Put the YAMLs in the `Players` folder (remove files from earlier games) and
   click **Generate** in the Launcher. The result is one file in the `output`
   folder named like `AP_12345678901234567890.zip`: that zip is the whole
   multiworld. Do not unzip it.

Generate on your own computer: the generator and YAML checker on the
archipelago.gg website do not include CTR. When generation fails, the error
names the YAML or option at fault; ask that player for a corrected file.

### The host: open the room

- **On archipelago.gg** (easiest): upload the zip on the
  [Host Game page](https://archipelago.gg/uploads), then click **Create New
  Room** on the seed page. The room page shows the address with its port (for
  example `archipelago.gg:38281`), every slot, a tracker and a console for
  commands. A room sleeps when nobody is connected and wakes when its page is
  opened; the port can change then, so players should copy the address from the
  page each time.
- **On your own computer**: click **Host** in the Launcher and pick the zip. The
  console prints `Hosting game at …` once the room is up; keep it open while you
  play. The port is 38281 unless you changed it. Players on the same computer use
  `localhost:38281`, players on your home network use this computer's local
  address (like `192.168.1.20:38281`), and players over the internet need your
  public address with TCP port 38281 forwarded to this computer on your router
  and allowed through its firewall. `0.0.0.0` in the console is not an address
  to give anyone.

Then send each player the address with its port (or the room page link), their
exact slot name and the password if you set one. The room's console takes
commands while the game runs (`/help` lists them): release the items of a
player who leaves, or collect for one who has finished. Players ask for hints by
typing `!hint <item>` in the Archipelago Text Client (in the Launcher).

The released client and apworld are a pair. Update both together, even when a
release appears to change only one side.

### Choosing your options

There is one list of options and no separate content setup. A YAML that sets
nothing gets the original game's shape: the 18 race tracks, the 5 Gem Cups and
the 4 Crystal Challenges on the 27 warp pads, plus the five boss garages. Each
option you set changes that seed. The options come in the order you would fill
them in:

- **Content:** `retail_tracks`, `track_modes`, `custom_tracks`,
  `cortex_vortex_track`, `required_entries`, the cups and arenas, and the boss
  garages and boss cups.
- **Goal:** `oxide_goal` and what it needs.
- **Pads and requirements:** where content lands (`pad_placement`), what each
  pad asks for (`warppad_unlock_requirements`, `requirement_weights`,
  `two_stage_density`, `warp_pad_rules`) and whether Keys and Gems are shuffled.
- **Checks:** which extra checks exist (podium rungs, relic perfects, letters,
  lap checks, item boxes, Itemsanity and more).
- **Items and traps, characters and stats, boost, DeathLink, display.**

Every option explains itself in the YAML builder and in the generated template.
Three presets give you a start: **Quick** (a short goal with few locks),
**Standard** (the defaults) and **Full Adventure** (everything on, every
location reachable). They are the same three that Play Solo offers.

## If an in-game update is interrupted

The in-game updater (a prompt on the main menu appears when a newer stable
release exists) installs `ctr.apworld`, the Cortex Vortex files (in the assets
folder the game reads), the bundled solo runtime (`solo-runtime/`, replaced as
a whole folder) and the client in one step. It keeps your previous files next
to the new ones with a `.old` ending (`solo-runtime.old` for the runtime). If
the game is closed, killed or loses power during that step, the next start
restores the `.old` files by itself. Your solo seeds and saves in `solo/` are
never touched. While Play Solo runs a session or makes a seed, the update
waits: stop Play Solo first, then choose RESTART.

On Linux and Steam Deck the client file is never missing during the update:
the new one replaces it in a single step, and the previous one is kept as
`ctr_native_ap.old`.

On Windows one moment cannot be covered: the client is renamed to
`ctr_native_ap.exe.old` just before the new one takes its place. If that is the
moment the install was cut off, the game does not start because no
`ctr_native_ap.exe` exists. Rename `ctr_native_ap.exe.old` back to
`ctr_native_ap.exe` and start the game; it then restores the other files. The
same step by hand works for any file whose `.old` copy is still there (for
example `ctr.apworld.old` back to `ctr.apworld`, or the `solo-runtime.old`
folder back to `solo-runtime` after removing the new one). Your `config.ini`,
saves, solo seeds and the disc image are never touched by an update.

## Playing solo, without a server

From 0.3.0 the client can make and host a single-player seed itself. Choose
**PLAY SOLO** on the title screen, or open **Options > Connection > Play Solo**:

- **New solo seed** offers the bundled presets (Quick, Standard, Full
  Adventure), any YAML you imported, and any YAML you drop into the `solo/inbox`
  folder next to the game (the screen shows the path). A preset opens an
  options screen with every setting of the world, in groups (Goal, Cups and
  Arenas, Bosses and so on). Cross opens a group, Left and Right change a
  value, Square shows what an option does, and Continue goes on. Settings that
  are lists, such as the track and weapon pools, are not on the screen; set
  them in a YAML. DeathLink is off in solo, because nobody else is in the room.
- When custom tracks are installed, a preset or a YAML then shows **Custom
  Tracks**, with Cortex Vortex once its files are installed. Cross on a track
  cycles: not used, **In pool** (the seed may use it) and **Own pad** (it gets
  one of the race pads). The default seed has 18 race pads, 5 Gem Cups, 4
  Crystal Challenges and the boss garages. Your picked tracks and the retail
  tracks share one pool, so a custom track on a pad takes the place of a retail
  track. A YAML keeps its own settings, and the tracks it lists start as picked.
  If a pick does not fit the YAML, the screen says why and you can change the
  picks. A track is listed
  when its files are in `assets/tracks/<track>/original` and the client knows
  its descriptor: from a room that used the track, or a descriptor file in the
  `solo/tracks` folder.
- **Import YAML file** takes a YAML you made with the [YAML builder](https://decryptu.github.io/ctr-decrypted/builder.html).
- **Import room file** takes a one-player CTR room (`.archipelago` or
  `AP_*.zip`) generated elsewhere.
- Your solo seeds are listed with their progress. Cross resumes one; Square shows
  its details and lets you delete it.

The client starts a private Archipelago server on your own computer
(127.0.0.1, ports 38301 to 38320) and connects to it; nothing goes over the
internet. Everything solo is stored in the `solo` folder next to the game; the
game keeps backups of its saves with your memory card (see
[Backups](#backups-going-back-to-an-earlier-save)). Multiplayer still uses
**Connect** as before.

## Backups: going back to an earlier save

The game keeps copies of your progress in the `backups` folder next to it:
your memory card save and the save of every Play Solo seed. A copy is made
each time the game starts and, while you play, before a save when the last copy
is more than 15 minutes old. Nothing is copied when nothing changed. The 10
newest copies are kept.

To go back, open **OPTIONS → Backups**. Each copy shows when it was made and
why (game start, while playing, before a restore); the line under the list
says what it holds. **Cross** on one asks to restore it, and the game restarts
and loads it. What you had before is backed up first, so a restore can be undone
the same way. Started from Steam, the game cannot restart itself: quit and start
it again from Steam, and the copy is restored then. Until then the row reads
**AT NEXT START**, and Cross on it cancels.

A multiworld's progress (checked locations, received items) lives on its
server, not here: a restore brings back your save and Play Solo seeds only.

## Custom tracks and racers: the Mods screen

Choose **MODS** on the title screen (or **OPTIONS → Mods** from anywhere). L1
and R1 switch tabs:

- **Seed**, in a room that uses custom tracks: what the room needs, each track
  marked READY or MISSING. **Cross** downloads every missing track from Project
  Saphi in one go, after asking.
- **Tracks**: the tracks from Project Saphi and the CTR Decrypted track
  catalogue in one list. **Square** lists a track's other versions.
- **Racers**: the custom racer catalogue, with each racer's portrait. A
  downloaded racer is in the character select at once.
- **Installed**: everything you have, tracks first, then racers (tagged
  **RACER**).

Every tab works the same way. The list tags what you have **OWNED**, what has
a new version **UPDATE**, and what you turned off **OFF**. **Cross** downloads
what you don't have and updates what has a new version; on something you have,
it opens its options:

- **Turn off**: it stays installed but the game leaves it out (a track leaves
  the Arcade and Time Trial lists, a racer leaves the character select).
  **Turn on** brings it back.
- **Remove**: deletes it, after asking.
- **Check files** (tracks): checks the installed files again.

**L2** refreshes the lists, **R2** searches, and on the track tabs **Select**
imports track files.

Installed tracks appear on extra pages after the normal tracks in a one-player
Arcade race and in Time Trial: at the track list, press Right past the last
page. They race outside Archipelago, so disconnect from a room first. A room
that needs a track you installed from Tracks finds it without downloading it
again.

## 60 FPS

The game runs at 60 FPS by default. To go back to the original 30 FPS, turn off
**OPTIONS → Video → 60 FPS (Next Load)** (saved as `fps60` in `config.ini`).
The change applies the next time a track or menu loads. Every mode runs at
60, Time Trial included; ghosts recorded at either rate play at both.

## Importing custom tracks from an xdelta patch

Some custom tracks come as an xdelta patch for the game disc (for example a
custom adventure pack). The client can take the race tracks out of such a patch
using your own disc; nothing from the patch is downloaded or shared.

1. Open **MODS** on the title screen and press **Select** (Import track files).
2. Choose the patch with the file dialog, drop the `.xdelta` file on the game
   window, or put it in `assets/tracks/import/` (useful on Steam Deck in Game
   Mode) and pick it from the list.
3. Check the list of found tracks and press **Cross** to install.

The tracks are saved in `assets/tracks/<track>/original/`, where rooms and solo
seeds look for custom tracks, and appear on the Mods screen's Installed tab and
in Arcade and Time Trial. If a pack descriptor (`<patch name>.ctrpack.json`)
sits next to the patch, it supplies the track names, laps and relic times.
Every imported track is also listed on the Play Solo Custom Tracks page at
once (a descriptor in `solo/tracks`). Its facts are measured from its files;
laps, author and relic times come only from the pack descriptor. Without an
author the page shows it as Unknown. Without laps (for example a patch with no
pack descriptor) the page asks for them first: Left and Right pick 1 to 7 laps,
starting at 3, and Cross keeps them with the track's Play Solo entry. Until
then the track waits in `solo/needs-laps` and cannot be picked. Such a track
has no relic, because relic times belong to the author's laps. The track files
are never changed.
Only race tracks are imported; arenas, game code and other changed files are
listed but skipped.

## Optional: controllers and Steam Input

Controllers work out of the box: the game uses SDL, so common gamepads (Xbox
controllers, DualShock 4 / DualSense, and most others) are picked up
automatically with a PlayStation-style layout, whether connected before or
after launch.

To change which device each player uses, rebind buttons, set a stick deadzone
for a drifting stick, or switch **Steering** to **Modern** if the stick feels
weaker than the D-pad, open **Options → Controls** and pick a player.
Your choices are saved in `controls.ini` next to `config.ini`. The full guide is
[docs/CONTROLS.md](docs/CONTROLS.md).

If you would rather run your controller through **Steam Input** (a Steam
Controller, custom button remapping, or per-game controller configurations),
add the game to Steam as a non-Steam game: in Steam, choose **Games → Add a
Non-Steam Game to My Library…**, browse to `ctr_native_ap.exe`, and from then
on launch it from your Steam library. Steam Input then treats it like any
other Steam game.

**On a Steam Deck**, use the Linux `.tar.gz`. In Desktop Mode, extract it into
a folder of its own (for example `~/Games/CTR`), put your disc image in its
`assets` folder, then right-click `ctr_native_ap` and choose **Add to Steam**.
From then on start it from your library in Gaming Mode. The first launch on a
Deck is fullscreen at 16:10, the Deck's screen, and the built-in controls work
as a controller. You do not need a keyboard: focusing any field on the
Connection screen brings up the Steam on-screen keyboard, and the whole edit
works from the pad: X or START saves the field, TRIANGLE cancels it.

## Reporting a crash or a stuck seed

If the game crashes, or a seed seems impossible to progress, run
`support-bundle.bat` (Windows) or `./support-bundle.sh` (Linux/Steam Deck) from
the game folder. It creates one small archive (`ctr-ap-support-<date>.zip` /
`.tar.gz`) containing the game log, the AP log, the crash report if there was
one, and your connection settings with the password removed. No game data is
included. Attach that archive to a [GitHub issue](https://github.com/Decryptu/ctr-decrypted/issues/new/choose),
or bring it to the [Crash Team Racing channel](https://discord.com/channels/731205301247803413/1222304293751750777)
on the Archipelago Discord, together with a line about what you were doing; if the seed itself seems broken,
include your YAML and the spoiler log too.

The client also checks every seed on connect: if a solo seed cannot be
completed, a red "SEED NOT COMPLETABLE" warning appears on the hub and the AP
log records which locations are unreachable. Mention that verdict in your
report if you see it.

## Troubleshooting

- `InvalidSlot` or `Slot not found`: copy the slot name from the room page
  exactly, including uppercase and lowercase letters. You can also correct the
  saved `slot` value under `[Connection]` in `config.ini`.
- "Missing or incomplete assets" at startup: the `assets` folder holds no
  usable disc image, or a file did not extract. Make sure the `assets` folder
  sits in the same directory as `ctr_native_ap.exe` and holds either a raw
  NTSC-U `.bin` (any filename) or a complete extracted set. The startup log
  prints a reason line for every `.bin` it looked at and rejected.
- "ignoring ...: not a raw MODE2/2352 PlayStation disc image" at startup: that
  `.bin` is not a raw dump (it may be a renamed cooked `.iso`, an archive, or a
  corrupt file). Re-dump the disc as a raw MODE2/2352 image.
- "ignoring ...: boot id ... is the PAL (European) release" (or NTSC-J) at
  startup: your image is not the North American release. You need the NTSC-U
  disc, boot id `SCUS_944.26`.
- "PAL is not supported yet" from the extractor: your disc is the European
  release. You need the North American (NTSC-U) disc, whose boot id starts with
  SCUS.
- "This is a .chd image, which needs the chdman tool": install `chdman` (it
  ships with the MAME tools) and make sure it is on your PATH, or convert the
  `.chd` to `.bin`/`.cue` yourself and run the extractor on the `.cue`.
- "does not look like a PlayStation disc image": you likely pointed the tool at
  the wrong file (a zip, a folder, or a cooked `.iso`). Use the raw `.bin`,
  `.cue`, or `.chd` of the disc.
- The game window opens but there is no music or the intro video is black: your
  image was probably a cooked 2048-byte `.iso`, which drops the XA and STR
  sector data. Re-dump or re-obtain the disc as a raw MODE2/2352 image.
- Cannot connect to the server: check the server address, slot name, and
  password in **OPTIONS → Connection** (the status line there shows the error
  reason), or in `ap-config.txt` if you use the config file instead. The slot
  name must match the room exactly. Settings saved from the in-game screen
  (`config.ini`) override `ap-config.txt`.
- Linux, no sound when launching outside Steam (often with
  `Cannot open shared library libasound_module_pcm_pipewire.so` in the log):
  the game is a 32-bit binary, so its audio goes through the 32-bit build of
  the ALSA-to-PipeWire bridge, and most 64-bit distros no longer install that
  by default. Install the i386/32-bit build of the PipeWire ALSA plugin from
  your distro's repos (confirmed working on LMDE). On immutable distros like
  Bazzite, adding the game to Steam and using the Steam Linux Runtime is the
  practical route: Valve's runtime ships a complete 32-bit library stack.

## Appendix: extracting the assets (most people should skip this)

**You almost certainly do not need this.** Step 2 is the normal way to set the
game up, and it needs no extra tools. Extract only if one of these applies:

- your disc image is a `.chd`, or
- you want to save disk space, because extraction copies out only the files the
  game actually uses instead of keeping the whole image.

It needs Python 3.8 or newer, and `.chd` images additionally need the `chdman`
tool on your PATH. From the folder that holds `ctr_native_ap.exe`, run:

```sh
python extract_assets.py "path/to/your/CTR.cue"
```

You can also point it at a `.bin` or a `.chd`. By default it writes an `assets`
folder in the current directory. To send it somewhere else, add `--out`:

```sh
python extract_assets.py "path/to/your/CTR.bin" --out "path/to/CTR-Decrypted/assets"
```

When it finishes you should have this layout:

```
CTR-Decrypted/
  ctr_native_ap.exe
  assets/
    BIGFILE.BIG
    SOUNDS/KART.HWL
    TEST.STR
    XA/ENG.XNF
    XA/ENG/EXTRA/S00.XA ... S05.XA
    XA/ENG/GAME/S00.XA ... S20.XA
    XA/MUSIC/S00.XA ... S01.XA
```

If you extracted with a version older than v0.1.2, run the extractor again with
the current one. Earlier versions truncated part of the audio data, which makes
music and the race announcer sound wrong or go missing.
