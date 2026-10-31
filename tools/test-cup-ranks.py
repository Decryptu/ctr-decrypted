"""Run the production cup ranking (UI_CupStandings.c) over every field size and
cup order: the final order stays a permutation, so the end-of-cup ranking gives
each racer in the field one distinct place (ctr-native-ap #282), and an Arcade
or VS tie for first goes to the tied racer who placed better in the last race."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/UI/UI_CupStandings.c").read_text()
start = source.index("// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8005607c")
end = source.index("void UI_CupStandings_InputAndDraw(void)", start)
functions = source[start:end]

fixture = r'''
#include <assert.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned short u16;
typedef short s16;
struct Driver { s16 driverRank; };
struct GameTracker {
 u8 numPlyrCurrGame, numBotsNextGame;
 struct { s16 points[8]; } cup;
 struct Driver *drivers[8];
};
static struct GameTracker game;
static struct { struct GameTracker *gGT; } sdataStore = { &game }, *sdata = &sdataStore;
static struct { int cupPositionPerPlayer[8]; } data = { {0, 1, 2, 3, 4, 5, 6, 7} };
'''

tests = r'''
static struct Driver karts[8];
static unsigned rng = 1;
static int next(int n) { rng = rng * 1103515245u + 12345u; return (int)((rng >> 16) % (unsigned)n); }

static void seat(int players, int bots) {
 game.numPlyrCurrGame = (u8)players; game.numBotsNextGame = (u8)bots;
 for (int i = 0; i < 8; i++) game.drivers[i] = (i < players + bots) ? &karts[i] : 0;
}

// One race: a random finishing order, retail points, then the standings update.
static void race(void) {
 int n = game.numPlyrCurrGame + game.numBotsNextGame, order[8];
 static const int points[8] = {9, 6, 3, 1, 0, 0, 0, 0};
 for (int i = 0; i < n; i++) order[i] = i;
 for (int i = n - 1; i > 0; i--) { int j = next(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
 for (int i = 0; i < n; i++) { karts[order[i]].driverRank = (s16)i; if (i < 4) game.cup.points[order[i]] += (s16)points[i]; }
 UI_CupStandings_UpdateCupRanks();
}

// What the end of a cup does: rank drivers[slot] = slot for all eight slots.
static void checkPodium(void) {
 int n = game.numPlyrCurrGame + game.numBotsNextGame, seen[8] = {0};
 for (int i = 0; i < 8; i++) { assert(data.cupPositionPerPlayer[i] >= 0 && data.cupPositionPerPlayer[i] < 8); seen[data.cupPositionPerPlayer[i]]++; }
 for (int i = 0; i < 8; i++) assert(seen[i] == 1);
 for (int i = 0; i < n; i++) karts[i].driverRank = 99;
 for (int i = 0; i < 8; i++) { struct Driver *d = game.drivers[data.cupPositionPerPlayer[i]]; if (d) d->driverRank = (s16)i; }
 for (int i = 0; i < n; i++) assert(karts[i].driverRank < n);
 for (int i = 0; i < 8; i++) game.cup.points[i] = 0;
}

int main(void) {
 // Fields a cup can have: Adventure (8), Purple (5), 2P Arcade (6), VS 2-4.
 static const int fields[][2] = { {1, 7}, {1, 4}, {2, 4}, {2, 0}, {3, 0}, {4, 0} };
 int cups = 0;
 for (int trial = 0; trial < 4000; trial++) {
  const int *f = fields[next(6)];
  seat(f[0], f[1]);
  for (int leg = 0; leg < 4; leg++) race();
  if (next(2)) UI_CupStandings_FinalizeCupRanks();
  checkPodium();
  cups++;
 }

 // The reported case: an eight-racer cup, then Purple, with stale slots
 // that name racers in the Purple field.
 seat(1, 7);
 memcpy(data.cupPositionPerPlayer, (int[8]){0, 1, 3, 6, 5, 7, 4, 2}, sizeof(data.cupPositionPerPlayer));
 seat(1, 4);
 game.cup.points[3] = 30; game.cup.points[0] = 20; game.cup.points[2] = 10; game.cup.points[1] = 5;
 UI_CupStandings_UpdateCupRanks();
 checkPodium();

 // Arcade tie for first between drivers 3 and 5: driver 5 placed better in
 // the last race, so driver 5 takes first and driver 3 second.
 seat(1, 7);
 for (int i = 0; i < 8; i++) { game.cup.points[i] = (s16)i; karts[i].driverRank = (s16)(7 - i); }
 game.cup.points[3] = 30; game.cup.points[5] = 30; karts[3].driverRank = 4; karts[5].driverRank = 1;
 UI_CupStandings_UpdateCupRanks();
 assert(data.cupPositionPerPlayer[0] == 3 && data.cupPositionPerPlayer[1] == 5);
 UI_CupStandings_FinalizeCupRanks();
 assert(data.cupPositionPerPlayer[0] == 5 && data.cupPositionPerPlayer[1] == 3);
 checkPodium();
 return cups == 4000 ? 0 : 1;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    src, exe = Path(tmp) / "cup.c", Path(tmp) / "cup"
    src.write_text(fixture + functions + tests)
    # MinGW ships no UBSan runtime; CI (Linux) runs with it.
    sanitize = [] if os.name == "nt" else ["-fsanitize=undefined"]
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable",
                    *sanitize, str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: 4000 random cups over every field size and cup order keep a permutation and a "
      "distinct place per racer; the Purple-after-eight-racer case; an Arcade tie for first, UBSan")
