#include "platform/native_checkpoint.h"

#include <common.h>
#include <macros.h>

#include <platform.h>
#include "ctr_scratchpad.h"
#include "platform/native_build_id.h"
#include "platform/native_memory.h"
#include "platform/native_state.h"

#include <string.h>

#ifdef CTR_CUSTOM_PACKAGES
#include "platform/native_custom_offline.h"
#include "platform/native_custom_state_identity.h"
#endif

// A checkpoint is a copy of the game's mutable memory: the resident globals,
// the overlays' data, the mempack, the scratchpad and the native subsystems'
// state. Every region is static memory in an image that loads at a fixed
// address (CMakeLists.txt links without ASLR), so the pointers inside a
// checkpoint, at whatever width, stay valid in any launch of the build that
// made it, and restoring is copying back. A checkpoint names that build and
// where each region was; a state from anything else is refused.
//
// What host code derives from restored data in its own memory is rebuilt after
// the copy (NativeCheckpoint_Restore). Not in a checkpoint: what host code
// builds into the PSX heap (racer and bot
// models, Archipelago models and lanes, missing VisMem lists). Its owners keep
// it per process and rebuild it when needed (AP_NavRec_AfterCheckpointRestore).

#define NATIVE_CHECKPOINT_FOURCC(a, b, c, d) ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))

#define NATIVE_CHECKPOINT_MAGIC      NATIVE_CHECKPOINT_FOURCC('C', 'T', 'R', 'C')
#define NATIVE_CHECKPOINT_VERSION    7u
#define NATIVE_CHECKPOINT_BUILD_SIZE 64

enum NativeCheckpointRegionKind
{
	NATIVE_CHECKPOINT_REGION_RDATA = NATIVE_CHECKPOINT_FOURCC('R', 'D', 'A', 'T'), // resident rdata globals
	NATIVE_CHECKPOINT_REGION_DATA = NATIVE_CHECKPOINT_FOURCC('D', 'A', 'T', 'A'),  // resident data globals
	NATIVE_CHECKPOINT_REGION_SDATA = NATIVE_CHECKPOINT_FOURCC('S', 'D', 'A', 'T'), // resident sdata globals
	NATIVE_CHECKPOINT_REGION_D230 = NATIVE_CHECKPOINT_FOURCC('D', '2', '3', '0'),  // main-menu overlay data
	NATIVE_CHECKPOINT_REGION_V230 = NATIVE_CHECKPOINT_FOURCC('V', '2', '3', '0'),  // main-menu video BSS
	NATIVE_CHECKPOINT_REGION_D231 = NATIVE_CHECKPOINT_FOURCC('D', '2', '3', '1'),  // race/battle overlay data
	NATIVE_CHECKPOINT_REGION_D232 = NATIVE_CHECKPOINT_FOURCC('D', '2', '3', '2'),  // adventure overlay data
	NATIVE_CHECKPOINT_REGION_D233 = NATIVE_CHECKPOINT_FOURCC('D', '2', '3', '3'),  // cutscene overlay mutable data
	NATIVE_CHECKPOINT_REGION_GAR3 = NATIVE_CHECKPOINT_FOURCC('G', 'A', 'R', '3'),  // garage runtime state
	NATIVE_CHECKPOINT_REGION_CRD3 = NATIVE_CHECKPOINT_FOURCC('C', 'R', 'D', '3'),  // credits runtime state
	NATIVE_CHECKPOINT_REGION_MPAK = NATIVE_CHECKPOINT_FOURCC('M', 'P', 'A', 'K'),  // mempack backing store
	NATIVE_CHECKPOINT_REGION_SCRP = NATIVE_CHECKPOINT_FOURCC('S', 'C', 'R', 'P'),  // PS1 scratchpad RAM
	NATIVE_CHECKPOINT_REGION_NATS = NATIVE_CHECKPOINT_FOURCC('N', 'A', 'T', 'S'),  // native subsystem state bundle
#ifdef CTR_CUSTOM_PACKAGES
	NATIVE_CHECKPOINT_REGION_CUST = NATIVE_CHECKPOINT_FOURCC('C', 'U', 'S', 'T'), // loaded custom package identity
#endif
};

