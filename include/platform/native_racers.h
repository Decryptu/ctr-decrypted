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
// enum EngineClass; the template's unless the racer names one.
int NativeRacers_Engine(int racer);
int NativeRacers_Wheels(int racer);
// The racer's names, or NULL to use the template's.
const char *NativeRacers_Name(int racer);
const char *NativeRacers_ShortName(int racer);
// The racer's portrait and models, or NULL to use the template's. The menu
// model is scaled to the character select's windows.
struct Icon *NativeRacers_Portrait(int racer);
struct Model *NativeRacers_MenuModel(int racer);
struct Model *NativeRacers_RaceModel(int racer);

// The racer each human player picked on the character select (-1: the
// retail character in GAME_CHARACTER_IDS).
void NativeRacers_SetPlayer(int player, int racer);
int NativeRacers_Player(int player);

#endif
