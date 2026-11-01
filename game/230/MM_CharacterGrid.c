// Native character select. Included from MM_Characters.c in place of the
// retail roster, navigation and drawing (the PS1 build keeps those).
//
// Retail keeps its 15 icons in hand-made tables (D230.characterSelectMeta*):
// fixed coordinates and neighbour links per player count, and every player is
// tracked by character ID. That has no room for N. Oxide or custom racers: a
// custom racer borrows a retail character ID (its template), so two icons can
// share one ID, and a long roster needs pages. Here the screen is built from a
// roster list instead:
//
// - The roster is the retail racers in each layout's reading order, then N.
//   Oxide once unlocked, then the custom racers (NativeRacers_*).
// - Each layout lists its rows in fill order with their capacity; entries flow
//   into them and every row is centred, so the full retail roster lands
//   exactly where retail puts it, and extra racers extend the last row and
//   then go on more pages. A locked retail racer keeps an empty place, as in
//   retail, except in the last retail row, where the extra racers join it.
// - Navigation is by position: up and down go to the nearest icon in the next
//   row that has one, left and right along the row, and off a row's end to the
//   next or previous page. L1 and R1 also turn the page. Icons taken by another
//   player are skipped.
//
// Players are tracked by roster entry (s_entry), and GAME_CHARACTER_IDS and
// NativeRacers_SetPlayer are written from it every frame.

#include <platform/native_racers.h>

enum
{
	CS_RETAIL_COUNT = 15,
	CS_MAX_ROSTER = CS_RETAIL_COUNT + 1 + 64,
	CS_MAX_ROWS = 4,
	CS_MAX_PAGE_ICONS = 18,
	CS_PITCH_X = 0x40,
	CS_PITCH_Y = 0x27,
	CS_LIMITED_SHIFT_Y = 0x20,
	CS_PAGE_FRAMES = MM_CHARACTER_SELECT_TRANSITION_FRAMES,
	CS_PAGE_SLIDE_X = 0x200,
	CS_ARROW_GAP = 10,
	// The chrome transitions: the title, one per player window, a terminator.
	CS_CHROME_TITLE = 0,
	CS_CHROME_WINDOW_FIRST = 1,
	CS_CHROME_COUNT = 2 + MM_CHARACTER_SELECT_MAX_PLAYERS,
	CS_MENU_WHEEL_SIZE = 0xccc,
};

struct CsRow
{
	s16 y;
	u8 capacity;
};

struct CsLayout
{
	// Retail character IDs in reading order (the order they fill the rows).
	s8 order[CS_RETAIL_COUNT];
	// Rows in fill order. The last one holds the last retail row.
	struct CsRow rows[CS_MAX_ROWS];
	u8 numRows;
	// Icon x (left edge) of a row's centre: rows are centred on it.
	s16 centerX;
	// Where icons slide in from: below (1P/2P) or the left (3P/4P).
	u8 slideFromLeft;
};

// Retail positions (D230.characterSelectMeta*), as rows: 1P/2P has rows of
// 6, 6 and 3; 3P rows of 4 under a top row of 3; 4P two rows of 6 between the
// windows, two icons under them and Penta above.
static const struct CsLayout s_layout1P2P = {
    {N_TROPY, CRASH_BANDICOOT, NEO_CORTEX, TINY_TIGER, COCO_BANDICOOT, PINSTRIPE, RIPPER_ROO, N_GIN, DINGODILE, POLAR, PURA, PAPU_PAPU,
     KOMODO_JOE, PENTA_PENGUIN, FAKE_CRASH},
    {{0x60, 6}, {0x87, 6}, {0xAE, 6}},
    3,
    0xE0,
    0,
};
static const struct CsLayout s_layout3P = {
    {CRASH_BANDICOOT, NEO_CORTEX, TINY_TIGER, COCO_BANDICOOT, N_GIN, DINGODILE, POLAR, PURA, N_TROPY, PINSTRIPE, RIPPER_ROO, PAPU_PAPU,
     KOMODO_JOE, PENTA_PENGUIN, FAKE_CRASH},
    {{0x47, 4}, {0x6E, 4}, {0x95, 4}, {0x20, 4}},
    4,
    0x80,
    1,
};
static const struct CsLayout s_layout4P = {
    {RIPPER_ROO, CRASH_BANDICOOT, NEO_CORTEX, TINY_TIGER, COCO_BANDICOOT, PAPU_PAPU, KOMODO_JOE, N_GIN, DINGODILE, POLAR, PURA, PINSTRIPE,
     N_TROPY, FAKE_CRASH, PENTA_PENGUIN},
    {{0x47, 6}, {0x6E, 6}, {0x95, 2}, {0x20, 2}},
    4,
    0xE0,
    1,
};

struct CsEntry
{
	s16 characterID;
	s16 racer; // -1: the retail character
	u8 visible; // unlocked; a locked retail racer keeps an empty place
	u8 page;
	u8 row; // index into the layout's rows
	u8 slot; // index among the icons on its page
	s16 x;
	s16 y;
};

static struct CsEntry s_entries[CS_MAX_ROSTER];
static int s_entryCount;
static int s_pageCount;
static const struct CsLayout *s_activeLayout;
static int s_limited; // 1P/2P with the last row empty: the grid moves down, as retail's unexpanded layout

static s16 s_entry[MM_CHARACTER_SELECT_MAX_PLAYERS];
static s16 s_currentEntry[MM_CHARACTER_SELECT_MAX_PLAYERS];
static s16 s_desiredEntry[MM_CHARACTER_SELECT_MAX_PLAYERS];
static s8 s_racerBackup[MM_CHARACTER_SELECT_MAX_PLAYERS] = {-1, -1, -1, -1};
static int s_page;
static int s_pageFrame;
static int s_pageSlide; // -1 or 1: the side a turned page comes in from

static struct TransitionMeta s_iconTransition[CS_MAX_PAGE_ICONS + 1];
static struct TransitionMeta s_chromeTransition[CS_CHROME_COUNT];

static int MM_CharacterGrid_Unlocked(u16 unlockFlags)
{
	return ((s16)unlockFlags == MM_CHARACTER_UNLOCK_ALWAYS) || CHECK_ADV_BIT(GAME_PROGRESS.unlocks, unlockFlags);
}

