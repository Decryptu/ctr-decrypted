#include <common.h>

#if defined(CTR_NATIVE)
#include <math.h>
#include <string.h>
#endif

#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif

struct DrawSkyContext
{
	const struct ShortVertex *verts;
	u32 *ot;
	u32 screenBounds;
#if defined(CTR_NATIVE)
	int clipped;    // draw every face through DrawSky_ClipFace
	double rot[9];  // the view's rotation, precise when PGXP has it
	double h, ofx, ofy;
#endif
#ifdef CTR_CUSTOM_TRACKS
	// Where the frame's primitive arena runs out. Carried on the context
	// because DrawSky_Piece has no other route to the PrimMem.
	const void *primGuardEnd;
#endif
};

struct DrawSkyScratch
{
	u32 baseFaceOffset;
	u32 baseCountOffset;
};

CTR_STATIC_ASSERT(sizeof(struct DrawSkyScratch) == 0x8);
CTR_STATIC_ASSERT(offsetof(struct DrawSkyScratch, baseFaceOffset) == 0x0);
CTR_STATIC_ASSERT(offsetof(struct DrawSkyScratch, baseCountOffset) == 0x4);

#ifdef CTR_CUSTOM_TRACKS
// Said once per process, not once per frame: the clamp holds every frame the
// track is on screen, and a per-frame line would bury the log at 60 Hz.
static void DrawSky_NoteClamped(void)
{
	static int warned = 0;

	if (warned)
		return;

	warned = 1;
	CustomTrack_Log("[CustomTracks] sky clamped: the skybox did not fit the frame's primitive arena, "
	                "so the far side of the sky is not drawn\n");
}
#endif

static u32 DrawSky_ReadWord(const void *base, int offset)
{
	return *(const u32 *)(const void *)((const char *)base + offset);
}

static int DrawSky_IsVisible(u32 gteFlag, u32 sxy0, u32 sxy1, u32 sxy2, u32 screenBounds)
{
	u32 overlap;
	u32 bounds;

	if (((gteFlag << 13) >> 29) != 0)
	{
		return 0;
	}

	overlap = sxy0 & sxy1 & sxy2;
	bounds = ~((sxy0 - screenBounds) | (sxy1 - screenBounds) | (sxy2 - screenBounds)) | overlap;
	if ((s32)bounds < 0)
	{
		return 0;
	}

	return (s32)(bounds << 16) >= 0;
}

static void DrawSky_LoadFaceVertices(struct DrawSkyContext *ctx, const struct SkyboxFace *face)
{
	const char *verts = (const char *)ctx->verts;
	const struct ShortVertex *a = (const struct ShortVertex *)(const void *)(verts + face->A);
	const struct ShortVertex *b = (const struct ShortVertex *)(const void *)(verts + face->B);
	const struct ShortVertex *c = (const struct ShortVertex *)(const void *)(verts + face->C);

	MTC2(DrawSky_ReadWord(&a->Position, 0x0), 0);
	MTC2(DrawSky_ReadWord(&a->Position, 0x4), 1);
	MTC2(DrawSky_ReadWord(&a->Color, 0x0), 20);

	MTC2(DrawSky_ReadWord(&b->Position, 0x0), 2);
	MTC2(DrawSky_ReadWord(&b->Position, 0x4), 3);
	MTC2(DrawSky_ReadWord(&b->Color, 0x0), 21);

	MTC2(DrawSky_ReadWord(&c->Position, 0x0), 4);
	MTC2(DrawSky_ReadWord(&c->Position, 0x4), 5);
	MTC2(DrawSky_ReadWord(&c->Color, 0x0), 22);
}

static void DrawSky_EmitPrimitive(u32 **primCursor, u32 *ot)
{
	POLY_G3 *poly = (POLY_G3 *)*primCursor;

	CtrGpu_WriteColorCode(&poly->r0, MFC2(20));
	CtrGpu_WritePackedXY(&poly->x0, MFC2(12));
	CtrGpu_WriteColorCode(&poly->r1, MFC2(21));
	CtrGpu_WritePackedXY(&poly->x1, MFC2(13));
	CtrGpu_WriteColorCode(&poly->r2, MFC2(22));
	CtrGpu_WritePackedXY(&poly->x2, MFC2(14));
	CtrGpu_LinkPacket24(ot, &poly->tag, poly, 0x06000000);
#ifdef CTR_NATIVE
	// The sky is projected through the precise view like the level, but its
	// screen positions are copied out by hand: attach the precise ones, or the
	// sky's gradient steps with the integer camera's pitch while the speed
	// changes.
	if (NATIVE_PGXP_ACTIVE())
	{
		NativePgxp_StoreGteSXY(&poly->x0, 12, MFC2(12));
		NativePgxp_StoreGteSXY(&poly->x1, 13, MFC2(13));
		NativePgxp_StoreGteSXY(&poly->x2, 14, MFC2(14));
	}
#endif

	*primCursor = (u32 *)(poly + 1);
}