static const u32 sNativeCheckpointRegionKinds[] = {
    NATIVE_CHECKPOINT_REGION_RDATA, NATIVE_CHECKPOINT_REGION_DATA, NATIVE_CHECKPOINT_REGION_SDATA, NATIVE_CHECKPOINT_REGION_D230,
    NATIVE_CHECKPOINT_REGION_V230,  NATIVE_CHECKPOINT_REGION_D231, NATIVE_CHECKPOINT_REGION_D232,  NATIVE_CHECKPOINT_REGION_D233,
    NATIVE_CHECKPOINT_REGION_GAR3,  NATIVE_CHECKPOINT_REGION_CRD3, NATIVE_CHECKPOINT_REGION_MPAK,  NATIVE_CHECKPOINT_REGION_SCRP,
    NATIVE_CHECKPOINT_REGION_NATS,
#ifdef CTR_CUSTOM_PACKAGES
    NATIVE_CHECKPOINT_REGION_CUST,
#endif
};

#define NATIVE_CHECKPOINT_REGION_COUNT ((u32)(sizeof(sNativeCheckpointRegionKinds) / sizeof(sNativeCheckpointRegionKinds[0])))

struct NativeCheckpointRegion
{
	u32 kind;
	u32 offset; // in the checkpoint
	u32 size;
	u32 reserved;
	u64 address; // where the region lives in the build that made it; 0 for state that is not a block of memory
};

struct NativeCheckpointHeader
{
	u32 magic;
	u32 version;
	u32 size;
	u32 regionCount;
	char buildId[NATIVE_CHECKPOINT_BUILD_SIZE];
	u64 codeAnchor; // a function's address: a rebuilt image moves it
	u32 psxRandSeed;
	s32 activeMempackIndex;
	// The 60 FPS mode the state was taken in (MainMain_ApplyFrameRateMode).
	// Frame timers are scaled when they are set, so a state only replays
	// correctly in the mode it was captured in.
	s32 fps60Enabled;
	u32 reserved;
	struct NativeCheckpointRegion regions[16];
};

CTR_STATIC_ASSERT(NATIVE_CHECKPOINT_REGION_COUNT <= 16);

static u32 NativeCheckpoint_Align8(u32 value)
{
	return (value + 7u) & ~7u;
}

static int NativeCheckpoint_GetActiveMempackIndex(void)
{
	for (int i = 0; i < 4; i++)
	{
		if (sdata_static.PtrMempack == &sdata_static.mempack[i])
		{
			return i;
		}
	}

	if ((sdata_static.gameTracker.activeMempackIndex >= 0) && (sdata_static.gameTracker.activeMempackIndex < 4))
	{
		return sdata_static.gameTracker.activeMempackIndex;
	}

	return 0;
}

static int NativeCheckpoint_GetRegionSize(u32 kind)
{
	switch (kind)
	{
	case NATIVE_CHECKPOINT_REGION_RDATA:
		return (int)sizeof(rdata);
	case NATIVE_CHECKPOINT_REGION_DATA:
		return (int)sizeof(data);
	case NATIVE_CHECKPOINT_REGION_SDATA:
		return (int)sizeof(sdata_static);
	case NATIVE_CHECKPOINT_REGION_D230:
		return (int)sizeof(D230);
	case NATIVE_CHECKPOINT_REGION_V230:
		return (int)sizeof(V230);
	case NATIVE_CHECKPOINT_REGION_D231:
		return (int)sizeof(D231);
	case NATIVE_CHECKPOINT_REGION_D232:
		return (int)sizeof(D232);
	case NATIVE_CHECKPOINT_REGION_D233:
		return (int)sizeof(D233);
	case NATIVE_CHECKPOINT_REGION_GAR3:
		return (int)sizeof(gGarage);
	case NATIVE_CHECKPOINT_REGION_CRD3:
		return (int)sizeof(creditsBSS) - OFFSETOF(struct Ovr233_Credits_BSS, creditThread);
	case NATIVE_CHECKPOINT_REGION_MPAK:
		return Platform_GetMempackBackingSize();
	case NATIVE_CHECKPOINT_REGION_SCRP:
		return (int)CTR_SCRATCHPAD_SIZE;
	case NATIVE_CHECKPOINT_REGION_NATS:
		return NativeState_GetSize();
#ifdef CTR_CUSTOM_PACKAGES
	case NATIVE_CHECKPOINT_REGION_CUST:
		return (int)sizeof(struct CustomStateIdentity);
#endif
	}

	return 0;
}

