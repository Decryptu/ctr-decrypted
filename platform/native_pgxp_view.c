// Precise view for PGXP: the camera the game renders with, without the
// integer rounding of its position and angles. See docs/PGXP.md#precise-camera.
//
// The game keeps its integer camera for everything it decides (culling, LOD,
// audio, collision). This file only changes where vertices are drawn:
// - CAM_FollowDriver_Normal hands over the same camera computed in floating
//   point (NativePgxp_ViewFollowDriver).
// - PushBuffer_SetMatrixVP registers the precise view matrix next to the
//   integer one it built (NativePgxp_ViewRegisterCamera), and the instance
//   renderer registers each model's precise model-view matrix
//   (NativePgxp_ViewRegisterInstance).
// - The GTE projects with the precise matrix whenever the integer one it was
//   given is a registered one (NativePgxp_ViewLookup), keyed by value like the
//   rest of PGXP.
#include <macros.h>
#include "platform/native_pgxp.h"

#include <math.h>
#include <string.h>

#define NATIVE_PGXP_VIEW_CAMERAS        4
#define NATIVE_PGXP_VIEW_INSTANCE_BITS  11
#define NATIVE_PGXP_VIEW_INSTANCE_COUNT (1u << NATIVE_PGXP_VIEW_INSTANCE_BITS)
#define NATIVE_PGXP_VIEW_INSTANCE_PROBE 8
#define NATIVE_PGXP_VIEW_ANGLE          (6.283185307179586 / 4096.0)

typedef struct
{
	// The follow camera's precise pose, valid while the PushBuffer still holds
	// the integer pose it was computed with.
	b32 hasPose;
	SVec3 intPos;
	SVec3 intRot;
	double pos[3];
	double rot[3];

	// Float twin of the PushBuffer position deadband (pushBufferPosCorrection).
	b32 hasDeadband;
	Vec3 deadbandIntPos;
	double deadbandPos[3];
	double deadband[3];

	// The registered view: the integer ViewProj rotation words are the key.
	u32 viewGeneration;
	u32 words[5];
	double view[9];      // precise ViewProj rotation, 4096 = 1
	double fromInt[9];   // precise view of an integer view-space vector
	double offset[3];    // precise view of (integer camera - precise camera)
	double camPos[3];    // precise camera position
} NativePgxpViewCamera;

typedef struct
{
	u32 key[8];
	u32 generation;
	double rot[9];
	double trans[3];
} NativePgxpViewInstance;

global_variable NativePgxpViewCamera s_viewCameras[NATIVE_PGXP_VIEW_CAMERAS];
global_variable NativePgxpViewInstance s_viewInstances[NATIVE_PGXP_VIEW_INSTANCE_COUNT];
global_variable u32 s_viewGeneration = 1;

// One-entry cache: the GTE projects many vertices with the same matrix.
global_variable u32 s_viewCacheKey[8];
global_variable u32 s_viewCacheGeneration;
global_variable b32 s_viewCacheHit;
global_variable double s_viewCacheRot[9];
global_variable double s_viewCacheTrans[3];

// ConvertRotToMatrix in floating point: Ry * Rx * Rz, angles in 4096ths of a
// turn, unit scale.
internal void NativePgxpView_RotToMatrix(const double rot[3], double m[9])
{
	const double sx = sin(rot[0] * NATIVE_PGXP_VIEW_ANGLE), cx = cos(rot[0] * NATIVE_PGXP_VIEW_ANGLE);
	const double sy = sin(rot[1] * NATIVE_PGXP_VIEW_ANGLE), cy = cos(rot[1] * NATIVE_PGXP_VIEW_ANGLE);
	const double sz = sin(rot[2] * NATIVE_PGXP_VIEW_ANGLE), cz = cos(rot[2] * NATIVE_PGXP_VIEW_ANGLE);
	const double ry[9] = {cy, 0, sy, 0, 1, 0, -sy, 0, cy};
	const double rx[9] = {1, 0, 0, 0, cx, -sx, 0, sx, cx};
	const double rz[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 1};
	double yx[9];

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			yx[i * 3 + j] = ry[i * 3] * rx[j] + ry[i * 3 + 1] * rx[3 + j] + ry[i * 3 + 2] * rx[6 + j];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			m[i * 3 + j] = yx[i * 3] * rz[j] + yx[i * 3 + 1] * rz[3 + j] + yx[i * 3 + 2] * rz[6 + j];
}

