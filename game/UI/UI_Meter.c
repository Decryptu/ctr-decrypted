#include <common.h>
#ifdef CTR_NATIVE
#include "../../ap/ap_reserves_meter_logic.h" // #387 reserves meter display math
#include <ctr_extras.h>
#endif

#include "UI_JumpMeter_Update.c"
#include "UI_JumpMeter_Draw.c"
#include "UI_DrawSlideMeter.c"


#ifdef CTR_NATIVE
// Extras "Reserves Meter": the turbo "reserves" meter -- the seconds of
// stacked exhaust fire a player is holding. Ported from the CTR-ModSDK
// ReservesMeter module (GPL-3.0); geometry, thresholds and colours are kept verbatim so it matches the modded
// original. A 49x3 bar drawn right-anchored on the same x as the boost bar,
// filling leftward. driver->reserves is milliseconds-of-fire in the game's
// 960ms "second"; the bar caps (pegs) at 8400 units (~8.75s) and turns blue as
// the overflow tell ("saffi fire"), while the value itself has no real cap.
// Past 0x7fff the signed field wraps negative and the engine stops counting it
// down; the display math in ap_reserves_meter_logic.h reads it unsigned so it
// still shows as a full bar, drawn purple for that "Saffi fire" state as CTR
// Unlimited does (#387). Written in the UI_DrawSlideMeter idiom above.
void UI_DrawReservesMeter(s16 posX, s16 posY, struct Driver *driver)
{
	const struct GameTracker *gGT = GAME_TRACKER;
	const int barWidth = 49;
	const int barHeight = 3;

	s16 topX = posX - barWidth;
	s16 topY = posY - barHeight;

	RECT box;
	box.x = topX;
	box.y = topY;
	box.w = barWidth;
	box.h = barHeight;
	Color black = MakeColor(0, 0, 0);
	CTR_Box_DrawWireBox(&box, &black, gGT->pushBuffer_UI.ptrOT, &gGT->backBuffer->primMem);

	const PrimCode primCode = {.kind.poly = {.quad = 1, .renderCode = RenderCode_Polygon}};

	// Fill colour by reserves tier (thresholds verbatim from ReservesMeter).
	// Fill grows leftward from the right edge; when it would exceed the bar it
	// pegs at full width and turns blue (the reserves overflow / "saffi fire").
	ColorCode colorCode;
	switch (AP_ReservesMeterTier(driver->reserves))
	{
	case AP_RESERVES_TIER_SAFFI:
		// Wrapped past 0x7fff, no longer draining. N. Gin's purple from the game's
		// own driver palette (data.colors N_GIN_PURPLE, 0xFF00A9 as BGR).
		colorCode = MakeColorCode(0xA9, 0, 0xFF, primCode);
		break;
	case AP_RESERVES_TIER_BLUE:
		colorCode = MakeColorCode(0, 0, 0xFF, primCode); // blue, pegged
		break;
	case AP_RESERVES_TIER_GREEN:
		colorCode = MakeColorCode(0, 0xFF, 0, primCode); // green, high
		break;
	case AP_RESERVES_TIER_YELLOW:
		colorCode = MakeColorCode(0xFF, 0xFF, 0, primCode); // yellow, mid
		break;
	default:
		colorCode = MakeColorCode(0xFF, 0, 0, primCode); // red, low reserves
		break;
	}
	int meterLength = AP_ReservesMeterLength(driver->reserves);

	for (int i = 0; i < 2; i++)
	{
		PolyF4 *p;
		GetPrimMem(p);
		if (p == nullptr)
		{
			return;
		}

		p->colorCode = colorCode;

		p->v[0].pos.y = topY;
		p->v[1].pos.y = topY;
		p->v[2].pos.y = posY;
		p->v[3].pos.y = posY;

		p->v[0].pos.x = posX - meterLength;
		p->v[1].pos.x = posX;
		p->v[2].pos.x = posX - meterLength;
		p->v[3].pos.x = posX;

		AddPrimitive(p, gGT->pushBuffer_UI.ptrOT);
		colorCode = MakeColorCode(0x80, 0x80, 0x80, primCode); // grey background bar
		meterLength = barWidth;
	}
}
#endif
