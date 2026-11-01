#ifndef NATIVE_RACERS_H
#define NATIVE_RACERS_H

// Custom racers: extra slots on the character select, each with its own name,
// portrait and model (docs/CUSTOM_RACERS.md). Every racer borrows a retail
// character ID (its template) for the systems that are keyed by character ID
// (voice, podium, records, Archipelago); the racer index rides alongside it.
// Racer indices are 0..NativeRacers_Count()-1; -1 means "the retail character".

struct Icon;
struct Model;

int NativeRacers_Count(void);
int NativeRacers_Template(int racer);
// The racer's display name, or NULL to use the template's.
const char *NativeRacers_Name(int racer);
// The racer's portrait and menu model, or NULL to use the template's.
struct Icon *NativeRacers_Portrait(int racer);
struct Model *NativeRacers_MenuModel(int racer);

// The racer each human player picked on the character select (-1: the
// retail character in GAME_CHARACTER_IDS).
void NativeRacers_SetPlayer(int player, int racer);
int NativeRacers_Player(int player);

#endif
