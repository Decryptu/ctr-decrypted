#ifndef CTR_NATIVE_NAMESPACE_DRAWLEVEL_H
#define CTR_NATIVE_NAMESPACE_DRAWLEVEL_H

enum DrawLevelOvr1PRenderListConstant
{
	DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT = 5,
};

struct DrawLevelOvr1PRenderListSlot
{
	struct QuadBlock **ptrQuadBlocksRendered;
	struct VisMemBspListNode *bspListStart;
};

struct DrawLevelOvr1PRenderList
{
	struct DrawLevelOvr1PRenderListSlot list[DRAW_LEVEL_OVR1P_RENDER_LIST_SLOT_COUNT];
	struct VisMemBspListNode *bspListStart_FullDynamic;
	struct QuadBlock **ptrQuadBlocksRendered_FullDynamic;
};

#define DRAW_LEVEL_OVR1P_LIST_OFFSET(INDEX, MEMBER)                                                            \
	(offsetof(struct DrawLevelOvr1PRenderList, list) + (INDEX) * sizeof(struct DrawLevelOvr1PRenderListSlot) + \
	 offsetof(struct DrawLevelOvr1PRenderListSlot, MEMBER))

enum DrawLevelOvr1PRenderListOffset
{
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X4_RENDERED = DRAW_LEVEL_OVR1P_LIST_OFFSET(0, ptrQuadBlocksRendered),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X4_LIST = DRAW_LEVEL_OVR1P_LIST_OFFSET(0, bspListStart),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_DYNAMIC_RENDERED = DRAW_LEVEL_OVR1P_LIST_OFFSET(1, ptrQuadBlocksRendered),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_DYNAMIC_LIST = DRAW_LEVEL_OVR1P_LIST_OFFSET(1, bspListStart),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X2_RENDERED = DRAW_LEVEL_OVR1P_LIST_OFFSET(2, ptrQuadBlocksRendered),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X2_LIST = DRAW_LEVEL_OVR1P_LIST_OFFSET(2, bspListStart),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X1_RENDERED = DRAW_LEVEL_OVR1P_LIST_OFFSET(3, ptrQuadBlocksRendered),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_4X1_LIST = DRAW_LEVEL_OVR1P_LIST_OFFSET(3, bspListStart),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_WATER_RENDERED = DRAW_LEVEL_OVR1P_LIST_OFFSET(4, ptrQuadBlocksRendered),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_WATER_LIST = DRAW_LEVEL_OVR1P_LIST_OFFSET(4, bspListStart),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_FULL_DYNAMIC_LIST = offsetof(struct DrawLevelOvr1PRenderList, bspListStart_FullDynamic),
	DRAW_LEVEL_OVR1P_RENDER_LIST_OFFSET_FULL_DYNAMIC_RENDERED = offsetof(struct DrawLevelOvr1PRenderList, ptrQuadBlocksRendered_FullDynamic),
};

#undef DRAW_LEVEL_OVR1P_LIST_OFFSET

enum DrawLevelOvr1PBucketKind
{
	DRAW_LEVEL_OVR1P_BUCKET_QUADBLOCKS_RENDERED,
	DRAW_LEVEL_OVR1P_BUCKET_BSP_LIST,
};

enum DrawLevelOvr1PBucketRole
{
	DRAW_LEVEL_OVR1P_BUCKET_4X4_RENDERED,
	DRAW_LEVEL_OVR1P_BUCKET_4X4_LIST,
	DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_RENDERED,
	DRAW_LEVEL_OVR1P_BUCKET_DYNAMIC_LIST,
	DRAW_LEVEL_OVR1P_BUCKET_4X2_RENDERED,
	DRAW_LEVEL_OVR1P_BUCKET_4X2_LIST,
	DRAW_LEVEL_OVR1P_BUCKET_4X1_RENDERED,
	DRAW_LEVEL_OVR1P_BUCKET_4X1_LIST,
	DRAW_LEVEL_OVR1P_BUCKET_WATER_RENDERED,
	DRAW_LEVEL_OVR1P_BUCKET_WATER_LIST,
	DRAW_LEVEL_OVR1P_BUCKET_FULL_DYNAMIC_LIST,
};

struct DrawLevelOvr1PBucket
{
	u8 renderListOffset;
	u8 kind;
	u8 role;
	u8 lodMode;
};

CTR_RETAIL_LAYOUT_ASSERT(sizeof(struct DrawLevelOvr1PRenderListSlot) == 0x8);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PRenderListSlot, ptrQuadBlocksRendered) == 0x0);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PRenderListSlot, bspListStart) == 0x4);
CTR_RETAIL_LAYOUT_ASSERT(sizeof(struct DrawLevelOvr1PRenderList) == 0x30);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PRenderList, list) == 0x0);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PRenderList, bspListStart_FullDynamic) == 0x28);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PRenderList, ptrQuadBlocksRendered_FullDynamic) == 0x2c);

enum DrawLevelOvr1PUvScratchSlot
{
	DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_0,
	DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_1,
	DRAW_LEVEL_OVR1P_UV_SCRATCH_SLOT_2,
};

