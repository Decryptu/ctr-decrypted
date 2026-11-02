/* OPTIONS > Backups: the save backups (platform/native_backup.c), newest
   first, and a restore. Included by MM_ConfigMenu.c; a bespoke page like
   Controls.

   A restore is done at the next start, before anything reads a save, so the
   page records it and starts the game again. Started from Steam, a direct
   start would leave Steam, so the player is asked to start the game from
   Steam instead, as the updater does. A restore waiting for the next start is
   marked on its row; Cross on that row cancels it. */

#include <platform/native_backup.h>
#include <platform/native_relaunch.h>
#ifdef CTR_AP
#include <platform/native_link_host.h>
#include <platform/native_steam_route.h>
#include "../../ap/ap_link.h"
#include "../../ap/ap_solo.h"
#endif

#define BACKUPS_DATE_X   0x38
#define BACKUPS_REASON_X 0xD0
#define BACKUPS_ROW_Y    0x3C
#define BACKUPS_ROW_STEP 0x0E
/* Rows above the detail line; the list scrolls past that. */
#define BACKUPS_VISIBLE  8
#define BACKUPS_DETAIL_Y 0xB0

static NativeBackupInfo s_bkList[NATIVE_BACKUP_KEEP];
static int s_bkCount;
static int s_bkLoaded;
static int s_bkSel;
static int s_bkTop; /* first row shown */
static int s_bkConfirm;
static char s_bkPending[NATIVE_BACKUP_NAME_MAX];
static const char *s_bkMessage; /* shown instead of the list until Back */

static const char *const s_bkMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static void MM_Backups_Load(void)
{
	s_bkCount = NativeBackup_List(s_bkList, NATIVE_BACKUP_KEEP);
	if (!NativeBackup_PendingRestore(s_bkPending, sizeof s_bkPending))
		s_bkPending[0] = '\0';
	if (s_bkSel >= s_bkCount)
		s_bkSel = s_bkCount > 0 ? s_bkCount - 1 : 0;
	s_bkLoaded = 1;
}

/* Back out of the confirmation or a message before leaving the page. */
static int MM_Backups_ConsumesBack(void)
{
	return s_bkConfirm || s_bkMessage != NULL;
}

static int MM_Backups_FromSteam(void)
{
#ifdef CTR_AP
	unsigned long long id;
	return NativeSteamRoute_ParseGameId(getenv("SteamGameId"), &id);
#else
	return 0;
#endif
}

static void MM_Backups_Date(const NativeBackupInfo *b, char *out, int size)
{
	snprintf(out, size, "%s %d %02d:%02d", s_bkMonths[b->when.tm_mon], b->when.tm_mday, b->when.tm_hour, b->when.tm_min);
}

static const char *MM_Backups_Reason(const NativeBackupInfo *b)
{
	switch (b->reason)
	{
	case NATIVE_BACKUP_PLAY: return "WHILE PLAYING";
	case NATIVE_BACKUP_BEFORE_RESTORE: return "BEFORE A RESTORE";
	case NATIVE_BACKUP_LAUNCH:
	default: return "GAME START";
	}
}

/* What the selected backup holds, under the list. */
static void MM_Backups_Files(const NativeBackupInfo *b, char *out, int size)
{
	const char *seeds = b->soloSeeds == 1 ? "SEED" : "SEEDS";
	if (b->soloSeeds == 0)
		snprintf(out, size, "HOLDS THE SAVE");
	else if (b->saves == 0)
		snprintf(out, size, "HOLDS %d PLAY SOLO %s", b->soloSeeds, seeds);
	else
		snprintf(out, size, "SAVE AND %d PLAY SOLO %s", b->soloSeeds, seeds);
}

