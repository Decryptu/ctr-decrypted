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

	printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
	return failures != 0;
}
