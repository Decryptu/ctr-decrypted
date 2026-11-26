#include <common.h>
#ifdef CTR_NATIVE
#include <math.h>
#include "../../ap/ap_reserves_meter_logic.h" // #387 reserves meter display math
#include <ctr_extras.h>
#include <widescreen.h>
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

static void UI_DriftRing_AddQuad(float cx, float cy, float xPerY,
                                 float a0, float a1, float rIn, float rOut,
                                 ColorCode colorCode, u32 *ot)
{
	PolyF4 *p;
	GetPrimMem(p);
	if (p == nullptr)
	{
		return;
	}

	const float c0 = cosf(a0);
	const float s0 = sinf(a0);
	const float c1 = cosf(a1);
	const float s1 = sinf(a1);

	p->colorCode = colorCode;
	p->v[0].pos.x = (s16)lroundf(cx + rOut * xPerY * c0);
	p->v[0].pos.y = (s16)lroundf(cy + rOut * s0);
	p->v[1].pos.x = (s16)lroundf(cx + rOut * xPerY * c1);
	p->v[1].pos.y = (s16)lroundf(cy + rOut * s1);
	p->v[2].pos.x = (s16)lroundf(cx + rIn * xPerY * c0);
	p->v[2].pos.y = (s16)lroundf(cy + rIn * s0);
	p->v[3].pos.x = (s16)lroundf(cx + rIn * xPerY * c1);
	p->v[3].pos.y = (s16)lroundf(cy + rIn * s1);

	AddPrimitive(p, ot);
}