enum DrawLevelOvr1PSharedConstant
{
	DRAW_LEVEL_OVR1P_SPLIT_GROUND_MOSAIC_RELOAD_SPAN = 0xc0,
};

enum DrawLevelOvrCopiedSetupConstant
{
	DRAW_LEVEL_OVR_COPIED_SETUP0_WORD_COUNT = 15,
	DRAW_LEVEL_OVR_COPIED_SETUP1_WORD_COUNT = 3,
	DRAW_LEVEL_OVR_COPIED_SETUP0_LAST_WORD_INDEX = DRAW_LEVEL_OVR_COPIED_SETUP0_WORD_COUNT - 1,
	DRAW_LEVEL_OVR_COPIED_SETUP1_LAST_WORD_INDEX = DRAW_LEVEL_OVR_COPIED_SETUP1_WORD_COUNT - 1,
	DRAW_LEVEL_OVR_COPIED_SETUP0_SCRATCH_OFFSET = 0x14c,
	DRAW_LEVEL_OVR_COPIED_SETUP1_SCRATCH_OFFSET = 0x188,
};

struct DrawLevelOvrBucketSetupCopy
{
	u32 lastWordIndex;
	u32 sourceAddress;
	u32 scratchOffset;
};

struct DrawLevelOvrBucketSetupRecord
{
	struct DrawLevelOvrBucketSetupCopy copies[2];
	u32 padding;
	u32 copy0[DRAW_LEVEL_OVR_COPIED_SETUP0_WORD_COUNT];
	u32 copy1[DRAW_LEVEL_OVR_COPIED_SETUP1_WORD_COUNT];
};

CTR_STATIC_ASSERT(sizeof(struct DrawLevelOvrBucketSetupCopy) == 0xc);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvrBucketSetupCopy, lastWordIndex) == 0x0);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvrBucketSetupCopy, sourceAddress) == 0x4);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvrBucketSetupCopy, scratchOffset) == 0x8);
CTR_STATIC_ASSERT(sizeof(struct DrawLevelOvrBucketSetupRecord) == 0x64);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvrBucketSetupRecord, copies) == 0x0);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvrBucketSetupRecord, padding) == 0x18);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvrBucketSetupRecord, copy0) == 0x1c);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvrBucketSetupRecord, copy1) == 0x58);



struct DrawLevelOvr1PScratchVertex
{
	s16 pos[3];
	u16 flags;
	u8 color_hi[4];
	s16 posScreen[2];
	u16 depth;
	u8 clipNear;
	u8 clipHalfNear;
};

struct DrawLevelOvr1PClipRecordVertex
{
	s16 pos[3];
	u16 flags;
	u8 color_hi[4];
};

struct DrawLevelOvr1PClipRecord
{
	u32 header;
	PSX_PTR(u32) otEntry; // an ordering table entry; OTs come from the mempack, so the record keeps retail's size
	s16 tpage;
	s16 clut;
	struct DrawLevelOvr1PClipRecordVertex vertex[4];
};

union DrawLevelOvr1PUvWord
{
	u32 packed;
	struct
	{
		s16 low;
		s16 high;
	} halves;
};

struct DrawLevelOvr1PUvScratch
{
	union DrawLevelOvr1PUvWord uv0;
	union DrawLevelOvr1PUvWord uv1;
	union DrawLevelOvr1PUvWord uv2;
	u32 savedUv0;
	u32 savedUv1;
};

struct DrawLevelOvr1PStableScratch
{
	union
	{
		struct MainRenderLevelGeometryScratch render;
		struct
		{
			u8 *playerClipCursor[4];
			u8 *clipCursor;
		} clip;
	} entry;
	void *primMemEnd;
	u32 currentBucketOffset;
	void *savedStack;
	u8 pad_03c[0x20];
	s32 depthClipThreshold;
	struct DrawLevelOvr1PRenderList *renderList;
	struct QuadBlock **renderedOverflow;
	s32 quadCount;
	u32 clipWindowPacked;
	u32 directMask;
	u32 currentHandlerAddress;
	u8 pad_078[0x04];
	u32 drawOrderOrHeader;
	u32 clipRecordHeader;
	u32 mosaicTextureWord;
	const struct TextureLayout *waterEnvMap;
	u8 pad_08c[0x10];
	u32 previousDirectHandlerAddress;
	u8 pad_0a0[0x1c];
	s32 visibilityBitIndex;
	u32 *visibilityWordAt;
	u32 visibilityWord;
	const int *visFaceList;
	const int *visFaceListArg[4];
	struct PushBuffer *pushBuffer[4];
	u8 pad_0ec[0xa8];
	u32 selected4x1TableWord;
	SVec3 projectedCenter;
	u8 pad_19e[0x02];
	struct DrawLevelOvr1PUvScratch uv;
};