// Retail's unlock requirement of a retail racer, from its own table.
static u16 MM_CharacterGrid_RetailUnlock(int characterID)
{
	for (int i = 0; i < CS_RETAIL_COUNT; i++)
	{
		if (D230.characterSelectMeta1P2P[i].characterID == characterID)
			return D230.characterSelectMeta1P2P[i].unlockFlags;
	}
	return (u16)MM_CHARACTER_UNLOCK_ALWAYS;
}

static void MM_CharacterGrid_Add(int characterID, int racer, int visible)
{
	if (s_entryCount >= CS_MAX_ROSTER)
		return;
	struct CsEntry *e = &s_entries[s_entryCount++];
	e->characterID = (s16)characterID;
	e->racer = (s16)racer;
	e->visible = (u8)visible;
}

static int MM_CharacterGrid_PageCapacity(const struct CsLayout *layout)
{
	int capacity = 0;
	for (int r = 0; r < layout->numRows; r++)
		capacity += layout->rows[r].capacity;
	return capacity;
}

// Builds the roster and places it. Runs every frame (the unlocks and the
// player count can change under the menu); the players' entries index it, and
// the order only grows at the end, so they stay valid.
static void MM_CharacterGrid_Build(void)
{
	const int numPlayers = GAME_TRACKER->numPlyrNextGame;
	const struct CsLayout *layout = (numPlayers >= 4) ? &s_layout4P : (numPlayers == 3) ? &s_layout3P : &s_layout1P2P;
	s_activeLayout = layout;

	// Where the last retail row starts in reading order.
	int lastRowStart = 0;
	{
		int left = CS_RETAIL_COUNT;
		for (int r = 0; (r < layout->numRows) && (left > layout->rows[r].capacity); r++)
		{
			lastRowStart += layout->rows[r].capacity;
			left -= layout->rows[r].capacity;
		}
	}

	// The players' entries are re-found by racer after the rebuild: the
	// order differs between layouts (the player count changes between
	// RestoreIDs and the first frame of the menu).
	s16 *const tracked[3] = {s_entry, s_currentEntry, s_desiredEntry};
	s16 trackedID[3][MM_CHARACTER_SELECT_MAX_PLAYERS];
	s16 trackedRacer[3][MM_CHARACTER_SELECT_MAX_PLAYERS];
	for (int k = 0; k < 3; k++)
	{
		for (int p = 0; p < MM_CHARACTER_SELECT_MAX_PLAYERS; p++)
		{
			const int entry = tracked[k][p];
			const int valid = (entry >= 0) && (entry < s_entryCount);
			trackedID[k][p] = valid ? s_entries[entry].characterID : -1;
			trackedRacer[k][p] = valid ? s_entries[entry].racer : -1;
		}
	}

	s_entryCount = 0;
	for (int i = 0; i < CS_RETAIL_COUNT; i++)
	{
		const int id = layout->order[i];
		const int unlocked = MM_CharacterGrid_Unlocked(MM_CharacterGrid_RetailUnlock(id));
		if (unlocked || (i < lastRowStart))
			MM_CharacterGrid_Add(id, -1, unlocked);
	}
	if (MM_CharacterGrid_Unlocked(GAME_UNLOCK_BIT_OXIDE))
		MM_CharacterGrid_Add(NITROS_OXIDE, -1, 1);
	for (int r = 0; r < NativeRacers_Count(); r++)
		MM_CharacterGrid_Add(NativeRacers_Template(r), r, 1);

	// Flow into rows and pages.
	const int perPage = MM_CharacterGrid_PageCapacity(layout);
	s_pageCount = (s_entryCount + perPage - 1) / perPage;
	if (s_pageCount < 1)
		s_pageCount = 1;

	int rowCount[CS_MAX_ROWS];
	int index = 0;
	for (int page = 0; page < s_pageCount; page++)
	{
		int start = index;
		for (int r = 0; r < layout->numRows; r++)
		{
			const int left = s_entryCount - index;
			rowCount[r] = (left < layout->rows[r].capacity) ? (left > 0 ? left : 0) : layout->rows[r].capacity;
			for (int j = 0; j < rowCount[r]; j++, index++)
			{
				struct CsEntry *e = &s_entries[index];
				e->page = (u8)page;
				e->row = (u8)r;
				e->slot = (u8)(index - start);
				e->x = (s16)(layout->centerX - (rowCount[r] - 1) * (CS_PITCH_X / 2) + j * CS_PITCH_X);
				e->y = layout->rows[r].y;
			}
		}
	}

	// 1P/2P: when the roster leaves the last row empty, the grid moves down
	// under the "Select Character" title, as retail's unexpanded layout.
	s_limited = 0;
	if (numPlayers <= 2)
	{
		s_limited = 1;
		for (int i = 0; i < s_entryCount; i++)
		{
			if ((s_entries[i].row == layout->numRows - 1) && s_entries[i].visible)
				s_limited = 0;
		}
		if (s_limited)
		{
			for (int i = 0; i < s_entryCount; i++)
				s_entries[i].y += CS_LIMITED_SHIFT_Y;
		}
	}

	for (int k = 0; k < 3; k++)
	{
		for (int p = 0; p < MM_CHARACTER_SELECT_MAX_PLAYERS; p++)
		{
			if (trackedID[k][p] < 0)
				continue;
			int found = -1;
			for (int i = 0; (i < s_entryCount) && (found < 0); i++)
			{
				if ((s_entries[i].characterID == trackedID[k][p]) && (s_entries[i].racer == trackedRacer[k][p]))
					found = i;
			}
			tracked[k][p] = (s16)found;
		}
	}
	// A pick that left the roster (a removed racer) falls back to Crash, who
	// is always there, so every player always has an entry.
	for (int p = 0; p < numPlayers; p++)
	{
		if (s_entry[p] < 0)
		{
			for (int i = 0; i < s_entryCount; i++)
			{
				if ((s_entries[i].characterID == CRASH_BANDICOOT) && (s_entries[i].racer < 0))
					s_entry[p] = (s16)i;
			}
		}
		if (s_currentEntry[p] < 0)
			s_currentEntry[p] = s_entry[p];
		if (s_desiredEntry[p] < 0)
			s_desiredEntry[p] = s_entry[p];
	}
}

static int MM_CharacterGrid_FindEntry(int characterID, int racer)
{
	for (int i = 0; i < s_entryCount; i++)
	{
		if (s_entries[i].visible && (s_entries[i].characterID == characterID) && (s_entries[i].racer == racer))
			return i;
	}
	return -1;
}

