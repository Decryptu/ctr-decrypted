#ifndef NATIVE_DRAW_DISTANCE_H
#define NATIVE_DRAW_DISTANCE_H

#include <platform/native_config.h>

// increase_draw_distance (config.ini): every distance limit of the renderer
// moves out by the one factor, so none of them cuts in before the others
// (docs/LEVEL-VISIBILITY.md#the-stages). Retail's values pass through untouched
// when the option is off.
#define NATIVE_DRAW_DISTANCE_SCALE 4

// A limit the renderer compares a distance against: the frustum's far clip,
// the depth past which a BSP leaf goes to the full-dynamic emitter, and the
// depths that pick a level texture's LOD and how far a quadblock is subdivided
// (the high-resolution textures only load in the deepest subdivision).
static inline int NativeDrawDistance_Limit(int retail)
{
	return g_config.increaseDrawDistance ? retail * NATIVE_DRAW_DISTANCE_SCALE : retail;
}

// A level whose configFlags carry LEVEL_CONFIG_ONE_TEXTURE_LOD has the same
// texture at every LOD (a converted track: CrashTeamEditor writes one layout to
// all four of a texture group's slots). Deeper subdivision then shows nothing
// new, as PGXP textures in perspective anyway, so its LOD and subdivision depths
// keep retail's values and only the far clip moves out. On an open converted
// course nearly every block is within the scaled depths, and subdividing them
// made a frame cost three to four times a retail track's. Retail levels never
// set the bit (docs/LEVEL-VISIBILITY.md#the-stages).
#define LEVEL_CONFIG_ONE_TEXTURE_LOD 0x40000000u

// A level LOD or subdivision depth (RenderAllLevelGeometry's scratch thresholds).
static inline int NativeDrawDistance_LevelLod(int retail, unsigned int levelConfigFlags)
{
	return (levelConfigFlags & LEVEL_CONFIG_ONE_TEXTURE_LOD) ? retail : NativeDrawDistance_Limit(retail);
}

// A model's projected distance, before it picks its LOD header: divided rather
// than the headers' limits multiplied, since those live in the model data.
static inline int NativeDrawDistance_ModelLod(int projected)
{
	return g_config.increaseDrawDistance ? projected / NATIVE_DRAW_DISTANCE_SCALE : projected;
}

#endif
