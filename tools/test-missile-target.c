// Exercise target selection with the real native GTE and inverse transform.
// cc -m32 -O2 -fwhole-program -ffunction-sections -fdata-sections -Wl,--gc-sections -DCTR_NATIVE -I . -I include -o /tmp/test-missile-target tools/test-missile-target.c -lm
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "game/Vehicle/VehPickupItem.c"
#include "game/MATH/MATH_3_HitboxMatrix.c"
#include "platform/native_gte_core.c"
#include "platform/native_inline_c.c"
#include "platform/native_libgte.c"

int gNativePgxpMode;
int NativePgxp_ViewLookup(const u32 words[5], const s32 t[3], double rot[9], double trans[3])
{ (void)words; (void)t; (void)rot; (void)trans; return 0; }
int NativePgxp_GteGetTranslation(const s32 t[3], double out[3])
{ (void)t; (void)out; return 0; }
struct sData sdata_static;
static struct GameTracker tracker;
static struct Driver drivers[3];
static struct Instance instances[3];
static struct Thread threads[3];

// Cardinal rotation fixtures leave projection and inversion in production code.
void ConvertRotToMatrix(MATRIX *m, const SVec3 *r)
{
    assert(r->x == 0 && (r->y == 0 || r->y == 0x400) && r->z == 0);
    memset(m, 0, sizeof(*m));
    m->m[0][0] = m->m[1][1] = m->m[2][2] = 4096;
    if (r->y == 0x400)
    {
        m->m[0][0] = m->m[2][2] = 0;
        m->m[0][2] = 4096;
        m->m[2][0] = -4096;
    }
}
void PushBuffer_SetPsyqGeom(struct PushBuffer *pb)
{
    SetGeomOffset(pb->rect.w / 2, pb->rect.h / 2);
    SetGeomScreen(pb->distanceToScreen_PREV);
}

static void place(int i, int x, int z)
{
    drivers[i].posCurr.x = x * 256;
    drivers[i].posCurr.z = z * 256;
    instances[i].matrix.t[0] = x;
    instances[i].matrix.t[2] = z;
}

int main(void)
{
    sdata->gGT = &tracker;
    tracker.pushBuffer[0].rect.w = 512;
    tracker.pushBuffer[0].rect.h = 216;
    tracker.pushBuffer[0].distanceToScreen_PREV = 256;
    for (int i = 0; i < 3; ++i)
    {
        tracker.drivers[i] = &drivers[i];
        drivers[i].instSelf = &instances[i];
        instances[i].thread = &threads[i];
        drivers[i].driverID = i + 4; // Bot IDs have no player viewport.
    }
    place(0, 2000, 2000);
    place(1, 2000, 3000);
    place(2, 2000, 3500);
    // Projection state left by rendering must not affect a bot's target.
    SetGeomOffset(-1000, -1000);
    SetGeomScreen(1);
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == &drivers[1]);
    drivers[1].invisibleTimer = 1;
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == &drivers[2]);
    place(2, 2000, 1000); // Behind the shooter.
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == NULL);
    place(2, 2000, 3500);
    tracker.gameMode1 = BATTLE_MODE;
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == NULL);
    drivers[2].BattleHUD.teamID = 1;
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == &drivers[2]);
    drivers[0].rotCurr.y = 0x400;
    place(2, 3000, 2000);
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == &drivers[2]);
    place(2, 1000, 2000);
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == NULL);

    // Human shooters keep their own camera and viewport in split screen.
    threads[0].modelIndex = DYNAMIC_PLAYER;
    drivers[0].driverID = 1;
    struct PushBuffer *pb = &tracker.pushBuffer[1];
    pb->rect.w = 256;
    pb->rect.h = 108;
    pb->distanceToScreen_PREV = 128;
    SVec3 rotation = {0};
    ConvertRotToMatrix(&pb->matrix_ViewProj, &rotation);
    PushBuffer_SetPsyqGeom(pb);
    place(2, 0, 1000);
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == &drivers[2]);
    place(2, 1000, 1000);
    assert(VehPickupItem_MissileGetTargetDriver(&drivers[0]) == NULL);
    puts("missile targeting: all assertions passed");
    return 0;
}
