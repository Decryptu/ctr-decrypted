#pragma once

// Layout of a tab bar: the L1 and R1 hints at the two ends and the tabs
// between them, each in a box of its own (game/230/MM_TabBar.c draws it).
// Freestanding arithmetic, so tools/test-tab-bar.c checks it at any width.
//
// The tabs keep their full names when they fit, with their padding shrinking
// first, then switch to their short names, and as a last resort only the
// active tab is shown, between the hints, with its place ("2/4") beside it.
// Text never runs past the bar or into a neighbouring tab.

#define CTR_TAB_BAR_MAX 8
#define CTR_TAB_BAR_PAD_MAX 10 // text to box edge, each side
#define CTR_TAB_BAR_PAD_MIN 4
#define CTR_TAB_BAR_GAP 6      // between two boxes
#define CTR_TAB_BAR_HINT_GAP 8 // between a hint and the first or last box

typedef enum
{
	CTR_TAB_BAR_FULL = 0,  // every tab, full names
	CTR_TAB_BAR_SHORT = 1, // every tab, short names
	CTR_TAB_BAR_ACTIVE = 2 // the active tab only
} CtrTabBarMode;

typedef struct
{
	CtrTabBarMode mode;
	int pad;
	int x[CTR_TAB_BAR_MAX]; // box left edge; -1 for a tab that is not drawn
	int w[CTR_TAB_BAR_MAX]; // box width
	int hintLeftX;          // where "L1" starts
	int hintRightX;         // where "R1" ends
} CtrTabBarLayout;

static inline int CTR_TabBar_Total(const int *textW, int count, int pad)
{
	int total = (count - 1) * CTR_TAB_BAR_GAP;
	for (int i = 0; i < count; i++)
		total += textW[i] + 2 * pad;
	return total;
}

// left and right: the bar's edges. hintW: the width of the wider hint text.
// fullW and shortW: each tab's text width with its full and its short name.
static inline CtrTabBarLayout CTR_TabBar_Layout(int left, int right, int hintW, const int *fullW, const int *shortW,
                                                int count, int active)
{
	CtrTabBarLayout out;
	const int areaLeft = left + hintW + CTR_TAB_BAR_HINT_GAP;
	const int areaRight = right - hintW - CTR_TAB_BAR_HINT_GAP;
	const int area = areaRight - areaLeft;
	const int *textW = fullW;

	out.hintLeftX = left;
	out.hintRightX = right;
	out.mode = CTR_TAB_BAR_FULL;
	out.pad = CTR_TAB_BAR_PAD_MAX;
	if (count > CTR_TAB_BAR_MAX)
		count = CTR_TAB_BAR_MAX;
	for (int i = 0; i < CTR_TAB_BAR_MAX; i++)
	{
		out.x[i] = -1;
		out.w[i] = 0;
	}
	if (count <= 0)
		return out;
	if (active < 0 || active >= count)
		active = 0;

	for (int mode = CTR_TAB_BAR_FULL; mode <= CTR_TAB_BAR_SHORT; mode++)
	{
		textW = mode == CTR_TAB_BAR_FULL ? fullW : shortW;
		for (int pad = CTR_TAB_BAR_PAD_MAX; pad >= CTR_TAB_BAR_PAD_MIN; pad--)
		{
			const int total = CTR_TabBar_Total(textW, count, pad);
			if (total > area)
				continue;
			int x = areaLeft + (area - total) / 2;
			out.mode = (CtrTabBarMode)mode;
			out.pad = pad;
			for (int i = 0; i < count; i++)
			{
				out.x[i] = x;
				out.w[i] = textW[i] + 2 * pad;
				x += out.w[i] + CTR_TAB_BAR_GAP;
			}
			return out;
		}
	}

	// Only the active tab, short name, centred; the caller adds its place.
	out.mode = CTR_TAB_BAR_ACTIVE;
	out.pad = CTR_TAB_BAR_PAD_MIN;
	out.w[active] = shortW[active] + 2 * CTR_TAB_BAR_PAD_MIN;
	if (out.w[active] > area)
		out.w[active] = area > 0 ? area : 0;
	out.x[active] = areaLeft + (area - out.w[active]) / 2;
	return out;
}