static int MM_CharacterGrid_TakenByOther(int entry, int player)
{
	for (int p = 0; p < GAME_TRACKER->numPlyrNextGame; p++)
	{
		if ((p != player) && (s_entry[p] == entry))
			return 1;
	}
	return 0;
}

static int MM_CharacterGrid_Selectable(int entry, int player)
{
	return (entry >= 0) && (entry < s_entryCount) && s_entries[entry].visible && !MM_CharacterGrid_TakenByOther(entry, player);
}

// The free icon on `page` nearest to (x, y), rows weighing more than columns
// so the cursor keeps its row when it can.
static int MM_CharacterGrid_Nearest(int page, int x, int y, int player)
{
	int best = -1;
	int bestScore = 0x7fffffff;
	for (int i = 0; i < s_entryCount; i++)
	{
		if ((s_entries[i].page != page) || !MM_CharacterGrid_Selectable(i, player))
			continue;
		const int dx = s_entries[i].x - x;
		const int dy = s_entries[i].y - y;
		const int score = (dy < 0 ? -dy : dy) * 16 + (dx < 0 ? -dx : dx);
		if (score < bestScore)
		{
			bestScore = score;
			best = i;
		}
	}
	return best;
}

// The next icon from `from` in a direction (CHARACTER_SELECT_DIR_*), or -1:
// along the row for left and right, and for up and down the nearest row with
// an icon at most half a column aside (retail's links never go further, so
// an edge column has no way down to a shorter row). On a tie between two
// columns, down goes right and up goes left, as retail's links do. Icons
// taken by another player than `player` are passed over; -1 counts them.
static int MM_CharacterGrid_Step(int from, int direction, int player)
{
	const struct CsEntry *f = &s_entries[from];
	const int horizontal = (direction == CHARACTER_SELECT_DIR_LEFT) || (direction == CHARACTER_SELECT_DIR_RIGHT);
	int best = -1;
	int bestScore = 0x7fffffff;

	for (int i = 0; i < s_entryCount; i++)
	{
		const struct CsEntry *e = &s_entries[i];
		if ((e->page != f->page) || (i == from) || !e->visible || ((player >= 0) && !MM_CharacterGrid_Selectable(i, player)))
			continue;

		const int dx = e->x - f->x;
		const int dy = e->y - f->y;
		const int adx = (dx < 0) ? -dx : dx;
		const int ady = (dy < 0) ? -dy : dy;
		int score;
		if (horizontal)
		{
			if ((dy != 0) || ((direction == CHARACTER_SELECT_DIR_LEFT) ? (dx >= 0) : (dx <= 0)))
				continue;
			score = adx;
		}
		else
		{
			if (((direction == CHARACTER_SELECT_DIR_UP) ? (dy >= 0) : (dy <= 0)) || (adx > CS_PITCH_X / 2))
				continue;
			const int towardTie = (direction == CHARACTER_SELECT_DIR_DOWN) ? (dx > 0) : (dx < 0);
			score = ady * 0x1000 + adx * 2 + (towardTie ? 0 : 1);
		}
		if (score < bestScore)
		{
			bestScore = score;
			best = i;
		}
	}
	return best;
}

// Retail's way out of a dead end: one step across (down then up for left and
// right, left then right for up and down), then the move from there.
static int MM_CharacterGrid_StepAround(int from, int direction, int player)
{
	const int horizontal = (direction == CHARACTER_SELECT_DIR_LEFT) || (direction == CHARACTER_SELECT_DIR_RIGHT);
	const int across[2] = {horizontal ? CHARACTER_SELECT_DIR_DOWN : CHARACTER_SELECT_DIR_LEFT,
	                       horizontal ? CHARACTER_SELECT_DIR_UP : CHARACTER_SELECT_DIR_RIGHT};
	for (int k = 0; k < 2; k++)
	{
		const int side = MM_CharacterGrid_Step(from, across[k], -1);
		if (side < 0)
			continue;
		const int to = MM_CharacterGrid_Step(side, direction, player);
		if ((to >= 0) && (to != from))
			return to;
	}
	return -1;
}

// Whether the row of `from` has any icon further that way, free or not.
static int MM_CharacterGrid_RowContinues(int from, int direction)
{
	const struct CsEntry *f = &s_entries[from];
	for (int i = 0; i < s_entryCount; i++)
	{
		const struct CsEntry *e = &s_entries[i];
		if ((e->page == f->page) && e->visible && (e->y == f->y) &&
		    ((direction == CHARACTER_SELECT_DIR_LEFT) ? (e->x < f->x) : (e->x > f->x)))
			return 1;
	}
	return 0;
}

static void MM_CharacterGrid_TurnPage(int delta)
{
	const int page = s_page + delta;
	if ((page < 0) || (page >= s_pageCount))
		return;
	s_page = page;
	s_pageSlide = delta;
	s_pageFrame = CS_PAGE_FRAMES;
	for (int i = 0; i < CS_MAX_PAGE_ICONS; i++)
	{
		s_iconTransition[i].currX = (s16)(delta * CS_PAGE_SLIDE_X);
		s_iconTransition[i].currY = 0;
	}
}

// The transitions of the icons on the current page and of the title and
// windows. Positions only: the current offsets are kept between frames.
static void MM_CharacterGrid_BuildTransitions(void)
{
	const struct CsLayout *layout = s_activeLayout;
	const int turning = (s_pageFrame > 0);
	int count = 0;

	for (int i = 0; i < s_entryCount; i++)
	{
		const struct CsEntry *e = &s_entries[i];
		if (e->page != s_page)
			continue;
		struct TransitionMeta *t = &s_iconTransition[e->slot];
		// Retail staggers the icons so the grid sweeps in as a wave; the
		// head starts here follow the same pattern from row and column.
		const int column = (e->x - 0x40) / CS_PITCH_X;
		int headStart;
		if (turning)
		{
			t->distX = (s16)(s_pageSlide * CS_PAGE_SLIDE_X);
			t->distY = 0;
			headStart = (s_pageSlide > 0) ? column / 2 : (5 - column) / 2;
		}
		else if (layout->slideFromLeft)
		{
			t->distX = -512;
			t->distY = 0;
			headStart = column + (layout->numRows - 2 - e->row);
		}
		else
		{
			t->distX = 0;
			t->distY = 0xC8;
			headStart = 7 - column - e->row;
		}
		t->headStart = (s16)((headStart < 0) ? 0 : (headStart > 7) ? 7 : headStart);
		if (e->slot + 1 > count)
			count = e->slot + 1;
	}
	s_iconTransition[count].headStart = -1;

	// Title and windows: retail's own entries (after its 15 icons).
	const struct TransitionMeta *retail = D230.characterSelectTransitionByPlayerCount[GAME_TRACKER->numPlyrNextGame - 1];
	for (int i = 0; i < CS_CHROME_COUNT - 1; i++)
	{
		s_chromeTransition[i].distX = retail[CS_RETAIL_COUNT + i].distX;
		s_chromeTransition[i].distY = retail[CS_RETAIL_COUNT + i].distY;
		s_chromeTransition[i].headStart = retail[CS_RETAIL_COUNT + i].headStart;
	}
	s_chromeTransition[CS_CHROME_COUNT - 1].headStart = -1;
}

