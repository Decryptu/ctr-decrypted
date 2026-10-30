#ifndef AP_BLUE_FIRE_H
#define AP_BLUE_FIRE_H

#ifdef CTR_AP
struct Driver;

// Progressive Boost's final tier is presentation only: 1 while the local
// player carries USF speed at that tier. VehBlueFire_Tick
// (game/Vehicle/VehBlueFire.c) swaps the exhaust palettes.
int AP_BlueFireWanted(const struct Driver *driver);
#endif

#endif