// The block of memory a region copies; NULL for state captured by its owner.
static void *NativeCheckpoint_GetRegionPtr(u32 kind)
{
	switch (kind)
	{
	case NATIVE_CHECKPOINT_REGION_RDATA:
		return &rdata;
	case NATIVE_CHECKPOINT_REGION_DATA:
		return &data;
	case NATIVE_CHECKPOINT_REGION_SDATA:
		return &sdata_static;
	case NATIVE_CHECKPOINT_REGION_D230:
		return &D230;
	case NATIVE_CHECKPOINT_REGION_V230:
		return &V230;
	case NATIVE_CHECKPOINT_REGION_D231:
		return &D231;
	case NATIVE_CHECKPOINT_REGION_D232:
		return &D232;
	case NATIVE_CHECKPOINT_REGION_D233:
		return &D233;
	case NATIVE_CHECKPOINT_REGION_GAR3:
		return &gGarage;
	case NATIVE_CHECKPOINT_REGION_CRD3:
		return &creditsBSS.creditThread;
	case NATIVE_CHECKPOINT_REGION_MPAK:
		return Platform_GetMempackBacking();
	case NATIVE_CHECKPOINT_REGION_SCRP:
		return CTR_SCRATCHPAD_BASE;
	}

	return NULL;
}

static int NativeCheckpoint_InitHeader(struct NativeCheckpointHeader *header)
{
	u32 offset = NativeCheckpoint_Align8((u32)sizeof(*header));

	memset(header, 0, sizeof(*header));
	header->magic = NATIVE_CHECKPOINT_MAGIC;
	header->version = NATIVE_CHECKPOINT_VERSION;
	header->regionCount = NATIVE_CHECKPOINT_REGION_COUNT;
	strncpy(header->buildId, gNativeBuildId, sizeof(header->buildId) - 1);
	header->codeAnchor = (u64)(uintptr_t)&NativeCheckpoint_GetSize;

	for (u32 i = 0; i < header->regionCount; i++)
	{
		const u32 kind = sNativeCheckpointRegionKinds[i];
		const int regionSize = NativeCheckpoint_GetRegionSize(kind);

		if (regionSize <= 0)
		{
			return 0;
		}

		header->regions[i].kind = kind;
		header->regions[i].offset = offset;
		header->regions[i].size = (u32)regionSize;
		header->regions[i].address = (u64)(uintptr_t)NativeCheckpoint_GetRegionPtr(kind);
		offset = NativeCheckpoint_Align8(offset + (u32)regionSize);
	}

	header->size = offset;
	return 1;
}

