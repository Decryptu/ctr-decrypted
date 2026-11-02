// Harness for the tab bar layout (include/ctr_tab_bar.h): boxes never overlap
// or leave the bar, the hints stay clear, the padding shrinks before the short
// names are used, and a bar too narrow for every tab shows the active one.
//
//   cc -Wall -Wextra -I include -o /tmp/test-tab-bar tools/test-tab-bar.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>

#include "ctr_tab_bar.h"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

// Retail small glyphs advance 13 pixels.
#define CHAR_W 13
#define HINT_W (2 * CHAR_W)

// Every drawn box inside the tab area, in order, CTR_TAB_BAR_GAP apart or more.
static int fits(const CtrTabBarLayout *l, int left, int right, int count)
{
	const int areaLeft = left + HINT_W + CTR_TAB_BAR_HINT_GAP;
	const int areaRight = right - HINT_W - CTR_TAB_BAR_HINT_GAP;
	int prevEnd = -1;
	for (int i = 0; i < count; i++)
	{
		if (l->x[i] < 0)
			continue;
		if (l->x[i] < areaLeft || l->x[i] + l->w[i] > areaRight)
			return 0;
		if (prevEnd >= 0 && l->x[i] < prevEnd + CTR_TAB_BAR_GAP)
			return 0;
		prevEnd = l->x[i] + l->w[i];
	}
	return 1;
}

static int textFits(const CtrTabBarLayout *l, const int *textW, int count)
{
	for (int i = 0; i < count; i++)
		if (l->x[i] >= 0 && l->w[i] - 2 * l->pad < textW[i] && l->mode != CTR_TAB_BAR_ACTIVE)
			return 0;
	return 1;
}

int main(void)
{
	// The Mods screen: Seed, Tracks, Racers, Installed (short: Owned).
	const int full[4] = {4 * CHAR_W, 6 * CHAR_W, 6 * CHAR_W, 9 * CHAR_W};
	const int shortW[4] = {4 * CHAR_W, 6 * CHAR_W, 6 * CHAR_W, 5 * CHAR_W};

	// Three tabs across the Mods panel: full names, full padding, centred.
	{
		const CtrTabBarLayout l = CTR_TabBar_Layout(20, 492, HINT_W, full + 1, shortW + 1, 3, 1);
		expect(l.mode == CTR_TAB_BAR_FULL && l.pad == CTR_TAB_BAR_PAD_MAX, "three tabs: full names, full padding");
		expect(fits(&l, 20, 492, 3) && textFits(&l, full + 1, 3), "three tabs: inside the bar, apart");
		{
			const int areaLeft = 20 + HINT_W + CTR_TAB_BAR_HINT_GAP, areaRight = 492 - HINT_W - CTR_TAB_BAR_HINT_GAP;
			const int leftSpace = l.x[0] - areaLeft, rightSpace = areaRight - (l.x[2] + l.w[2]);
			expect(leftSpace - rightSpace <= 1 && rightSpace - leftSpace <= 1, "three tabs: centred");
		}
		expect(l.hintLeftX == 20 && l.hintRightX == 492, "hints at the bar's ends");
	}

	// Four tabs (a room with custom tracks): the padding shrinks, names stay.
	{
		const CtrTabBarLayout l = CTR_TabBar_Layout(20, 492, HINT_W, full, shortW, 4, 0);
		expect(l.mode == CTR_TAB_BAR_FULL && l.pad < CTR_TAB_BAR_PAD_MAX && l.pad >= CTR_TAB_BAR_PAD_MIN, "four tabs: padding shrinks first");
		expect(fits(&l, 20, 492, 4) && textFits(&l, full, 4), "four tabs: inside the bar, apart");
	}

	// Narrower: the short names.
	{
		const CtrTabBarLayout l = CTR_TabBar_Layout(20, 420, HINT_W, full, shortW, 4, 3);
		expect(l.mode == CTR_TAB_BAR_SHORT, "narrow: short names");
		expect(fits(&l, 20, 420, 4) && textFits(&l, shortW, 4), "narrow: inside the bar, apart");
	}

	// Too narrow for every tab: only the active one, centred.
	{
		const CtrTabBarLayout l = CTR_TabBar_Layout(20, 260, HINT_W, full, shortW, 4, 2);
		expect(l.mode == CTR_TAB_BAR_ACTIVE, "very narrow: active tab only");
		expect(l.x[0] < 0 && l.x[1] < 0 && l.x[3] < 0 && l.x[2] >= 0, "very narrow: only the active tab drawn");
		expect(fits(&l, 20, 260, 4), "very narrow: inside the bar");
	}

	// No width at all: still nothing outside the bar, and an active index out
	// of range falls back to the first tab.
	{
		const CtrTabBarLayout l = CTR_TabBar_Layout(20, 90, HINT_W, full, shortW, 4, 9);
		expect(l.mode == CTR_TAB_BAR_ACTIVE && l.x[0] >= 0 && l.w[0] >= 0, "no room: first tab, clamped");
	}

	// Every width from very narrow to full: never overlapping, never outside.
	{
		int ok = 1;
		for (int right = 120; right <= 492; right++)
			for (int active = 0; active < 4; active++)
			{
				const CtrTabBarLayout l = CTR_TabBar_Layout(20, right, HINT_W, full, shortW, 4, active);
				ok &= fits(&l, 20, right, 4) && l.x[active] >= 0;
			}
		expect(ok, "every width: boxes inside and apart, active tab always drawn");
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