CTR_STATIC_ASSERT(sizeof(struct DrawLevelOvr1PScratchVertex) == 0x14);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PScratchVertex, pos) == 0x00);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PScratchVertex, flags) == 0x06);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PScratchVertex, color_hi) == 0x08);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PScratchVertex, posScreen) == 0x0c);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PScratchVertex, depth) == 0x10);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PScratchVertex, clipNear) == 0x12);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PScratchVertex, clipHalfNear) == 0x13);
CTR_STATIC_ASSERT(sizeof(struct DrawLevelOvr1PClipRecordVertex) == 0xc);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PClipRecordVertex, pos) == 0x00);
CTR_STATIC_ASSERT(sizeof(struct DrawLevelOvr1PClipRecord) == 0x3c);
CTR_STATIC_ASSERT(sizeof(struct DrawLevelOvr1PUvScratch) == 0x14);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv0.packed) == 0x00);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv0.halves.low) == 0x00);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv0.halves.high) == 0x02);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv1.packed) == 0x04);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv1.halves.low) == 0x04);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv1.halves.high) == 0x06);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv2.packed) == 0x08);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv2.halves.low) == 0x08);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, uv2.halves.high) == 0x0a);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, savedUv0) == 0x0c);
CTR_STATIC_ASSERT(offsetof(struct DrawLevelOvr1PUvScratch, savedUv1) == 0x10);
CTR_RETAIL_LAYOUT_ASSERT(sizeof(struct DrawLevelOvr1PStableScratch) == 0x1b4);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, entry.render) == 0x00);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, entry.clip.playerClipCursor) == 0x00);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, entry.clip.clipCursor) == 0x10);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, primMemEnd) == 0x30);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, currentBucketOffset) == 0x34);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, savedStack) == 0x38);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, depthClipThreshold) == 0x5c);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, renderList) == 0x60);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, renderedOverflow) == 0x64);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, quadCount) == 0x68);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, clipWindowPacked) == 0x6c);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, directMask) == 0x70);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, currentHandlerAddress) == 0x74);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, drawOrderOrHeader) == 0x7c);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, clipRecordHeader) == 0x80);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, mosaicTextureWord) == 0x84);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, waterEnvMap) == 0x88);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, previousDirectHandlerAddress) == 0x9c);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, visibilityBitIndex) == 0xbc);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, visibilityWordAt) == 0xc0);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, visibilityWord) == 0xc4);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, visFaceList) == 0xc8);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, visFaceListArg) == 0xcc);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, pushBuffer) == 0xdc);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, selected4x1TableWord) == 0x194);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, projectedCenter) == 0x198);
CTR_RETAIL_LAYOUT_ASSERT(offsetof(struct DrawLevelOvr1PStableScratch, uv) == 0x1a0);

// Scratchpad slots the level renderer addresses by offset. The handler tables
// sit in the stable scratch's gap at 0xec and the projected frames follow it,
// so on a 64-bit host, where its pointers make it larger, they move with it.
// The numbers in comments are retail's.
enum DrawLevelOvr1PScratchOffset
{
	DRAW_LEVEL_OVR1P_SCRATCH_INIT_TABLE_OFFSET = offsetof(struct DrawLevelOvr1PStableScratch, pad_0ec), // 0x0ec
	DRAW_LEVEL_OVR1P_DIRECT_NEAR_HANDLER_TABLE_OFFSET = DRAW_LEVEL_OVR1P_SCRATCH_INIT_TABLE_OFFSET + 0x5c, // 0x148
	DRAW_LEVEL_OVR1P_NEAR_SUBDIVISION_HANDLER_TABLE_OFFSET = DRAW_LEVEL_OVR1P_SCRATCH_INIT_TABLE_OFFSET + 0x60, // 0x14c
	DRAW_LEVEL_OVR1P_DIRECT_HANDLER_TABLE_OFFSET = DRAW_LEVEL_OVR1P_SCRATCH_INIT_TABLE_OFFSET + 0x98, // 0x184
	DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET = sizeof(struct DrawLevelOvr1PStableScratch), // 0x1b4
	DRAW_LEVEL_OVR1P_TERMINAL_CLIP_VERTEX_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0x50, // 0x204
	DRAW_LEVEL_OVR1P_GT3_CLIP_RECORD_JUMP_TABLE_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0x8c, // 0x240
	DRAW_LEVEL_OVR1P_GT4_CLIP_RECORD_JUMP_TABLE_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0xac, // 0x260
	DRAW_LEVEL_OVR1P_WATER_RENDERED_SENTINEL_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0xb4, // 0x268
	DRAW_LEVEL_OVR1P_TERMINAL_RETURN_PC_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0xec, // 0x2a0
	DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_INDEX_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0x16c, // 0x320
	DRAW_LEVEL_OVR1P_DEEPEST_PROJECTED_FRAME_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0x170, // 0x324
	DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_BIAS_OFFSET = DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET + 0x224, // 0x3d8
};

CTR_RETAIL_LAYOUT_ASSERT(DRAW_LEVEL_OVR1P_SCRATCH_INIT_TABLE_OFFSET == 0x0ec);
CTR_RETAIL_LAYOUT_ASSERT(DRAW_LEVEL_OVR1P_PROJECTED_FRAME0_OFFSET == 0x1b4);
CTR_STATIC_ASSERT(DRAW_LEVEL_OVR1P_MOSAIC_SOURCE_BIAS_OFFSET + sizeof(u32) <= CTR_SCRATCHPAD_SIZE);

#endif
