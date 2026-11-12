/* The Seed tab of the Mods screen, in a content-plan room. Included by
   MM_CustomManager.c in the AP player client, which draws the title and the
   tabs and calls MM_CurrentSeed_Page for the tab's body.

   Laid out like the other tabs: the room's custom tracks in the list, tagged
   READY, MISSING or CHECK; the panel shows the selected one (the panel every
   tab shares, MM_Mods_DrawPanelHead): its author, size and where it comes
   from, and what to do about it. The footer's message line names the slot
   and how many tracks are ready. Cross asks to install what is missing
   (count, size), in the panel; Cross there installs, Triangle cancels. L2
   rescans, as it refreshes on every tab. A track installed from Tracks counts
   as Ready (CustomTrackManager_LocateContentFiles also looks in the package
   store). Rooms without a content plan, or whose plan names no custom
   content, have no Seed tab. Decisions and text live in
   ap/ap_seed_content_logic.h; the install in ap/ap_hooks_seed_content.c. */

#include "../../ap/ap_net.h" /* ap_net_slot_name: the footer's slot line */

static int s_seedSelected;
static int s_seedOverview;     /* the Install missing question is open */
static unsigned int s_seedLastFrame;
static char s_seedMessage[256];

/* Back closes the install question first instead of leaving the Mods screen. */
static int MM_CurrentSeed_ConsumesBack(int section)
{
	return section == s_customContentSection && s_seedOverview && s_modsSeed && AP_SeedContentActive();
}

static const char *MM_Seed_Tag(int state)
{
	return state == AP_SEED_STATE_READY ? "READY" : state == AP_SEED_STATE_MISSING ? "MISSING" : "CHECK";
}