void MM_Characters_SetMenuLayout(void)
{
	MM_CharacterGrid_Build();

	int layoutIndex = GAME_TRACKER->numPlyrNextGame - 1;
	if ((layoutIndex < MM_CHARACTER_SELECT_FULL_LAYOUT_COUNT) && s_limited)
		layoutIndex += MM_CHARACTER_SELECT_LIMITED_LAYOUT_OFFSET;

	MM_CHARACTER_SELECT_ROSTER_EXPANDED = !s_limited;
	MM_CHARACTER_SELECT_LAYOUT_INDEX = layoutIndex;
	MM_ACTIVE_CHARACTER_SELECT_WINDOW_POS = MM_CHARACTER_SELECT_WINDOW_POS_BY_LAYOUT[layoutIndex];
	// Unused by the native screen; kept pointing at a retail table so the
	// pointer stays valid (savestates relocate it).
	MM_ACTIVE_CHARACTER_SELECT_META = MM_CHARACTER_SELECT_META_BY_LAYOUT[layoutIndex];
	MM_CHARACTER_SELECT_WINDOW_WIDTH = MM_CHARACTER_SELECT_LAYOUT_WINDOW_W[layoutIndex];
	MM_CHARACTER_SELECT_WINDOW_HEIGHT = MM_CHARACTER_SELECT_LAYOUT_WINDOW_H[layoutIndex];
	MM_CHARACTER_SELECT_NAME_TEXT_Y = MM_CHARACTER_SELECT_LAYOUT_TEXT_Y[layoutIndex];
	MM_CHARACTER_SELECT_DRIVER_POS.y = MM_CHARACTER_SELECT_LAYOUT_DRIVER_POS_Y[layoutIndex];
	MM_CHARACTER_SELECT_DRIVER_POS.z = MM_CHARACTER_SELECT_LAYOUT_DRIVER_POS_Z[layoutIndex];

	MM_CharacterGrid_BuildTransitions();
}

void MM_Characters_BackupIDs(void)
{
	for (int i = 0; i < MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT; i++)
		MM_CHARACTER_IDS_BACKUP[i] = GAME_CHARACTER_IDS[i];
	for (int p = 0; p < MM_CHARACTER_SELECT_MAX_PLAYERS; p++)
		s_racerBackup[p] = (s8)NativeRacers_Player(p);
}

static void MM_CharacterGrid_WritePlayers(void)
{
	for (int p = 0; p < GAME_TRACKER->numPlyrNextGame; p++)
	{
		const struct CsEntry *e = &s_entries[s_entry[p]];
		GAME_CHARACTER_IDS[p] = e->characterID;
		NativeRacers_SetPlayer(p, e->racer);
	}
}

void MM_Characters_RestoreIDs(void)
{
	MM_CHARACTER_SELECT_FLAGS = 0;
	MM_CHARACTER_SELECT_TRANSITION_FRAME = MM_CHARACTER_SELECT_TRANSITION_FRAMES;
	MM_CHARACTER_SELECT_MENU_STATE = ENTERING_MENU;

	for (int i = 0; i < MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT; i++)
		GAME_CHARACTER_IDS[i] = MM_CHARACTER_IDS_BACKUP[i];

	MM_Characters_SetMenuLayout();

	const int numPlayers = GAME_TRACKER->numPlyrNextGame;
	for (int p = 0; p < MM_CHARACTER_SELECT_MAX_PLAYERS; p++)
		s_entry[p] = -1;
	for (int p = 0; p < numPlayers; p++)
	{
		// The last pick, or Crash when it is gone (locked, or a racer that
		// was removed), as retail falls back to Crash.
		int entry = MM_CharacterGrid_FindEntry(GAME_CHARACTER_IDS[p], s_racerBackup[p]);
		if (entry < 0)
			entry = MM_CharacterGrid_FindEntry(CRASH_BANDICOOT, -1);
		s_entry[p] = (s16)entry;

		// Two players on one icon: the later one takes the first free
		// default racer, as retail's PreventOverlap.
		if (MM_CharacterGrid_TakenByOther(entry, p))
		{
			for (int id = 0; id < MM_CHARACTER_SELECT_DEFAULT_DRIVER_COUNT; id++)
			{
				const int candidate = MM_CharacterGrid_FindEntry(id, -1);
				if (MM_CharacterGrid_Selectable(candidate, p))
				{
					s_entry[p] = (s16)candidate;
					break;
				}
			}
		}
	}
	MM_CharacterGrid_WritePlayers();

	for (int p = 0; p < MM_CHARACTER_SELECT_MAX_PLAYERS; p++)
	{
		s_currentEntry[p] = s_desiredEntry[p] = s_entry[p];
		MM_CHARACTER_SELECT_MOVE_TIMERS[p] = 0;
		MM_CHARACTER_SELECT_ANGLE[p] = (p * MM_CHARACTER_SELECT_ANGLE_STEP) + MM_CHARACTER_SELECT_ANGLE_OFFSET;
	}

	s_page = (s_entry[0] >= 0) ? s_entries[s_entry[0]].page : 0;
	s_pageFrame = 0;
	MM_CharacterGrid_BuildTransitions();

	MM_Characters_DrawWindows(0);
}

static struct Model *MM_CharacterGrid_Model(int entry)
{
	if ((entry < 0) || (entry >= s_entryCount))
		return NULL;
	const struct CsEntry *e = &s_entries[entry];
	struct Model *model = (e->racer >= 0) ? NativeRacers_MenuModel(e->racer) : NULL;
	return model ? model : MM_Characters_GetModelByName(GAME_CHARACTER_METADATA[e->characterID].name_Debug);
}

