/* First-launch setup: a guided screen the first time the title screen settles,
   so nobody has to edit config.ini. Included by MM_ConfigMenu.c in the
   Archipelago player client (CTR_AP, not CTR_AP_AUTHORING), after the
   Connection page, whose text editor and status lines it shares. It runs
   inside the options menu and owns the whole page while open.

   Pages:
     Welcome      what the setup does; Start setup or Skip setup.
     Archipelago  Server, Slot, Password (the Connection page's rows), Connect,
                  Next. Next dials the room when one was typed and not dialed.
     Options      a few options a new player looks for first, then Next.
     Done         where to change things later; Start playing.
   Triangle goes back one page. Finishing or skipping sets [State] setup_done,
   so it opens once. The disc image is found before the window opens
   (platform/native_disc_search.c), since nothing can be drawn without it.

   It opens only while setup_done is off, no room is saved (config.ini or
   ap-config.txt), no room link is waiting and nothing is dialing: a player
   who already plays never sees it. CTR_DEV_SETUP=1 opens it regardless. */

#include <platform/native_dev.h>
#include <platform/native_fs_utf8.h>
#include "../../ap/ap_net.h"
#include "../../ap/ap_link.h"

#define SETUP_TEXT_X   0x38
#define SETUP_TEXT_W   0x1A0
#define SETUP_VALUE_X  0xB0
#define SETUP_ROW_Y    0x3C
#define SETUP_ROW_STEP 0x0E

enum SetupPage
{
	SETUP_OFF = 0,
	SETUP_WELCOME,
	SETUP_ROOM,
	SETUP_OPTIONS,
	SETUP_DONE,
};

/* Archipelago page rows after the three text rows. */
#define SETUP_ROOM_CONNECT 3
#define SETUP_ROOM_NEXT    4
#define SETUP_ROOM_ROWS    5

static int s_setupPage;
static int s_setupSel;
static int s_setupChecked;

/* The options on the Options page, in order, by the field they edit. */
static void *const s_setupOptions[] = {
	&g_config.fullscreen,
	&g_config.aspectRatio,
	&g_config.renderScale,
	&g_config.fps60,
	&g_config.skipIntro,
	&g_config.discordStatus,
};
#define SETUP_OPTION_COUNT ((int)(sizeof s_setupOptions / sizeof s_setupOptions[0]))

static const ConfigEntry *Setup_Entry(const void *valuePtr)
{
	int i;
	for (i = 0; i < g_numConfigEntries; i++)
		if (g_configEntries[i].valuePtr == valuePtr)
			return &g_configEntries[i];
	return NULL;
}

static const ConfigEntry *Setup_RoomEntry(int row)
{
	static void *const fields[] = {g_config.uri, g_config.slot, g_config.password};
	return Setup_Entry(fields[row]);
}

int MM_Setup_ShouldOpen(void)
{
	if (s_setupChecked)
		return 0;
	s_setupChecked = 1;
	if (NativeDev_Setup())
		return 1;
	if (g_config.setupDone || g_config.slot[0] != '\0')
		return 0;
	if (NativeFs_FileExists("ap-config.txt") || AP_LinkWaiting() || ap_net_status() != AP_NET_STATUS_IDLE)
		return 0;
	return 1;
}

void MM_Setup_OpenFromTitle(void)
{
	if (s_numSections == 0)
		BuildSectionMap();
	s_currentSection = -1;
	g_configMenu.rowSelected = 0;
	s_setupPage = SETUP_WELCOME;
	s_setupSel = 0;
}

static void MM_Setup_Go(int page)
{
	s_setupPage = page;
	s_setupSel = 0;
}

/* Finished or skipped: remember it and go back to the title screen. */
static void MM_Setup_Finish(struct RectMenu *menu)
{
	s_setupPage = SETUP_OFF;
	g_config.setupDone = true;
	menu->rowSelected = 0;
	s_currentSection = -1;
	NativeRenderer_CaptureWindowGeometry();
	NativeConfig_Save();
	sdata->ptrDesiredMenu = &D230.menuMainMenu;
}