/* The Seed tab's body, below the Mods screen's title and tabs. */
static void MM_CurrentSeed_Page(uint32_t *ot, struct GamepadBuffer *pad)
{
	struct AP_SeedContentRow rows[AP_SEED_ROWS_MAX];
	const unsigned int now = (unsigned int)sdata->gGT->timer;
	int tapped = pad->buttonsTapped, count, ready, i, first;
	char line[256], size[24], slot[64] = "";

	if (!AP_SeedContentActive())
	{
		s_seedOverview = 0;
		return;
	}
	/* A fresh visit starts clean; a running install keeps its message. */
	if (now - s_seedLastFrame > 2 && !AP_SeedContentInstallBusy())
	{
		s_seedMessage[0] = '\0';
		s_seedOverview = 0;
	}
	s_seedLastFrame = now;
	{
		/* A finished install added packages: rescan the library for Installed. */
		const int wasInstalling = AP_SeedContentInstallBusy();
		AP_SeedContentInstallTick(s_seedMessage, sizeof s_seedMessage);
		if (wasInstalling && !AP_SeedContentInstallBusy()) s_contentStoreWanted = 1;
	}

	count = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
	ready = ap_seed_ready_count(rows, count);
	if (s_seedSelected >= count) s_seedSelected = count ? count - 1 : 0;

	if (s_seedOverview)
	{
		if (tapped & (BTN_TRIANGLE | BTN_START))
		{
			s_seedOverview = 0;
			OtherFX_Play(2, 1);
		}
		else if (tapped & (BTN_CROSS | BTN_CIRCLE))
		{
			s_seedOverview = 0;
			OtherFX_Play(1, 1);
			AP_SeedContentInstallStart(s_seedMessage, sizeof s_seedMessage);
		}
		tapped = 0;
	}
	if (count && (tapped & (BTN_UP | BTN_DOWN)))
	{
		s_seedSelected = (s_seedSelected + ((tapped & BTN_UP) ? count - 1 : 1)) % count;
		OtherFX_Play(0, 1);
	}
	if (tapped & BTN_L2)
	{
		if (AP_SeedContentInstallBusy())
			snprintf(s_seedMessage, sizeof s_seedMessage, "Wait for the install to finish, then refresh.");
		else
		{
			AP_CustomContentRescan();
			count = AP_SeedContentRows(rows, AP_SEED_ROWS_MAX);
			ready = ap_seed_ready_count(rows, count);
			snprintf(s_seedMessage, sizeof s_seedMessage, "Rescanned. Ready: %d / %d.", ready, count);
		}
	}
	if (tapped & (BTN_CROSS | BTN_CIRCLE))
	{
		int plan[AP_SEED_ROWS_MAX], queued = 0;
		uint64_t bytes = 0;
		OtherFX_Play(1, 1);
		if (AP_SeedContentInstallPreview(plan, &queued, &bytes, s_seedMessage, sizeof s_seedMessage) == AP_SEED_PLAN_OK)
		{
			s_seedOverview = 1;
			s_seedMessage[0] = '\0';
		}
	}

	first = (s_seedSelected / 7) * 7;
	for (i = first; i < first + 7 && i < count; i++)
	{
		RECT box = MM_Mods_RowBox(i - first);
		const int state = ap_seed_row_state(&rows[i]);
		MM_CustomText_Draw(rows[i].title, 32, box.y + 2, MODS_LIST_TEXT_W - 64, 1, i == s_seedSelected ? ORANGE : WHITE, ot);
		MM_CustomText_Draw(MM_Seed_Tag(state), MODS_LIST_RIGHT, box.y + 2, 80, 1,
		                   JUSTIFY_RIGHT | (state == AP_SEED_STATE_READY ? GRAY : RED), ot);
		if (i == s_seedSelected)
			CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &sdata->gGT->backBuffer->primMem);
	}

	if (count)
	{
		const struct AP_SeedContentRow *r = &rows[s_seedSelected];
		char instruction[256];
		const int bundled = r->source == AP_SEED_SOURCE_BUNDLED;
		MM_Mods_DrawPanelHead(MODS_PANEL_X, r->title, r->revision, bundled ? "" : r->author, ot);
		if (s_seedOverview)
		{
			int plan[AP_SEED_ROWS_MAX], queued = 0;
			uint64_t bytes = 0;
			char reason[160];
			if (AP_SeedContentInstallPreview(plan, &queued, &bytes, reason, sizeof reason) != AP_SEED_PLAN_OK)
			{
				/* Something changed while the question was open: show why and close. */
				snprintf(s_seedMessage, sizeof s_seedMessage, "%s", reason);
				s_seedOverview = 0;
			}
			else
			{
				ap_seed_format_bytes(bytes, size, sizeof size);
				snprintf(line, sizeof line, "Download %d track%s (%s) from Project Saphi?", queued, queued == 1 ? "" : "s", size);
				MM_CustomText_Draw(line, MODS_PANEL_X, MODS_PANEL_INFO_Y, MODS_PANEL_W, 3, WHITE, ot);
				MM_CustomText_Draw(NativeMenu_Labels("* Install  ^ Cancel"), MODS_PANEL_X, MODS_PANEL_PROMPT_Y, MODS_PANEL_W, 1, ORANGE, ot);
			}
		}
		if (!s_seedOverview)
		{
			if (bundled) snprintf(line, sizeof line, "Included");
			else
			{
				ap_seed_format_bytes((uint64_t)r->lev_bytes + r->vrm_bytes, size, sizeof size);
				snprintf(line, sizeof line, "%s, %s", size, r->source == AP_SEED_SOURCE_SAPHI ? "Project Saphi" : "by hand");
			}
			if (bundled && ap_seed_row_state(r) == AP_SEED_STATE_READY) snprintf(instruction, sizeof instruction, "Files verified");
			else ap_seed_instruction(r, instruction, sizeof instruction);
			MM_Mods_DrawPanelBody(line, instruction, ready < count ? (AP_SeedContentInstallBusy() ? "Installing..." : "* Install missing") : "", ot);
		}
	}

	/* The footer's message line: a message, or the slot and what is ready. */
	ap_net_slot_name(slot, sizeof slot);
	snprintf(line, sizeof line, "%s%s%d / %d tracks ready", slot, slot[0] ? ": " : "", ready, count);
	MM_Mods_DrawFooter(s_seedMessage[0] ? s_seedMessage : line, NULL, 1, (size_t)s_seedSelected, (size_t)count, NULL, ot);
	MM_Mods_DrawBoxes(ot);
}
