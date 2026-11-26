#ifndef CTR_EXTRAS_H
#define CTR_EXTRAS_H

// Pure rules for the Extras options (docs/EXTRAS.md): the CTR-ModSDK HUD and
// effect mods that High Octane ships, kept free of game state so
// tools/test-extras.c can check them.

// Powerslide bar colour for "Slide Meter Gradient". meterLeft is the room left
// before a boost is allowed (driver->turbo_MeterRoomLeft), readyRoom the point
// where the boost window opens (const_turboLowRoomWarning * ELAPSED_MS) and
// maxRoom the full bar (const_turboMaxRoom * ELAPSED_MS).
// While the bar fills toward the window the colour goes green to yellow; inside
// the window it goes yellow to orange over the first 60%, then to red. With a
// degenerate driver (no room configured) it falls back to retail's colours:
// green before the window, red inside it. Blue is always 0.
static inline void CtrExtras_SlideMeterColour(int meterLeft, int readyRoom, int maxRoom, int *red, int *green)
{
	if (maxRoom <= readyRoom || readyRoom <= 0)
	{
		*red = readyRoom < meterLeft ? 0 : 0xFF;
		*green = readyRoom < meterLeft ? 0xFF : 0;
		return;
	}

	if (meterLeft > readyRoom)
	{
		int progress = ((maxRoom - meterLeft) * 0xFF) / (maxRoom - readyRoom);
		*red = progress < 0 ? 0 : progress > 0xFF ? 0xFF : progress;
		*green = 0xFF;
		return;
	}

	int window = ((readyRoom - meterLeft) * 100) / readyRoom;
	window = window < 0 ? 0 : window > 100 ? 100 : window;
	*red = 0xFF;
	*green = window < 60 ? 0xFF - (window * 0x9F) / 60 : 0x60 - ((window - 60) * 0x60) / 40;
}

// "Blue Fire At USF": whether player 1's exhaust should use the blue palette.
// Ultimate Speed Fire is a fire speed cap above the sacred-fire cap (the same
// test VehFire_Increment uses to shrink USF on an ordinary boost). The ModSDK
// module compared with 0x4800, the value that cap takes with retail's
// constants; reading the driver's own constants keeps it right if they change.
static inline int CtrExtras_BlueFireWanted(int reserves, int fireSpeedCap, int sacredFireSpeed)
{
	return reserves != 0 && fireSpeedCap > sacredFireSpeed;
}

// "Cyan Super Turbo Pads": which level quads draw a super turbo pad.
// A pad's trigger is an invisible quad (QUADBLOCK_FLAG_TRIGGER) whose terrain
// byte carries the turbo or super turbo step bit. The pad you see is a separate
// visible quad laid over it, textured with the pad's animated arrows; ordinary
// and super pads share that animation. Measured on Hot Air Skyway, Oxide
// Station, Cortex Castle and Turbo Track (tools/decrypted/quad_dump.py).

// Share of box a's X/Z footprint covered by box b, in thousandths.
static inline int CtrExtras_FootprintCover(int aMinX, int aMaxX, int aMinZ, int aMaxZ,
                                           int bMinX, int bMaxX, int bMinZ, int bMaxZ)
{
	int w = (aMaxX < bMaxX ? aMaxX : bMaxX) - (aMinX > bMinX ? aMinX : bMinX);
	int d = (aMaxZ < bMaxZ ? aMaxZ : bMaxZ) - (aMinZ > bMinZ ? aMinZ : bMinZ);
	if (w <= 0 || d <= 0)
		return 0;
	long long area = (long long)(aMaxX - aMinX > 0 ? aMaxX - aMinX : 1) * (aMaxZ - aMinZ > 0 ? aMaxZ - aMinZ : 1);
	return (int)(((long long)w * d * 1000) / area);
}

// A visible quad is a pad's overlay when it covers most of a turbo trigger's
// footprint: every pad overlay measured covers its trigger by 98% or more,
// while road quads beside a pad cover at most about half.
#define CTR_EXTRAS_PAD_COVER 900
static inline int CtrExtras_QuadIsPadOverlay(int triggerCoveredByQuad)
{
	return triggerCoveredByQuad >= CTR_EXTRAS_PAD_COVER;
}

// ...and lies at the trigger's height: overlays measured sit within 30 units
// of it, so 256 keeps out the road on a level above or below. Centres are
// passed doubled (min + max) to stay in integers.
#define CTR_EXTRAS_PAD_HEIGHT 256
static inline int CtrExtras_QuadAtTriggerHeight(int quadCenterY2, int triggerCenterY2)
{
	int dy = quadCenterY2 - triggerCenterY2;
	if (dy < 0)
		dy = -dy;
	return dy <= 2 * CTR_EXTRAS_PAD_HEIGHT;
}

