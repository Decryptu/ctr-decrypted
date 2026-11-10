// Recycled hazard objects must start at the first authored path point.
// cc -m32 -Wall -Wextra -DCTR_NATIVE -I . -I include -o /tmp/test-snowball-path tools/test-snowball-path.c -lm
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <common.h>
#include "../game/231/RB_Snowball.c"

struct sData sdata_static;
int gNative60FpsEnabled;
static struct GameTracker tracker;
static struct Level level;
static struct Thread thread;
static struct Snowball object;
static int failBirth;

struct Thread *PROC_BirthWithObject(ThreadFlags flags, void *tick, const char *name, struct Thread *parent)
{
    (void)flags; (void)tick; (void)name; (void)parent;
    thread.object = &object;
    return failBirth ? NULL : &thread;
}
void ConvertRotToMatrix(MATRIX *matrix, const SVec3 *rot) { (void)matrix; (void)rot; }
void PlaySound3D_Flags(u32 *flags, u32 sound, struct Instance *inst) { (void)flags; (void)sound; (void)inst; }
void ThTick_FastRET(struct Thread *t) { (void)t; }
struct Instance *LinkedCollide_Radius(struct Instance *inst, struct Thread *t, struct Thread *bucket, u32 radius)
{ (void)inst; (void)t; (void)bucket; (void)radius; return NULL; }
int RB_Hazard_HurtDriver(struct Driver *victim, int damage, struct Driver *attacker, int reason)
{ (void)victim; (void)damage; (void)attacker; (void)reason; return 0; }

int main(void)
{
    struct SpawnPosRot frames[3] = {0};
    struct SpawnType2 path = {0};
    struct Model model = {0};
    struct Instance inst = {0};
    sdata->gGT = &tracker;
    tracker.level1 = &level;
    level.numSpawnType2_PosRot = 1;
    level.ptrSpawnType2_PosRot = &path;
    path.numCoords = 3;
    path.coords.posRot = frames;
    frames[0].pos.x = 100;
    frames[1].pos.x = 200;
    frames[2].pos.x = 300;
    inst.model = &model;
    strcpy(inst.name, "snow0");

    const int models[] = {TEMP_SNOWBALL, DYNAMIC_BARREL};
    for (unsigned m = 0; m < sizeof(models) / sizeof(models[0]); ++m)
    for (int fps60 = 0; fps60 <= 1; ++fps60)
    {
        // Simulate a freed pool slot containing a stale, invalid path cursor.
        memset(&object, 0xa5, sizeof(object));
        inst.thread = NULL;
        model.id = models[m];
        gNative60FpsEnabled = fps60;
        tracker.timer = 0;
        RB_Snowball_LInB(&inst);
        assert(object.pointIndex == 0);
        assert(object.numPoints == 2 && object.snowID == 0);
        assert(thread.inst == &inst && inst.thread == &thread);
        RB_Snowball_ThTick(&thread);
        assert(inst.matrix.t[0] == 100);
        if (fps60) tracker.timer = 1;
        RB_Snowball_ThTick(&thread);
        assert(inst.matrix.t[0] == (fps60 ? 150 : 200));
        // Re-entering birth for a live instance must not restart its path.
        object.pointIndex = 2;
        RB_Snowball_LInB(&inst);
        assert(object.pointIndex == 2);
    }
    failBirth = 1;
    inst.thread = NULL;
    RB_Snowball_LInB(&inst);
    assert(inst.thread == NULL);
    puts("snowball/barrel paths: all assertions passed (30/60 FPS)");
    return 0;
}
