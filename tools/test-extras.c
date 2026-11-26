// Rules behind the Extras options (include/ctr_extras.h, docs/EXTRAS.md).
// Build: cc -Wall -Wextra -I include -o test-extras tools/test-extras.c
#include <stdio.h>

#include <ctr_extras.h>

static int failures;

static void expect(const char *name, int got, int want)
{
	if (got != want)
	{
		printf("FAIL %-44s got=%d want=%d\n", name, got, want);
		failures++;
	}
	else
		printf("PASS %s\n", name);
}

int main(void)
{
	// Room values are stand-ins for the driver constants;
	// only the ratios matter.
	const int maxRoom = 1000, readyRoom = 300;
	int r, g;

	CtrExtras_SlideMeterColour(maxRoom, readyRoom, maxRoom, &r, &g);
	expect("empty bar is green (red)", r, 0);
	expect("empty bar is green (green)", g, 0xFF);

	CtrExtras_SlideMeterColour(650, readyRoom, maxRoom, &r, &g);
	expect("half way to the window is yellow-green", r, 127);
	expect("half way keeps full green", g, 0xFF);

	CtrExtras_SlideMeterColour(readyRoom, readyRoom, maxRoom, &r, &g);
	expect("window opens yellow (red)", r, 0xFF);
	expect("window opens yellow (green)", g, 0xFF);

	CtrExtras_SlideMeterColour(120, readyRoom, maxRoom, &r, &g);
	expect("60% into the window is orange", g, 0x60);

	CtrExtras_SlideMeterColour(0, readyRoom, maxRoom, &r, &g);
	expect("end of the window is red (red)", r, 0xFF);
	expect("end of the window is red (green)", g, 0);

	// Green never rises again as the bar fills.
	int last = 0x100, monotonic = 1;
	for (int left = maxRoom; left >= 0; left--)
	{
		CtrExtras_SlideMeterColour(left, readyRoom, maxRoom, &r, &g);
		if (r < 0 || r > 0xFF || g < 0 || g > 0xFF)
			monotonic = 0;
		if (left <= readyRoom)
		{
			if (g > last)
				monotonic = 0;
			last = g;
		}
	}
	expect("colours stay in range and fade one way", monotonic, 1);

	CtrExtras_SlideMeterColour(500, 0, 0, &r, &g);
	expect("degenerate driver falls back to retail green", g, 0xFF);
	CtrExtras_SlideMeterColour(0, 0, 0, &r, &g);
	expect("degenerate driver falls back to retail red", r, 0xFF);

	// A super turbo pad gives a fire cap of 0x4800 (measured on Turbo Track);
	// 0x1000 stands in for the sacred-fire cap.
	expect("USF with reserves is blue", CtrExtras_BlueFireWanted(500, 0x4800, 0x1000), 1);
	expect("sacred fire is red", CtrExtras_BlueFireWanted(500, 0x1000, 0x1000), 0);
	expect("no reserves is red", CtrExtras_BlueFireWanted(0, 0x4800, 0x1000), 0);
	expect("wrapped reserves at USF stay blue", CtrExtras_BlueFireWanted(-5, 0x4800, 0x1000), 1);

	// Pad overlays measured on Hot Air Skyway: super trigger 410 is
	// x 10176..10944, z 5756..6525, y -944..-584. Overlay quad 411 matches it
	// (road quad 408 does too, but carries no animation, which the caller
	// checks), and a road quad shifted half a pad along covers half of it.
	expect("overlay covers its trigger fully",
	       CtrExtras_FootprintCover(10176, 10944, 5756, 6525, 10176, 10944, 5756, 6525), 1000);
	expect("overlay counts as a pad",
	       CtrExtras_QuadIsPadOverlay(CtrExtras_FootprintCover(10176, 10944, 5756, 6525, 10176, 10944, 5756, 6525)), 1);
	expect("road beside a pad is not one",
	       CtrExtras_QuadIsPadOverlay(CtrExtras_FootprintCover(10176, 10944, 5756, 6525, 10560, 11328, 5756, 6525)), 0);
	expect("touching edges cover nothing", CtrExtras_FootprintCover(0, 100, 0, 100, 100, 200, 0, 100), 0);
	expect("same height", CtrExtras_QuadAtTriggerHeight(-944 + -584, -944 + -584 + 60), 1);
	expect("a level above is not", CtrExtras_QuadAtTriggerHeight(-944 + -584, -944 + -584 + 2 * 600), 0);
	expect("cyan bit is free in PSX palette words", (0x7FFF & CTR_EXTRAS_CYAN_CLUT_BIT) == 0, 1);

	// Nitro-Fueled physics rules: auto-disabled in Time Trial, allowed elsewhere.
	const unsigned kTimeTrial = 0x20000u;
	expect("NF allowed in Arcade", CtrExtras_NitroFueledAllowed(0x400000u, kTimeTrial), 1);
	expect("NF disabled in Time Trial", CtrExtras_NitroFueledAllowed(kTimeTrial, kTimeTrial), 0);
	expect("NF Blue Fire keeps USF cap", CtrExtras_ShouldKeepFireCap(1, 0x4800, 0x1000), 1);
	expect("NF Blue Fire off drops USF cap", CtrExtras_ShouldKeepFireCap(0, 0x4800, 0x1000), 0);
	expect("NF Blue Fire ignores sacred fire", CtrExtras_ShouldKeepFireCap(1, 0x1000, 0x1000), 0);

	// NF U-turn reserve preservation and forced-Cross suppression.
	expect("U-turn keeps reserves with Down", CtrExtras_ShouldKeepReservesOnUturn(1, 1, 0), 1);
	expect("U-turn keeps reserves airborne", CtrExtras_ShouldKeepReservesOnUturn(1, 0, 120), 1);
	expect("normal brake clears reserves", CtrExtras_ShouldKeepReservesOnUturn(1, 0, 0), 0);
	expect("U-turn off clears reserves", CtrExtras_ShouldKeepReservesOnUturn(0, 1, 120), 0);
	expect("U-turn suppresses forced Cross", CtrExtras_ShouldSuppressForcedCrossOnUturn(1, 1, 1, 0, 0), 1);
	expect("air-brake suppresses forced Cross", CtrExtras_ShouldSuppressForcedCrossOnUturn(1, 1, 0, 64, 0), 1);
	expect("holding Cross does not suppress", CtrExtras_ShouldSuppressForcedCrossOnUturn(1, 1, 1, 0, 1), 0);
	expect("without Square does not suppress", CtrExtras_ShouldSuppressForcedCrossOnUturn(1, 0, 1, 0, 0), 0);

	// NF Perfect Boost Weight: early/Good weaker than retail linear, Perfect stronger.
	const int fullGain = 480;
	expect("outside window grants 0", CtrExtras_PerfectBoostReserves(readyRoom, readyRoom, fullGain), 0);
	expect("early Good (20%) is weaker than linear", CtrExtras_PerfectBoostReserves(240, readyRoom, fullGain), 72);
	expect("mid Good (50%) is weaker than linear", CtrExtras_PerfectBoostReserves(150, readyRoom, fullGain), 180);
	expect("Perfect threshold (60%) jumps up", CtrExtras_PerfectBoostReserves(120, readyRoom, fullGain), 360);
	expect("frame-perfect (100%) grants 1.5x", CtrExtras_PerfectBoostReserves(0, readyRoom, fullGain), 720);
	int prevRes = -1, resMono = 1;
	for (int left = readyRoom; left >= 0; left--)
	{
		int res = CtrExtras_PerfectBoostReserves(left, readyRoom, fullGain);
		if (res < prevRes)
			resMono = 0;
		prevRes = res;
	}
	expect("Perfect boost reserves are monotonic", resMono, 1);

	// Circular drift ring progress and tick positions.
	int fillMilli = 0, goodMilli = 0, perfectMilli = 0;
	expect("ring hidden when meterLeft == 0",
	       CtrExtras_DriftRingProgress(0, maxRoom, readyRoom, &fillMilli, &goodMilli, &perfectMilli), 0);
	expect("ring shown at drift start",
	       CtrExtras_DriftRingProgress(maxRoom, maxRoom, readyRoom, &fillMilli, &goodMilli, &perfectMilli), 1);
	expect("drift start fill is 0", fillMilli, 0);
	expect("Good tick at 70% of bar", goodMilli, 700);
	expect("Perfect tick at 88% of bar", perfectMilli, 880);
	expect("ring shown mid-Perfect",
	       CtrExtras_DriftRingProgress(60, maxRoom, readyRoom, &fillMilli, &goodMilli, &perfectMilli), 1);
	expect("mid-Perfect fill is 940", fillMilli, 940);

	printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures != 0;
}
