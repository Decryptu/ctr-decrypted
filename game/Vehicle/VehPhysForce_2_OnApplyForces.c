#include <common.h>

static u32 VehPhysForce_OnApplyForces_PackS16Pair(s32 lo, s32 hi)
{
	return ((u32)(u16)lo) | ((u32)(u16)hi << 16);
}

static Vec3 VehPhysForce_OnApplyForces_RotateVector(const MATRIX *m, s16 vx, s16 vy, s16 vz)
{
	Vec3 out;

	// NOTE(aalhendi): Retail loads matrixFacingDir into CP2 color matrix regs
	// and runs opcode 0x4c6012 for this center-offset calculation.
	gte_SetColorMatrix(m);
	MTC2(VehPhysForce_OnApplyForces_PackS16Pair(vx, vy), 0);
	MTC2((u32)(u16)vz, 1);
	gte_mvmva(1, 2, 0, 3, 0);
	out.x = MFC2_S(25);
	out.y = MFC2_S(26);
	out.z = MFC2_S(27);

	return out;
}

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8005ea60-0x8005ebac.
void VehPhysForce_OnApplyForces(struct Thread *thread, struct Driver *driver)
{
	(void)thread;

	const int maxMudSinkYLevel = FP(-1);
	const int maxSpeed = FP8(100);
	driver->speed = min(driver->speed, maxSpeed);

	/* origin of driver model is center-bottom of kart,
	use orientation matrix, and half-radius {0, 25, 0},
	to find the "true" center of the 3D model */
	driver->originToCenter = VehPhysForce_OnApplyForces_RotateVector(&driver->matrixFacingDir, 0, 25, 0);

	VehPhysForce_ConvertSpeedToVecOut(driver, &driver->velocity);

	if ((driver->underDriver) && (driver->underDriver->terrain_type == TERRAIN_MUD))
	{
		if (driver->posCurr.y > maxMudSinkYLevel)
		{
			// sink slower as you approach the mud's bottom
			int sinkSpeed = maxMudSinkYLevel - driver->posCurr.y;
			driver->velocity.y = max(driver->velocity.y, sinkSpeed);
		}
	}

	VehPhysForce_OnGravity(driver, &driver->velocity);

	const SVec3 up = {.x = FP(0), .y = FP(1), .z = FP(0)};
	driver->normalVecUP = up;
	driver->AxisAngle1_normalVec = up;
	driver->unkAA = 0; // driver quadblock flags?
	driver->currBlockTouching = nullptr;

	driver->velocity.x += driver->accel.x;
	driver->velocity.z += driver->accel.z;
	driver->velocity.y += driver->accel.y;
}
