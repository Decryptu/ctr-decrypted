#include <common.h>

#ifdef CTR_AP
#include "../../ap/ap_oxide_garage_advert.h"
#ifdef CTR_CUSTOM_TRACKS
#include <platform/native_custom_tracks.h>
#endif
#endif

enum AHGarageConstants
{
	AH_GARAGE_ACCESS_FLAG = 1,
	AH_GARAGE_NORMAL_OPEN_SFX = 0x95,
	AH_GARAGE_OXIDE_OPEN_SFX = 0x96,
	AH_GARAGE_DOOR_MOVE_STEP = 0x20,
	AH_GARAGE_DOOR_HEIGHT = 0x300,
	AH_GARAGE_DOOR_COOLDOWN_MS = 2 * SECOND,
	AH_GARAGE_TOP_ROT_STEP = 0x40,
	AH_GARAGE_CHALLENGE_NEAR_DIST_MAX_SQ = 0x143fff,
	AH_GARAGE_BOSS_COLLIDE_RADIUS = 0x300,
	AH_GARAGE_BOSS_COLLIDE_RADIUS_SQ = 0x90000,
	AH_GARAGE_INTERIOR_FORWARD_OFFSET = -0x280,
	AH_GARAGE_INTERIOR_DIST_SQ = 0x40000,
	AH_GARAGE_FADE_STEP = -0x2aa,
	AH_GARAGE_OXIDE_FINAL_BOSS_ID = 5,
	AH_GARAGE_TOP_FORWARD_OFFSET = 0x4c,
	AH_GARAGE_TOP_DEPTH_BIAS = 0xfe,
};

void AH_Garage_ThDestroy(struct Thread *t)
{
	struct BossGarageDoor *garage = t->object;

	if (garage->garageTopInst != NULL)
	{
		INSTANCE_Death(garage->garageTopInst);
		garage->garageTopInst = NULL;
	}

	return;
}

void AH_Garage_Open(struct ScratchpadStruct *sps, void *hitObject)
{
	struct Thread *garageThread;
	struct BossGarageDoor *garage;
	struct Instance *garageInst;
	struct Thread *otherTh = hitObject;

	if (otherTh->modelIndex != DYNAMIC_PLAYER)
	{
		return;
	}

	garageThread = sps->Union.ThBuckColl.thread;
	garage = garageThread->object;
	garageInst = garageThread->inst;

	if (
	    // if door is not opening
	    (garage->direction != BOSS_GARAGE_DOOR_OPENING) &&

	    // if door is closed,
	    // if posY is the same as instDef posY
	    (garageInst->matrix.t[1] == garageInst->instDef->pos.y))
	{
		u32 soundID = AH_GARAGE_NORMAL_OPEN_SFX;

		// if you are not in gemstone valley
		// play sound of normal boss door opening

		// Level ID
		// if you are in Gemstone Valley
		if (GAME_TRACKER->levelID == GEM_STONE_VALLEY)
		{
			// play sound of oxide door opening
			soundID = AH_GARAGE_OXIDE_OPEN_SFX;
		}

		// Play sound
		OtherFX_Play(soundID, 1);
	}

	// door is now opening
	garage->direction = BOSS_GARAGE_DOOR_OPENING;

	// enable access through a door (disable collision)
	GAME_DOOR_ACCESS_FLAGS |= AH_GARAGE_ACCESS_FLAG;
}

