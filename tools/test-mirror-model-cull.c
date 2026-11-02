// Production matrix builders and NCLIP, with synthetic geometry (no disc needed).
// cc -m32 -std=c99 -DCTR_NATIVE -DCTR_AP -DBUILD=926 -I include -I .
// -ffunction-sections -fdata-sections -Wl,--gc-sections
// tools/test-mirror-model-cull.c platform/native_inline_c.c platform/native_libgte.c
// -lm -o /tmp/test-mirror-model-cull
//
// The orb fixture uses the observed header flags/scale of both NTSC-U LODs:
// ALWAYS_POINT_NORTH, {11120,11120,6748}. Vertices are a representative triangle,
// not retail geometry. A custom basis has negative Y even without Mirror Mode.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <platform/native_pgxp.h>
#include <platform/native_config.h>
#include <platform/native_gpu_links.h>
#include "../game/RenderBucket/RenderBucket_QueueExecute.c"
#include "../platform/native_gte_core.c"
#include "../game/MATH/MATH_7_MatrixStubs.c"
#include "../game/MATH/MATH_LightMatrix.c"

static int mirrored;
static int checks, failures;
struct sData sdata_static;
int AP_TrapMirrorCullFlip(void) { return mirrored; }
// The polygon-dump labels (platform/native_dev.c) are a developer view only.
void NativeDev_NamePrims(const void *start, const void *end, const char *name) { (void)start; (void)end; (void)name; }

// PGXP off: the cull is checked as retail computes it. The GTE calls these
// entry points only while PGXP is on, so they only have to link; reaching one
// fails the harness rather than changing what it measures.
int gNativePgxpMode = NATIVE_PGXP_MODE_OFF;
#define HARNESS_UNREACHED(name) do { printf("FAIL: harness reached %s\n", name); exit(1); } while (0)
void NativePgxp_GteWriteSXY(int reg, u32 value) { (void)reg, (void)value; HARNESS_UNREACHED("NativePgxp_GteWriteSXY"); }
void NativePgxp_GteInvalidateInput(int reg) { (void)reg; HARNESS_UNREACHED("NativePgxp_GteInvalidateInput"); }
void NativePgxp_GteReadSXY(int reg, u32 value) { (void)reg, (void)value; HARNESS_UNREACHED("NativePgxp_GteReadSXY"); }
void NativePgxp_StoreGteSXY(const void *dst, int reg, u32 value) { (void)dst, (void)reg, (void)value; HARNESS_UNREACHED("NativePgxp_StoreGteSXY"); }
void NativePgxp_GteProject(float x, float y, float w, u32 value) { (void)x, (void)y, (void)w, (void)value; HARNESS_UNREACHED("NativePgxp_GteProject"); }
void NativePgxp_GteProjectTexture(u32 value, float w) { (void)value, (void)w; HARNESS_UNREACHED("NativePgxp_GteProjectTexture"); }
int NativePgxp_GteNclip(u32 sxy0, u32 sxy1, u32 sxy2, s64 *out) { (void)sxy0, (void)sxy1, (void)sxy2, (void)out; HARNESS_UNREACHED("NativePgxp_GteNclip"); }
int NativePgxp_GteGetInput(int slot, s16 vx, s16 vy, s16 vz, double *out) { (void)slot, (void)vx, (void)vy, (void)vz, (void)out; HARNESS_UNREACHED("NativePgxp_GteGetInput"); }
void NativePgxp_GteSetMvmvaResult(const double *precise, s16 ir1, s16 ir2, s16 ir3) { (void)precise, (void)ir1, (void)ir2, (void)ir3; HARNESS_UNREACHED("NativePgxp_GteSetMvmvaResult"); }
void NativePgxp_Transform(const double *rotation, const double *translation, const double *input, double *result) { (void)rotation, (void)translation, (void)input, (void)result; HARNESS_UNREACHED("NativePgxp_Transform"); }
int NativePgxp_ViewLookup(const u32 words[5], const s32 t[3], double rot[9], double trans[3]) { (void)words, (void)t, (void)rot, (void)trans; HARNESS_UNREACHED("NativePgxp_ViewLookup"); }
void NativePgxp_ViewRegisterInstance(int cameraID, const void *mvp, const void *model, const void *inst, const s32 instPos[3], int translationShift) { (void)cameraID, (void)mvp, (void)model, (void)inst, (void)instPos, (void)translationShift; HARNESS_UNREACHED("NativePgxp_ViewRegisterInstance"); }
void NativePgxp_ViewRegisterFacing(int cameraID, const void *mvp, const void *inst, const s32 instPos[3], int translationShift, int reverse) { (void)cameraID, (void)mvp, (void)inst, (void)instPos, (void)translationShift, (void)reverse; HARNESS_UNREACHED("NativePgxp_ViewRegisterFacing"); }
int NativePgxp_GteGetTranslation(const s32 t[3], double out[3]) { (void)t, (void)out; HARNESS_UNREACHED("NativePgxp_GteGetTranslation"); }

