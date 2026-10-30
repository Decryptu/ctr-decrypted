#include <common.h>
#include <ctr_extras.h>
#include "../../ap/ap_blue_fire_logic.h"

// Blue exhaust fire: swaps the retail flame and plume palettes in VRAM while
// player 1 carries Ultimate Speed Fire. Two things ask for it: the Extras
// option "Blue Fire At USF" (the CTR-ModSDK BlueFire module, as High Octane
// ships it) and, in the Archipelago build, the Progressive Boost capstone
// (AP_BlueFireWanted, ap/ap_blue_fire.c). One writer serves both so they never
// fight over the same VRAM cells.
// Palette data and NTSC VRAM positions are the BlueFire module's (Retro-Fueled
// carries the same data). See THIRD_PARTY_NOTICES.md.
static u32 s_redFlamesClut[8] = {
	0x80DA809A, 0x805A80BA, 0x811A803A, 0x819A81DA,
	0x813A817A, 0x829A821A, 0x835A839A, 0x82DA831A};
static u32 s_blueFlamesClut[8] = {
	0xFF25FF65, 0xFFA5FF45, 0xFEE5FFC5, 0xFE65FE25,
	0xFEC5FE85, 0xFD65FDE5, 0xFCA5FC65, 0xFD25FCE5};
static u32 s_redPlumesClut[8] = {
	0x000083FF, 0x821B833E, 0x829D81B8, 0x80F6800D,
	0x819A8054, 0x80D98011, 0x80078019, 0x84008015};
static u32 s_bluePlumesClut[8] = {
	0x7FFFF260, 0xFC20F980, 0xF400FC20, 0xF980AC00,
	0xF260F980, 0xB400FCA0, 0x8800FC20, 0x8800FC20};

static RECT s_flamesPos = {176, 256, 16, 1};
static RECT s_plumesPos = {464, 257, 16, 1};
static int s_blueFirePaletteState = AP_BLUE_FIRE_PALETTE_UNKNOWN;

// Every frame, in every mode (MainMain). The palettes are only touched while
// the racing or battle overlay is resident: hubs and menus load unrelated
// content into the same VRAM cells, so outside a race the latch is forgotten
// and the next race writes the right palette again.
void VehBlueFire_Tick(struct GameTracker *gGT)
{
	int resident;
	int wantBlue = 0;

	if (gGT == 0)
		return;

	resident = sdata != 0 && sdata->Loading.stage == LOAD_IDLE &&
	           LOAD_IsOpen_RacingOrBattle() != 0 &&
	           (gGT->gameMode1 & (MAIN_MENU | GAME_CUTSCENE | LOADING)) == 0;
	if (resident && gGT->numPlyrCurrGame == 1 && gGT->drivers[0] != 0)
	{
		const struct Driver *driver = gGT->drivers[0];
		wantBlue = g_config.blueFire &&
		           CtrExtras_BlueFireWanted(driver->reserves, driver->fireSpeedCap, driver->const_SacredFireSpeed);
#ifdef CTR_AP
		wantBlue = wantBlue || AP_BlueFireWanted(driver);
#endif
	}

	switch (AP_BlueFirePaletteActionFor(s_blueFirePaletteState, resident, wantBlue))
	{
	case AP_BLUE_FIRE_ACTION_FORGET:
		s_blueFirePaletteState = AP_BLUE_FIRE_PALETTE_UNKNOWN;
		break;
	case AP_BLUE_FIRE_ACTION_RED:
		LoadImage(&s_flamesPos, s_redFlamesClut);
		LoadImage(&s_plumesPos, s_redPlumesClut);
		s_blueFirePaletteState = AP_BLUE_FIRE_PALETTE_RED;
		break;
	case AP_BLUE_FIRE_ACTION_BLUE:
		LoadImage(&s_flamesPos, s_blueFlamesClut);
		LoadImage(&s_plumesPos, s_bluePlumesClut);
		s_blueFirePaletteState = AP_BLUE_FIRE_PALETTE_BLUE;
		break;
	default:
		break;
	}
}