// The palette word's top bit marks a super-pad face for the renderer. PSX
// palette words keep it clear (the hardware requires 0 there), so it never
// collides with real data; the shader clears it before the palette lookup
// and recolours the texel cyan (platform/native_renderer.c).
#define CTR_EXTRAS_CYAN_CLUT_BIT 0x8000u

// Percentage into the powerslide boost window where the Perfect zone begins.
// Shared between the Slide Meter Gradient colour transition, the Circular Drift
// Ring's Perfect tick mark, and NF Perfect Boost Weight.
#define CTR_EXTRAS_PERFECT_WINDOW_PCT 60

// Nitro-Fueled gameplay enhancements are auto-disabled in Time Trial so classic
// Time Trial records and ghosts stay retail-accurate. Active in all other modes,
// including Archipelago.
static inline int CtrExtras_NitroFueledAllowed(unsigned gameMode1, unsigned timeTrialMask)
{
	return (gameMode1 & timeTrialMask) == 0;
}

// "NF Blue Fire & Reserves": keep an already-held USF / blue-fire speed cap
// when powersliding, hitting an ordinary turbo pad / boost ramp, or landing
// from a jump boost, instead of downgrading it to the lower boost tier.
static inline int CtrExtras_ShouldKeepFireCap(int enabled, int currentCap, int sacredCap)
{
	return enabled && currentCap > sacredCap;
}

// "NF U-Turn Reserves": air-braking (jump_LandingBoost != 0) and ground
// U-turning (holding Down + Brake) keep reserves and active fire tier instead
// of wiping reserves on brake.
static inline int CtrExtras_ShouldKeepReservesOnUturn(int enabled, int holdingDown, int landingBoost)
{
	return enabled && (holdingDown || landingBoost);
}

// Companion to CtrExtras_ShouldKeepReservesOnUturn: while reserves are nonzero,
// retail forces Cross held, which prevents Square from entering the brake /
// U-turn branch. Suppress forced Cross while U-turning with gas released.
static inline int CtrExtras_ShouldSuppressForcedCrossOnUturn(int enabled,
                                                             int holdingSquare,
                                                             int holdingDown,
                                                             int landingBoost,
                                                             int holdingCross)
{
	return enabled && holdingSquare && (holdingDown || landingBoost) && !holdingCross;
}

// "NF Perfect Boost Weight": non-linear powerslide reserve weighting.
// Early ("Good") boosts in the first 60% of the boost window grant 0..45% of
// fullGain (weaker than retail's linear 0..60%); late ("Perfect") boosts in the
// last 40% of the window jump to 75..150% of fullGain.
static inline int CtrExtras_PerfectBoostReserves(int meterLeft, int highMeter, int fullGain)
{
	if (highMeter <= 0 || fullGain <= 0 || meterLeft >= highMeter)
		return 0;
	if (meterLeft < 0)
		meterLeft = 0;

	int window = ((highMeter - meterLeft) * 100) / highMeter;
	if (window < CTR_EXTRAS_PERFECT_WINDOW_PCT)
		return (fullGain * window * 3) / 400;

	int pct = 75 + ((window - CTR_EXTRAS_PERFECT_WINDOW_PCT) * 75) / (100 - CTR_EXTRAS_PERFECT_WINDOW_PCT);
	return (fullGain * pct) / 100;
}

// "Circular Drift Ring": normalized 0..1000 clockwise fill fraction and the
// Good / Perfect zone tick positions around the ring. Returns 1 when the ring
// should draw (valid driver constants and meterLeft > 0), 0 otherwise.
static inline int CtrExtras_DriftRingProgress(int meterLeft, int maxRoom, int readyRoom,
                                              int *fillMilli, int *goodMilli, int *perfectMilli)
{
	if (meterLeft <= 0 || maxRoom <= 0)
		return 0;

	int clampedLeft = meterLeft > maxRoom ? maxRoom : meterLeft;
	int fill = ((maxRoom - clampedLeft) * 1000) / maxRoom;
	if (fill < 0)
		fill = 0;
	if (fill > 1000)
		fill = 1000;
	*fillMilli = fill;

	if (readyRoom > 0 && readyRoom < maxRoom)
	{
		*goodMilli = ((maxRoom - readyRoom) * 1000) / maxRoom;
		int perfectLeft = (readyRoom * (100 - CTR_EXTRAS_PERFECT_WINDOW_PCT)) / 100;
		*perfectMilli = ((maxRoom - perfectLeft) * 1000) / maxRoom;
	}
	else
	{
		*goodMilli = 0;
		*perfectMilli = 0;
	}
	return 1;
}

#endif