void AH_Garage_ThTick(struct Thread *t)
{
	s16 raceLevels[5] = {AH_BOSS_RACE_LEVELS};
	struct GameTracker *gGT = GAME_TRACKER;
	s16 bosses[5] = {AH_BOSS_CHARACTER_IDS};
	struct PushBuffer *view;
	b32 bossIsOpen = true;
	s32 i;
	s32 top;
	s32 move;
	s32 ratio;
	s32 bottom;
	u32 soundID;
	s32 closeLevel;
	s32 distX, distY, distZ;

	struct BossGarageDoor *garage;
	struct Instance *inst;
	struct Instance *drv_inst;
	struct ScratchpadStruct *sps;

	garage = t->object;
	inst = t->inst;
	drv_inst = gGT->drivers[0]->instSelf;
	view = &gGT->pushBuffer[0];

	// Animate the moving door.
	if (garage->direction != BOSS_GARAGE_DOOR_STOPPED)
	{
		// Increment animation by 0x20 in either direction
		move = inst->matrix.t[1] + garage->direction * FPS_HALF(AH_GARAGE_DOOR_MOVE_STEP);
		inst->matrix.t[1] = move;

		top = inst->instDef->pos.y + AH_GARAGE_DOOR_HEIGHT;
		bottom = inst->instDef->pos.y;

		// If the door has gone past the top (height=0x300)
		if (move > top)
		{
			// Set position to the top
			inst->matrix.t[1] = top;

			// Door is now open (not moving)
			garage->direction = BOSS_GARAGE_DOOR_STOPPED;

			// Cooldown for 2 seconds
			garage->cooldown = AH_GARAGE_DOOR_COOLDOWN_MS;

			// Make invisible
			inst->flags |= HIDE_MODEL;
		}
		// If the door has gone past the bottom
		else if (move < bottom)
		{
			// Set position to the bottom
			inst->matrix.t[1] = bottom;

			garage->direction = BOSS_GARAGE_DOOR_STOPPED;

			garage->cooldown = 0;

			// Enable door collision
			GAME_DOOR_ACCESS_FLAGS &= ~AH_GARAGE_ACCESS_FLAG;
		}
		// If the door is between the top and bottom positions
		else if (garage->garageTopInst != 0)
		{
			// Update rotation of garagetop
			garage->rot.x += garage->direction * FPS_HALF(AH_GARAGE_TOP_ROT_STEP);

			ConvertRotToMatrix(&garage->garageTopInst->matrix, &garage->rot);
		}

		inst->flags = (inst->flags | SPLIT_LINE | REFLECTION_FUNC23 | WATER_SPLIT_WHITE) & ~SPLIT_SPECIAL;
	}
	else
	{
		// Count down before closing the open door.
		if (garage->cooldown != 0)
		{
			// subtract frame timer
			garage->cooldown -= gGT->elapsedTimeMS;

			// if countdown is not done, dont close door
			if (garage->cooldown > 0)
			{
				goto checkRewards;
			}

			// play sound of normal boss door opening, except for Oxide
			// NOTE(aalhendi): Finish reading the tracker before selecting the sound
			// argument; this preserves retail register lifetimes without fixed registers.
			closeLevel = gGT->levelID;
			CTR_PSX_OBSERVE_VALUE(closeLevel);
			soundID = closeLevel == GEM_STONE_VALLEY ? AH_GARAGE_OXIDE_OPEN_SFX : AH_GARAGE_NORMAL_OPEN_SFX;

			// Play sound
			OtherFX_Play(soundID, 1);

			// erase cooldown
			garage->cooldown = 0;

			// door is closing
			garage->direction = BOSS_GARAGE_DOOR_CLOSING;

			inst->flags &= ~HIDE_MODEL;
		}
		else
		{
			inst->flags |= SPLIT_SPECIAL;
			inst->flags &= ~(SPLIT_LINE | REFLECTION_FUNC23 | WATER_SPLIT_WHITE);
		}
	}
checkRewards:

{
	s32 hubLevel = GAME_TRACKER->levelID;
	i = 0;
	if (hubLevel == GEM_STONE_VALLEY)
	{
#ifdef CTR_AP
		// AP: THE canonical Oxide entry gate (WO-A1). Was the per-seed garage
		// requirement (boss_req[4], normally four received Keys) alone, which
		// opened the door on `Any% + All Four Bosses` with zero boss races won.
		// AP_OxideGarageOpen composes that same requirement with every active
		// companion goal condition when goal_oxide != 0, and still falls back to
		// the unchanged Phase-1 four-Key rule without slot_data. The map icon and
		// AH_Garage_LInB call the same function, so no entry path is left open.
		if (!AP_OxideGarageOpen())
			bossIsOpen = false;
#else
		for (; i < AH_BOSS_KEY_COUNT; i++)
		{
			if (!CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, i + ADV_REWARD_FIRST_BOSS_KEY))
			{
				bossIsOpen = false;
				break;
			}
		}
#endif
	}
	else
	{
#ifdef CTR_AP
		// AP Phase 2: per-seed boss garage requirement when slot_data active.
		// hubID = levelID - GEM_STONE_VALLEY is 1..4 for the four boss hubs
		// (Roo, Papu, Komodo, Pinstripe) -> boss_req[0..3]. Phase-1 fallback is
		// the flat cumulative Trophy total (4, 8, 12, 16).
		if (ctr_cfg_active())
		{
			// AP Phase 2 (item #6): per-mode boss gate. Modes 0/1 (Original4Tracks /
			// SameHubTracks) check WON tracks; mode 2 (Trophies) falls back to the
			// resolved trophy-count requirement -- all inside AP_BossGarageOpen.
			if (!AP_BossGarageOpen((hubLevel - GEM_STONE_VALLEY) - 1))
				bossIsOpen = false;
		}
		else if (AP_GateCount(AP_IDX_TROPHY) < (hubLevel - GEM_STONE_VALLEY) * 4)
			bossIsOpen = false;
#else
		u32 *rewards = GAME_ADV_PROGRESS.rewards;
		const s16 *tracks = AH_HUB_TRACK_IDS;
		s32 byteOffset = (hubLevel - N_SANITY_BEACH) * AH_HUB_TRACK_COUNT * sizeof(s16);
		for (; i < AH_HUB_TRACK_COUNT; i++)
		{
			if (!CHECK_ADV_BIT(rewards, *(s16 *)((u8 *)tracks + byteOffset) + ADV_REWARD_FIRST_TROPHY))
			{
				bossIsOpen = false;
				break;
			}
			byteOffset += sizeof(s16);
		}
#endif
	}
}

	distX = drv_inst->matrix.t[0] - inst->instDef->pos.x;
	distY = drv_inst->matrix.t[1] - inst->instDef->pos.y;
	distZ = drv_inst->matrix.t[2] - inst->instDef->pos.z;

	i = distX * distX + distY * distY + distZ * distZ;
	// if in a state where you're seeing the boss key open an adv door,
	// or some other kind of cutscene where you can't move
	if ((GAME_TRACKER->gameMode2 & 4) != 0)
	{
		return;
	}

	// check distance
	if (AH_GARAGE_CHALLENGE_NEAR_DIST_MAX_SQ < i)
	{
		goto checkUnlocked;
	}

	{
		s16 challengeBosses[5] = {AH_BOSS_CHARACTER_IDS};
		s32 challengeY = (view->rect.y + view->rect.h) - 0x1e;
#ifdef CTR_AP
		char advert[192];
		int advertVisible = 0;
		int advertLines = 0;

		// Format the locked-door panel before drawing its title so both can be
		// laid out as one bottom-anchored block: count the panel's explicit
		// lines and shift the title plus panel upward by exactly the overflow
		// past the bottom of the view.
		if (AH_HINT_STATE == 0)
		{
			int advertBoss = (GAME_TRACKER->levelID == GEM_STONE_VALLEY) ? 4 : ((GAME_TRACKER->levelID - GEM_STONE_VALLEY) - 1);
			char venueLine[64];

			if (!(s16)bossIsOpen)
				advertVisible = AP_BossGateAdvert(advertBoss, advert, (int)sizeof advert);
			// #169: a drawn boss track is named on the panel, open or shut,
			// as the first line above any requirement.
			if (advertBoss < 4 && AP_BossVenueAdvert(advertBoss, venueLine, (int)sizeof venueLine))
			{
				char joined[192];
				// #169 tiers 2 and 3: the drawn rival and kit follow the track.
				char rivalLine[48], kitLine[96];
				int used = snprintf(joined, sizeof joined, "%s", venueLine);
				if (AP_BossRivalAdvert(advertBoss, rivalLine, (int)sizeof rivalLine) && used < (int)sizeof joined)
					used += snprintf(joined + used, sizeof joined - used, "\r%s", rivalLine);
				if (AP_BossKitAdvert(advertBoss, kitLine, (int)sizeof kitLine) && used < (int)sizeof joined)
					used += snprintf(joined + used, sizeof joined - used, "\r%s", kitLine);
				if (advertVisible && used < (int)sizeof joined)
					snprintf(joined + used, sizeof joined - used, "\r%s", advert);
				memcpy(advert, joined, sizeof advert);
				advertVisible = 1;
			}
			// J03 (decision 7): N. Oxide's garage names the racer the final
			// race drew, open or shut, as the first line.
			if (advertBoss == 4 && AP_BossFinalGarageOpponent() >= 0)
			{
				char joined[192];
				int c = AP_BossFinalGarageOpponent();
				int used = snprintf(joined, sizeof joined, "Rival: %s", GAME_LANGUAGE_STRINGS[data.MetaDataCharacters[c].name_LNG_short]);
				if (advertVisible && used < (int)sizeof joined)
					snprintf(joined + used, sizeof joined - used, "\r%s", advert);
				memcpy(advert, joined, sizeof advert);
				advertVisible = 1;
			}
			if (advertVisible)
			{
				advertLines = AP_OxideGarageAdvertLineCount(advert);
				challengeY = AP_OxideGarageBlockTitleY(view->rect.y + view->rect.h, challengeY, data.font_charPixHeight[FONT_BIG],
				                                       data.font_charPixHeight[FONT_SMALL], advertLines);
			}
		}
#endif

		// if aku is not giving a hint
		if (AH_HINT_STATE == 0)
		{
			// draw string, lng_challenge
			DecalFont_DrawLine(

			    GAME_LANGUAGE_STRINGS[AH_BOSS_CHALLENGE_TEXT[challengeBosses[GAME_TRACKER->levelID - GEM_STONE_VALLEY]]],

			    view->rect.x + (view->rect.w >> 1), challengeY, FONT_BIG, JUSTIFY_CENTER | ORANGE);
		}

#ifdef CTR_AP
		// #24: the door is locked and the player is standing at it. The vanilla Aku
		// hint below can only say "four trophies" / "four keys", so under slot_data it
		// misstates the requirement this gate actually enforces. Add the real
		// requirement directly under the challenge name (the hint itself still
		// fires -- it is the retail flavour text). Only drawn while Aku is silent,
		// matching the challenge-name line above, so the two cannot overlap his
		// hint box. Oxide's door is boss index 4; the four boss garages are 0..3.
		// Oxide's panel can carry the composed goal's companion terms as well as
		// the door requirement (WO-A1), '\r' separated, one term per line, so it
		// is drawn with DecalFont_DrawMultiLine. 480 (0x1e0) is a safety margin
		// above every line the compact panel can produce (proven under 460 px in
		// tools/test-oxide-garage-advert.c), not a load-bearing wrap width.
		// #169: an open garage still names its drawn track (and rival) here,
		// under the title; only a shut door adds the requirement lines.
		if (advertVisible)
		{
			DecalFont_DrawMultiLine(advert, view->rect.x + (view->rect.w >> 1), challengeY + data.font_charPixHeight[FONT_BIG], 0x1e0, FONT_SMALL,
			                        JUSTIFY_CENTER | ORANGE);
		}
#endif
	}
	if ((s16)bossIsOpen)
	{
		goto openGarage;
	}

	// if this is gemstone valley
	if (GAME_TRACKER->levelID == GEM_STONE_VALLEY)
	{
		// if hint is not unlocked "need 4 keys for oxide"
		if (!(AH_STORY_REWARDS & (1u << (ADV_REWARD_HINT_NEED_FOUR_KEYS_FOR_OXIDE & 31))))
		{
			// HintID: need four keys to race oxide
			MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_NEED_FOUR_KEYS_FOR_OXIDE, 0);
		}
	}
	// not gemstone valley
	else
	{
		//  if hint is not unlocked "to access this boss garage..."
		if (!(AH_STORY_REWARDS & (1u << (ADV_REWARD_HINT_NEED_FOUR_TROPHIES_FOR_BOSS & 31))))
		{
			// HintID: need four trophies to enter boss
			MainFrame_RequestMaskHint(ADV_MASK_HINT_ID_NEED_FOUR_TROPHIES_FOR_BOSS, 0);
		}
	}

