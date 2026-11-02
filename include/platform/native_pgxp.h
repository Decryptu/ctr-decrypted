#ifndef NATIVE_PGXP_H
#define NATIVE_PGXP_H

#include <macros.h>
#include <ctr_math.h>
#include <stddef.h>

enum NativePgxpMode
{
	NATIVE_PGXP_MODE_OFF,
	// Sub-pixel vertex positions, retail affine texture mapping.
	NATIVE_PGXP_MODE_GEOMETRY,
	// Sub-pixel vertex positions and perspective-correct texture mapping.
	NATIVE_PGXP_MODE_FULL,
	NATIVE_PGXP_MODE_TEXTURES,
	NATIVE_PGXP_MODE_COUNT,
};

static inline int NativePgxp_NormalizeMode(int mode)
{
	if (mode < NATIVE_PGXP_MODE_OFF || mode >= NATIVE_PGXP_MODE_COUNT)
		return NATIVE_PGXP_MODE_OFF;
	return mode;
}

extern int gNativePgxpMode;
extern int gNativePgxpIntegerNclipEnabled;

// Copy the persisted option (g_config.pgxp) into gNativePgxpMode. Called at
// the frame boundary so a mode never changes halfway through a frame.
void NativePgxp_SyncConfig(void);

typedef struct
{
	float x;
	float y;
	// View-space depth; <= 0 when the projection has no usable depth.
	float w;
	// Retail packed SXY (x low, y high) this vertex was truncated to.
	u32 value;
} NativePgxpVertex;

#define NATIVE_PGXP_ACTIVE() (gNativePgxpMode != NATIVE_PGXP_MODE_OFF)
#define NATIVE_PGXP_GEOMETRY_ACTIVE() (gNativePgxpMode == NATIVE_PGXP_MODE_GEOMETRY || gNativePgxpMode == NATIVE_PGXP_MODE_FULL)
#define NATIVE_PGXP_TEXTURES_ACTIVE() (gNativePgxpMode == NATIVE_PGXP_MODE_TEXTURES || gNativePgxpMode == NATIVE_PGXP_MODE_FULL)
#define NATIVE_VERTEX_TRACKING_ACTIVE() NATIVE_PGXP_GEOMETRY_ACTIVE()

void NativePgxp_SetPrimRegion(const void *start, size_t size);

// GTE side: mirror of the SXY0..SXY2 FIFO.
void NativePgxp_GteProject(float x, float y, float w, u32 value);
// Fast GTE TEXTURES path: projected SXY is retail; only SZ3 is required.
void NativePgxp_GteProjectTexture(u32 value, float w);
void NativePgxp_GteWriteSXY(int reg, u32 value);
void NativePgxp_GteReadSXY(int reg, u32 value);

void NativePgxp_StoreGteSXY(const void *dst, int reg, u32 value);
void NativePgxp_CopyXY(const void *dst, const void *src, u32 value);
// A screen position the game computed itself (no GTE), unrounded, for the
// packed SXY value it stores at dst.
void NativePgxp_SetXY(const void *dst, float x, float y, u32 value);

void NativePgxp_SetPosition(const void *key, const s16 *vector, const float *precise);
void NativePgxp_ClearPosition(const void *key);
// Returns 1 with the exact position, or 0 with `vector` converted to float.
int NativePgxp_GetPosition(const void *key, const s16 *vector, float *out);
void NativePgxp_CopyPosition(const void *dstKey, const void *srcKey, const s16 *vector);

void NativePgxp_GteLoadSXY(int reg, const void *src, u32 value);
// NCLIP from the precise SXY0..SXY2; returns 0 when any is unknown.
int NativePgxp_GteNclip(u32 sxy0, u32 sxy1, u32 sxy2, s64 *out);

void NativePgxp_GteInvalidateInput(int reg);
void NativePgxp_GteSetInput(int slot, const s16 *vector, const float *precise);
int NativePgxp_GteGetInput(int slot, s16 vx, s16 vy, s16 vz, double *out);
// Exact MAC1..MAC3 of the last MVMVA, matched against the IR1..IR3 it produced.
void NativePgxp_GteSetMvmvaResult(const double *precise, s16 ir1, s16 ir2, s16 ir3);
int NativePgxp_GteGetMvmvaResult(const s16 *ir, float *out);
// The exact value of a GTE translation (TR) the game loaded from a result it
// read back, such as a particle's view-space centre; projections through that
// TR and a rotation with no precise view use it. Cleared at the frame end.
void NativePgxp_GteSetTranslation(const s32 t[3], const float precise[3]);
void NativePgxp_GteClearTranslation(void);
int NativePgxp_GteGetTranslation(const s32 t[3], double out[3]);

