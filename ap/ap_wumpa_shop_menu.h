#ifndef AP_WUMPA_SHOP_MENU_H
#define AP_WUMPA_SHOP_MENU_H

// The hub save screen's green load/save menu with the #249 WUMPA SHOP row:
// LOAD, SAVE, DELETE, WUMPA SHOP, EXIT. Retail EXIT is row 3; with the shop row
// it is row 4, and the menu procedure maps it back so retail code sees 3.

// MenuRow.stringIndex of the WUMPA SHOP row: AP_LNG_WUMPA_SHOP, in
// ctr_menu_strings.h with every other owned row string.
#include "../include/ctr_menu_strings.h"

#define AP_WSHOP_GREEN_SHOP 3
#define AP_WSHOP_GREEN_EXIT 4
#define AP_WSHOP_GREEN_ROWS 5
// AP_WumpaShopGreenMenuRow's answer when the row was the shop's own.
#define AP_WSHOP_GREEN_HANDLED (-2)

#endif