#if defined(CTR_NATIVE)
// Retail drops a sky face whole when a corner is too close to the camera plane
// (the GTE flags a divide overflow), and the GPU drops one 512 or more pixels
// tall. The sky is a dome of large faces around the camera, so from the far
// camera and in widescreen such a face still covers part of the screen, and a
// corner of the sky went missing until the camera moved on
// (docs/LEVEL-VISIBILITY.md). Native clips every face in view space against the
// near plane and the screen's edges instead, projects what is left and draws it
// as a fan, colours interpolated at the cuts: the pieces fit the GPU's limits
// and keep their true shape, with or without PGXP. Every face goes this way, not
// only the ones retail would drop, so neighbours share bit-identical corners and
// no hairline seams open between the two kinds.
#define DRAW_SKY_CLIP_MAX 9

struct DrawSkyClipVertex
{
	double v[3]; // view space, GTE units
	double c[3]; // colour
};

static int DrawSky_ClipPlane(struct DrawSkyClipVertex *dst, const struct DrawSkyClipVertex *src, int count, const double plane[4])
{
	int out = 0;
	for (int i = 0; i < count; i++)
	{
		const struct DrawSkyClipVertex *a = &src[i];
		const struct DrawSkyClipVertex *b = &src[(i + 1) % count];
		const double da = plane[0] * a->v[0] + plane[1] * a->v[1] + plane[2] * a->v[2] + plane[3];
		const double db = plane[0] * b->v[0] + plane[1] * b->v[1] + plane[2] * b->v[2] + plane[3];
		if (da >= 0.0)
			dst[out++] = *a;
		if ((da >= 0.0) != (db >= 0.0))
		{
			const double t = da / (da - db);
			for (int k = 0; k < 3; k++)
			{
				dst[out].v[k] = a->v[k] + (b->v[k] - a->v[k]) * t;
				dst[out].c[k] = a->c[k] + (b->c[k] - a->c[k]) * t;
			}
			out++;
		}
	}
	return out;
}

