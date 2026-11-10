// Exercise the production driver-constant loader with both Penta styles.
// cc -m32 -std=c17 -DCTR_NATIVE -DCTR_AP -DCTR_INTERNAL -I . -I include -I ap -msse -O2 -fwhole-program -ffunction-sections -fdata-sections -Wl,--gc-sections -o /tmp/test-penta tools/test-penta.c -lm
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "ap/ap_hooks.h"
#include "ap/ap_charswap.h"
#include "game/Vehicle/VehBirth.c"

struct Data data;
struct sData sdata_static;
NativeConfig g_config;
int gNative60FpsEnabled;
static int customRacer = -1;
static int customEngine = ACCEL;
static int seedActive;
static int roomStats;
static int applyPackage;
static int speedClass;

int NativeRacers_Driver(int driverID) { (void)driverID; return customRacer; }
int NativeRacers_DriverEngine(int driverID, int engine)
{
	(void)driverID;
	return customRacer < 0 ? engine : customEngine;
}
int ctr_cfg_active(void) { return seedActive; }
int AP_CapabilityRoomStatsRace(void) { return roomStats; }
void AP_CharSwap_ApplyStatPackage(struct Driver *d)
{
	if (applyPackage)
		d->const_Speed_ClassStat = 7777;
}
void SpeedClass_ApplyConsts(struct Driver *d)
{
	if (speedClass)
		d->const_Speed_ClassStat = d->const_Speed_ClassStat * 6 / 5;
}

static void row(int index, int offset, int size, int turning, int accel)
{
	struct MetaPhys *m = &data.metaPhys[index];
	m->offset = offset;
	m->size = size;
	for (int i = 0; i < NUM_CLASSES; i++) m->value[i] = turning;
	m->value[ACCEL] = accel;
}

int main(void)
{
	struct Driver d;
	memset(&d, 0, sizeof d);
	data.characterIDs[0] = PENTA_PENGUIN;
	data.MetaDataCharacters[PENTA_PENGUIN].engineID = TURN;
	data.MetaDataCharacters[POLAR].engineID = TURN;
	row(ACCEL_CLASS_STAT, ACCEL_CLASS_STAT_OFFSET, 2, 512, 544);
	row(SPEED_CLASS_STAT, SPEED_CLASS_STAT_OFFSET, 2, 12950, 13520);
	row(ACCEL_SPEED_CLASS_STAT, ACCEL_SPEED_CLASS_STAT_OFFSET, 2, 14450, 15020);
	row(TURN_RATE, TURN_RATE_OFFSET, 1, 30, 26);
	row(DRIFT_TURN_BASE, DRIFT_TURN_BASE_OFFSET, 1, 18, 10);
	row(PROTOTYPE_KEY, PROTOTYPE_KEY_OFFSET, 4, 15, 15);
	row(STEER_VEL_DRIFT_SWITCH_WAY, STEER_VEL_DRIFT_SWITCH_WAY_OFFSET, 1, -10, -10);

	VehBirth_SetConsts(&d);
	assert(d.const_Accel_ClassStat == 512 && d.const_Speed_ClassStat == 12950);
	assert(d.const_AccelSpeed_ClassStat == 14450 && d.const_TurnRate == 30);
	g_config.pentaStats = PENTA_STATS_PAL;
	VehBirth_SetConsts(&d);
	assert(d.const_Accel_ClassStat == 544 && d.const_Speed_ClassStat == 13900);
	assert(d.const_AccelSpeed_ClassStat == 15400 && d.const_TurnRate == 30);
	assert(d.const_DriftTurnBase == 18);
	assert(*(signed char *)((unsigned char *)&d + STEER_VEL_DRIFT_SWITCH_WAY_OFFSET) == -10);
	int key;
	memcpy(&key, (unsigned char *)&d + PROTOTYPE_KEY_OFFSET, sizeof key);
	assert(key == 15);
	d.driverID = 3;
	data.characterIDs[3] = PENTA_PENGUIN;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 13900); // Bots get the same retail Penta style.
	d.driverID = 0;

	// Every non-PAL row, including signed byte and four-byte values, is untouched.
	for (int i = 0; i < 65; i++)
	{
		if (i == 9 || i == 11 || i == 12) continue;
		assert(Penta_PhysValue(1, i, -128) == -128);
		assert(Penta_PhysValue(0, i, 12345) == 12345);
	}
	data.characterIDs[0] = POLAR;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 12950);
	data.characterIDs[0] = PENTA_PENGUIN;
	customRacer = 0;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 13520 && d.const_TurnRate == 26);
	customRacer = -1;
	seedActive = roomStats = 1;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 12950);
	roomStats = 0; // A connected seed does not govern Arcade/TT/VS/Battle.
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 13900);
	seedActive = 0;
	roomStats = 1; // Unconnected Adventure uses the local choice.
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 13900);
	applyPackage = 1;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 7777);
	applyPackage = 0;
	speedClass = 1;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 16680);
	speedClass = 0;
	g_config.pentaStats = -1;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 12950);
	g_config.pentaStats = 2;
	VehBirth_SetConsts(&d);
	assert(d.const_Speed_ClassStat == 12950);
	puts("Penta: production loader, custom racers, room ownership and 200cc passed");
	return 0;
}
