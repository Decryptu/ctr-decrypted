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

#endif
