/* The Seed tab of the Mods screen (#133 PR 7), in a content-plan room.
   Included by MM_CustomManager.c in the AP player client, which draws the
   title and the tabs and calls MM_CurrentSeed_Page for the tab's body.

   It lists the room's required tracks with Ready, Missing or Check files;
   Up / Down selects one and shows its author, size, instructions and patch
   locator. Cross opens the "Install missing content" overview (authors,
   versions, sizes, total); Cross there installs, Triangle closes it. Square
   rescans. A track installed from Browse counts as Ready
   (CustomTrackManager_LocateContentFiles also looks in the package store).
   Rooms without a content plan, or whose plan names no custom content, have
   no Seed tab. Decisions and text live in ap/ap_seed_content_logic.h; the
   install in ap/ap_hooks_seed_content.c. */

/* Rows on the main page and in the overview. Everything ends by y 196, the
   last line the Mods screen's frame shows, like the Browse and Installed
   tabs; every line is cut or wrapped to fit 456 pixels. */
#define MM_SEED_VISIBLE_ROWS 4
#define MM_SEED_OVERVIEW_ROWS 6
#define MM_SEED_LIST_Y 0x48
#define MM_SEED_ROW_H 0x0C
#define MM_SEED_WIDTH 0x1C8

static int s_seedSelected;
static int s_seedOverview;     /* the Install missing content overview is open */
static unsigned int s_seedLastFrame;
static char s_seedMessage[256];

/* Cut plain text until the small font fits `width` pixels, ending in "...". */
static void MM_Seed_Fit(const char *text, char *out, size_t size, int width)
{
	size_t n;
	ap_seed_plain_text(text, out, size);
	n = strlen(out);
	if (DecalFont_GetLineWidth(out, FONT_SMALL) <= width) return;
	while (n > 0)
	{
		n--;
		if (n + 4 > size) continue;
		memcpy(out + n, "...", 4);
		if (DecalFont_GetLineWidth(out, FONT_SMALL) <= width) return;
	}
	out[0] = '\0';
}

/* One centred line, cut to the screen's width. */
static void MM_Seed_DrawCentered(const char *text, int y, int color, uint32_t *ot)
{
	char fit[200];
	MM_Seed_Fit(text, fit, sizeof fit, MM_SEED_WIDTH);
	DecalFont_DrawLineOT(fit, 0x100, y, FONT_SMALL, JUSTIFY_CENTER | color, ot);
}

/* Draw plain text left-justified in up to `lines` lines of `width` pixels,
   breaking at spaces; the last line is cut with "..." when text remains. */
static void MM_Seed_DrawWrapped(const char *text, int x, int y, int width, int lines, int color, uint32_t *ot)
{
	char plain[320], line[160];
	const char *cursor = plain;
	int i;
	ap_seed_plain_text(text, plain, sizeof plain);
	for (i = 0; i < lines && *cursor; i++)
	{
		size_t n = 0, cut = 0;
		while (cursor[n] && n + 1 < sizeof line)
		{
			line[n] = cursor[n];
			line[n + 1] = '\0';
			if (DecalFont_GetLineWidth(line, FONT_SMALL) > width) break;
			if (cursor[n] == ' ') cut = n;
			n++;
		}
		if (cursor[n] && i + 1 == lines)
		{
			MM_Seed_Fit(cursor, line, sizeof line, width);
			DecalFont_DrawLineOT(line, x, y + i * 0x0C, FONT_SMALL, color, ot);
			return;
		}
		if (cursor[n] && cut > 0) n = cut;
		line[n] = '\0';
		DecalFont_DrawLineOT(line, x, y + i * 0x0C, FONT_SMALL, color, ot);
		cursor += n;
		while (*cursor == ' ') cursor++;
	}
}

/* Back closes the overview first instead of leaving the Mods screen. */
static int MM_CurrentSeed_ConsumesBack(int section)
{
	return section == s_customContentSection && s_seedOverview && s_modsSeed && AP_SeedContentActive();
}