static struct Icon *MM_CharacterGrid_Portrait(const struct CsEntry *e)
{
	struct Icon *icon = (e->racer >= 0) ? NativeRacers_Portrait(e->racer) : NULL;
	return icon ? icon : GAME_TRACKER->ptrIcons[GAME_CHARACTER_METADATA[e->characterID].iconID];
}

static char *MM_CharacterGrid_Name(const struct CsEntry *e)
{
	const char *name = (e->racer >= 0) ? NativeRacers_Name(e->racer) : NULL;
	return name ? (char *)name : GAME_LANGUAGE_STRINGS[GAME_CHARACTER_METADATA[e->characterID].name_LNG_long];
}

void MM_Characters_DrawWindows(b32 showDrivers)
{
	struct GameTracker *gGT = GAME_TRACKER;

	if (showDrivers)
		gGT->renderFlags |= RENDER_FLAG_TIRES;

	for (int p = 0; p < gGT->numPlyrNextGame; p++)
	{
		struct PushBuffer *pb = &gGT->pushBuffer[p];
		const SVec2 *windowPos = &MM_ACTIVE_CHARACTER_SELECT_WINDOW_POS[p];
		const struct TransitionMeta *t = &s_chromeTransition[CS_CHROME_WINDOW_FIRST + p];

		pb->rect.x = windowPos->x + t->currX;
		pb->rect.y = windowPos->y + t->currY;
		pb->rect.w = MM_CHARACTER_SELECT_WINDOW_WIDTH;
		pb->rect.h = MM_CHARACTER_SELECT_WINDOW_HEIGHT;

		// Clip the viewport to the screen while the window slides. The native
		// renderer needs at least one pixel where retail leaves zero.
		if (pb->rect.x < 0)
		{
			pb->rect.w += pb->rect.x;
			pb->rect.x = 0;
		}
		if (pb->rect.y < 0)
		{
			pb->rect.h += pb->rect.y;
			pb->rect.y = 0;
		}
		if (pb->rect.x + pb->rect.w > MM_CHARACTER_SELECT_SCREEN_W)
			pb->rect.w = MM_CHARACTER_SELECT_SCREEN_W - pb->rect.x;
		if (pb->rect.y + pb->rect.h > MM_CHARACTER_SELECT_SCREEN_H)
			pb->rect.h = MM_CHARACTER_SELECT_SCREEN_H - pb->rect.y;
		if (pb->rect.x > MM_CHARACTER_SELECT_SCREEN_W)
			pb->rect.x = MM_CHARACTER_SELECT_SCREEN_W;
		if (pb->rect.y > MM_CHARACTER_SELECT_SCREEN_H)
			pb->rect.y = MM_CHARACTER_SELECT_SCREEN_H;
		if (pb->rect.w <= 0)
			pb->rect.w = 1;
		if (pb->rect.h <= 0)
			pb->rect.h = 1;

		pb->distanceToScreen_CURR = MM_CHARACTER_SELECT_DISTANCE_TO_SCREEN;
		pb->distanceToScreen_PREV = MM_CHARACTER_SELECT_DISTANCE_TO_SCREEN;
		pb->pos.x = pb->pos.y = pb->pos.z = 0;
		pb->rot.x = pb->rot.y = pb->rot.z = 0;

		struct Instance *inst = gGT->drivers[p]->instSelf;
		inst->flags &= ~HIDE_MODEL;
		if (!showDrivers)
			inst->flags |= HIDE_MODEL;

		// Each window's camera sees only its own driver.
		struct InstDrawPerPlayer *idpp = INST_GETIDPP(inst);
		for (int v = 0; v < MM_CHARACTER_SELECT_MAX_PLAYERS; v++)
			idpp[v].pushBuffer = NULL;
		idpp[p].pushBuffer = pb;

		inst->animFrame = 0;
		inst->animIndex = 0;
		inst->model = MM_CharacterGrid_Model(s_currentEntry[p]);

		// Oxide's hovercraft has no wheels.
		const int currentID = (s_currentEntry[p] >= 0) ? s_entries[s_currentEntry[p]].characterID : CRASH_BANDICOOT;
		gGT->drivers[p]->wheelSize = (currentID == NITROS_OXIDE) ? 0 : CS_MENU_WHEEL_SIZE;

		gGT->cameraDC[p].cameraMode = CAMERA_MODE_FREECAM;

		inst->matrix.t[0] = MM_CHARACTER_SELECT_DRIVER_POS.x;
		inst->matrix.t[1] = MM_CHARACTER_SELECT_DRIVER_POS.y;
		inst->matrix.t[2] = MM_CHARACTER_SELECT_DRIVER_POS.z;

		// A new pick slides the old driver out and the new one in.
		s16 *moveTimer = &MM_CHARACTER_SELECT_MOVE_TIMERS[p];
		if (*moveTimer != 0)
		{
			const int frames = MM_CHARACTER_SELECT_MOVE_FRAMES;
			const int dir = MM_CHARACTER_SELECT_MOVE_DIR[p];
			const int timer = --(*moveTimer);
			if (timer < frames)
			{
				s_currentEntry[p] = s_desiredEntry[p];
				const int scale = RaceFlag_MoveModels(timer, frames);
				inst->matrix.t[0] += -dir * ((scale * MM_CHARACTER_SELECT_SLIDE_DISTANCE) >> MM_CHARACTER_SELECT_MODEL_MOVE_FP_SHIFT);
			}
			else
			{
				const int scale = RaceFlag_MoveModels(timer - frames, frames);
				inst->matrix.t[0] += dir * (((MM_CHARACTER_SELECT_MODEL_MOVE_FP - scale) * MM_CHARACTER_SELECT_SLIDE_DISTANCE) >> MM_CHARACTER_SELECT_MODEL_MOVE_FP_SHIFT);
			}
		}
		else if (s_currentEntry[p] != s_entry[p])
		{
			*moveTimer = (s16)(MM_CHARACTER_SELECT_MOVE_FRAMES * 2);
			s_desiredEntry[p] = s_entry[p];
		}

		SVec3 rot;
		rot.x = MM_CHARACTER_SELECT_DRIVER_ROT.x;
		rot.y = (s16)(MM_CHARACTER_SELECT_DRIVER_ROT.y + MM_CHARACTER_SELECT_ANGLE[p]);
		rot.z = MM_CHARACTER_SELECT_DRIVER_ROT.z;
		ConvertRotToMatrix(&inst->matrix, &rot);
	}
}

