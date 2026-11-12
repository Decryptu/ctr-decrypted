/* OPTIONS > Cheats: the retail title-screen cheats as toggles. Included by
   MM_ConfigMenu.c; a bespoke page like Backups.

   Each row flips the same gameMode2 bit its code sets (MM_CheatCodes.c), so a
   code typed on the title screen shows as ON here, and a cheat lasts until the
   game closes, as in retail. Retail still turns the item cheats off when
   Adventure or Time Trial starts. The unlock codes are one-off save changes,
   not toggles, and stay codes only. Nothing here is written to config.ini. */

#define CHEATS_LABEL_X   0x38
#define CHEATS_VALUE_X   CONFIG_VALUE_X
#define CHEATS_ROW_Y     0x34
#define CHEATS_ROW_STEP  0x0A
#define CHEATS_DETAIL_Y  0xAE
#define CHEATS_NOTE_Y    0xB8
#define CHEATS_HINT_Y    0xC6

typedef struct
{
	const char *label;
	u32 bit;
	const char *detail;
	int item; // retail turns it off when Adventure or Time Trial starts
} MM_CheatRow;

static const MM_CheatRow s_cheatRows[] = {
	{"Max Wumpa", CHEAT_WUMPA, "Always ten Wumpa Fruit.", 1},
	{"Infinite Masks", CHEAT_MASK, "Endless masks.", 1},
	{"Infinite Turbos", CHEAT_TURBO, "Endless turbo boosts.", 1},
	{"Infinite Bombs", CHEAT_BOMBS, "Endless bombs.", 1},
	{"Super Engine", CHEAT_ENGINE, "A super engine all race.", 1},
	{"Invisibility", CHEAT_INVISIBLE, "Players are invisible all race.", 0},
	{"Super Turbo Pads", CHEAT_TURBOPAD, "Every turbo pad is a super pad.", 0},
	{"Icy Tracks", CHEAT_ICY, "Every track is as slippery as ice.", 0},
	{"Adventure AI", CHEAT_ADV, "Opponents drive as in Adventure.", 0},
	{"Super Hard", CHEAT_SUPERHARD, "Opponents drive harder still.", 0},
	{"One Lap Race", CHEAT_ONELAP, "Arcade and VS races are one lap.", 0},
	{"Turbo Counter", CHEAT_TURBOCOUNT, "Counts chained turbo boosts.", 0},
};
#define CHEATS_COUNT ((int)(sizeof(s_cheatRows) / sizeof(s_cheatRows[0])))

static int s_cheatSel;

static int Config_IsCheatsSection(int sec)
{
	return sec >= 0 && sec < s_numSections && strcmp(s_sectionName[sec], CTR_MENU_PAGE_CHEATS) == 0;
}

static void MM_ConfigProc_Cheats(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	const u32 tap = pad->buttonsTapped;
	struct GameTracker *gGT = sdata->gGT;
	int i;

	(void)menu;
	if ((tap & BTN_UP) != 0)
	{
		s_cheatSel = s_cheatSel > 0 ? s_cheatSel - 1 : CHEATS_COUNT - 1;
		OtherFX_Play(0, 1);
	}
	if ((tap & BTN_DOWN) != 0)
	{
		s_cheatSel = s_cheatSel < CHEATS_COUNT - 1 ? s_cheatSel + 1 : 0;
		OtherFX_Play(0, 1);
	}
	if ((tap & (BTN_CROSS | BTN_CIRCLE | BTN_LEFT | BTN_RIGHT)) != 0)
	{
		gGT->gameMode2 ^= s_cheatRows[s_cheatSel].bit;
		OtherFX_Play((gGT->gameMode2 & s_cheatRows[s_cheatSel].bit) ? MM_CHEAT_SUCCESS_SFX : 1, 1);
	}

	DecalFont_DrawLineOT("Cheats", 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
	for (i = 0; i < CHEATS_COUNT; i++)
	{
		const int y = CHEATS_ROW_Y + i * CHEATS_ROW_STEP;
		const int on = (gGT->gameMode2 & s_cheatRows[i].bit) != 0;
		DecalFont_DrawLineOT((char *)s_cheatRows[i].label, CHEATS_LABEL_X, y, FONT_SMALL, ORANGE, ot);
		DecalFont_DrawLineOT(on ? "ON" : "OFF", CHEATS_VALUE_X, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
		if (i == s_cheatSel)
			Config_DrawRowHighlight(ot, y);
	}
	DecalFont_DrawLineOT((char *)s_cheatRows[s_cheatSel].detail, 0x100, CHEATS_DETAIL_Y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	if (s_cheatRows[s_cheatSel].item)
		DecalFont_DrawLineOT("Off in Adventure and Time Trial.", 0x100, CHEATS_NOTE_Y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	else if (s_cheatRows[s_cheatSel].bit == CHEAT_TURBOPAD)
		DecalFont_DrawLineOT("Not in Time Trial.", 0x100, CHEATS_NOTE_Y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	DecalFont_DrawLineOT("*: TOGGLE   ^: BACK", 0x100, CHEATS_HINT_Y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
}
