#ifndef DRAW_LEVEL_OVR1P_SHARED_H
#define DRAW_LEVEL_OVR1P_SHARED_H

// NOTE(aalhendi): These helpers are static in the 226 source but intentionally
// reused by the 227/228/229 unity slices after 226 is included.
extern const struct DrawLevelOvr1PBucket sDrawLevelOvr1PBuckets[];
struct DrawLevelOvr1PStableScratch *DrawLevelOvr1P_Scratch(void);
struct MainRenderLevelGeometryScratch *DrawLevelOvr1P_RenderScratch(void);
void DrawLevelOvr1P_SetMosaicReloadSpanOverride(u32 reloadSpan);
void Ovr226_800a0dc4_ClearProjectedScratch(void);
u8 *DrawLevelOvr1P_GetClipRecordCursor(void);
void DrawLevelOvr1P_SetClipRecordCursor(u8 *cursor);
void DrawLevelOvr1P_SetClipRecordStart(u8 *start);
void DrawLevelOvr1P_SetRenderedOverflowBase(struct QuadBlock **renderedList);
void DrawLevelOvr1P_SetPrimReserveBias(u32 bias);
void DrawLevelOvr1P_SetListHandlersSeedRenderedCursor(int enabled);
int DrawLevelOvr1P_ConsumeClipRecords(struct PushBuffer *pb, struct PrimMem *primMem);
void *DrawLevelOvr1P_GetRenderListField(struct DrawLevelOvr1PRenderList *renderList, int offset);
int DrawLevelOvr1P_DrawRenderedQuadBlocks(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem, int role);
void Ovr226_800a0d34_SetEntryGteAndCameraScratch(struct PushBuffer *pb);
int Ovr226_800a0ef4_DrawFullDynamicBspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                           const int *visFaceList);
int Ovr226_800a1e30_DrawWaterBspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                     const int *visFaceList);
void DrawLevelOvr1P_SetSplitGroundThresholdScratch(void);
int DrawLevelOvr1P_DrawSplitGroundListABspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                               const int *visFaceList);
int Ovr226_800a36a8_DrawGround4x1BspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                         const int *visFaceList);
int Ovr226_800a4fa0_DrawGround4x2BspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                         const int *visFaceList);
int Ovr226_800a5e5c_DrawGround4x2RenderedList(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem);
int Ovr226_800a6f40_DrawDynamicBspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                       const int *visFaceList);
int Ovr226_800a7ba8_DrawDynamicRenderedList(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem);
int Ovr226_800a8b60_DrawWideDynamicBspList(struct VisMemBspListNode *slot, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem,
                                           const int *visFaceList);
int Ovr226_800a97c8_DrawQuad4x4RenderedList(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh, struct PrimMem *primMem);
int Ovr226_800a2904_DrawWaterRenderedListWithDefaultHandler(struct QuadBlock **renderedList, struct PushBuffer *pb, struct mesh_info *mesh,
                                                            struct PrimMem *primMem, u32 defaultHandlerAddress);

#endif