// Whether this build can restore the checkpoint; logs why not.
static int NativeCheckpoint_ValidateHeader(const struct NativeCheckpointHeader *header, int srcSize)
{
	struct NativeCheckpointHeader live;

	if ((header == NULL) || (srcSize < (int)sizeof(*header)) || (header->magic != NATIVE_CHECKPOINT_MAGIC))
	{
		Platform_Log("[CTR State] restore refused: not a savestate\n");
		return 0;
	}
	if (header->version != NATIVE_CHECKPOINT_VERSION)
	{
		Platform_Log("[CTR State] restore refused: savestate format %u, this build reads %u\n", header->version, NATIVE_CHECKPOINT_VERSION);
		return 0;
	}
	if (!NativeCheckpoint_InitHeader(&live))
	{
		return 0;
	}
	if ((memcmp(header->buildId, live.buildId, sizeof(live.buildId)) != 0) || (header->codeAnchor != live.codeAnchor))
	{
		Platform_Log("[CTR State] restore refused: saved by build %.*s, this is %s (a state loads only in the build that made it)\n",
		             (int)sizeof(header->buildId), header->buildId, live.buildId);
		return 0;
	}
	if ((header->size != live.size) || (header->size > (u32)srcSize) || (header->regionCount != live.regionCount))
	{
		Platform_Log("[CTR State] restore refused: its layout differs from this build's\n");
		return 0;
	}
	for (u32 i = 0; i < live.regionCount; i++)
	{
		const struct NativeCheckpointRegion *region = &header->regions[i];
		const struct NativeCheckpointRegion *liveRegion = &live.regions[i];

		if ((region->kind != liveRegion->kind) || (region->offset != liveRegion->offset) || (region->size != liveRegion->size) ||
		    (region->address != liveRegion->address))
		{
			Platform_Log("[CTR State] restore refused: its memory layout differs from this build's (region %.4s)\n", (const char *)&liveRegion->kind);
			return 0;
		}
	}

	return 1;
}

int NativeCheckpoint_GetSize(void)
{
	struct NativeCheckpointHeader header;

	if (!NativeCheckpoint_InitHeader(&header))
	{
		return 0;
	}

	return (int)header.size;
}

int NativeCheckpoint_Capture(void *dst, int dstSize)
{
	struct NativeCheckpointHeader header;
	u8 *bytes = (u8 *)dst;

	if (!NativeCheckpoint_InitHeader(&header))
	{
		return 0;
	}
	if ((dst == NULL) || (dstSize < (int)header.size))
	{
		return 0;
	}

	header.psxRandSeed = PSX_BIOS_GetRandSeed();
	header.activeMempackIndex = NativeCheckpoint_GetActiveMempackIndex();
	header.fps60Enabled = gNative60FpsEnabled;

	memset(dst, 0, header.size);
	memcpy(dst, &header, sizeof(header));

	for (u32 i = 0; i < header.regionCount; i++)
	{
		const struct NativeCheckpointRegion *region = &header.regions[i];
		void *regionDst = &bytes[region->offset];
		void *src = NativeCheckpoint_GetRegionPtr(region->kind);

		if (src != NULL)
		{
			memcpy(regionDst, src, region->size);
		}
		else if (region->kind == NATIVE_CHECKPOINT_REGION_NATS)
		{
			if (!NativeState_Capture(regionDst, (int)region->size))
			{
				return 0;
			}
		}
#ifdef CTR_CUSTOM_PACKAGES
		else if (region->kind == NATIVE_CHECKPOINT_REGION_CUST)
		{
			CustomOffline_RuntimeStateIdentity((struct CustomStateIdentity *)regionDst);
		}
#endif
		else
		{
			return 0;
		}
	}

	return 1;
}

#ifdef CTR_CUSTOM_PACKAGES
// Box authoring build. A checkpoint holds the level's geometry but not the
// offline custom-race runtime that says whether the host slot serves a
// package, so a state may only be restored into the same load context: no
// custom track on both sides, or the same package (UUID, LEV and VRM SHA-256).
// Anything else would race custom geometry under the host slot's retail
// identity, or the reverse, and file box placements and AI recordings under the
// wrong track (issue #356). Checked before a single byte is restored.
static char s_nativeCheckpointCustomRefusal[] = "STATE NOT LOADED: OTHER TRACK";
static int s_nativeCheckpointCustomRefusalFrames;

