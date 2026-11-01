// ap_seed_prompt.c -- part of the ap_hooks unit (game/game_unity.h).
// The prompt on connecting to a content-plan room whose tracks are not all
// ready: on the settled main menu, once per room, it says how many tracks
// are missing and offers to download the ones Project Saphi has, through the
// same "Install missing content" the Mods screen's Seed tab runs
// (ap/ap_hooks_seed_content.c). The wording is ap_seed_prompt_text in
// ap/ap_seed_content_logic.h (tools/test-seed-content.c).
//
// It appears where the room-link and update prompts do (the same settled
// main-menu gate), after them, and owns input while it is up. Triangle puts
// it off until the next room; the Seed tab still lists the tracks.

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)

enum
{
	AP_SEED_PROMPT_IDLE,
	AP_SEED_PROMPT_ASKING,
	AP_SEED_PROMPT_INSTALLING,
	AP_SEED_PROMPT_DONE,
};

static struct RectMenu *ap_seed_prompt_menu;
static u32 ap_seed_prompt_menu_frame;
static int ap_seed_prompt_state;
static uint64_t ap_seed_prompt_answered_epoch; // the room the player answered for
static char ap_seed_prompt_message[256];

void AP_SeedPromptMainMenuSettled(struct RectMenu *menu)
{
	ap_seed_prompt_menu = menu;
	ap_seed_prompt_menu_frame = sdata->frameCounter;
}

// The settled top-level main menu, nothing loading (the room-link prompt's gate).
static int AP_SeedPromptSafe(struct GameTracker *gGT)
{
	if (gGT == NULL || (gGT->gameMode1 & MAIN_MENU) == 0 || (gGT->gameMode1 & LOADING) != 0)
		return 0;
	if (gGT->levelID != MAIN_MENU_LEVEL || sdata->Loading.stage != LOAD_IDLE)
		return 0;
	if (ap_seed_prompt_menu == NULL || sdata->ptrActiveMenu != ap_seed_prompt_menu || sdata->ptrDesiredMenu != NULL)
		return 0;
	if ((ap_seed_prompt_menu->state & DRAW_NEXT_MENU_IN_HIERARCHY) != 0)
		return 0;
	return ap_seed_prompt_state != AP_SEED_PROMPT_IDLE || (sdata->frameCounter - ap_seed_prompt_menu_frame) <= 1;
}

// Box width less a margin: the text lines must fit inside it.
#define AP_SEED_PROMPT_TEXT_W 0x190

static void AP_SeedPromptDraw(const char *title, const char *l1, const char *l2, const char *keys)
{
	u32 *ot = sdata->gGT->backBuffer->otMem.uiOT;
	RECT bg = {0x30, 0x46, 0x1A0, 0x64};
	char lines[2][128];

	// A single message (the install's) wraps onto the second line at the
	// last space that fits.
	ap_seed_plain_text(l1 ? l1 : "", lines[0], sizeof lines[0]);
	ap_seed_plain_text(l2 ? l2 : "", lines[1], sizeof lines[1]);
	if (!l2 && DecalFont_GetLineWidth(lines[0], FONT_SMALL) > AP_SEED_PROMPT_TEXT_W)
	{
		for (int cut = (int)strlen(lines[0]) - 1; cut > 0; cut--)
		{
			if (lines[0][cut] != ' ')
				continue;
			lines[0][cut] = '\0';
			if (DecalFont_GetLineWidth(lines[0], FONT_SMALL) <= AP_SEED_PROMPT_TEXT_W)
			{
				snprintf(lines[1], sizeof lines[1], "%s", &lines[0][cut + 1]);
				break;
			}
			lines[0][cut] = ' ';
		}
	}

	DecalFont_DrawLineOT((char *)title, 0x100, 0x50, FONT_SMALL, JUSTIFY_CENTER | ORANGE, ot);
	DecalFont_DrawLineOT(lines[0], 0x100, 0x64, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	DecalFont_DrawLineOT(lines[1], 0x100, 0x74, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	DecalFont_DrawLineOT((char *)keys, 0x100, 0x96, FONT_SMALL, JUSTIFY_CENTER | WHITE, ot);
	RECTMENU_DrawInnerRect(&bg, 4, ot);
}

int AP_SeedPromptMenuFrame(void)
{
	struct GameTracker *gGT = sdata->gGT;
	struct AP_SeedContentRow rows[AP_SEED_ROWS_MAX];
	struct AP_SeedPromptText text;
	int plan[AP_SEED_ROWS_MAX], planCount = 0, planResult, n, missing, tap;
	uint64_t bytes = 0;
	char reason[160];

	if (!AP_SeedContentActive() || ap_content_plan_epoch() == ap_seed_prompt_answered_epoch)
	{
		ap_seed_prompt_state = AP_SEED_PROMPT_IDLE;
		return 0;
	}
	if (!AP_SeedPromptSafe(gGT))
		return 0;

	tap = sdata->buttonTapPerPlayer[0];
	gGT->demoCountdownTimer = 900;

	switch (ap_seed_prompt_state)
	{
	case AP_SEED_PROMPT_INSTALLING:
		if (AP_SeedContentInstallTick(ap_seed_prompt_message, sizeof ap_seed_prompt_message))
		{
			AP_SeedPromptDraw("DOWNLOADING TRACKS", ap_seed_prompt_message, NULL, "PLEASE WAIT");
			return 1;
		}
		ap_seed_prompt_state = AP_SEED_PROMPT_DONE;
		// fallthrough: the finished message
	case AP_SEED_PROMPT_DONE:
		AP_SeedPromptDraw("TRACK DOWNLOAD", ap_seed_prompt_message, NULL, "*: OK");
		if (tap & BTN_CROSS)
		{
			ap_seed_prompt_answered_epoch = ap_content_plan_epoch();
			ap_seed_prompt_state = AP_SEED_PROMPT_IDLE;
		}
		return 1;

	default:
		break;
	}

	n = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	missing = n - ap_seed_ready_count(rows, n);
	if (missing <= 0)
	{
		// Everything is ready: nothing to say for this room.
		ap_seed_prompt_answered_epoch = ap_content_plan_epoch();
		ap_seed_prompt_state = AP_SEED_PROMPT_IDLE;
		return 0;
	}

	planResult = AP_SeedContentInstallPreview(plan, &planCount, &bytes, reason, sizeof reason);
	ap_seed_prompt_text(missing, planResult, planCount, bytes, &text);
	ap_seed_prompt_state = AP_SEED_PROMPT_ASKING;
	AP_SeedPromptDraw(text.title, text.line1, text.line2, text.keys);

	if ((tap & BTN_CROSS) && text.can_install)
	{
		if (AP_SeedContentInstallStart(ap_seed_prompt_message, sizeof ap_seed_prompt_message))
			ap_seed_prompt_state = AP_SEED_PROMPT_INSTALLING;
		else
			ap_seed_prompt_state = AP_SEED_PROMPT_DONE;
	}
	else if (tap & (BTN_CROSS | BTN_TRIANGLE))
	{
		ap_seed_prompt_answered_epoch = ap_content_plan_epoch();
		ap_seed_prompt_state = AP_SEED_PROMPT_IDLE;
	}
	return 1;
}

#else

void AP_SeedPromptMainMenuSettled(struct RectMenu *menu)
{
	(void)menu;
}

int AP_SeedPromptMenuFrame(void)
{
	return 0;
}

#endif