static void MM_Backups_Wrapped(const char *text, int y, uint32_t *ot)
{
	char line[96];
	const char *cursor = text;
	while (*cursor)
	{
		size_t n = 0, cut = 0;
		while (cursor[n] && n + 1 < sizeof line)
		{
			line[n] = cursor[n];
			line[n + 1] = '\0';
			if (DecalFont_GetLineWidth(line, FONT_SMALL) > 0x1A0)
				break;
			if (cursor[n] == ' ')
				cut = n;
			n++;
		}
		if (cursor[n] && cut > 0)
			n = cut;
		line[n] = '\0';
		DecalFont_DrawLineOT(line, 0x100, y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
		cursor += n;
		while (*cursor == ' ')
			cursor++;
		y += BACKUPS_ROW_STEP;
	}
}

/* Close the game the way a quit does and start it again; the new process
   applies the restore. Returns only when the new process did not start. */
static void MM_Backups_Restart(void)
{
	NativeRenderer_CaptureWindowGeometry();
	NativeConfig_Save();
	NativeControls_Save();
#ifdef CTR_AP
	AP_Solo_Shutdown(); /* a solo server saves before the restore reads its save */
	/* The new process must be able to take this install's primary lock. */
	NativeLinkHost_ReleasePrimary(AP_LinkHost());
#endif
	if (NativeRelaunch_Self())
	{
		printf("[CTR Native] restarting to restore backup %s\n", s_bkPending);
#ifdef CTR_AP
		AP_Net_Shutdown();
#endif
		exit(0);
	}
	s_bkMessage = "The game could not start again by itself. Quit and start it again to load the backup.";
}

static void MM_ConfigProc_Backups(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
	const u32 tap = pad->buttonsTapped;
	char text[64];
	int i;

	(void)menu;
	if (!s_bkLoaded)
		MM_Backups_Load();

	if (s_bkMessage != NULL)
	{
		if ((tap & (BTN_CROSS | BTN_CIRCLE | BTN_TRIANGLE | BTN_START)) != 0)
		{
			OtherFX_Play(1, 1);
			s_bkMessage = NULL;
			s_bkLoaded = 0;
			return;
		}
		DecalFont_DrawLineOT("Backups", 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
		MM_Backups_Wrapped(s_bkMessage, 0x50, ot);
		DecalFont_DrawLineOT("*: OK", 0x100, 0xC4, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
		return;
	}

	if (s_bkConfirm)
	{
		const NativeBackupInfo *b = &s_bkList[s_bkSel];
		const int steam = MM_Backups_FromSteam();
		if ((tap & (BTN_TRIANGLE | BTN_START)) != 0)
		{
			OtherFX_Play(2, 1);
			s_bkConfirm = 0;
		}
		else if ((tap & (BTN_CROSS | BTN_CIRCLE)) != 0)
		{
			OtherFX_Play(1, 1);
			s_bkConfirm = 0;
			if (!NativeBackup_RequestRestore(b->name))
				s_bkMessage = "The restore could not be prepared. Nothing was changed.";
			else
			{
				snprintf(s_bkPending, sizeof s_bkPending, "%s", b->name);
				if (steam)
					s_bkMessage = "Quit the game and start it again from Steam to load the backup.";
				else
					MM_Backups_Restart();
			}
			return;
		}
		DecalFont_DrawLineOT("Restore Backup", 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
		MM_Backups_Date(b, text, sizeof text);
		DecalFont_DrawLineOT(text, 0x100, 0x44, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
		DecalFont_DrawLineOT((char *)MM_Backups_Reason(b), 0x100, 0x52, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
		MM_Backups_Wrapped(steam ? "It is loaded the next time the game starts. What you have now is backed up first."
		                         : "The game restarts and loads it. What you have now is backed up first.",
		                   0x6C, ot);
		DecalFont_DrawLineOT("*: RESTORE   ^: CANCEL", 0x100, 0xC4, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
		return;
	}

	DecalFont_DrawLineOT("Backups", 0x100, 0x18, FONT_BIG, JUSTIFY_CENTER | ORANGE, ot);
	if (s_bkCount == 0)
	{
		MM_Backups_Wrapped("No backups yet. One is kept each time the game starts, and every 15 minutes while you play.", 0x50, ot);
		DecalFont_DrawLineOT("^: BACK", 0x100, 0xC4, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
		return;
	}

	if ((tap & BTN_UP) != 0)
	{
		s_bkSel = s_bkSel > 0 ? s_bkSel - 1 : s_bkCount - 1;
		OtherFX_Play(0, 1);
	}
	if ((tap & BTN_DOWN) != 0)
	{
		s_bkSel = s_bkSel < s_bkCount - 1 ? s_bkSel + 1 : 0;
		OtherFX_Play(0, 1);
	}
	if ((tap & (BTN_CROSS | BTN_CIRCLE)) != 0)
	{
		OtherFX_Play(1, 1);
		if (s_bkPending[0] != '\0' && strcmp(s_bkPending, s_bkList[s_bkSel].name) == 0)
		{
			NativeBackup_CancelRestore();
			s_bkPending[0] = '\0';
		}
		else
			s_bkConfirm = 1;
	}

	if (s_bkSel < s_bkTop)
		s_bkTop = s_bkSel;
	if (s_bkSel >= s_bkTop + BACKUPS_VISIBLE)
		s_bkTop = s_bkSel - BACKUPS_VISIBLE + 1;
	for (i = s_bkTop; i < s_bkCount && i < s_bkTop + BACKUPS_VISIBLE; i++)
	{
		const NativeBackupInfo *b = &s_bkList[i];
		const int y = BACKUPS_ROW_Y + (i - s_bkTop) * BACKUPS_ROW_STEP;
		const int pending = s_bkPending[0] != '\0' && strcmp(s_bkPending, b->name) == 0;
		MM_Backups_Date(b, text, sizeof text);
		DecalFont_DrawLineOT(text, BACKUPS_DATE_X, y, FONT_SMALL, ORANGE, ot);
		DecalFont_DrawLineOT(pending ? "AT NEXT START" : (char *)MM_Backups_Reason(b), BACKUPS_REASON_X, y, FONT_SMALL, pending ? TINY_GREEN : WHITE, ot);
		if (i == s_bkSel)
			Config_DrawRowHighlight(ot, y);
	}
	MM_Backups_Files(&s_bkList[s_bkSel], text, sizeof text);
	DecalFont_DrawLineOT(text, 0x100, BACKUPS_DETAIL_Y, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	DecalFont_DrawLineOT(s_bkPending[0] != '\0' && strcmp(s_bkPending, s_bkList[s_bkSel].name) == 0 ? "*: DO NOT RESTORE   ^: BACK"
	                                                                                                 : "*: RESTORE   ^: BACK",
	                     0x100, 0xC4, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
}