internal void NativePgxpView_Mul3(const double a[9], const double b[9], double out[9])
{
	double r[9];

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
	memcpy(out, r, sizeof(r));
}

internal void NativePgxpView_Apply(const double m[9], const double v[3], double out[3])
{
	double r[3];

	for (int i = 0; i < 3; i++)
		r[i] = m[i * 3] * v[0] + m[i * 3 + 1] * v[1] + m[i * 3 + 2] * v[2];
	memcpy(out, r, sizeof(r));
}

internal b32 NativePgxpView_Invert(const double m[9], double out[9])
{
	const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);

	if (fabs(det) < 1e-9)
		return 0;

	out[0] = (m[4] * m[8] - m[5] * m[7]) / det;
	out[1] = (m[2] * m[7] - m[1] * m[8]) / det;
	out[2] = (m[1] * m[5] - m[2] * m[4]) / det;
	out[3] = (m[5] * m[6] - m[3] * m[8]) / det;
	out[4] = (m[0] * m[8] - m[2] * m[6]) / det;
	out[5] = (m[2] * m[3] - m[0] * m[5]) / det;
	out[6] = (m[3] * m[7] - m[4] * m[6]) / det;
	out[7] = (m[1] * m[6] - m[0] * m[7]) / det;
	out[8] = (m[0] * m[4] - m[1] * m[3]) / det;
	return 1;
}

// A GTE matrix's five packed words as nine s16 entries.
internal void NativePgxpView_Unpack(const u32 words[5], double m[9])
{
	m[0] = (s16)words[0];
	m[1] = (s16)(words[0] >> 16);
	m[2] = (s16)words[1];
	m[3] = (s16)(words[1] >> 16);
	m[4] = (s16)words[2];
	m[5] = (s16)(words[2] >> 16);
	m[6] = (s16)words[3];
	m[7] = (s16)(words[3] >> 16);
	m[8] = (s16)words[4];
}

// The five packed rotation words, the last one (a lone R33) sign-extended as
// the GTE register holds it, whatever padding sits in memory after it.
internal void NativePgxpView_ReadWords(const void *m, u32 words[5])
{
	for (int i = 0; i < 5; i++)
		words[i] = CTR_ReadU32LE((const char *)m + i * 4);
	words[4] = (u32)(s32)(s16)words[4];
}

internal s32 NativePgxpView_ReadTranslation(const void *m, int axis)
{
	return (s32)CTR_ReadU32LE((const char *)m + 20 + axis * 4);
}

internal double NativePgxpView_WrapAngle(double a)
{
	return a - 4096.0 * floor((a + 2048.0) / 4096.0);
}

