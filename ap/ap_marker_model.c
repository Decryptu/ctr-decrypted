// The Archipelago-logo marker model for foreign multiworld items (#124).
//
// Compiled in as static data rather than loaded from an asset file: no file IO,
// no pointer-map fixup, no shipped asset. A model is disc-format data whose
// pointers are PSX addresses (include/ctr_psx_ptr.h), so the model, its frame,
// command list and colours are copied into one block of PSX space.
//
// The geometry comes from MMRecompRando's Archipelago-logo mesh (GPL-3.0),
// converted by tools/aplogo/convert.py; the logo itself is (c) 2022 Krista
// Corkos and Christopher Wilson, CC BY-NC 4.0. See tools/aplogo/README.md for
// the attribution still owed in THIRD_PARTY_NOTICES.

#if defined(CTR_AP) || defined(CTR_BOX_AUTHORING)

#include <common.h>

#include "ap_marker_model.h"
#include "ap_marker_model_data.h"
#include <platform/native_psx_space.h>

// The vertex bytes must sit immediately after the frame header, because the
// engine finds them at (char *)frame + frame->vertexOffset
// (namespace_Instance.h:301, read at RenderBucket_QueueExecute.c:5283).
struct ApMarkerFrame
{
	struct ModelFrame frame;
	u8                verts[AP_MARKER_NUM_VERTS * 3];
};

CTR_STATIC_ASSERT(sizeof(struct ModelFrame) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct ApMarkerFrame, verts) == 0x1c);

// Everything the model points at, in PSX space. Not const: the engine's
// pointer fields are non-const and the frame origin is written at build.
struct ApMarkerStore
{
	struct ApMarkerFrame frame;
	struct ModelHeader header;
	struct Model model;
	u32 commands[sizeof(s_apMarkerCommands) / sizeof(s_apMarkerCommands[0])];
	u32 colors[sizeof(s_apMarkerColors) / sizeof(s_apMarkerColors[0])];
};

static struct ApMarkerStore *s_apMarker;
static int s_apMarkerBuilt;

// Fallback model scale, used only if STATIC_GEM is not loaded when we register.
// Vanilla model scales live in BIGFILE data, not in this repo, so this number is
// a guess; the gem-derived path below avoids relying on it.
#define AP_MARKER_FALLBACK_SCALE 0x1000

static void AP_MarkerModel_Build(struct GameTracker *gGT)
{
	int i;

	s_apMarker = PsxSpace_Alloc(sizeof(*s_apMarker));
	if (s_apMarker == 0)
		return;
	memcpy(s_apMarker->commands, s_apMarkerCommands, sizeof(s_apMarker->commands));
	memcpy(s_apMarker->colors, s_apMarkerColors, sizeof(s_apMarker->colors));

	// Vertices are byte coordinates in a 0..255 cube, so the model would hang
	// off the +octant of the instance origin. The frame origin recentres it.
	// Pairing (verified at RenderBucket_QueueExecute.c:2237-2244, :2264):
	// pos.x offsets vertex byte0 (horizontal), pos.y offsets byte2 (vertical),
	// pos.z offsets byte1 (depth).
	s_apMarker->frame.frame.pos.x = -128;
	s_apMarker->frame.frame.pos.y = -128;
	s_apMarker->frame.frame.pos.z = -128;
	s_apMarker->frame.frame.vertexOffset = 0x1c;
	for (i = 0; i < AP_MARKER_NUM_VERTS * 3; i++)
		s_apMarker->frame.verts[i] = s_apMarkerVerts[i];

	s_apMarker->header.name[0] = 'a';
	s_apMarker->header.name[1] = 'p';
	s_apMarker->header.name[2] = '\0';
	// Win the LOD walk at every distance: the walk picks this header while
	// projectedDistance < (u16)maxDistanceLOD (QueueExecute.c:1283-1287), and
	// running out of headers means the model is not drawn at all (:1298-1300).
	s_apMarker->header.maxDistanceLOD = 0x7fff;
	s_apMarker->header.flags = 0;
	PSX_SET(s_apMarker->header.ptrCommandList, s_apMarker->commands);
	PSX_SET(s_apMarker->header.ptrFrameData, &s_apMarker->frame.frame);
	// NULL is legal only because every command's texture index is 0; a non-zero
	// index with no layout table would invalidate the prim (:2784-2805).
	PSX_SET(s_apMarker->header.ptrTexLayout, 0);
	PSX_SET(s_apMarker->header.ptrColors, s_apMarker->colors);
	// Both must stay zero: unk3 doubles as ptrDeltaArray and would send the
	// decoder down the bit-stream delta path instead of reading plain bytes
	// (:2398-2417); a non-null ptrAnimations would deselect ptrFrameData (:1956).
	PSX_SET(s_apMarker->header.unk3, 0);
	s_apMarker->header.numAnimations = 0;
	PSX_SET(s_apMarker->header.ptrAnimations, 0);
	PSX_SET(s_apMarker->header.animtex, 0);

	// Size the marker off the gem it replaces, so it lands in the same visual
	// ballpark without guessing at vanilla scale units. Vanilla model scales are
	// not in this repo (they live in BIGFILE data), so deriving beats inventing.
	{
		struct Model *gem = gGT->modelPtr[STATIC_GEM];
		s16           s = AP_MARKER_FALLBACK_SCALE;

		if (gem != 0 && PSX_GET(gem->headers) != 0 && gem->numHeaders > 0)
			s = PSX_GET(gem->headers)[0].scale.x;

		s_apMarker->header.scale.x = s;
		s_apMarker->header.scale.y = s;
		s_apMarker->header.scale.z = s;
	}

	s_apMarker->model.name[0] = 'a';
	s_apMarker->model.name[1] = 'p';
	s_apMarker->model.name[2] = '\0';
	s_apMarker->model.id = STATIC_AP;
	s_apMarker->model.numHeaders = 1;
	PSX_SET(s_apMarker->model.headers, &s_apMarker->header);

	s_apMarkerBuilt = 1;
}

void AP_MarkerModel_Register(struct GameTracker *gGT)
{
	if (gGT == 0)
		return;

	// Build once the gem is available, so the derived scale is real rather than
	// the fallback. Before that the slot simply stays empty and the glow keeps
	// its previous marker -- the render site NULL-checks modelPtr already.
	if (!s_apMarkerBuilt)
	{
		if (gGT->modelPtr[STATIC_GEM] == 0)
			return;
		AP_MarkerModel_Build(gGT);
	}

	gGT->modelPtr[STATIC_AP] = &s_apMarker->model;
}

int AP_MarkerModel_IsRegistered(void)
{
	return s_apMarkerBuilt;
}

#endif // CTR_AP
