#include <common.h>

static u32 VehPhysForce_CounterSteer_PackS16Pair(s32 lo, s32 hi)
{
	return ((u32)(u16)lo) | ((u32)(u16)hi << 16);
}

static SVec3 VehPhysForce_CounterSteer_RotateVector(const MATRIX *m, s16 vx, s16 vy, s16 vz)
{
	SVec3 out;
	(void)m;

	// NOTE(aalhendi): PhysTerrainSlope already loaded matrixMovingDir into GTE
	// rotation regs, matching retail before VehPhysForce_CounterSteer runs.
	MTC2(VehPhysForce_CounterSteer_PackS16Pair(vx, vy), 0);
	MTC2((u32)(u16)vz, 1);
	gte_mvmva(1, 0, 0, 3, 0);
	out.x = (s16)MFC2(25);
	out.y = (s16)MFC2(26);
	out.z = (s16)MFC2(27);

	return out;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8005fb4c-0x8005fc8c.
void VehPhysForce_CounterSteer(struct Driver *driver)
{
	driver->accel.x = 0;
	driver->accel.y = 0;
	driver->accel.z = 0;

	int speedApprox = abs(driver->speedApprox);
	if (speedApprox <= FP8(3) || driver->actionsFlagSet & ACTION_WARP || driver->kartState == KS_CRASHING || driver->set_0xF0_OnWallRub ||
	    !(driver->actionsFlagSet & ACTION_TOUCH_GROUND) || driver->terrainMeta1->counterSteerRatio == 0)
	{
		return;
	}

	int angleLimit = (u8)driver->angleMaxCounterSteer;
	int angle = driver->turnAngleCurr - driver->turnAnglePrev;
	if (angle > angleLimit)
	{
		angle = angleLimit;
	}
	else if (angle < -angleLimit)
	{
		angle = -angleLimit;
	}

	int sine = MATH_Sin(angle);
	int counterSteerStrength = (driver->terrainMeta1->counterSteerRatio * -8000) >> 8;
	SVec3 accel = VehPhysForce_CounterSteer_RotateVector(&driver->matrixMovingDir, (s16)((counterSteerStrength * sine) >> 12), 0, 0);

	driver->accel = accel;
}
