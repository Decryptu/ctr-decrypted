#include <common.h>

enum
{
	VEH_CALC_MIPS_SHIFT_MASK = 0x1f,
	VEH_CALC_FAST_SQRT_ADD_BIT_BIAS = 1,
	VEH_CALC_FAST_SQRT_ROOT_STEP_SHIFT = 2,
};


int VehCalc_InterpBySpeed(int val, int speed, int desired)
{
	if (val > desired)
	{
		val = CTR_MipsSubLo(val, speed);

		if (val < desired)
		{
			return desired;
		}
	}

	else if (val < desired)
	{
		val = CTR_MipsAddLo(val, speed);

		if (val > desired)
		{
			return desired;
		}
	}

	return val;
}

#if CTR_NATIVE_60FPS
// rate * elapsedTimeMS >> 5, the per-frame step of a value moving at `rate`
// units per 32 ms. At 60 FPS the shift drops up to a unit every frame, which
// stalls a slow lerp (1 * 16 >> 5 is 0); the even frame adds what the odd one
// dropped, so each pair of frames moves what one 30 FPS frame would. No state,
// so savestates and replays stay exact.
int VehCalc_FrameStep(int rate, int elapsedMS)
{
	const int step = CTR_MipsSra(CTR_MipsMulLo(rate, elapsedMS), 5);
	if (!CTR_NATIVE_60FPS_ACTIVE || ((sdata->gGT->timer & 1) != 0))
	{
		return step;
	}
	return CTR_MipsSubLo(CTR_MipsSra(CTR_MipsMulLo(rate, CTR_MipsSll(elapsedMS, 1)), 5), step);
}

// The length of (x, y, z), with 8 fractional bits.
int VehCalc_Length8(int x, int y, int z)
{
	u64 n = (u64)((s64)x * x + (s64)y * y + (s64)z * z) << 16;
	u64 root = 0;
	u64 bit = (u64)1 << 62;

	while (bit > n)
	{
		bit >>= 2;
	}
	while (bit != 0)
	{
		if (n >= root + bit)
		{
			n -= root + bit;
			root = (root >> 1) + bit;
		}
		else
		{
			root >>= 1;
		}
		bit >>= 2;
	}
	return (int)root;
}

static int VehCalc_ScaleRounded(int value, s64 num, s64 den)
{
	const s64 product = (s64)value * num;
	return (int)((product >= 0 ? product + den / 2 : product - den / 2) / den);
}

// Every frame retail turns the kart's velocity into other forms and back
// (VehPhysForce_OnApplyForces): speed and angles into a vector, the vector into
// the kart's frame and out again. Each conversion shortens it a little, since
// trig table entries and the rows of matrixMovingDir are a hair under unit
// length and every product is truncated: together about 0.13% of the speed,
// a drag retail applies once per 32 ms frame. Call this after a conversion of
// a vector that measured lengthBefore8 (VehCalc_Length8) going in: a shorter
// frame keeps only its share of the loss, half at 60 FPS, so the drag per
// second stays retail's. A 32 ms frame keeps all of it, bit for bit.
void VehCalc_KeepLossPerFrameTime(Vec3 *v, int lengthBefore8)
{
	const int elapsedMS = sdata->gGT->elapsedTimeMS;
	int lengthAfter8;
	s64 target8;

	if (elapsedMS >= 32)
	{
		return;
	}
	lengthAfter8 = VehCalc_Length8(v->x, v->y, v->z);
	if (lengthAfter8 == 0)
	{
		return;
	}
	target8 = lengthAfter8 + (s64)(lengthBefore8 - lengthAfter8) * (32 - elapsedMS) / 32;
	v->x = VehCalc_ScaleRounded(v->x, target8, lengthAfter8);
	v->y = VehCalc_ScaleRounded(v->y, target8, lengthAfter8);
	v->z = VehCalc_ScaleRounded(v->z, target8, lengthAfter8);
}
#endif

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x80058f9c-0x8005900c.
int VehCalc_MapToRange(int val, int oldMin, int oldMax, int newMin, int newMax)
{
	int distFromBottom;
	int newRange;
	int oldRange;
	int scaledDistance;

	if (val <= oldMin)
	{
		return newMin;
	}

	if (val >= oldMax)
	{
		return newMax;
	}

	distFromBottom = CTR_MipsSubLo(val, oldMin);
	newRange = CTR_MipsSubLo(newMax, newMin);
	oldRange = CTR_MipsSubLo(oldMax, oldMin);
	scaledDistance = CTR_MipsDiv(CTR_MipsMulLo(distFromBottom, newRange), oldRange);

	return CTR_MipsAddLo(newMin, scaledDistance);
}

int VehCalc_SteerAccel(int steeringFrameCount, int stage2FirstFrame, int stage2FrameLength, int stage4FirstFrame, int stage1MinSteer, int stage1MaxSteer)
{
	// Steering Stage 1,
	// if first 4 frames of steering
	// increase steer acceleration as time passes
	if (steeringFrameCount < stage2FirstFrame)
	{
		// map "frame" from [0,4] -> [0x800,0xC00]

		int steerAccel = VehCalc_MapToRange(steeringFrameCount, 0, stage2FirstFrame, stage1MinSteer, stage1MaxSteer);

		CTR_PSX_CLOBBER("$1");
		return steerAccel;
	}

	else
	{
		// Steering Stage 3
		// frames 12+
		// decrease steer acceleration as time passes
		if (CTR_MipsAddLo(stage2FirstFrame, stage2FrameLength) < steeringFrameCount)
		{
			// map "frame" from [12,64] -> [0xC00,0]

			return VehCalc_MapToRange(steeringFrameCount, CTR_MipsAddLo(stage2FirstFrame, stage2FrameLength), stage4FirstFrame, stage1MaxSteer, 0);
		}
	}

	// Steering Stage 2,
	// next 0x8 frames (frame 4 to 12)
	// max steer accel of 0xC00
	return stage1MaxSteer;

	// Steering Stage 4,
	// part of Stage 3's mapping,
	// for all steering after frame 64,
	// steer acceleration is zero, so
	// angular velocity is constant
}

u32 VehCalc_FastSqrt(u32 n, u32 shift)
{
	u32 result;
	s32 rootBitIndex;
	u32 lastApproximation;
	u32 testBit;
	u32 addBit;

	result = 0;
	testBit = (u32)CTR_MipsSll(1, shift);
	rootBitIndex = 1;
	lastApproximation = result;

	while ((testBit < n) && ((testBit << VEH_CALC_FAST_SQRT_ROOT_STEP_SHIFT) != 0))
	{
		rootBitIndex++;
		testBit <<= VEH_CALC_FAST_SQRT_ROOT_STEP_SHIFT;
	}

	addBit = (u32)CTR_MipsSll(1, CTR_MipsAddLo(rootBitIndex, CTR_MipsSubLo(shift, VEH_CALC_FAST_SQRT_ADD_BIT_BIAS)));

	while (testBit != 0)
	{
		u32 shiftedResult;
		u32 approximation;

		shiftedResult = (u32)CTR_MipsSll(result, (u32)rootBitIndex);

		if (rootBitIndex < 0)
		{
			shiftedResult = CTR_MipsSrl(result, (u32)CTR_MipsNegLo(rootBitIndex));
		}

		approximation = shiftedResult + (lastApproximation + testBit);

		if (approximation <= n)
		{
			lastApproximation = approximation;
			result += addBit;
		}

		addBit >>= 1;
		testBit >>= VEH_CALC_FAST_SQRT_ROOT_STEP_SHIFT;
		rootBitIndex--;
	}

	return result;
}