checkUnlocked:

	if (!(s16)bossIsOpen)
	{
		return;
	}

openGarage:
{
	Vec3 pos;

	sps = CTR_SCRATCHPAD_PTR(struct ScratchpadStruct, 0x108);
	sps->Input1.pos.x = inst->instDef->pos.x;
	sps->Input1.pos.y = inst->instDef->pos.y;
	sps->Input1.pos.z = inst->instDef->pos.z;
	sps->Input1.hitRadius = AH_GARAGE_BOSS_COLLIDE_RADIUS;
	sps->Input1.hitRadiusSquared = AH_GARAGE_BOSS_COLLIDE_RADIUS_SQ;

	sps->Union.ThBuckColl.thread = t;
	sps->Union.ThBuckColl.funcCallback = AH_Garage_Open;
	sps->Input1.modelID = STATIC_PINGARAGE;

	// Open garage door when player gets within radius of door
	PROC_CollideHitboxWithBucket(GAME_TRACKER->threadBuckets[PLAYER].thread, sps, 0);

	ratio = MATH_Sin((int)inst->instDef->rot.y);

	pos.x = (int)inst->instDef->pos.x + (ratio * AH_GARAGE_INTERIOR_FORWARD_OFFSET >> 0xc);
	pos.y = (int)inst->instDef->pos.y;

	ratio = MATH_Cos((int)inst->instDef->rot.y);

	pos.z = (int)inst->instDef->pos.z + (ratio * AH_GARAGE_INTERIOR_FORWARD_OFFSET >> 0xc);

	// DriverPos - DoorPos
	distX = drv_inst->matrix.t[0] - pos.x;
	distY = drv_inst->matrix.t[1] - pos.y;
	distZ = drv_inst->matrix.t[2] - pos.z;

	// If small distance (inside garage)
	i = distX * distX + distY * distY + distZ * distZ;
	if (i < AH_GARAGE_INTERIOR_DIST_SQ)
	{
		// Fade To Black
		GAME_TRACKER->pushBuffer_UI.fadeFromBlack_desiredResult = 0;
		GAME_TRACKER->pushBuffer_UI.fade_step = AH_GARAGE_FADE_STEP;
	}

#ifdef CTR_AP
	// #375: a start from the in-game tracker arrives inside this garage. Only
	// reached with the garage open (bossIsOpen above, the same gates the
	// door uses); the load below, its boss intro and the hub return are the
	// garage's own.
	if (ctr_cfg_active() && AP_TrackerLaunchGarageArrives())
	{
		AP_TrackerLaunchTaken();
		GAME_TRACKER->pushBuffer_UI.fadeFromBlack_desiredResult = 0;
		GAME_TRACKER->pushBuffer_UI.fade_step = AH_GARAGE_FADE_STEP;
	}
#endif

	// If fade complete, start loading level
	if (GAME_TRACKER->pushBuffer_UI.fadeFromBlack_currentValue == 0)
	{
#ifdef CTR_AP
		if (GAME_TRACKER->levelID == GEM_STONE_VALLEY && AP_OxideOffersFinalChallenge() && !AP_OxideFinalVenueEntryReady())
		{
			GAME_TRACKER->pushBuffer_UI.fadeFromBlack_desiredResult = 0x1000;
			GAME_TRACKER->pushBuffer_UI.fade_step = -AH_GARAGE_FADE_STEP;
			return;
		}
#endif
		GAME_REMOVE_CONFIG_0 |= ADVENTURE_ARENA;
		GAME_ADD_CONFIG_0 |= ADVENTURE_BOSS;

#ifdef CTR_AP
		// AP (issue #321): which Oxide encounter loads. AP_OxideOffersFinalChallenge()
		// requires the per-seed relic gate (issue #23) only AFTER the first
		// challenge is cleared, read from server-checked location state so a
		// reconnect cannot forget. Phase-1 fallback (no slot_data) = the vanilla
		// 18-Sapphire rule, unchanged.
		if ((GAME_TRACKER->levelID == GEM_STONE_VALLEY) && AP_OxideOffersFinalChallenge())
#else
		if ((GAME_TRACKER->levelID == GEM_STONE_VALLEY) && (GAME_TRACKER->currAdvProfile.numRelics == ADV_OXIDE_FINAL_RELIC_COUNT))
#endif
		{
			// set string index (0-5) to "N Oxide's Final Challenge"
			GAME_TRACKER->bossID = AH_GARAGE_OXIDE_FINAL_BOSS_ID;
		}

		else
		{
			GAME_TRACKER->bossID = bosses[GAME_TRACKER->levelID - GEM_STONE_VALLEY];
		}

		// Set the boss character (P2)
		GAME_CHARACTER_IDS[1] = AH_LEVEL_METADATA[raceLevels[GAME_TRACKER->levelID - GEM_STONE_VALLEY]].characterID_Boss;

#ifdef CTR_AP
		{
			s32 hubID = GAME_TRACKER->levelID - GEM_STONE_VALLEY;
			s32 raceLevel = raceLevels[hubID];

			// #169 boss track shuffle: the four ordinary garages race on the
			// seed's drawn venue. The opponent above is still read from the home
			// track and bossID stays the physical garage, so the Key, the
			// Boss Race check, the loss counter, the driving table and the
			// cutscenes are the garage's own. A refused record keeps the garage
			// shut (AP_BossGarageOpen); this guard only stops a load that
			// slipped past it, and never substitutes the retail venue.
			if (hubID >= 1 && hubID <= 4)
			{
#ifdef CTR_CUSTOM_TRACKS
				// #133 PR 6b: a content-plan garage holding a boss cup runs it on
				// the Adventure Cup machinery (engine cup 0) with boss mode kept:
				// four legs against the garage's boss, his AI and attack on each,
				// won on points (UI_CupStandings.c, AP_ContentCupFinish).
				int cupLevel = AP_ContentGarageCupPrepare(hubID - 1);
				if (cupLevel == -1)
				{
					GAME_TRACKER->pushBuffer_UI.fadeFromBlack_desiredResult = 0x1000;
					GAME_TRACKER->pushBuffer_UI.fade_step = -AH_GARAGE_FADE_STEP;
					return;
				}
				if (cupLevel >= 0)
				{
					int cupSlot;
					const ctr_content_pad *cupPad = ap_content_plan_pad(AP_CONTENT_GARAGE_CUP_PAD + hubID - 1);
					int cupOpponent = AP_BossGarageOpponent(hubID - 1);
					GAME_ADD_CONFIG_0 |= ADVENTURE_CUP;
					GAME_REMOVE_CONFIG_0 &= ~ADVENTURE_CUP;
					// One drawn racer through all four races, as in a boss race.
					if (cupOpponent >= 0)
						GAME_CHARACTER_IDS[1] = (s16)cupOpponent;
					GAME_TRACKER->cup.cupID = 0;
					GAME_TRACKER->cup.trackIndex = 0;
					for (cupSlot = 0; cupSlot < 8; cupSlot++)
						GAME_TRACKER->cup.points[cupSlot] = 0;
					AP_CupEnterFromHub(GAME_TRACKER->levelID);
					AP_ContentCupEnter();
					GAME_TRACKER->numLaps = (char)cupPad->legs[0].laps;
					RaceFlag_SetDrawOrder(1);
					MainRaceTrack_RequestLoad(cupLevel);
					return;
				}
#endif
				int venue = AP_BossGarageVenue(hubID - 1);
				int opponent;
				if (venue < 0)
				{
					GAME_TRACKER->pushBuffer_UI.fadeFromBlack_desiredResult = 0x1000;
					GAME_TRACKER->pushBuffer_UI.fade_step = -AH_GARAGE_FADE_STEP;
					return;
				}
				raceLevel = venue;
				// #169 tier 2: the seed's drawn racer replaces the garage's boss
				// as P2. bossID stays physical; LOAD_DriverMPK keeps the boss's
				// own pack and sideloads the racer's hi model.
				opponent = AP_BossGarageOpponent(hubID - 1);
				if (opponent >= 0)
					GAME_CHARACTER_IDS[1] = (s16)opponent;
#ifdef CTR_CUSTOM_TRACKS
				// The venue may be a custom track's host slot; this load is retail.
				CustomTrack_SelectRetailRace();
#endif
			}
			else if (hubID == 0)
			{
				// J03 (decision 7): with N. Oxide in the pool the final race
				// draws a racer, for both Oxide races. bossID stays 4 or 5, so
				// the checks, goal, flags and loss count are N. Oxide's; the
				// venue (Oxide Station or the Cortex Vortex Oxide 2 role) is
				// unchanged. LOAD_DriverMPK keeps Oxide's pack and sideloads the
				// racer's hi model.
				int finalOpponent = AP_BossFinalGarageOpponent();
				if (finalOpponent >= 0)
					GAME_CHARACTER_IDS[1] = (s16)finalOpponent;
			}

			RaceFlag_SetDrawOrder(1);
			MainRaceTrack_RequestLoad(raceLevel);
		}
#else
		// NOTE(aalhendi): Retail reloads the destination after the callback.
		RaceFlag_SetDrawOrder(1);
		MainRaceTrack_RequestLoad(raceLevels[GAME_TRACKER->levelID - GEM_STONE_VALLEY]);
#endif
	}
}
	return;
}