// One player's input. Returns nothing; writes s_entry, the select flags and
// the menu state.
static void MM_CharacterGrid_Input(int p)
{
	const u32 button = MM_GAME_BUTTON_TAPS[p];
	const int selected = ((MM_CHARACTER_SELECT_FLAGS >> p) & 1) != 0;

	if (MM_CHARACTER_SELECT_MENU_STATE != IN_MENU)
		return;
	if ((button & (MM_CHARACTER_SELECT_INPUT_DPAD | MM_CHARACTER_SELECT_INPUT_MENU | BTN_L1 | BTN_R1)) == 0)
		return;

	if (!selected)
	{
		const int from = s_entry[p];
		int to = from;

		if (button & (BTN_L1 | BTN_R1))
		{
			const int delta = (button & BTN_L1) ? -1 : 1;
			const int page = s_page + delta;
			if ((page >= 0) && (page < s_pageCount))
			{
				const struct CsEntry *f = &s_entries[from];
				MM_CharacterGrid_TurnPage(delta);
				const int nearest = MM_CharacterGrid_Nearest(page, (f->page == page - delta) ? f->x : s_activeLayout->centerX, f->y, p);
				if (nearest >= 0)
					to = nearest;
			}
			MM_CHARACTER_SELECT_MOVE_DIR[p] = (delta < 0) ? MM_CHARACTER_SELECT_MODEL_MOVE_PREV : MM_CHARACTER_SELECT_MODEL_MOVE_NEXT;
		}
		else if (button & MM_CHARACTER_SELECT_INPUT_DPAD)
		{
			int direction;
			if (button & BTN_UP)
				direction = CHARACTER_SELECT_DIR_UP;
			else if (button & BTN_DOWN)
				direction = CHARACTER_SELECT_DIR_DOWN;
			else if (button & BTN_LEFT)
				direction = CHARACTER_SELECT_DIR_LEFT;
			else
				direction = CHARACTER_SELECT_DIR_RIGHT;
			MM_CHARACTER_SELECT_MOVE_DIR[p] = ((direction == CHARACTER_SELECT_DIR_UP) || (direction == CHARACTER_SELECT_DIR_LEFT))
			                                      ? MM_CHARACTER_SELECT_MODEL_MOVE_PREV
			                                      : MM_CHARACTER_SELECT_MODEL_MOVE_NEXT;

			if (s_entries[from].page != s_page)
			{
				// The cursor is on another page: bring it onto this one.
				const int nearest = MM_CharacterGrid_Nearest(s_page, s_activeLayout->centerX, s_activeLayout->rows[0].y, p);
				if (nearest >= 0)
					to = nearest;
			}
			else
			{
				to = MM_CharacterGrid_Step(from, direction, p);
				if ((to < 0) && ((direction == CHARACTER_SELECT_DIR_LEFT) || (direction == CHARACTER_SELECT_DIR_RIGHT)))
				{
					// Off the end of a row: the next or previous page, on
					// the same row's far side.
					const int delta = (direction == CHARACTER_SELECT_DIR_RIGHT) ? 1 : -1;
					const int page = s_page + delta;
					if (!MM_CharacterGrid_RowContinues(from, direction) && (page >= 0) && (page < s_pageCount))
					{
						const int edgeX = (delta > 0) ? 0 : 0x200;
						to = MM_CharacterGrid_Nearest(page, edgeX, s_entries[from].y, p);
						if (to >= 0)
							MM_CharacterGrid_TurnPage(delta);
					}
				}
				if (to < 0)
					to = MM_CharacterGrid_StepAround(from, direction, p);
				if (to < 0)
					to = from;
			}
		}

		if (to != from)
		{
			s_entry[p] = (s16)to;
			OtherFX_Play(0, 1);
		}

		if (button & MM_CHARACTER_SELECT_INPUT_CONFIRM)
		{
			MM_CHARACTER_SELECT_FLAGS |= (u16)(1 << p);
			OtherFX_Play(1, 1);
			if (MM_CHARACTER_SELECT_FLAGS == (u32)((1 << GAME_TRACKER->numPlyrNextGame) - 1))
			{
				MM_CHARACTER_SELECT_EXITS_FORWARD = 1;
				MM_CHARACTER_SELECT_MENU_STATE = EXITING_MENU;
			}
		}

		// Player 1's Square or Triangle leaves for the main menu.
		if ((p == 0) && (button & MM_CHARACTER_SELECT_INPUT_BACK))
		{
			MM_CHARACTER_SELECT_EXITS_FORWARD = 0;
			MM_CHARACTER_SELECT_MENU_STATE = EXITING_MENU;
			OtherFX_Play(2, 1);
		}
	}
	else if (button & MM_CHARACTER_SELECT_INPUT_BACK)
	{
		OtherFX_Play(2, 1);
		MM_CHARACTER_SELECT_FLAGS &= ~(u16)(1 << p);
	}

	MM_GAME_BUTTON_TAPS[p] = 0;
}

static void MM_CharacterGrid_DrawPageArrows(u32 *ot)
{
	struct GameTracker *gGT = GAME_TRACKER;
	const struct CsLayout *layout = s_activeLayout;
	if (s_pageCount < 2)
		return;

	// Beside the widest row, at the grid's middle height.
	int capacity = 0;
	int top = 0x7fff, bottom = 0;
	for (int r = 0; r < layout->numRows; r++)
	{
		if (layout->rows[r].capacity > capacity)
			capacity = layout->rows[r].capacity;
		if (layout->rows[r].y < top)
			top = layout->rows[r].y;
		if (layout->rows[r].y > bottom)
			bottom = layout->rows[r].y;
	}
	const int left = layout->centerX - (capacity - 1) * (CS_PITCH_X / 2);
	const int right = left + (capacity - 1) * CS_PITCH_X + MM_CHARACTER_SELECT_ICON_RECT_W;
	const int y = (top + bottom + MM_CHARACTER_SELECT_ICON_RECT_H) / 2 + (s_limited ? CS_LIMITED_SHIFT_Y : 0);

	struct Icon **icons = ICONGROUP_GETICONS(gGT->iconGroup[4]);
	u32 *colors = data.ptrColor[ORANGE];
	const int x[2] = {left - CS_ARROW_GAP, right + CS_ARROW_GAP};
	const int rotation[2] = {0x800, 0};
	const int shown[2] = {s_page > 0, s_page < s_pageCount - 1};
	for (int i = 0; i < 2; i++)
	{
		if (!shown[i])
			continue;
		DecalHUD_Arrow2D(icons[0x38], x[i], y, &gGT->backBuffer->primMem, ot, ColorCode_Load(&colors[0]), ColorCode_Load(&colors[1]),
		                 ColorCode_Load(&colors[2]), ColorCode_Load(&colors[3]), 0, 0x1000, rotation[i]);
	}
}