static void MM_CurrentSeed_DrawOverview(const struct AP_SeedContentRow *rows, int count, uint32_t *ot)
{
	int plan[AP_SEED_ROWS_MAX], queued = 0, i, manual = 0, y = MM_SEED_LIST_Y;
	uint64_t bytes = 0;
	char line[200], fit[200], size[24], reason[160];
	if (AP_SeedContentInstallPreview(plan, &queued, &bytes, reason, sizeof reason) != AP_SEED_PLAN_OK)
	{
		/* Something changed while the overview was open: show why and close. */
		snprintf(s_seedMessage, sizeof s_seedMessage, "%s", reason);
		s_seedOverview = 0;
		return;
	}
	MM_Seed_DrawCentered("Install missing content", 0x3C, ORANGE, ot);
	for (i = 0; i < queued && i < MM_SEED_OVERVIEW_ROWS; i++)
	{
		const struct AP_SeedContentRow *r = &rows[plan[i]];
		ap_seed_format_bytes((uint64_t)r->lev_bytes + r->vrm_bytes, size, sizeof size);
		snprintf(line, sizeof line, "%.48s v%.24s by %.48s", r->title, r->revision, r->author);
		MM_Seed_Fit(line, fit, sizeof fit, 0x150);
		DecalFont_DrawLineOT(fit, 0x20, y, FONT_SMALL, WHITE, ot);
		DecalFont_DrawLineOT(size, 0x1E0, y, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
		y += MM_SEED_ROW_H;
	}
	if (queued > MM_SEED_OVERVIEW_ROWS)
	{
		snprintf(line, sizeof line, "and %d more", queued - MM_SEED_OVERVIEW_ROWS);
		DecalFont_DrawLineOT(line, 0x20, y, FONT_SMALL, WHITE, ot);
	}
	for (i = 0; i < count; i++)
		if (ap_seed_row_state(&rows[i]) != AP_SEED_STATE_READY && !ap_seed_row_downloadable(&rows[i])) manual++;
	ap_seed_format_bytes(bytes, size, sizeof size);
	snprintf(line, sizeof line, "Total: %d track%s, %s from Project Saphi.", queued, queued == 1 ? "" : "s", size);
	MM_Seed_DrawWrapped(line, 0x20, 0x9C, MM_SEED_WIDTH, 2, ORANGE, ot);
	if (manual)
	{
		snprintf(line, sizeof line, "%d other track%s need%s files added by hand. See each track.",
		         manual, manual == 1 ? "" : "s", manual == 1 ? "s" : "");
		MM_Seed_DrawWrapped(line, 0x20, 0xB4, MM_SEED_WIDTH, 1, WHITE, ot);
	}
	MM_Seed_DrawCentered(NativeMenu_Labels("* Install   ^ Cancel"), 0xC4, WHITE, ot);
}

/* The Seed tab's body, below the Mods screen's title and tabs. */
static void MM_CurrentSeed_Page(uint32_t *ot, struct GamepadBuffer *pad)
{
	struct AP_SeedContentRow rows[AP_SEED_ROWS_MAX];
	const unsigned int now = (unsigned int)sdata->gGT->timer;
	int tapped = pad->buttonsTapped, count, ready, i, first;
	char line[200], fit[200], size[24];

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
		if (tapped & BTN_TRIANGLE)
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
	if (tapped & BTN_SQUARE)
	{
		OtherFX_Play(1, 1);
		if (AP_SeedContentInstallBusy())
			snprintf(s_seedMessage, sizeof s_seedMessage, "Wait for the install to finish, then Rescan.");
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

	if (s_seedOverview)
	{
		MM_CurrentSeed_DrawOverview(rows, count, ot);
		if (s_seedOverview) return;
	}

	snprintf(line, sizeof line, "Seed content ready: %d / %d", ready, count);
	MM_Seed_DrawCentered(line, 0x3C, ready == count ? ORANGE : RED, ot);

	first = (s_seedSelected / MM_SEED_VISIBLE_ROWS) * MM_SEED_VISIBLE_ROWS;
	for (i = first; i < first + MM_SEED_VISIBLE_ROWS && i < count; i++)
	{
		const struct AP_SeedContentRow *r = &rows[i];
		const int state = ap_seed_row_state(r);
		const int y = MM_SEED_LIST_Y + (i - first) * MM_SEED_ROW_H;
		if (r->revision[0]) snprintf(line, sizeof line, "%.64s v%.32s", r->title, r->revision);
		else snprintf(line, sizeof line, "%.64s", r->title);
		MM_Seed_Fit(line, fit, sizeof fit, 0x150);
		DecalFont_DrawLineOT(fit, 0x20, y, FONT_SMALL, i == s_seedSelected ? ORANGE : WHITE, ot);
		DecalFont_DrawLineOT((char *)ap_seed_state_text(state), 0x1E0, y, FONT_SMALL,
		                     JUSTIFY_RIGHT | (state == AP_SEED_STATE_READY ? WHITE : RED), ot);
		if (i == s_seedSelected)
		{
			RECT sel = {0x18, y - 2, 0x1D0, 0x0C};
			CTR_Box_DrawClearBox(&sel, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &sdata->gGT->backBuffer->primMem);
		}
	}
	if (count > MM_SEED_VISIBLE_ROWS)
	{
		snprintf(line, sizeof line, "%d / %d", s_seedSelected + 1, count);
		DecalFont_DrawLineOT(line, 0x1E0, 0x3C, FONT_SMALL, JUSTIFY_RIGHT | WHITE, ot);
	}

	if (count)
	{
		const struct AP_SeedContentRow *r = &rows[s_seedSelected];
		char instruction[256], locator[160];
		if (r->source == AP_SEED_SOURCE_BUNDLED)
			snprintf(line, sizeof line, "Comes with the client");
		else
		{
			ap_seed_format_bytes((uint64_t)r->lev_bytes + r->vrm_bytes, size, sizeof size);
			snprintf(line, sizeof line, "By %.48s  |  %s  |  %s", r->author, size,
			         r->source == AP_SEED_SOURCE_SAPHI ? "Project Saphi" : "No download");
		}
		MM_Seed_Fit(line, fit, sizeof fit, MM_SEED_WIDTH);
		DecalFont_DrawLineOT(fit, 0x20, 0x7C, FONT_SMALL, ORANGE, ot);
		ap_seed_instruction(r, instruction, sizeof instruction);
		MM_Seed_DrawWrapped(instruction, 0x20, 0x88, MM_SEED_WIDTH, 2, WHITE, ot);
		if (r->source != AP_SEED_SOURCE_BUNDLED)
		{
			ap_seed_locator_line(r, locator, sizeof locator);
			MM_Seed_Fit(locator, fit, sizeof fit, MM_SEED_WIDTH);
			DecalFont_DrawLineOT(fit, 0x20, 0xA0, FONT_SMALL, WHITE, ot);
		}
	}
	/* The bottom three lines match Browse: a message, Select, the buttons. */
	if (s_seedMessage[0]) MM_Seed_DrawCentered(s_seedMessage, 0xB4, WHITE, ot);
	MM_Seed_DrawCentered("Select: Import track files", 0xBC, ORANGE, ot);
	snprintf(line, sizeof line, "%s   %s",
	         AP_SeedContentInstallBusy() ? "Installing..." : NativeMenu_Labels("* Install missing"),
	         NativeMenu_Labels(s_modsFromTitle ? "[ Rescan   ^: Title" : "[ Rescan   ^: Options"));
	MM_Seed_DrawCentered(line, 0xC4, WHITE, ot);
}