static int NativeCheckpoint_CustomIdentityAllows(const struct NativeCheckpointHeader *header, const u8 *bytes)
{
	struct CustomStateIdentity saved;
	struct CustomStateIdentity live;
	char savedText[128];
	char liveText[128];
	u32 i;

	for (i = 0; i < header->regionCount; i++)
	{
		const struct NativeCheckpointRegion *region = &header->regions[i];

		if (region->kind != NATIVE_CHECKPOINT_REGION_CUST)
		{
			continue;
		}
		if (region->size != sizeof(saved))
		{
			break;
		}
		memcpy(&saved, &bytes[region->offset], sizeof(saved));
		CustomOffline_RuntimeStateIdentity(&live);
		if (CustomStateIdentity_RestoreAllowed(&saved, &live))
		{
			return 1;
		}
		CustomStateIdentity_Describe(&saved, savedText, sizeof(savedText));
		CustomStateIdentity_Describe(&live, liveText, sizeof(liveText));
		Platform_Log("[CTR State] restore refused: the state was saved with custom track %s, the game now has %s\n",
		             savedText, liveText);
		s_nativeCheckpointCustomRefusalFrames = 180;
		return 0;
	}

	Platform_Log("[CTR State] restore refused: the state carries no custom track identity\n");
	return 0;
}

// The refusal, on screen for a few seconds of race HUD (UI_RenderFrame_Racing).
void NativeCheckpoint_DrawCustomRefusal(void)
{
	if (s_nativeCheckpointCustomRefusalFrames <= 0)
	{
		return;
	}
	s_nativeCheckpointCustomRefusalFrames--;
	DecalFont_DrawLine(s_nativeCheckpointCustomRefusal, 0x100, 0x60, FONT_SMALL, (JUSTIFY_CENTER | ORANGE));
}
#endif

int NativeCheckpoint_Restore(const void *src, int srcSize)
{
	const struct NativeCheckpointHeader *header = (const struct NativeCheckpointHeader *)src;
	const u8 *bytes = (const u8 *)src;
	const struct NativeCheckpointRegion *nativeStateRegion = NULL;

	if (!NativeCheckpoint_ValidateHeader(header, srcSize))
	{
		return 0;
	}
#ifdef CTR_CUSTOM_PACKAGES
	if (!NativeCheckpoint_CustomIdentityAllows(header, bytes))
	{
		return 0;
	}
#endif

	// NOTE(aalhendi): Restore defaults for uncaptured data, such as the credits
	// position prefix. The regions restored below include the complete D233 image.
	OVR233_ResetRuntimeState();

	for (u32 i = 0; i < header->regionCount; i++)
	{
		const struct NativeCheckpointRegion *region = &header->regions[i];
		void *dst = NativeCheckpoint_GetRegionPtr(region->kind);

		if (dst != NULL)
		{
			memcpy(dst, &bytes[region->offset], region->size);
		}
		else if (region->kind == NATIVE_CHECKPOINT_REGION_NATS)
		{
			nativeStateRegion = region;
		}
	}

	PSX_BIOS_SetRandSeed(header->psxRandSeed);
	gNative60FpsEnabled = header->fps60Enabled;
	Platform_ConfigureMempackArena();
	Platform_RepairResidentPointers(header->activeMempackIndex);

	// Process-owned data derived from what was just restored:
	// the language string table points into the restored LNG file...
	if ((sdata_static.lngFile != NULL) && (sdata_static.numLngStrings > 0))
	{
		LOAD_LangFile_IndexStrings(sdata_static.lngFile);
	}
#ifdef CTR_AP
	// ...and the trap scheduler's Empty Crates census caches the level's InstDef
	// table, which the next level instantiation republishes.
	AP_TrapForgetLevelInstances();
#endif

#if defined(CTR_AP) || defined(CTR_AI_LAP_RECORDER)
	// Recorded AI lanes live in the PSX heap, which a state does not hold: reload
	// the recording for the restored level and republish it, or put the engine
	// back on the level's own nav data. A no-op when no recording is in play.
	AP_NavRec_AfterCheckpointRestore();
#endif

	if ((nativeStateRegion == NULL) || !NativeState_Restore(&bytes[nativeStateRegion->offset], (int)nativeStateRegion->size))
	{
		return 0;
	}

	return 1;
}