void NativePgxp_Transform(const double *rotation, const double *translation, const double *input, double *result);

// Precise view (platform/native_pgxp_view.c, docs/PGXP.md#precise-camera).

// CAM_FollowDriver_Normal's inputs and integer results, for the same camera
// computed without rounding.
struct NativePgxpFollowCamera
{
	SVec3 orbitRot;     // rotation the orbit offset is taken in
	SVec3 headingRot;   // rotation the damage pitch offset is taken in
	int moveSpeed;      // cameraMoveSpeed, mapped to the orbit distance
	int speedMin, speedMax, distMin, distMax;
	int distanceOffset; // fire speed zoom, 8 fractional bits
	int driverPos[3];   // driver position, world units
	double driverFrac[3]; // and the fraction the game drops from it, 0..1
	const void *driverInst; // the driver's Instance, drawn at the exact position
	b32 maskGrabY;      // camera height set by the mask grab
	int vertDistance;
	int damagePitch;
	int lookAtHeight;
	double roll;
	Vec3 basePos;       // camera and look-at points before the game's adjustments
	Vec3 baseDelta;
	Vec3 finalPos;      // and after them
	Vec3 finalDelta;
	Vec3 prevPos;       // cameraPos before this frame
	Vec3 correction;    // pushBufferPosCorrection after this frame
};

void NativePgxp_ViewFollowDriver(int cameraID, const struct NativePgxpFollowCamera *in, const SVec3 *pbPos, const SVec3 *pbRot);
void NativePgxp_ViewForgetCamera(int cameraID);
// The follow camera's precise pose, for the race trace; 0 when it has none.
int NativePgxp_ViewGetPose(int cameraID, double pos[3], double rot[3]);
// The precise angles of the view registered this frame, for screen-space
// effects placed from the camera's angles; 0 when the view is integer.
int NativePgxp_ViewGetAngles(int cameraID, double rot[3]);
// Matrices are GTE MATRIX layouts: five packed rotation words, then t[3].
void NativePgxp_ViewRegisterCamera(int cameraID, const SVec3 *pbPos, const SVec3 *pbRot, const void *viewProj);
void NativePgxp_ViewRegisterInstance(int cameraID, const void *mvp, const void *model, const void *inst, const s32 instPos[3], int translationShift);
// A camera-facing model (RenderBucket_BuildCustomMatrix): its integer matrix
// turns it towards its integer view position; the precise one turns it towards
// the precise position.
void NativePgxp_ViewRegisterFacing(int cameraID, const void *mvp, const void *inst, const s32 instPos[3], int translationShift, int reverse);
// Between these, vectors projected through a camera's rotation are positions
// the game took relative to the integer camera and scaled by 1 << shift
// (tires, shadows, skid marks, particles). The precise view adds the camera's
// sub-unit shift at that scale, and for `inst`, when it is the kart a camera
// follows, the fraction of a unit the kart is drawn at (without its height
// unless withHeight).
void NativePgxp_ViewBeginRelative(int shift, const void *inst, int withHeight);
void NativePgxp_ViewEndRelative(void);
// The same offset in world units, for code that shapes such geometry from the
// camera's direction itself (the tires); 0 when the camera's view is integer.
int NativePgxp_ViewRelativeShift(int cameraID, const void *inst, int withHeight, double shift[3]);
// The precise rotation (4096 = 1) and translation (view units) to use instead
// of a GTE matrix and translation, or 0 when there is none.
int NativePgxp_ViewLookup(const u32 words[5], const s32 t[3], double rot[9], double trans[3]);
void NativePgxp_ViewEndFrame(void);

// GPU side.
int NativePgxp_Lookup(const void *addr, u32 value, NativePgxpVertex *out);
void NativePgxp_EndFrame(void);

#endif