// Extras "Circular Drift Ring": a Breath of the Wild-style circular stamina
// ring drawn near the top-right of the kart while drifting, filling clockwise
// from 12 o'clock with green-to-red progression and Good/Perfect zone ticks.
void UI_DrawDriftRing(struct Driver *driver)
{
	if (!g_config.driftRing || driver == nullptr || driver->kartState != KS_DRIFTING)
	{
		return;
	}

	const int maxRoom = (u8)driver->const_turboMaxRoom * ELAPSED_MS;
	const int readyRoom = (u8)driver->const_turboLowRoomWarning * ELAPSED_MS;
	int fillMilli = 0;
	int goodMilli = 0;
	int perfectMilli = 0;
	if (!CtrExtras_DriftRingProgress(driver->turbo_MeterRoomLeft, maxRoom, readyRoom,
	                                 &fillMilli, &goodMilli, &perfectMilli))
	{
		return;
	}

	const struct GameTracker *gGT = GAME_TRACKER;
	const struct PushBuffer *pb = &gGT->pushBuffer[driver->driverID];
	u32 *ot = gGT->pushBuffer_UI.ptrOT;
	const PrimCode primCode = {.kind.poly = {.quad = 1, .renderCode = RenderCode_Polygon}};

	const int factor = Widescreen_GetFactor();
	const int isSplit = (gGT->numPlyrCurrGame >= 2 || pb->rect.h < 180);
	const float ryOuter = isSplit ? 9.5f : 13.5f;
	const float ryInner = isSplit ? 5.5f : 8.0f;
	const float ryRimOut = ryOuter + 1.5f;
	const float ryRimIn = ryInner - 1.5f;
	const float xPerY = (16.0f * (float)factor) / 9000.0f;
	const float cx = (float)(pb->rect.x + pb->rect.w / 2) + (isSplit ? 34.0f : 48.0f) * ((float)factor / 1000.0f);
	const float cy = (float)(pb->rect.y + pb->rect.h / 2) - (isSplit ? 2.0f : 4.0f);

	const float kTwoPi = 6.28318530718f;
	const float kStartAngle = -1.57079632679f; // 12 o'clock, filling clockwise
	const float fillFrac = (float)fillMilli / 1000.0f;
	const float goodFrac = (float)goodMilli / 1000.0f;
	const float perfectFrac = (float)perfectMilli / 1000.0f;

	int red = 0;
	int green = 0;
	CtrExtras_SlideMeterColour(driver->turbo_MeterRoomLeft, readyRoom, maxRoom, &red, &green);
	const ColorCode fillColor = MakeColorCode((u8)red, (u8)green, 0, primCode);
	const ColorCode rimColor = MakeColorCode(18, 18, 20, primCode);
	const ColorCode trackBaseColor = MakeColorCode(48, 52, 56, primCode);
	const ColorCode trackGoodColor = MakeColorCode(78, 66, 38, primCode);
	const ColorCode trackPerfectColor = MakeColorCode(92, 40, 36, primCode);
	const ColorCode goodTickColor = MakeColorCode(255, 255, 255, primCode);
	const ColorCode perfectTickColor = MakeColorCode(255, 235, 110, primCode);

	// PSX OT is LIFO: primitives added first draw on top.
	// 1. Threshold zone ticks (topmost).
	{
		const float startHalf = 0.009f * kTwoPi;
		UI_DriftRing_AddQuad(cx, cy, xPerY, kStartAngle - startHalf, kStartAngle + startHalf,
		                     ryRimIn, ryRimOut, rimColor, ot);
	}
	if (goodMilli > 0 && goodMilli < 1000)
	{
		const float a = kStartAngle + goodFrac * kTwoPi;
		const float innerHalf = 0.009f * kTwoPi;
		const float outerHalf = 0.016f * kTwoPi;
		UI_DriftRing_AddQuad(cx, cy, xPerY, a - innerHalf, a + innerHalf,
		                     ryInner - 1.0f, ryOuter + 1.0f, goodTickColor, ot);
		UI_DriftRing_AddQuad(cx, cy, xPerY, a - outerHalf, a + outerHalf,
		                     ryRimIn, ryRimOut, rimColor, ot);
	}
	if (perfectMilli > 0 && perfectMilli < 1000)
	{
		const float a = kStartAngle + perfectFrac * kTwoPi;
		const float innerHalf = 0.009f * kTwoPi;
		const float outerHalf = 0.016f * kTwoPi;
		UI_DriftRing_AddQuad(cx, cy, xPerY, a - innerHalf, a + innerHalf,
		                     ryInner - 1.0f, ryOuter + 1.0f, perfectTickColor, ot);
		UI_DriftRing_AddQuad(cx, cy, xPerY, a - outerHalf, a + outerHalf,
		                     ryRimIn, ryRimOut, rimColor, ot);
	}

	// 2. Filled clockwise arc.
	const int kSegs = 32;
	for (int i = 0; i < kSegs; i++)
	{
		const float t0 = (float)i / (float)kSegs;
		const float t1 = (float)(i + 1) / (float)kSegs;
		if (t0 < fillFrac)
		{
			const float endT = t1 < fillFrac ? t1 : fillFrac;
			UI_DriftRing_AddQuad(cx, cy, xPerY,
			                     kStartAngle + t0 * kTwoPi,
			                     kStartAngle + endT * kTwoPi,
			                     ryInner, ryOuter, fillColor, ot);
		}
	}

	// 3. Unfilled track with subtle Good / Perfect zone tinting.
	for (int i = 0; i < kSegs; i++)
	{
		const float t0 = (float)i / (float)kSegs;
		const float t1 = (float)(i + 1) / (float)kSegs;
		if (t1 > fillFrac)
		{
			const float startT = t0 > fillFrac ? t0 : fillFrac;
			const float midT = 0.5f * (startT + t1);
			ColorCode trackColor = trackBaseColor;
			if (perfectMilli > 0 && midT >= perfectFrac)
			{
				trackColor = trackPerfectColor;
			}
			else if (goodMilli > 0 && midT >= goodFrac)
			{
				trackColor = trackGoodColor;
			}
			UI_DriftRing_AddQuad(cx, cy, xPerY,
			                     kStartAngle + startT * kTwoPi,
			                     kStartAngle + t1 * kTwoPi,
			                     ryInner, ryOuter, trackColor, ot);
		}
	}

	// 4. Outer / inner dark rim (backmost).
	for (int i = 0; i < kSegs; i++)
	{
		const float t0 = (float)i / (float)kSegs;
		const float t1 = (float)(i + 1) / (float)kSegs;
		UI_DriftRing_AddQuad(cx, cy, xPerY,
		                     kStartAngle + t0 * kTwoPi,
		                     kStartAngle + t1 * kTwoPi,
		                     ryRimIn, ryRimOut, rimColor, ot);
	}
}
#endif