// "Select Character" over an empty top row (3P/4P) or above the moved-down
// grid (1P/2P), where retail draws it.
static void MM_CharacterGrid_DrawTitle(void)
{
	const struct TransitionMeta *t = &s_chromeTransition[CS_CHROME_TITLE];
	const int numPlayers = GAME_TRACKER->numPlyrNextGame;

	if (numPlayers <= 2)
	{
		if (s_limited)
			DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_SELECT_CHARACTER], t->currX + MM_CHARACTER_SELECT_LIMITED_TITLE_X,
			                   t->currY + MM_CHARACTER_SELECT_LIMITED_TITLE_Y, FONT_BIG, (JUSTIFY_CENTER | ORANGE));
		return;
	}

	const int topRow = s_activeLayout->numRows - 1;
	for (int i = 0; i < s_entryCount; i++)
	{
		if ((s_entries[i].page == s_page) && (s_entries[i].row == topRow) && s_entries[i].visible)
			return;
	}

	const int is3P = (numPlayers == 3);
	const int x = t->currX + (is3P ? MM_CHARACTER_SELECT_3P_TITLE_X : MM_CHARACTER_SELECT_4P_TITLE_X);
	const s16 font = is3P ? FONT_BIG : FONT_CREDITS;
	DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_SELECT_CHARACTER_SELECT], x,
	                   t->currY + (is3P ? MM_CHARACTER_SELECT_3P_SELECT_Y : MM_CHARACTER_SELECT_4P_SELECT_Y), font, (JUSTIFY_CENTER | ORANGE));
	DecalFont_DrawLine(GAME_LANGUAGE_STRINGS[LNG_CHARACTER], x,
	                   t->currY + (is3P ? MM_CHARACTER_SELECT_3P_CHARACTER_Y : MM_CHARACTER_SELECT_4P_CHARACTER_Y), font, (JUSTIFY_CENTER | ORANGE));
}

