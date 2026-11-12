/* The layout every tab of the Mods screen shares (MM_CustomManager.c,
   MM_CustomRacers.c, MM_CurrentSeed.c): the list box on the left, the panel
   on the right, and the three footer lines. Kept on its own so
   tools/test-current-seed-page-production.py draws the real thing. Needs
   MM_CustomText.c, NativeMenu_Labels, s_contentSearchEditing, s_saphiQuery
   and s_modsFromTitle. */

/* Every tab's list (left) and detail panel (right): a row is 15 pixels and
   holds MODS_LIST_TEXT_W pixels of text; the panel's text starts at
   MODS_PANEL_X and is MODS_PANEL_W wide. A track's picture takes the panel's
   top left corner (MM_CustomPicture.c), its text MODS_PANEL_SIDE_X onwards. */
#define MODS_LIST_TEXT_W 182
#define MODS_LIST_RIGHT 216
#define MODS_PANEL_X 241
#define MODS_PANEL_W 240
#define MODS_PANEL_SIDE_X 359
#define MODS_PANEL_SIDE_W 122
static RECT MM_Mods_RowBox(int row)
{
    RECT box = {24, 68 + row * 15, 198, 14};
    return box;
}
static void MM_Mods_DrawBoxes(uint32_t *ot)
{
    RECT listBox = {20, 64, 206, 108}, detailBox = {235, 64, 252, 108};
    RECTMENU_DrawInnerRect(&listBox, 0, ot);
    RECTMENU_DrawInnerRect(&detailBox, 0, ot);
}

/* ---- The panel and the footer every tab shares ----
   The panel (x 241, y 64 to 172): the picture at the top left (the caller
   draws it), the name (one or two lines) and the version beside it, the
   author under it, then one info line, the state (two lines) and the Cross
   prompt at the bottom. A mod's options and the Remove question take the
   lines from y 124. Tracks, Racers, Installed and Seed all draw it. */
#define MODS_PANEL_BY_Y 112
#define MODS_PANEL_INFO_Y 124
#define MODS_PANEL_STATE_Y 136
#define MODS_PANEL_PROMPT_Y 160
static void MM_Mods_DrawPanelHead(int textX, const char *name, const char *version, const char *author, uint32_t *ot)
{
    char line[200];
    const int textW = MODS_PANEL_X + MODS_PANEL_W - textX;
    snprintf(line, sizeof line, "%s", name);
    const int nameLines = DecalFont_GetLineWidth(line, FONT_SMALL) <= textW ? 1 : 2;
    MM_CustomText_Draw(name, textX, 70, textW, nameLines, ORANGE, ot);
    if (version && version[0])
    {
        snprintf(line, sizeof line, "v%.32s", version);
        MM_CustomText_Draw(line, textX, 70 + 12 * nameLines, textW, 1, WHITE, ot);
    }
    if (author && author[0])
    {
        snprintf(line, sizeof line, "by %.120s", author);
        MM_CustomText_Draw(line, MODS_PANEL_X, MODS_PANEL_BY_Y, MODS_PANEL_W, 1, WHITE, ot);
    }
}

static void MM_Mods_DrawPanelBody(const char *info, const char *state, const char *prompt, uint32_t *ot)
{
    if (info && info[0]) MM_CustomText_Draw(info, MODS_PANEL_X, MODS_PANEL_INFO_Y, MODS_PANEL_W, 1, WHITE, ot);
    if (state && state[0]) MM_CustomText_Draw(state, MODS_PANEL_X, MODS_PANEL_STATE_Y, MODS_PANEL_W, 2, WHITE, ot);
    if (prompt && prompt[0]) MM_CustomText_Draw(NativeMenu_Labels(prompt), MODS_PANEL_X, MODS_PANEL_PROMPT_Y, MODS_PANEL_W, 1, ORANGE, ot);
}

/* The three lines under the boxes, the same on every tab: a message, or else
   the search, or else the tab's other buttons; Select (import track files),
   left blank where Select does nothing (Racers: no racer comes as a patch);
   then the count, L2 and Back. */
static void MM_Mods_DrawFooter(const char *message, const char *hints, int import, size_t selected, size_t total, const char *back, uint32_t *ot)
{
    char line[300];
    if (message && message[0]) snprintf(line, sizeof line, "%s", message);
    else if (s_contentSearchEditing || s_saphiQuery[0]) snprintf(line, sizeof line, "R2 Search: %s%s", s_saphiQuery, s_contentSearchEditing ? "_" : "");
    else snprintf(line, sizeof line, "%s", hints ? NativeMenu_Labels(hints) : "");
    MM_CustomText_Draw(line, 256, 180, 456, 1, JUSTIFY_CENTER | WHITE, ot);
    if (import) MM_CustomText_Draw("Select: Import track files", 256, 188, 456, 1, JUSTIFY_CENTER | ORANGE, ot);
    snprintf(line, sizeof line, "%u/%u   L2 Refresh   %s", total ? (unsigned)selected + 1 : 0, (unsigned)total,
             NativeMenu_Labels(back ? back : s_modsFromTitle ? "^: Title" : "^: Options"));
    MM_CustomText_Draw(line, 256, 196, 456, 1, JUSTIFY_CENTER | WHITE, ot);
}

/* The modes a track races in, one line. */
static const char *MM_Mods_ModesText(int arcade, int timeTrial)
{
    return arcade && timeTrial ? "Arcade, Time Trial" : arcade ? "Arcade" : timeTrial ? "Time Trial" : "No races";
}
