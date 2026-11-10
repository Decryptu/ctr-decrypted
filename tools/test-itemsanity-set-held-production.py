"""Exercise the real SetHeldItem order with production Itemsanity hooks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
fixture = (root / 'tools/test-itemsanity-unlocked.c').read_text().split('int main(void)')[0]
fixture = fixture.replace('"../ap/', '"' + str(root / 'ap') + '/')
fixture = fixture.replace('struct Driver { int numWumpas; };',
                          'struct Driver { int numWumpas, heldItemID, numHeldItems, driverRank, lapIndex; };')
fixture = fixture.replace('int gameMode1; };', '''int gameMode1, numPlyrCurrGame,
    numBotsNextGame, levelID, bossID, numPlayersWith3Missiles;
    struct { int enabledWeapons, numWeapons, RNG_itemSetCustom[20]; } battleSetup; };''')
fixture = fixture.replace('struct GameTracker *gGT; } state',
                          'struct GameTracker *gGT; struct { int timesLostBossRace[5]; } advProgress; } state')
fixture += r'''
#include <stdint.h>
typedef int32_t s32;
typedef uint32_t u32;
typedef uint8_t u8;
typedef int8_t s8;
#define SKULL_ROCK 18
#define RAMPAGE_RUINS 19
#define DRAGON_MINES 7
#define WARPBALL_HELD 16
static int rng;
static int MixRNG_Scramble(void) { return rng; }
static inline int CTR_MipsSra(int a, int b) { return a >> b; }
static inline int CTR_MipsSubLo(int a, int b) { return a - b; }
static inline int CTR_MipsAddLo(int a, int b) { return a + b; }
static inline int CTR_MipsMulLo(int a, int b) { return a * b; }
static inline int CTR_MipsDiv(int a, int b) { return a / b; }
static int AP_BossRetailRulesLevel(int level) { return level; }
static int AP_TiziFilterRoll(struct Driver *driver, int proposed) { (void)driver; return proposed; }
'''
source = (root / 'game/Vehicle/VehPhysGeneral.c').read_text()
start = source.index('enum ItemSet\n')
end = source.index('\nenum\n{\n\tVEH_BASE_SPEED_MAX_WUMPA', start)
# The engine's names the production slice reads (include/namespace_Vehicle.h,
# include/common.h, game/Vehicle/VehCommon.h); the item tables are all zeros,
# as in the retail-order fixture below.
fixture += r'''
#define CTR_PSX_REGISTER(r)
#define CTR_PSX_OBSERVE_MEMORY(x) ((void)0)
#define CTR_PSX_OBSERVE_VALUE(x) ((void)0)
#define GAME_TRACKER (sdata->gGT)
#define GAME_ADV_PROGRESS (sdata->advProgress)
#define VEH_LOAD_GAME_TRACKER(g) ((g) = sdata->gGT)
#define BATTLE_DEFAULT_WEAPON_FLAGS 0x34de
enum { HELD_ITEM_TURBO = 0, HELD_ITEM_BOMB_1X = 1, HELD_ITEM_MISSILE_1X = 2, HELD_ITEM_TNT = 3,
       HELD_ITEM_POTION = 4, HELD_ITEM_SPRING = 5, HELD_ITEM_SHIELD = 6, HELD_ITEM_MASK = 7,
       HELD_ITEM_CLOCK = 8, HELD_ITEM_WARPBALL = 9, HELD_ITEM_BOMB_3X = 0xa, HELD_ITEM_MISSILE_3X = 0xb,
       HELD_ITEM_INVISIBILITY = 0xc, HELD_ITEM_SUPER_ENGINE = 0xd, HELD_ITEM_NONE = 0xf,
       HELD_ITEM_ROULETTE = 0x10, HELD_ITEM_STACK_COUNT = 3 };
static u8 table[256]; /* larger than any retail set count */
#define VEH_ITEM_SET_RACE1 table
#define VEH_ITEM_SET_RACE2 table
#define VEH_ITEM_SET_RACE3 table
#define VEH_ITEM_SET_RACE4 table
#define VEH_ITEM_SET_BOSS_RACE table
#define VEH_ITEM_SET_BATTLE_DEFAULT table
'''
fixture += source[start:end]
fixture += r'''
u8 *charPtr[8] = {table, table, table, table, table, table, 0, table};
u8 numWeapons[8] = {20, 20, 20, 20, 20, 20, 0, 20};
int main(void) {
    const int ids[] = {0,1,2,3,4,6,7,8,9,10,11};
    struct Driver local = {0}, bot = {0};
    struct GameTracker gt = {0};
    gt.drivers[0] = &local; gt.drivers[1] = &bot; sdata->gGT = &gt;
    gt.numPlyrCurrGame = 1; gt.numBotsNextGame = 1;
    for (int weapon = 0; weapon < 11; weapon++) {
        memset(ap_itemsanity_owned, 0, sizeof ap_itemsanity_owned);
        ap_itemsanity_owned[weapon] = 1;
        for (int level = 18; level <= 24; level++) {
            gt.levelID = level; gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
            VehPhysGeneral_SetHeldItem(&local);
            assert(local.heldItemID == ids[weapon]);
            if (weapon >= 9) assert(local.numHeldItems == 3);
        }
        for (int rank = -1; rank <= 1; rank++) for (int lost = 0; lost < 7; lost++) {
            local.driverRank = rank; state.advProgress.timesLostBossRace[0] = lost;
            gt.levelID = DRAGON_MINES; gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_BOSS;
            VehPhysGeneral_SetHeldItem(&local);
            assert(local.heldItemID == ids[weapon]);
        }
    }
    memset(ap_itemsanity_owned, 0, sizeof ap_itemsanity_owned);
    fruit = 0; gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
    VehPhysGeneral_SetHeldItem(&local);
    assert(local.heldItemID == AP_ITEMSANITY_NO_ITEM && fruit == 0);
    for (int rank = -1; rank <= 1; rank++) {
        fruit = 0; local.driverRank = rank;
        gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_BOSS;
        VehPhysGeneral_SetHeldItem(&local);
        assert(local.heldItemID == AP_ITEMSANITY_NO_ITEM && fruit == 1);
    }
    // Item Reroll must not repeat the discarded weapon when another unlock
    // exists, in the arena, default boss arm and normal boss table arm.
    ap_itemsanity_owned[0] = 1;
    ap_itemsanity_owned[2] = 1;
    rerollExcluded = 0;
    for (int mode = 0; mode < 3; mode++) {
        gt.gameMode1 = ADVENTURE_MODE | (mode ? ADVENTURE_BOSS : CRYSTAL_CHALLENGE);
        local.driverRank = mode == 1 ? -1 : 1;
        VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 2);
    }
    // A downstream cap must not restore the item the trap discarded.
    memset(ap_itemsanity_owned, 0, sizeof ap_itemsanity_owned);
    ap_itemsanity_owned[0] = ap_itemsanity_owned[8] = ap_itemsanity_owned[10] = 1;
    rerollExcluded = 11;
    for (int mode = 0; mode < 3; mode++) {
        gt.gameMode1 = ADVENTURE_MODE | WARPBALL_HELD |
                       (mode ? ADVENTURE_BOSS : CRYSTAL_CHALLENGE);
        local.driverRank = mode == 1 ? -1 : 1;
        rng = mode == 1 ? 1 : 8; // select Warpball, capped to discarded Missile x3
        VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 0);
    }
    memset(ap_itemsanity_owned, 0, sizeof ap_itemsanity_owned);
    ap_itemsanity_owned[0] = ap_itemsanity_owned[2] = ap_itemsanity_owned[10] = 1;
    rerollExcluded = 2;
    gt.numPlyrCurrGame = 4; gt.numBotsNextGame = 0; gt.numPlayersWith3Missiles = 2;
    for (int mode = 0; mode < 3; mode++) {
        gt.gameMode1 = ADVENTURE_MODE | (mode ? ADVENTURE_BOSS : CRYSTAL_CHALLENGE);
        local.driverRank = mode == 1 ? -1 : 1;
        rng = mode == 1 ? 2 : 16; // select Missile x3, capped to discarded Missile
        VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 0);
    }
    gt.numPlyrCurrGame = 1; gt.numBotsNextGame = 1;
    rng = 0; rerollExcluded = 0;
    memset(ap_itemsanity_owned, 0, sizeof ap_itemsanity_owned);
    ap_itemsanity_owned[0] = ap_itemsanity_owned[2] = 1;
    // A sole unlock may repeat, and an inactive trap does not narrow the pool.
    ap_itemsanity_owned[2] = 0;
    gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
    VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 0);
    ap_itemsanity_owned[2] = 1; rerollExcluded = -1;
    VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 0);
    // Inactive controls keep the retail fixed arena grant and boss rewrites.
    active = 0;
    gt.levelID = SKULL_ROCK; rerollExcluded = 0;
    gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
    VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 0);
    rerollExcluded = -1;
    gt.levelID = 21; gt.gameMode1 = ADVENTURE_MODE | CRYSTAL_CHALLENGE;
    VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 1);
    memset(table, 7, sizeof table); local.driverRank = 1;
    state.advProgress.timesLostBossRace[0] = 0;
    gt.levelID = 0; gt.gameMode1 = ADVENTURE_MODE | ADVENTURE_BOSS;
    VehPhysGeneral_SetHeldItem(&local); assert(local.heldItemID == 11);
    // The bot keeps that rewrite even when the local player uses Itemsanity.
    active = 1; VehPhysGeneral_SetHeldItem(&bot); assert(bot.heldItemID == 11);
    puts("production SetHeldItem: all unlocks, seven arenas, boss ranks/losses and controls passed");
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path / 'test.c').write_text(fixture)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