static void DrawSky_ClipFace(struct DrawSkyContext *ctx, const struct SkyboxFace *face, u32 *ot, u32 **primCursor)
{
	const char *verts = (const char *)ctx->verts;
	const struct ShortVertex *corner[3] = {(const struct ShortVertex *)(const void *)(verts + face->A), (const struct ShortVertex *)(const void *)(verts + face->B),
	                                       (const struct ShortVertex *)(const void *)(verts + face->C)};
	const double *rot = ctx->rot;
	struct DrawSkyClipVertex poly[DRAW_SKY_CLIP_MAX];
	struct DrawSkyClipVertex next[DRAW_SKY_CLIP_MAX];
	for (int i = 0; i < 3; i++)
	{
		const double p[3] = {corner[i]->Position.vx, corner[i]->Position.vy, corner[i]->Position.vz};
		for (int k = 0; k < 3; k++)
			poly[i].v[k] = (rot[k * 3] * p[0] + rot[k * 3 + 1] * p[1] + rot[k * 3 + 2] * p[2]) / 4096.0;
		poly[i].c[0] = corner[i]->Color.r;
		poly[i].c[1] = corner[i]->Color.g;
		poly[i].c[2] = corner[i]->Color.b;
	}

	// Screen x = OFX + H * x / z, so x >= left is H * x + (OFX - left) * z >= 0.
	// The planes sit 8 pixels outside the screen, so any cut is off screen.
	const double h = ctx->h;
	const double ofx = ctx->ofx;
	const double ofy = ctx->ofy;
	const double right = (double)(ctx->screenBounds & 0xffff) + 8.0;
	const double bottom = (double)(ctx->screenBounds >> 16) + 8.0;
	const double planes[5][4] = {
		{0.0, 0.0, 1.0, -1.0}, // z >= 1
		{h, 0.0, ofx + 8.0, 0.0},
		{-h, 0.0, right - ofx, 0.0},
		{0.0, h, ofy + 8.0, 0.0},
		{0.0, -h, bottom - ofy, 0.0},
	};
	int count = 3;
	for (int p = 0; (p < 5) && (count >= 3); p++)
	{
		count = DrawSky_ClipPlane(next, poly, count, planes[p]);
		memcpy(poly, next, sizeof(poly[0]) * count);
	}
	if (count < 3)
		return;

	float sx[DRAW_SKY_CLIP_MAX];
	float sy[DRAW_SKY_CLIP_MAX];
	u32 rgb[DRAW_SKY_CLIP_MAX];
	const u32 code = DrawSky_ReadWord(&corner[0]->Color, 0) & 0xff000000u;
	for (int i = 0; i < count; i++)
	{
		sx[i] = (float)(ofx + h * poly[i].v[0] / poly[i].v[2]);
		sy[i] = (float)(ofy + h * poly[i].v[1] / poly[i].v[2]);
		rgb[i] = code;
		for (int k = 0; k < 3; k++)
		{
			const int c = (int)(poly[i].c[k] + 0.5);
			rgb[i] |= (u32)(c < 0 ? 0 : (c > 255 ? 255 : c)) << (k * 8);
		}
	}

	for (int i = 1; i + 1 < count; i++)
	{
		const int idx[3] = {0, i, i + 1};
		POLY_G3 *g3 = (POLY_G3 *)*primCursor;
		u8 *color[3] = {&g3->r0, &g3->r1, &g3->r2};
		VERTTYPE *xy[3] = {&g3->x0, &g3->x1, &g3->x2};
		for (int k = 0; k < 3; k++)
		{
			const int j = idx[k];
			const u32 packed = ((u32)(u16)(s16)lrintf(sx[j])) | ((u32)(u16)(s16)lrintf(sy[j]) << 16);
			CtrGpu_WriteColorCode(color[k], rgb[j]);
			CtrGpu_WritePackedXY(xy[k], packed);
			if (NATIVE_PGXP_ACTIVE())
				NativePgxp_SetXY(xy[k], sx[j], sy[j], packed);
		}
		CtrGpu_LinkPacket24(ot, &g3->tag, g3, 0x06000000);
		*primCursor = (u32 *)(g3 + 1);
	}
}
#endif

static u32 *DrawSky_Piece(struct Skybox *skybox, struct DrawSkyContext *ctx, int faceIndex, int countIndex, u32 *prim)
{
	u32 numFaces = (u16)skybox->numFaces[countIndex];
	const struct SkyboxFace *face = skybox->ptrFaces[faceIndex];

	if (numFaces == 0)
	{
		return prim;
	}

	for (u32 i = 0; i < numFaces; i++, face++)
	{
		u32 *ot = (u32 *)(void *)((char *)ctx->ot + (s16)face->D);
		u32 sxy0;
		u32 sxy1;
		u32 sxy2;
		u32 gteFlag;

#ifdef CTR_CUSTOM_TRACKS
		// Retail sizes the primitive arena per levelID and its own skyboxes fit
		// inside it with room to spare, so this loop is written to trust
		// numFaces. A custom track inherits the borrowed slot's budget while
		// bringing its own sky, and the event track's sky wants roughly three
		// times the whole arena, so the trust has to end somewhere. Stopping
		// here rather than at the emit skips the GTE transform too, which is
		// what keeps a 2,772-face segment from costing a full projection pass
		// per frame once there is no room left to draw into.
		// A clipped face can come out as a fan of up to DRAW_SKY_CLIP_MAX - 2.
		if (!CustomTrackPolicy_PrimFits(prim, sizeof(POLY_G3) * (ctx->clipped ? DRAW_SKY_CLIP_MAX - 2 : 1), ctx->primGuardEnd))
		{
			DrawSky_NoteClamped();
			break;
		}
#endif

#if defined(CTR_NATIVE)
		if (ctx->clipped)
		{
			DrawSky_ClipFace(ctx, face, ot, &prim);
			continue;
		}
#endif

		DrawSky_LoadFaceVertices(ctx, face);
		gte_rtpt_b();

		sxy0 = MFC2(12);
		gteFlag = CFC2(31);
		sxy1 = MFC2(13);
		sxy2 = MFC2(14);

		if (DrawSky_IsVisible(gteFlag, sxy0, sxy1, sxy2, ctx->screenBounds))
		{
			DrawSky_EmitPrimitive(&prim, ot);
		}
	}

	return prim;
}

