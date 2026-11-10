"""Exercise native mask music selection and flag synchronization in production code."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "game/HOWL/HOWL_AudioState.c").read_text()
source = source[source.index("static int Audio_GetActiveMaskSongForDriver"):]
source = source[:source.index("#else")]
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t u32;
typedef int s32;
typedef int b32;
#include <stdbool.h>
enum { ACTION_BOT=1, KS_MASK_GRABBED=5, KS_ENGINE_REVVING=7, THREAD_FLAG_DEAD=8,
STATIC_AKUAKU=10, STATIC_UKAUKA=11, CSEQ_SONG_AKU=1, CSEQ_SONG_UKA=2, AKU_SONG=0x100, UKA_SONG=0x200 };
struct MaskHeadWeapon { int duration; };
struct Thread { int flags, modelIndex; void (*funcThTick)(void); void *object; struct Thread *childThread, *siblingThread; };
struct Instance { struct Thread *thread; };
struct Driver { int actionsFlagSet, kartState; struct Instance *instSelf; };
struct GameTracker { u32 gameMode1; int numPlyrCurrGame; struct Driver *drivers[8]; } gt;
static struct { struct GameTracker *gGT; } store = { &gt }, *sdata = &store;
static void RB_MaskWeapon_ThTick(void) {}
static void hint(void) {}
static int song, calls; static u32 tempoSeen;
static void Music_Adjust(u32 id, u32 tempo, int a, int b) { assert(!a && !b); song=id; tempoSeen=tempo; calls++; }
'''
tests = r'''
static void expect(int id) {
 calls=0; Audio_SetMaskSong(0x1234); assert(calls==1 && song==id && tempoSeen==0x1234);
 assert((gt.gameMode1 & (AKU_SONG|UKA_SONG)) == (id==1 ? AKU_SONG : id==2 ? UKA_SONG : 0));
 assert(gt.gameMode1 & 0x4000);
}
int main(void) {
 struct MaskHeadWeapon masks[2]={{100},{100}};
 struct Thread items[2]={{.modelIndex=STATIC_AKUAKU,.funcThTick=RB_MaskWeapon_ThTick,.object=&masks[0]},
 {.modelIndex=STATIC_UKAUKA,.funcThTick=RB_MaskWeapon_ThTick,.object=&masks[1]}};
 struct Thread owners[2]={{.childThread=&items[0]},{.childThread=&items[1]}};
 struct Instance instances[2]={{&owners[0]},{&owners[1]}};
 struct Driver drivers[2]={{.instSelf=&instances[0]},{.instSelf=&instances[1]}};
 gt.gameMode1=0x4000|UKA_SONG; gt.numPlyrCurrGame=1;
 gt.drivers[0]=NULL; expect(0);
 gt.drivers[0]=&drivers[0]; expect(1); // live thread works before action flag catches up
 drivers[0].actionsFlagSet=ACTION_BOT; expect(0); drivers[0].actionsFlagSet=0;
 drivers[0].kartState=KS_MASK_GRABBED; expect(0);
 drivers[0].kartState=KS_ENGINE_REVVING; expect(0); drivers[0].kartState=0;
 items[0].flags=THREAD_FLAG_DEAD; expect(0); items[0].flags=0;
 items[0].funcThTick=hint; expect(0); items[0].funcThTick=RB_MaskWeapon_ThTick;
 masks[0].duration=0; expect(0); masks[0].duration=-1; expect(0); masks[0].duration=100;
 items[0].object=NULL; expect(0); items[0].object=&masks[0];
 drivers[0].instSelf=NULL; expect(0); drivers[0].instSelf=&instances[0];
 instances[0].thread=NULL; expect(0); instances[0].thread=&owners[0];
 items[0].modelIndex=STATIC_UKAUKA; expect(2); items[0].modelIndex=STATIC_AKUAKU;
 gt.numPlyrCurrGame=2; gt.drivers[1]=&drivers[1]; expect(2); // keep currently audible live mask
 gt.gameMode1 &= ~(AKU_SONG|UKA_SONG); expect(1); expect(1);
 masks[0].duration=0; expect(2); masks[1].duration=0; expect(0);
 masks[1].duration=100; gt.drivers[0]=NULL; expect(2);
 gt.numPlyrCurrGame=0; expect(0);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ctr-mask-") as tmp:
    c = Path(tmp) / "mask.c"
    exe = Path(tmp) / ("mask.exe" if os.name == "nt" else "mask")
    c.write_text(fixture + source + tests)
    flags = [] if os.name == "nt" else ["-fsanitize=undefined", "-fno-sanitize-recover=all"]
    subprocess.run(["gcc", "-m32", "-O2", *flags, str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("mask music: live threads, lifetime, priority and flag synchronization passed")
