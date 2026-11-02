/* A tab bar: "L1", the tabs, "R1", one row. Each tab sits in a box of its
   own, so the names read as separate tabs and not as one line of text; the
   active tab's box is filled with the row highlight and its name is orange.
   The layout (include/ctr_tab_bar.h) shrinks the padding, then uses the short
   names, then shows only the active tab when the bar is too narrow.

   names and shortNames hold count entries; a NULL short name is the full
   one. y is the text line; the boxes reach 3 pixels above and below it. */
#include <ctr_tab_bar.h>

#define MM_TAB_BAR_BOX_H 14

static void MM_TabBar_Draw(const char *const *names, const char *const *shortNames, int count, int active,
                           int left, int right, int y, uint32_t *ot)
{
    int fullW[CTR_TAB_BAR_MAX], shortW[CTR_TAB_BAR_MAX];
    const int hintW = DecalFont_GetLineWidth("R1", FONT_SMALL);
    if (count > CTR_TAB_BAR_MAX) count = CTR_TAB_BAR_MAX;
    for (int i = 0; i < count; i++)
    {
        const char *shortName = shortNames && shortNames[i] ? shortNames[i] : names[i];
        fullW[i] = DecalFont_GetLineWidth((char *)names[i], FONT_SMALL);
        shortW[i] = DecalFont_GetLineWidth((char *)shortName, FONT_SMALL);
    }
    const CtrTabBarLayout layout = CTR_TabBar_Layout(left, right, hintW, fullW, shortW, count, active);

    /* The ordering table draws back to front: text first, boxes after it. */
    DecalFont_DrawLineOT("L1", layout.hintLeftX, y, FONT_SMALL, WHITE, ot);
    DecalFont_DrawLineOT("R1", layout.hintRightX - hintW, y, FONT_SMALL, WHITE, ot);
    for (int i = 0; i < count; i++)
    {
        if (layout.x[i] < 0) continue;
        const char *text = layout.mode == CTR_TAB_BAR_FULL || !shortNames || !shortNames[i] ? names[i] : shortNames[i];
        MM_CustomText_Draw(text, layout.x[i] + layout.w[i] / 2, y, layout.w[i] - 2 * layout.pad + 1, 1,
                           JUSTIFY_CENTER | (i == active ? ORANGE : WHITE), ot);
    }
    if (layout.mode == CTR_TAB_BAR_ACTIVE)
    {
        char place[8];
        snprintf(place, sizeof place, "%d/%d", active + 1, count);
        DecalFont_DrawLineOT(place, layout.x[active] + layout.w[active] + CTR_TAB_BAR_GAP, y, FONT_SMALL, GRAY, ot);
    }
    for (int i = 0; i < count; i++)
    {
        if (layout.x[i] < 0) continue;
        RECT box = {layout.x[i], y - 3, layout.w[i], MM_TAB_BAR_BOX_H};
        if (i == active)
            CTR_Box_DrawClearBox(&box, &sdata->menuRowHighlight_Normal, TRANS_50_DECAL, ot, &GAME_TRACKER->backBuffer->primMem);
        RECTMENU_DrawInnerRect(&box, 0, ot);
    }
}