void NativePgxp_ViewFollowDriver(int cameraID, const struct NativePgxpFollowCamera *in, const SVec3 *pbPos, const SVec3 *pbRot)
{
	if ((cameraID < 0) || (cameraID >= NATIVE_PGXP_VIEW_CAMERAS))
		return;

	NativePgxpViewCamera *cam = &s_viewCameras[cameraID];
	cam->hasPose = 0;

	// The orbit offset and side offset, as the GTE computes them but unrounded.
	double orbit[9];
	const double orbitRot[3] = {in->orbitRot.x, in->orbitRot.y, in->orbitRot.z};
	NativePgxpView_RotToMatrix(orbitRot, orbit);

	double distance = in->distMax;
	if (in->moveSpeed <= in->speedMin)
		distance = in->distMin;
	else if (in->moveSpeed < in->speedMax)
		distance = in->distMin + (double)(in->moveSpeed - in->speedMin) * (in->distMax - in->distMin) / (double)(in->speedMax - in->speedMin);
	distance += in->distanceOffset / 256.0;

	double pos[3], delta[3];
	for (int i = 0; i < 3; i++)
	{
		const double side = (i == 1) ? 0.0 : orbit[i * 3 + 1] * 64.0;
		delta[i] = in->driverPos[i] + side;
		pos[i] = orbit[i * 3 + 2] * distance + in->driverPos[i] + side;
	}
	pos[1] = in->maskGrabY ? in->basePos.y : pos[1] + in->vertDistance;

	double heading[9];
	const double headingRot[3] = {in->headingRot.x, in->headingRot.y, in->headingRot.z};
	NativePgxpView_RotToMatrix(headingRot, heading);
	for (int i = 0; i < 3; i++)
		delta[i] += heading[i * 3 + 2] * in->damagePitch;
	delta[1] += in->lookAtHeight;

	// What rounding lost, carried onto the camera after the game's own
	// adjustments (blast lerp, terrain floor, height smoothing).
	const double basePos[3] = {in->basePos.x, in->basePos.y, in->basePos.z};
	const double baseDelta[3] = {in->baseDelta.x, in->baseDelta.y, in->baseDelta.z};
	const double finalPos[3] = {in->finalPos.x, in->finalPos.y, in->finalPos.z};
	const double finalDelta[3] = {in->finalDelta.x, in->finalDelta.y, in->finalDelta.z};
	double camPos[3], lookAt[3], dir[3];
	for (int i = 0; i < 3; i++)
	{
		const double posResidual = pos[i] - basePos[i];
		const double deltaResidual = delta[i] - baseDelta[i];

		// Each point sums two GTE results the game floors, so up to 2 units.
		// More means the float model and the game disagree.
		if ((fabs(posResidual) > 4.0) || (fabs(deltaResidual) > 4.0))
			return;
		camPos[i] = finalPos[i] + posResidual;
		lookAt[i] = finalDelta[i] + deltaResidual;
		dir[i] = camPos[i] - lookAt[i];
	}

	double rot[3];
	rot[1] = atan2(dir[0], dir[2]) / NATIVE_PGXP_VIEW_ANGLE;
	rot[0] = 2048.0 - atan2(dir[1], sqrt(dir[0] * dir[0] + dir[2] * dir[2])) / NATIVE_PGXP_VIEW_ANGLE;
	rot[2] = in->roll;

	const double intRot[3] = {pbRot->x, pbRot->y, pbRot->z};
	for (int i = 0; i < 3; i++)
	{
		rot[i] = intRot[i] + NativePgxpView_WrapAngle(rot[i] - intRot[i]);
		if (fabs(rot[i] - intRot[i]) > 16.0)
			return;
	}

	// The PushBuffer position trails the camera through a deadband of +-2
	// units (pushBufferPosCorrection); run the same deadband unrounded. It
	// resynchronises whenever the game's camera did not continue from the
	// position this one last saw.
	const b32 continues = cam->hasDeadband && (cam->deadbandIntPos.x == in->prevPos.x) && (cam->deadbandIntPos.y == in->prevPos.y) &&
	                      (cam->deadbandIntPos.z == in->prevPos.z);
	const double intCorrection[3] = {in->correction.x, in->correction.y, in->correction.z};
	for (int i = 0; i < 3; i++)
	{
		double c = intCorrection[i];
		if (continues)
		{
			c = cam->deadband[i] - (camPos[i] - cam->deadbandPos[i]);
			c = (c > 2.0) ? 2.0 : ((c < -2.0) ? -2.0 : c);
			if (fabs(c - intCorrection[i]) > 3.0)
				c = intCorrection[i];
		}
		cam->deadband[i] = c;
		cam->deadbandPos[i] = camPos[i];
	}
	cam->deadbandIntPos = in->finalPos;
	cam->hasDeadband = 1;

	for (int i = 0; i < 3; i++)
		cam->pos[i] = camPos[i] + cam->deadband[i];
	memcpy(cam->rot, rot, sizeof(rot));
	cam->intPos = *pbPos;
	cam->intRot = *pbRot;
	cam->hasPose = 1;
}

int NativePgxp_ViewGetPose(int cameraID, double pos[3], double rot[3])
{
	if ((cameraID < 0) || (cameraID >= NATIVE_PGXP_VIEW_CAMERAS) || !s_viewCameras[cameraID].hasPose)
		return 0;
	memcpy(pos, s_viewCameras[cameraID].pos, sizeof(s_viewCameras[cameraID].pos));
	memcpy(rot, s_viewCameras[cameraID].rot, sizeof(s_viewCameras[cameraID].rot));
	return 1;
}