void AH_Garage_LInB(struct Instance *inst)
{
	b32 bossIsOpen = true;
#ifndef CTR_AP
	s32 i;
#endif
	s16 bitIndex;
	s32 ratio;
	struct Thread *t = inst->thread;
	struct Instance *garageTop;
	struct BossGarageDoor *garage;
	s32 levelID;

	if (t != NULL)
	{
		return;
	}

	t = inst->thread = PROC_BirthWithObject(SIZE_RELATIVE_POOL_BUCKET(sizeof(struct BossGarageDoor), NONE, SMALL, STATIC),

	                                        AH_Garage_ThTick, // behavior
	                                        AH_GARAGE_NAME,   // debug name
	                                        0                 // thread relative
	);

	if (t == NULL)
	{
		return;
	}

	t->inst = inst;
	t->funcThDestroy = AH_Garage_ThDestroy;

	garage = t->object;
	garage->direction = BOSS_GARAGE_DOOR_STOPPED;
	garage->cooldown = 0;

	// if this is not oxide's door
	if (inst->model->id != STATIC_OXIDEGARAGE)
	{
		// make a "garagetop" to make door appear to roll up

		garageTop = INSTANCE_Birth3D(GAME_TRACKER->modelPtr[STATIC_GARAGETOP], AH_GARAGE_TOP_NAME, t);

		garage->garageTopInst = garageTop;
		garageTop->matrix = inst->matrix;

		ratio = MATH_Sin((int)inst->instDef->rot.y) * AH_GARAGE_TOP_FORWARD_OFFSET;

		// continue setting GarageTop position
		garage->garageTopInst->matrix.t[0] = inst->matrix.t[0] + (ratio >> 12);
		garage->garageTopInst->matrix.t[1] = inst->matrix.t[1] + AH_GARAGE_DOOR_HEIGHT;

		ratio = MATH_Cos((int)inst->instDef->rot.y) * AH_GARAGE_TOP_FORWARD_OFFSET;

		// continue setting GarageTop position
		garage->garageTopInst->matrix.t[2] = inst->matrix.t[2] + (ratio >> 12);

		garage->garageTopInst->depthBiasNormal = AH_GARAGE_TOP_DEPTH_BIAS;
	}
	else
	{
		garage->garageTopInst = NULL;
	}

	levelID = GAME_TRACKER->levelID;
#ifndef CTR_AP
	i = 0;
#endif
	if (levelID == GEM_STONE_VALLEY)
	{
#ifdef CTR_AP
		// AP: the same canonical entry gate the ThTick collision/load path uses
		// (WO-A1). Presentation and gate MUST come from one predicate, or the
		// door renders open while the load refuses (or the reverse).
		if (!AP_OxideGarageOpen())
			bossIsOpen = false;
#else
		// ripper roo boss key
		// check all boss keys
		for (; i < AH_BOSS_KEY_COUNT; i++)
		{
			if (!CHECK_ADV_BIT(GAME_ADV_PROGRESS.rewards, i + ADV_REWARD_FIRST_BOSS_KEY))
			{
				bossIsOpen = false;
				break;
			}
		}
#endif
	}

	// if not gemstone valley
	else
	{
#ifdef CTR_AP
		// AP Phase 2: per-seed boss garage requirement (boss_req[hubID-1]) when
		// active; else Phase-1 flat cumulative Trophy total (Roo 4, Papu 8,
		// Komodo 12, Pinstripe 16). hubID = levelID - GEM_STONE_VALLEY is 1..4.
		if (ctr_cfg_active())
		{
			// AP Phase 2 (item #6): per-mode boss gate (see AH_Garage_ThTick).
			if (!AP_BossGarageOpen((levelID - GEM_STONE_VALLEY) - 1))
				bossIsOpen = false;
		}
		else if (AP_GateCount(AP_IDX_TROPHY) < (levelID - GEM_STONE_VALLEY) * 4)
			bossIsOpen = false;
#else
		u32 *rewards = GAME_ADV_PROGRESS.rewards;
		const s16 *tracks = AH_HUB_TRACK_IDS;
		s32 trackOffset = (levelID - N_SANITY_BEACH) * AH_HUB_TRACK_COUNT * sizeof(s16);
		// NOTE(aalhendi): Both targets have 32-bit addresses. Keep the byte
		// offset separate from the table base, as in retail's trophy traversal.
		// check all tracks on hub
		for (; i < AH_HUB_TRACK_COUNT; i++)
		{
			// if any trophy on this hub is not unlocked
			if (!CHECK_ADV_BIT(rewards, *(s16 *)(trackOffset + (u32)tracks) + ADV_REWARD_FIRST_TROPHY))
			{
				// boss is not open
				bossIsOpen = false;
				break;
			}
			trackOffset += sizeof(s16);
		}
#endif
	}

	// if boss is open
	if ((s16)bossIsOpen)
	{
		u32 *rewards = GAME_ADV_PROGRESS.rewards;
		// check if boss was NOT beaten on this hub (levelID - 0x19)
		bitIndex = AH_BOSS_REWARDS[GAME_TRACKER->levelID - GEM_STONE_VALLEY];
		if (CHECK_ADV_BIT(rewards, (s16)bitIndex))
		{
			t->modelIndex = 2;
		}
		else
		{
			t->modelIndex = 1;
		}
	}

	// if boss is not open
	else
	{
		// locked
		t->modelIndex = 0;
	}

	garage->rot.x = inst->instDef->rot.x;
	garage->rot.y = inst->instDef->rot.y;
	garage->rot.z = inst->instDef->rot.z;

	inst->depthBiasNormal = 1;
	inst->vertSplit = inst->instDef->pos.y + AH_GARAGE_DOOR_HEIGHT;
	inst->specLightX = 0;
	inst->depthBiasSecondary = inst->depthBiasNormal;
}
