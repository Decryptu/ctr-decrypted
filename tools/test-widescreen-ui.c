// cc -m32 -std=gnu11 -DCTR_NATIVE -DCTR_AP -I include -I . -I ap -ffunction-sections -fdata-sections -Wl,--gc-sections tools/test-widescreen-ui.c -o /tmp/test-widescreen-ui -lm
#include <common.h>
#include <platform/native_config.h>
#include <assert.h>
NativeConfig g_config;
struct Data data;
struct sData sdata_static;
int gNative60FpsEnabled;
uint32_t NativeGpuLinks_FromHostPointer(const void *p) { return (uintptr_t)p & 0xffffff; }
void AddPrim(void *ot, void *p) { (void)ot; (void)p; }
int TimeTrialRecords_OffCard(void) { return 0; }
void AH_Map_HubArrow(s32 x, s32 y, const SVec2 *p, char *c, s16 s, s32 a)
{ (void)x; (void)y; (void)p; (void)c; (void)s; (void)a; }
void DecalHUD_DrawPolyGT4(struct Icon *i, s32 x, s32 y, struct PrimMem *m, u32 *ot,
                         Color c0, Color c1, Color c2, Color c3, u8 t, s16 s)
{ (void)i; (void)x; (void)y; (void)m; (void)ot; (void)c0; (void)c1; (void)c2; (void)c3; (void)t; (void)s; }
#include "game/widescreen.c"
#include "ap/ap_statbar.c"
#include "game/UI/UI_Map.c"
static RECT outlines[2];
static int boxes;
void CTR_Box_DrawWireBox(RECT *r, const Color *color, void *ot, struct PrimMem *mem)
{
	(void)color; (void)ot; (void)mem;
	assert(boxes < 2); outlines[boxes++] = *r;
}
int main(void)
{
	static POLY_G4 polygons[16];
	struct PrimMem mem = {.cursor=polygons, .end=polygons+16, .guardEnd=polygons+14};
	u32 ot = 0xffffff;
	for (int aspect=0; aspect<4; aspect++) {
		g_config.aspectRatio=aspect;
		for (int len=0; len<=78; len++) {
			mem.cursor=polygons; boxes=0;
			AP_StatBar_Draw(100, 20, len, AP_STATBAR_COLORS, &ot, &mem);
			if (!len) { assert(boxes==0 && mem.cursor==polygons); continue; }
			assert(outlines[0].w==Widescreen_CompressXAbout(len, 0));
			POLY_G4 *end=mem.cursor;
			assert(end>polygons && end<=polygons+6);
			assert(polygons[0].x0==100 && (end-1)->x1==100+outlines[0].w);
			for (POLY_G4 *p=polygons; p<end; p++) {
				assert(p->x0==p->x2 && p->x1==p->x3 && p->x1>=p->x0);
				if (p+1<end) assert(p->x1==(p+1)->x0);
			}
		}
	}
	static struct GameTracker gt;
	sdata_static.gGT=&gt; gt.gameMode1=MAIN_MENU;
	struct Icon top={0}, bottom={0};
	top.texLayout.u1=bottom.texLayout.u1=40;
	top.texLayout.v2=20; bottom.texLayout.v2=30;
	static POLY_FT4 maps[4];
	mem.cursor=maps; mem.end=maps+4; mem.guardEnd=maps+3;
	UI_Map_DrawMap(&top, &bottom, 200, 160, &mem, &ot, 0);
	assert(mem.cursor==maps+2 && maps[0].y2==maps[1].y0 && maps[0].y3==maps[1].y1);
	gt.gameMode1=0; gt.level1=NULL;
	mem.cursor=maps;
	UI_Map_DrawMap(&top, &bottom, 200, 160, &mem, &ot, 0);
	assert(mem.cursor==maps+1 && maps[0].y0==130 && maps[0].y2==160);
	return 0;
}