void NativePgxp_ViewForgetCamera(int cameraID)
{
	if ((cameraID >= 0) && (cameraID < NATIVE_PGXP_VIEW_CAMERAS))
	{
		s_viewCameras[cameraID].hasPose = 0;
		s_viewCameras[cameraID].hasDeadband = 0;
	}
}

void NativePgxp_ViewRegisterCamera(int cameraID, const SVec3 *pbPos, const SVec3 *pbRot, const void *viewProj)
{
	if ((cameraID < 0) || (cameraID >= NATIVE_PGXP_VIEW_CAMERAS))
		return;

	NativePgxpViewCamera *cam = &s_viewCameras[cameraID];
	cam->viewGeneration = 0;

	if (!NATIVE_PGXP_GEOMETRY_ACTIVE() || !cam->hasPose)
		return;
	if ((cam->intPos.x != pbPos->x) || (cam->intPos.y != pbPos->y) || (cam->intPos.z != pbPos->z) || (cam->intRot.x != pbRot->x) ||
	    (cam->intRot.y != pbRot->y) || (cam->intRot.z != pbRot->z))
		return;

	// The integer ViewProj is the transposed camera rotation with each row
	// scaled (aspect, widescreen) and possibly negated (Archipelago mirror and
	// upside-down traps). Measure each row's factor against the float model of
	// the integer angles, and apply it to the precise rotation.
	u32 words[5];
	double intView[9], intModel[9], preciseModel[9];
	const double intAngles[3] = {pbRot->x, pbRot->y, pbRot->z};
	NativePgxpView_ReadWords(viewProj, words);
	NativePgxpView_Unpack(words, intView);
	NativePgxpView_RotToMatrix(intAngles, intModel);
	NativePgxpView_RotToMatrix(cam->rot, preciseModel);

	for (int row = 0; row < 3; row++)
	{
		double dot = 0.0;
		for (int k = 0; k < 3; k++)
			dot += intView[row * 3 + k] * intModel[k * 3 + row];
		// intModel is unit scale, so this is the row's 4096-scale factor.
		const double factor = dot;
		for (int k = 0; k < 3; k++)
		{
			cam->view[row * 3 + k] = factor * preciseModel[k * 3 + row];
			// A row that is not a scaled camera axis: leave this view integer.
			if (fabs(factor * intModel[k * 3 + row] - intView[row * 3 + k]) > 24.0)
				return;
		}
	}

	double intUnit[9], intInverse[9], viewUnit[9];
	for (int i = 0; i < 9; i++)
	{
		intUnit[i] = intView[i] / 4096.0;
		viewUnit[i] = cam->view[i] / 4096.0;
	}
	if (!NativePgxpView_Invert(intUnit, intInverse))
		return;

	const double shift[3] = {pbPos->x - cam->pos[0], pbPos->y - cam->pos[1], pbPos->z - cam->pos[2]};
	NativePgxpView_Mul3(viewUnit, intInverse, cam->fromInt);
	NativePgxpView_Apply(viewUnit, shift, cam->offset);
	memcpy(cam->camPos, cam->pos, sizeof(cam->pos));
	memcpy(cam->words, words, sizeof(words));
	cam->viewGeneration = s_viewGeneration;
}

internal u32 NativePgxpView_Hash(const u32 key[8])
{
	u32 h = 2166136261u;
	for (int i = 0; i < 8; i++)
		h = (h ^ key[i]) * 16777619u;
	return h;
}