void DrawSky_Full(void *skybox, struct PushBuffer *pb, struct PrimMem *primMem)
{
	struct Skybox *sky = skybox;
	u32 *prim = (u32 *)primMem->cursor;

	// NOTE(aalhendi): PSX-backfeed blocker: retail saves/restores ra and s0-s2 in scratchpad 0x00-0x0c.
	// Native C relies on the host ABI because the only retail data temporaries live in 0x10 and 0x14 and are explicit below.
	if (sky != NULL)
	{
		struct DrawSkyScratch *scratch = CTR_SCRATCHPAD_PTR(struct DrawSkyScratch, 0x10);
		struct DrawSkyContext ctx;
		u32 baseFaceOffset;
		u32 baseCountOffset;
		int faceIndex;
		int countIndex;

		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x00), 0);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x04), 1);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x08), 2);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x0c), 3);
		CTC2(DrawSky_ReadWord(&pb->matrix_ViewProj, 0x10), 4);
		CTC2(0, 5);
		CTC2(0, 6);
		CTC2(0, 7);

		baseFaceOffset = ((DrawSky_ReadWord(pb, 0x08) + 0x500) >> 7) & 0x1c;
		baseCountOffset = (baseFaceOffset >> 1) & 0xe;

		scratch->baseFaceOffset = baseFaceOffset;
		scratch->baseCountOffset = baseCountOffset;

		ctx.verts = sky->ptrVertex;
		ctx.ot = &pb->ptrOT[0x3ff];
		ctx.screenBounds = DrawSky_ReadWord(pb, 0x20);
#ifdef CTR_CUSTOM_TRACKS
		ctx.primGuardEnd = primMem->guardEnd;
#endif
#if defined(CTR_NATIVE)
		ctx.clipped = !NativeDev_RetailCull();
		if (ctx.clipped)
		{
			u32 words[5];
			const s32 noTranslation[3] = {0, 0, 0};
			double trans[3];
			for (int i = 0; i < 5; i++)
				words[i] = DrawSky_ReadWord(&pb->matrix_ViewProj, i * 4);
			// R33 alone in its word, sign-extended, as NativeGte_PreciseView keys it.
			words[4] = (u32)(s32)(s16)words[4];
			if (!NATIVE_PGXP_GEOMETRY_ACTIVE() || !NativePgxp_ViewLookup(words, noTranslation, ctx.rot, trans))
			{
				for (int i = 0; i < 9; i++)
					ctx.rot[i] = (double)(s16)(words[i >> 1] >> ((i & 1) * 16));
			}
			ctx.h = (double)(u16)CFC2(26);
			ctx.ofx = (double)(s32)CFC2(24) / 65536.0;
			ctx.ofy = (double)(s32)CFC2(25) / 65536.0;
		}
#endif

		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (baseFaceOffset + 4) & 0x1c;
		baseCountOffset = (baseCountOffset + 2) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (scratch->baseFaceOffset - 4) & 0x1c;
		baseCountOffset = (scratch->baseCountOffset - 2) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

		baseFaceOffset = (scratch->baseFaceOffset - 8) & 0x1c;
		baseCountOffset = (scratch->baseCountOffset - 4) & 0xe;
		faceIndex = (int)(baseFaceOffset >> 2);
		countIndex = (int)(baseCountOffset >> 1);
		prim = DrawSky_Piece(sky, &ctx, faceIndex, countIndex, prim);

#if defined(CTR_NATIVE)
		// The sky is cut into eight 45-degree slices around the camera, and
		// retail draws the four facing it (above), enough for the 4:3 view.
		// Widescreen sees past them, and a corner of the sky went missing
		// (docs/LEVEL-VISIBILITY.md). Draw the other four too: DrawSky_IsVisible
		// still drops every face behind the camera or off screen.
		if (!NativeDev_RetailCull())
		{
			for (int slice = 2; slice < 6; slice++)
			{
				baseFaceOffset = (scratch->baseFaceOffset + 4 * slice) & 0x1c;
				baseCountOffset = (scratch->baseCountOffset + 2 * slice) & 0xe;
				prim = DrawSky_Piece(sky, &ctx, (int)(baseFaceOffset >> 2), (int)(baseCountOffset >> 1), prim);
			}
		}
#endif
	}

	primMem->cursor = prim;
}