// The rest of the renderer unit links against these. ELF --gc-sections drops
// the code that uses them; PE (MinGW) keeps it, so they are defined here too.
// The matrix builders under test never reach them.
int gNative60FpsEnabled = 0;
NativeConfig g_config;
struct Data data;
u8 *gCTRNativeScratchpadBase;
uint32_t NativeGpuLinks_FromHostPointer(const void *hostPtr) { (void)hostPtr; HARNESS_UNREACHED("NativeGpuLinks_FromHostPointer"); }
b32 INSTANCE_Use60FpsAnimation(struct Instance *inst) { (void)inst; HARNESS_UNREACHED("INSTANCE_Use60FpsAnimation"); }

static void expect(int condition, const char *name)
{
	checks++;
	if (!condition) { printf("FAIL %s\n", name); failures++; }
}

static void identity(MATRIX *m)
{
	memset(m, 0, sizeof(*m));
	m->m[0][0] = m->m[1][1] = m->m[2][2] = 4096;
}

static int nclip(struct InstDrawPerPlayer *idpp, int override)
{
	gte_SetRotMatrix(&idpp->mvp);
	gte_SetTransMatrix(&idpp->mvp);
	gte_SetGeomOffset(160, 120);
	gte_SetGeomScreen(256);
	MTC2(RenderBucket_PackXY(-32, -32), 0); MTC2(0, 1);
	MTC2(RenderBucket_PackXY(32, -32), 2); MTC2(0, 3);
	MTC2(RenderBucket_PackXY(0, 32), 4); MTC2(0, 5);
	gte_rtpt();
	NativeGte_ModelCullFlip = override;
	gte_nclip();
	return (int)MFC2(24);
}

static void fixture(unsigned instFlags, unsigned modelFlags, int orb)
{
	struct GameTracker gt = {0};
	struct InstDrawPerPlayer idpp = {0};
	struct Instance inst = {0};
	struct ModelHeader mh = {0};
	struct ModelFrame frame = {0};
	struct RenderBucketMatrixState scale;
	MATRIX projection;
	int mode, baseline = 0;
	sdata->gGT = &gt;
	identity(&inst.matrix);
	inst.scale.x = inst.scale.y = inst.scale.z = 4096;
	mh.flags = modelFlags;
	mh.scale.x = mh.scale.y = orb ? 11120 : 4096;
	mh.scale.z = orb ? 6748 : 4096;
	for (mode = 0; mode < 2; mode++)
	{
		u32 flags = instFlags;
		int area;
		mirrored = mode;
		identity(&gt.pushBuffer[0].matrix_ViewProj);
		if (mode) gt.pushBuffer[0].matrix_ViewProj.m[0][0] = -4096;
		identity(&idpp.m3x3);
		idpp.mvp.t[0] = 0; idpp.mvp.t[1] = 0; idpp.mvp.t[2] = 4096;
		RenderBucket_BuildM3x3(&inst, &mh, 4096, &scale);
		RenderBucket_StoreMatrixWords(&idpp.m3x3, scale.m0, scale.m1, scale.m2, scale.m3, scale.m4);
		RenderBucket_BuildSplitState(&inst, &mh, &frame, 0, &gt.pushBuffer[0], &idpp,
		                            4096, &flags, &scale, &projection);
		expect(idpp.mirrorCullFlip == (mode && !RenderBucket_NeedsCustomMatrix(flags, &mh)), "builder records mirror contribution");
		area = nclip(&idpp, idpp.mirrorCullFlip);
		expect(area != 0, "representative triangle is nondegenerate");
		if (!mode) baseline = area;
		else expect((area > 0) == (baseline > 0), "front face survives Mirror Mode");
		if (mode && RenderBucket_NeedsCustomMatrix(flags, &mh))
		{
			expect(NativeGte_RotMatrixReversesWinding(), "custom fixed tilt reverses determinant");
			expect((nclip(&idpp, -1) > 0) != (baseline > 0), "old determinant policy reproduces the bug");
		}
		if (!mode) expect(nclip(&idpp, -1) == area, "Mirror Mode off is unchanged");
	}
	// A second player's normal view is never mirrored by the trap.
	identity(&gt.pushBuffer[1].matrix_ViewProj);
	RenderBucket_BuildMvp(&gt.pushBuffer[1], &idpp, &projection);
	expect(!idpp.mirrorCullFlip, "other viewport keeps its cull policy");
	// Split/reflection MVP builders also record the view contribution.
	RenderBucket_BuildSplitViewMvp(&gt.pushBuffer[0], &idpp, &projection);
	expect(idpp.mirrorCullFlip, "split view records mirrored view contribution");
	// Metadata is frozen with the matrix, not re-read from live trap state.
	mirrored = 0;
	expect(idpp.mirrorCullFlip, "queued draw retains metadata after trap changes");
	NativeGte_ModelCullFlip = -1;
	sdata->gGT = 0;
}

int main(void)
{
	fixture(0, 0, 0);                         // normal model
	fixture(CUSTOM_MATRIX, 0, 0);             // custom billboard matrix
	fixture(DRAW_BILLBOARD, 0, 0);            // color flag alone is not a matrix
	fixture(0, RB_MODEL_ALWAYS_POINT_NORTH, 1); // DYNAMIC_WARPBALL (0x36), both LODs
	printf("%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