void NativePgxp_ViewRegisterInstance(int cameraID, const void *mvp, const void *model, const s32 instPos[3], int translationShift)
{
	if ((cameraID < 0) || (cameraID >= NATIVE_PGXP_VIEW_CAMERAS))
		return;

	const NativePgxpViewCamera *cam = &s_viewCameras[cameraID];
	if (cam->viewGeneration != s_viewGeneration)
		return;

	u32 key[8];
	u32 modelWords[5];
	double modelMatrix[9];
	NativePgxpView_ReadWords(mvp, key);
	for (int i = 0; i < 3; i++)
		key[5 + i] = (u32)NativePgxpView_ReadTranslation(mvp, i);
	NativePgxpView_ReadWords(model, modelWords);
	NativePgxpView_Unpack(modelWords, modelMatrix);

	// The integer composite is ViewProj * model >> 12; the precise one uses
	// the precise view, and the instance position from the precise camera.
	double viewUnit[9], rot[9], trans[3];
	for (int i = 0; i < 9; i++)
		viewUnit[i] = cam->view[i] / 4096.0;
	NativePgxpView_Mul3(viewUnit, modelMatrix, rot);
	const double rel[3] = {instPos[0] - cam->camPos[0], instPos[1] - cam->camPos[1], instPos[2] - cam->camPos[2]};
	NativePgxpView_Apply(viewUnit, rel, trans);
	// The two differ by the camera's sub-unit shift and a fraction of an angle
	// unit, which grows with distance; anything more is not this instance.
	const double scale = ldexp(1.0, translationShift);
	for (int i = 0; i < 3; i++)
	{
		trans[i] *= scale;
		if (fabs(trans[i] - (double)(s32)key[5 + i]) > 8.0 * scale + 2.0 + 0.02 * fabs(trans[2]))
			return;
	}

	const u32 hash = NativePgxpView_Hash(key);
	for (u32 probe = 0; probe < NATIVE_PGXP_VIEW_INSTANCE_PROBE; probe++)
	{
		NativePgxpViewInstance *slot = &s_viewInstances[(hash + probe) & (NATIVE_PGXP_VIEW_INSTANCE_COUNT - 1)];
		if ((slot->generation == s_viewGeneration) && (memcmp(slot->key, key, sizeof(key)) != 0))
			continue;
		memcpy(slot->key, key, sizeof(key));
		memcpy(slot->rot, rot, sizeof(rot));
		memcpy(slot->trans, trans, sizeof(trans));
		slot->generation = s_viewGeneration;
		return;
	}
}

int NativePgxp_ViewLookup(const u32 words[5], const s32 t[3], double rot[9], double trans[3])
{
	u32 key[8];
	memcpy(key, words, 5 * sizeof(u32));
	key[5] = (u32)t[0];
	key[6] = (u32)t[1];
	key[7] = (u32)t[2];

	if ((s_viewCacheGeneration != s_viewGeneration) || (memcmp(key, s_viewCacheKey, sizeof(key)) != 0))
	{
		memcpy(s_viewCacheKey, key, sizeof(key));
		s_viewCacheGeneration = s_viewGeneration;
		s_viewCacheHit = 0;

		const u32 hash = NativePgxpView_Hash(key);
		for (u32 probe = 0; probe < NATIVE_PGXP_VIEW_INSTANCE_PROBE; probe++)
		{
			const NativePgxpViewInstance *slot = &s_viewInstances[(hash + probe) & (NATIVE_PGXP_VIEW_INSTANCE_COUNT - 1)];
			if (slot->generation != s_viewGeneration)
				break;
			if (memcmp(slot->key, key, sizeof(key)) == 0)
			{
				memcpy(s_viewCacheRot, slot->rot, sizeof(s_viewCacheRot));
				memcpy(s_viewCacheTrans, slot->trans, sizeof(s_viewCacheTrans));
				s_viewCacheHit = 1;
				break;
			}
		}

		// The camera's own rotation, with any translation: geometry placed
		// relative to the integer camera (the level, the sky, wheels, shadows).
		for (int i = 0; !s_viewCacheHit && (i < NATIVE_PGXP_VIEW_CAMERAS); i++)
		{
			const NativePgxpViewCamera *cam = &s_viewCameras[i];
			if ((cam->viewGeneration != s_viewGeneration) || (memcmp(cam->words, words, sizeof(cam->words)) != 0))
				continue;
			const double intTrans[3] = {t[0], t[1], t[2]};
			NativePgxpView_Apply(cam->fromInt, intTrans, s_viewCacheTrans);
			for (int k = 0; k < 3; k++)
				s_viewCacheTrans[k] += cam->offset[k];
			memcpy(s_viewCacheRot, cam->view, sizeof(s_viewCacheRot));
			s_viewCacheHit = 1;
		}
	}

	if (!s_viewCacheHit)
		return 0;
	memcpy(rot, s_viewCacheRot, sizeof(s_viewCacheRot));
	memcpy(trans, s_viewCacheTrans, sizeof(s_viewCacheTrans));
	return 1;
}

void NativePgxp_ViewEndFrame(void)
{
	s_viewGeneration++;
	if (s_viewGeneration == 0)
		s_viewGeneration = 1;
}
