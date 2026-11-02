#ifndef CTR_MENU_STRINGS_H
#define CTR_MENU_STRINGS_H

// Menu rows that have no retail string. MenuRow.stringIndex indexes
// sdata->lngStrings (masked with 0x7fff), so these rows carry an owned index
// instead and game/RECTMENU.c: RECTMENU_RowString resolves it to the label.
// No retail string index comes near 0x7f00. Every owned index is listed here,
// so two features can never take the same one.
#define AP_LNG_TRACKER    0x7f00 // hub pause menu: TRACKER
#define AP_LNG_WUMPA_SHOP 0x7f01 // hub save screen: WUMPA SHOP
#define AP_LNG_PLAY_SOLO  0x7f02 // title screen: PLAY SOLO
#define CTR_LNG_MODS      0x7f03 // title screen: MODS
#define CTR_LNG_SPEED     0x7f04 // Arcade/VS speed class screen title: SPEED
#define CTR_LNG_CLASSIC   0x7f05 // speed class row: CLASSIC
#define CTR_LNG_200CC     0x7f06 // speed class row: 200CC

#endif