void MM_Characters_MenuProc(struct RectMenu *unused)
{
	struct GameTracker *gGT = GAME_TRACKER;
	(void)unused;

	switch (MM_CHARACTER_SELECT_MENU_STATE)
	{
	case ENTERING_MENU:
	case EXITING_MENU:
		MM_TransitionInOut(s_chromeTransition, (int)MM_CHARACTER_SELECT_TRANSITION_FRAME, MM_CHARACTER_SELECT_TRANSITION_STEP);
		MM_TransitionInOut(s_iconTransition, (int)MM_CHARACTER_SELECT_TRANSITION_FRAME, MM_CHARACTER_SELECT_TRANSITION_STEP);
		MM_Characters_SetMenuLayout();
		MM_Characters_DrawWindows(1);

		if (MM_CHARACTER_SELECT_MENU_STATE == ENTERING_MENU)
		{
			if (MM_CHARACTER_SELECT_TRANSITION_FRAME != 0)
				MM_CHARACTER_SELECT_TRANSITION_FRAME--;
			else
				MM_CHARACTER_SELECT_MENU_STATE = IN_MENU;
			break;
		}

		if (++MM_CHARACTER_SELECT_TRANSITION_FRAME > MM_CHARACTER_SELECT_TRANSITION_FRAMES)
		{
			MM_Characters_BackupIDs();
			MM_Characters_HideDrivers();
			if (MM_CHARACTER_SELECT_EXITS_FORWARD == 0)
			{
				MM_JumpTo_Title_Returning();
				return;
			}
			if ((gGT->gameMode2 & CUP_ANY_KIND) != 0)
			{
				MM_DESIRED_MENU = &MM_MENU_CUP_SELECT;
				MM_CupSelect_Init();
				return;
			}
			MM_DESIRED_MENU = &MM_MENU_TRACK_SELECT;
			MM_TrackSelect_Init();
			return;
		}
		break;

	case IN_MENU:
		if (s_pageFrame > 0)
		{
			s_pageFrame--;
			MM_TransitionInOut(s_iconTransition, s_pageFrame, MM_CHARACTER_SELECT_TRANSITION_STEP);
		}
		MM_Characters_SetMenuLayout();
		MM_Characters_DrawWindows(1);
		break;

	default:
		break;
	}

	MM_CharacterGrid_DrawTitle();

	const int numPlayers = gGT->numPlyrNextGame;
	u32 *uiOT = gGT->backBuffer->otMem.uiOT;

	// Input, then each player's cursor: the outline and number while
	// choosing, a dark outline once chosen.
	for (int p = 0; p < numPlayers; p++)
	{
		MM_CharacterGrid_Input(p);

		const struct CsEntry *e = &s_entries[s_entry[p]];
		if (e->page != s_page)
			continue;
		const struct TransitionMeta *t = &s_iconTransition[e->slot];
		const int selected = ((MM_CHARACTER_SELECT_FLAGS >> p) & 1) != 0;

		Color playerColor;
		MM_Characters_AnimateColors((u8 *)&playerColor, p, (s16)(MM_CHARACTER_SELECT_FLAGS & (1 << p)));

		RECT r;
		r.x = t->currX + e->x;
		r.y = t->currY + e->y;
		r.w = MM_CHARACTER_SELECT_ICON_RECT_W;
		r.h = MM_CHARACTER_SELECT_ICON_RECT_H;
		if (!selected)
			DecalFont_DrawLine(MM_PLAYER_NUMBER_STRINGS[p], r.x + MM_CHARACTER_SELECT_CURSOR_LABEL_OFFSET_X, r.y + MM_CHARACTER_SELECT_CURSOR_LABEL_OFFSET_Y,
			                   FONT_BIG, WHITE);
		RECTMENU_DrawOuterRect_HighLevel(&r, selected ? &MM_CHARACTER_SELECT_OUTLINE_COLOR : &playerColor, 0, uiOT);
	}

	MM_CharacterGrid_WritePlayers();

	// Portraits, darkened once a player has chosen them.
	for (int i = 0; i < s_entryCount; i++)
	{
		const struct CsEntry *e = &s_entries[i];
		if ((e->page != s_page) || !e->visible)
			continue;
		const Color *color = &MM_CHARACTER_SELECT_NEUTRAL_COLOR;
		for (int p = 0; p < numPlayers; p++)
		{
			if ((s_entry[p] == i) && ((MM_CHARACTER_SELECT_FLAGS >> p) & 1))
				color = &MM_CHARACTER_SELECT_CHOSEN_COLOR;
		}
		const struct TransitionMeta *t = &s_iconTransition[e->slot];
		RECTMENU_DrawPolyGT4(MM_CharacterGrid_Portrait(e), t->currX + e->x + MM_CHARACTER_SELECT_ICON_DECAL_OFFSET_X,
		                     t->currY + e->y + MM_CHARACTER_SELECT_ICON_DECAL_OFFSET_Y, &gGT->backBuffer->primMem, gGT->pushBuffer_UI.ptrOT, *color, *color,
		                     *color, *color, TRANS_50_DECAL, FP(1.0));
	}

	// The flashing highlight under each choosing player's cursor, and each
	// player's racer name once their driver has settled in the window.
	for (int p = 0; p < numPlayers; p++)
	{
		const struct CsEntry *e = &s_entries[s_entry[p]];
		const int selected = ((MM_CHARACTER_SELECT_FLAGS >> p) & 1) != 0;

		if (!selected && (e->page == s_page))
		{
			Color color;
			MM_Characters_AnimateColors((u8 *)&color, p, 0);
			color.r = MM_Characters_ScaleColor(color.r);
			color.g = MM_Characters_ScaleColor(color.g);
			color.b = MM_Characters_ScaleColor(color.b);

			const struct TransitionMeta *t = &s_iconTransition[e->slot];
			RECT r;
			r.x = t->currX + e->x + MM_CHARACTER_SELECT_HIGHLIGHT_OFFSET_X;
			r.y = t->currY + e->y + MM_CHARACTER_SELECT_HIGHLIGHT_OFFSET_Y;
			r.w = MM_CHARACTER_SELECT_HIGHLIGHT_W;
			r.h = MM_CHARACTER_SELECT_HIGHLIGHT_H;
			MM_DRAW_SOLID_BOX_WITH_PRIM_MEM(&r, &color, uiOT, &gGT->backBuffer->primMem);
		}

		if ((MM_CHARACTER_SELECT_MOVE_TIMERS[p] == 0) && (s_currentEntry[p] == s_entry[p]))
		{
			const struct TransitionMeta *t = &s_chromeTransition[CS_CHROME_WINDOW_FIRST + p];
			const SVec2 *windowPos = &MM_ACTIVE_CHARACTER_SELECT_WINDOW_POS[p];
			const int nameYOffset = (numPlayers >= 3) ? 4 : 0;
			const int x = t->currX + windowPos->x + MM_CHARACTER_SELECT_WINDOW_WIDTH / 2;
			const int baseY = t->currY + windowPos->y;
			const int y = ((numPlayers != 4) || (p < 2)) ? baseY + MM_CHARACTER_SELECT_NAME_TEXT_Y + nameYOffset
			                                             : baseY + nameYOffset + MM_CHARACTER_SELECT_4P_NAME_BOTTOM_OFFSET;
			DecalFont_DrawLine(MM_CharacterGrid_Name(e), x, y, (numPlayers >= 3) ? FONT_SMALL : FONT_CREDITS, (JUSTIFY_CENTER | ORANGE));
		}

		MM_CHARACTER_SELECT_ANGLE[p] += FPS_HALF(MM_CHARACTER_SELECT_SPIN_STEP);
	}

	MM_CharacterGrid_DrawPageArrows(uiOT);

	// Icon backgrounds.
	for (int i = 0; i < s_entryCount; i++)
	{
		const struct CsEntry *e = &s_entries[i];
		if ((e->page != s_page) || !e->visible)
			continue;
		const struct TransitionMeta *t = &s_iconTransition[e->slot];
		RECT r;
		r.x = t->currX + e->x;
		r.y = t->currY + e->y;
		r.w = MM_CHARACTER_SELECT_ICON_RECT_W;
		r.h = MM_CHARACTER_SELECT_ICON_RECT_H;
		RECTMENU_DrawInnerRect(&r, 0, uiOT);
	}

	// The player windows: an animated outline, two inner borders once
	// chosen, the window background and the blue backdrop behind the driver.
	for (int p = 0; p < numPlayers; p++)
	{
		const struct TransitionMeta *t = &s_chromeTransition[CS_CHROME_WINDOW_FIRST + p];
		const SVec2 *windowPos = &MM_ACTIVE_CHARACTER_SELECT_WINDOW_POS[p];
		const int selected = ((MM_CHARACTER_SELECT_FLAGS >> p) & 1) != 0;
		RECT r;
		r.x = t->currX + windowPos->x;
		r.y = t->currY + windowPos->y;
		r.w = MM_CHARACTER_SELECT_WINDOW_WIDTH;
		r.h = MM_CHARACTER_SELECT_WINDOW_HEIGHT;

		Color color;
		MM_Characters_AnimateColors((u8 *)&color, p, !selected);
		RECTMENU_DrawOuterRect_HighLevel(&r, &color, 0, uiOT);

		if (selected)
		{
			RECT inner = r;
			for (int b = 0; b < MM_CHARACTER_SELECT_SELECTED_BORDER_COUNT; b++)
			{
				inner.x += MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_X;
				inner.y += MM_CHARACTER_SELECT_SELECTED_BORDER_INSET_Y;
				inner.w -= MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_W;
				inner.h -= MM_CHARACTER_SELECT_SELECTED_BORDER_SHRINK_H;
				color.r = MM_Characters_ScaleColor(color.r);
				color.g = MM_Characters_ScaleColor(color.g);
				color.b = MM_Characters_ScaleColor(color.b);
				RECTMENU_DrawOuterRect_HighLevel(&inner, &color, 0, uiOT);
			}
		}
		RECTMENU_DrawInnerRect(&r, 9, &uiOT[3]);

		// Viewport space from here.
		r.x = 0;
		r.y = 0;
		RECTMENU_DrawRwdBlueRect(&r, &MM_CHARACTER_SELECT_BLUE_RECT_COLORS[0], &gGT->pushBuffer[p].ptrOT[0x3ff], &gGT->backBuffer->primMem);
	}
}