static void MM_Setup_Help(const char *first, const char *second, uint32_t *ot)
{
	DecalFont_DrawLineOT((char *)first, 0x100, 0xB8, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	if (second)
		DecalFont_DrawLineOT((char *)second, 0x100, 0xC6, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
}

static void MM_Setup_Row(const char *label, int row, uint32_t *ot)
{
	const int y = SETUP_ROW_Y + row * SETUP_ROW_STEP;
	DecalFont_DrawLineOT((char *)label, SETUP_TEXT_X, y, FONT_SMALL, ORANGE, ot);
	if (row == s_setupSel)
		Config_DrawRowHighlight(ot, y);
}

/* Up / Down over `count` rows. */
static void MM_Setup_Navigate(struct GamepadBuffer *pad, int count)
{
	if ((pad->buttonsTapped & BTN_UP) != 0)
	{
		s_setupSel = s_setupSel > 0 ? s_setupSel - 1 : count - 1;
		OtherFX_Play(0, 1);
	}
	if ((pad->buttonsTapped & BTN_DOWN) != 0)
	{
		s_setupSel = s_setupSel < count - 1 ? s_setupSel + 1 : 0;
		OtherFX_Play(0, 1);
	}
}

static void MM_Setup_PageWelcome(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	MM_Setup_Navigate(pad, 2);
	if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
	{
		OtherFX_Play(1, 1);
		if (s_setupSel == 0)
			MM_Setup_Go(SETUP_ROOM);
		else
			MM_Setup_Finish(menu);
		return;
	}
	MM_Solo_Title("Welcome", ot);
	MM_Solo_DrawWrapped("This short setup connects you to your Archipelago room and picks a few options. "
	                    "Change any of it later in OPTIONS.",
	                    SETUP_TEXT_X, 0x38, SETUP_TEXT_W, 4, WHITE, ot);
	{
		const int y0 = 0x76;
		DecalFont_DrawLineOT("Start setup", SETUP_TEXT_X, y0, FONT_SMALL, ORANGE, ot);
		DecalFont_DrawLineOT("Skip setup", SETUP_TEXT_X, y0 + SETUP_ROW_STEP, FONT_SMALL, ORANGE, ot);
		Config_DrawRowHighlight(ot, y0 + s_setupSel * SETUP_ROW_STEP);
	}
	MM_Solo_Footer("*: SELECT", ot);
}

/* Next on the Archipelago page: dial a typed room that is not connected or
   dialing yet, so the room is ready by the time the player reaches the title. */
static void MM_Setup_DialIfTyped(void)
{
	const int status = ap_net_status();
	if (g_config.uri[0] == '\0' || g_config.slot[0] == '\0')
		return;
	if (status == AP_NET_STATUS_CONNECTED || status == AP_NET_STATUS_CONNECTING)
		return;
	AP_Net_Reconnect(g_config.uri, g_config.slot, g_config.password);
}

static void MM_Setup_PageRoom(uint32_t *ot, struct GamepadBuffer *pad)
{
	char buf[160];
	const int justResolved = TextEdit_Update();
	int row;

	if (!s_connEditing && !justResolved)
	{
		MM_Setup_Navigate(pad, SETUP_ROOM_ROWS);
		if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
		{
			OtherFX_Play(1, 1);
			if (s_setupSel < SETUP_ROOM_CONNECT)
			{
				const ConfigEntry *e = Setup_RoomEntry(s_setupSel);
				TextEdit_Begin(e, s_setupSel, SETUP_ROW_Y + s_setupSel * SETUP_ROW_STEP - 2);
				/* Most rooms are on archipelago.gg: start an empty Server there,
				   so only the port is left to type. Cancel restores the empty
				   value (TextEdit_Begin took the backup first). */
				if (s_setupSel == 0 && g_config.uri[0] == '\0')
					snprintf(g_config.uri, sizeof g_config.uri, "archipelago.gg:");
			}
			else if (s_setupSel == SETUP_ROOM_CONNECT)
			{
				if (g_config.uri[0] != '\0' && g_config.slot[0] != '\0')
					AP_Net_Reconnect(g_config.uri, g_config.slot, g_config.password);
			}
			else
			{
				NativeConfig_Save();
				MM_Setup_DialIfTyped();
				MM_Setup_Go(SETUP_OPTIONS);
				return;
			}
		}
	}

	MM_Solo_Title("Archipelago", ot);
	for (row = 0; row < SETUP_ROOM_CONNECT; row++)
	{
		const ConfigEntry *e = Setup_RoomEntry(row);
		const int y = SETUP_ROW_Y + row * SETUP_ROW_STEP;
		const int editing = s_connEditing && s_connEditRow == row;
		MM_Setup_Row(e->label, row, ot);
		Conn_FormatValue(e, strcmp(e->key, "password") == 0, editing, buf, sizeof buf);
		DecalFont_DrawLineOT(buf, SETUP_VALUE_X, y, FONT_SMALL, WHITE, ot);
		if (e->valuePtr == g_config.slot)
			Conn_DrawSlotCaseMarks(g_config.slot, SETUP_VALUE_X, y, ot);
	}
	MM_Setup_Row("Connect", SETUP_ROOM_CONNECT, ot);
	MM_Setup_Row("Next", SETUP_ROOM_NEXT, ot);
	if (s_connEditing)
		s_setupSel = s_connEditRow; /* keep the highlight on the row being typed */

	/* Status under the rows, as on the Connection page. */
	{
		const int y = SETUP_ROW_Y + (SETUP_ROOM_ROWS + 1) * SETUP_ROW_STEP;
		const char *status = AP_Net_StatusLine();
		DecalFont_DrawLineOT("Status", SETUP_TEXT_X, y, FONT_SMALL, ORANGE, ot);
		if ((int)strlen(status) > CONN_STATUS_INLINE_MAX)
			Conn_DrawLongStatus(status, y, SETUP_VALUE_X, SETUP_ROW_STEP, ot);
		else
			DecalFont_DrawLineOT((char *)status, SETUP_VALUE_X, y, FONT_SMALL, WHITE, ot);
	}

	if (s_connEditing)
	{
		TextEdit_DrawHint(0xC6, ot);
		return;
	}
	switch (s_setupSel)
	{
	case 0: MM_Setup_Help("The room's address, for example", "archipelago.gg:38281", ot); break;
	case 1: MM_Setup_Help("Your player name in the room.", CTR_MenuSlotCaseHint(), ot); break;
	case 2: MM_Setup_Help("Only if the room has one.", NULL, ot); break;
	case SETUP_ROOM_CONNECT: MM_Setup_Help("Connect now to check the room.", NULL, ot); break;
	default: MM_Setup_Help("No room yet? Leave it empty.", "PLAY SOLO is on the title screen.", ot); break;
	}
}

static void MM_Setup_OptionHelp(const void *field, uint32_t *ot)
{
	if (field == &g_config.fullscreen)
		MM_Setup_Help("Play fullscreen or in a window.", NULL, ot);
	else if (field == &g_config.aspectRatio)
		MM_Setup_Help("16:9 fills a widescreen monitor.", "4:3 is the original picture.", ot);
	else if (field == &g_config.renderScale)
		MM_Setup_Help("2X and up draw sharper 3D.", "ORIGINAL is the PS1 resolution.", ot);
	else if (field == &g_config.fps60)
		MM_Setup_Help("Smoother motion, from the", "next race or hub.", ot);
	else if (field == &g_config.skipIntro)
		MM_Setup_Help("Start on the title screen", "without the intro videos.", ot);
	else if (field == &g_config.discordStatus)
		MM_Setup_Help("Show the track and checks", "on your Discord profile.", ot);
}

static void MM_Setup_PageOptions(uint32_t *ot, struct GamepadBuffer *pad)
{
	char buf[32];
	const int rows = SETUP_OPTION_COUNT + 1; /* + Next */
	const ConfigEntry *sel;
	int i;

	MM_Setup_Navigate(pad, rows);
	sel = s_setupSel < SETUP_OPTION_COUNT ? Setup_Entry(s_setupOptions[s_setupSel]) : NULL;
	if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
	{
		OtherFX_Play(1, 1);
		if (sel == NULL)
		{
			NativeRenderer_CaptureWindowGeometry();
			NativeConfig_Save();
			MM_Setup_Go(SETUP_DONE);
			return;
		}
		if (sel->type == CFG_BOOL)
			*(bool *)sel->valuePtr ^= 1;
		else
			Enum_Advance(sel);
	}
	if (sel != NULL && (pad->buttonsTapped & (BTN_LEFT | BTN_RIGHT)) != 0)
	{
		const int dir = (pad->buttonsTapped & BTN_LEFT) != 0 ? -1 : +1;
		if (sel->type == CFG_BOOL)
			CTR_MenuBoolStep((bool *)sel->valuePtr, dir);
		else
			Enum_Step(sel, dir);
		OtherFX_Play(0, 1);
	}

	MM_Solo_Title("Options", ot);
	for (i = 0; i < SETUP_OPTION_COUNT; i++)
	{
		const ConfigEntry *e = Setup_Entry(s_setupOptions[i]);
		MM_Setup_Row(e->label, i, ot);
		Config_DrawValue(e, CONFIG_VALUE_X, SETUP_ROW_Y + i * SETUP_ROW_STEP, ot, buf, 0);
	}
	MM_Setup_Row("Next", SETUP_OPTION_COUNT, ot);
	if (sel != NULL)
		MM_Setup_OptionHelp(sel->valuePtr, ot);
	else
		MM_Setup_Help("More options, Controls and filters", "are in OPTIONS.", ot);
}

static void MM_Setup_PageDone(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	const int status = ap_net_status();
	if ((pad->buttonsTapped & (BTN_CROSS | BTN_CIRCLE)) != 0)
	{
		OtherFX_Play(1, 1);
		MM_Setup_Finish(menu);
		return;
	}
	MM_Solo_Title("All Set", ot);
	MM_Solo_DrawWrapped("You are ready to race. Everything you set here, and much more, is in OPTIONS.",
	                    SETUP_TEXT_X, 0x38, SETUP_TEXT_W, 4, WHITE, ot);
	if (status != AP_NET_STATUS_IDLE)
	{
		DecalFont_DrawLineOT("Archipelago", SETUP_TEXT_X, 0x62, FONT_SMALL, ORANGE, ot);
		if ((int)strlen(AP_Net_StatusLine()) > CONN_STATUS_INLINE_MAX)
			Conn_DrawLongStatus(AP_Net_StatusLine(), 0x62, SETUP_VALUE_X + 0x28, SETUP_ROW_STEP, ot);
		else
			DecalFont_DrawLineOT((char *)AP_Net_StatusLine(), SETUP_VALUE_X + 0x28, 0x62, FONT_SMALL, WHITE, ot);
	}
	DecalFont_DrawLineOT("Start playing", SETUP_TEXT_X, 0x96, FONT_SMALL, ORANGE, ot);
	Config_DrawRowHighlight(ot, 0x96);
	MM_Solo_Footer("*: SELECT   ^: BACK", ot);
}

/* One frame of the setup. 1 while it owns the options menu (the caller draws
   the panel and nothing else), 0 when it is closed. */
static int MM_Setup_Frame(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	if (s_setupPage == SETUP_OFF)
		return 0;

	/* Back one page, except while a text row owns the keyboard (TRIANGLE then
	   cancels the edit) and on the frame an edit resolved. */
	if (s_setupPage != SETUP_WELCOME && !s_connEditing && !NativeText_Active() &&
	    (pad->buttonsTapped & BTN_TRIANGLE) != 0)
	{
		OtherFX_Play(2, 1);
		if (s_setupPage == SETUP_OPTIONS)
			NativeConfig_Save();
		MM_Setup_Go(s_setupPage - 1);
		return 1;
	}

	switch (s_setupPage)
	{
	case SETUP_WELCOME: MM_Setup_PageWelcome(menu, ot, pad); break;
	case SETUP_ROOM: MM_Setup_PageRoom(ot, pad); break;
	case SETUP_OPTIONS: MM_Setup_PageOptions(ot, pad); break;
	case SETUP_DONE: MM_Setup_PageDone(menu, ot, pad); break;
	default: break;
	}
	return 1;
}
