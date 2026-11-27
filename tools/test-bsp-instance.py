"""Execute the production BSP leaf function with absent and live instances."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'game/COLL/COLL_InstanceQuery.c').read_text()
start = source.index('void COLL_FIXED_BSPLEAF_TestInstance(')
end = source.index('\n}\n', start) + 3
function = source[start:end]
fixture = r'''
#include <assert.h>
#include <stddef.h>
#include "tools/harness-psx-space.h"
/* The list ends at a record whose whole flag/id word is zero. */
#define CTR_ReadU32AlignedLE(p) (*(const unsigned *)(const void *)(p))
#define BSP_HITBOX_COLLIDABLE 128
#define DRAW_COLLISION_MASK 15
struct Vec { int x,y,z; };
struct BoundingBox { struct Vec min,max; };
struct Instance { int flags; };
struct InstDef { PSX_PTR(struct Instance) ptrInstance; };
struct BSP { int flag; struct BoundingBox box; union {
 struct { PSX_PTR(struct BSP) bspHitboxArray; } leaf;
 struct { PSX_PTR(struct InstDef) instDef; } hitbox;
} data; };
struct ScratchpadStruct { int numBspHitboxesHit; struct BSP *bspHitboxesHit[4]; struct BoundingBox bbox; };
static int calls;
void COLL_FIXED_INSTANC_TestPoint(struct ScratchpadStruct *s, struct BSP *b) { (void)s; (void)b; calls++; }
'''
tests = r'''
/* The LEV's hitbox rows, instance definitions and instances live in PSX space. */
struct Lev { struct Instance visible, hidden; struct InstDef absent, live, invisible; struct BSP rows[2]; };
int main(void) {
 struct Lev *lev = PsxSpace_Alloc(sizeof *lev);
 struct BSP *rows = lev->rows, leaf={0};
 struct ScratchpadStruct s={0};
 lev->visible.flags=15;
 PSX_SET(lev->live.ptrInstance,&lev->visible); PSX_SET(lev->invisible.ptrInstance,&lev->hidden);
 PSX_SET(leaf.data.leaf.bspHitboxArray,rows);
 rows[0].flag=128; PSX_SET(rows[0].data.hitbox.instDef,&lev->absent);
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==0);
 PSX_SET(rows[0].data.hitbox.instDef,&lev->live);
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==1);
 PSX_SET(rows[0].data.hitbox.instDef,&lev->invisible);
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==1);
 PSX_SET(rows[0].data.hitbox.instDef,(struct InstDef *)NULL);
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==2);
 rows[0].flag=1; PSX_SET(rows[0].data.hitbox.instDef,&lev->absent);
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 s.numBspHitboxesHit=1; s.bspHitboxesHit[0]=rows;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 s.numBspHitboxesHit=0; rows[0].box.min.x=1;
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 PSX_SET(leaf.data.leaf.bspHitboxArray,(struct BSP *)NULL);
 COLL_FIXED_BSPLEAF_TestInstance(&leaf,&s); assert(calls==3);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    src = Path(tmp) / 'fixture.c'
    exe = Path(tmp) / 'fixture'
    src.write_text(fixture + function + tests)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined','-DCTR_NATIVE',
                    '-I',str(root),'-I',str(root/'include'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: 8 production BSP collision cases, UBSan enabled')
