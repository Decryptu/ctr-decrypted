#include <common.h>
#include <platform/native_dev.h>

#ifdef CTR_AP
#include "../ap/ap_boss_adapter.h"

// J03 (#169 full weapon suite): the boss adapter's per-race state and the
// driver it armed. Reset at every race init, so a restart, an exit to the map
// or a failed load never carries a use or a timer into the next race.
static AP_BossAdapterState s_apBossAdapter;
static struct Driver *s_apBossAdapterDriver;
#endif

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
static struct MetaDataBOSS s_cortexVortexOxideWeapons[14];
static const struct MetaDataBOSS s_cortexVortexOxideWeaponTemplate[14] = {
	{0x00, 0x02, 0x66, 0x01, 0x02, 0x00},
	{0x10, 0x02, 0x64, 0x01, 0x02, 0x00},
	{0x21, 0x03, 0x03, 0x01, 0x0c, 0x01},
	{0x2f, 0x03, 0x01, 0x01, 0x0c, 0x00},
	{0x3b, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x40, 0x03, 0x01, 0x01, 0x0c, 0x00},
	{0x46, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x56, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x5d, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x60, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x65, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x66, 0x02, 0x0f, 0x01, 0x0c, 0x01},
	{0x7f, 0x02, 0x04, 0x01, 0x0c, 0x01},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

static void PickupBots_InitCortexVortexOxideWeapons(struct GameTracker *gGT)
{
	int i;
	int lastCheckpoint = gGT->level1 != NULL ? gGT->level1->cnt_restart_points - 1 : -1;

	memcpy(s_cortexVortexOxideWeapons, s_cortexVortexOxideWeaponTemplate,
	       sizeof s_cortexVortexOxideWeapons);
	if (lastCheckpoint < 0)
		return;
	for (i = 0; i < 13; i++)
		s_cortexVortexOxideWeapons[i].trackCheckpoint =
		    (u8)AP_OxideWeaponCheckpoint(
		        s_cortexVortexOxideWeaponTemplate[i].trackCheckpoint,
		        lastCheckpoint);
}
#endif

void PickupBots_Init(void)
{
	u16 hub;
	int lev = GAME_TRACKER->levelID;

#ifdef CTR_AP
	// J03: reset before any early return, so no armed driver pointer from an
	// earlier race survives into a custom-page race (review round 1).
	AP_BossAdapterReset(&s_apBossAdapter);
	s_apBossAdapterDriver = NULL;
#endif

#ifdef CTR_CUSTOM_PACKAGES
	// A custom-page race has no hub and no boss: never read the host slot's
	// level metadata. bossWeaponMeta is only read by boss AI.
	if (MainRaceTrack_OfflineCustomLoad())
		return;
#endif

	// get hubID of level
	hub = GAME_LEVEL_METADATA[lev].hubID;

	// If Level ID is Oxide Station
	if (lev == OXIDE_STATION)
	{
		hub = 0;
	}

#ifdef CTR_AP
	// #169 boss track shuffle: a garage boss on a drawn venue keeps its own
	// retail attack, mapped onto this track's checkpoints. The venue's own
	// hubID would hand it another boss's table, or none on a trial track.
	// J03: also N. Oxide's composed final record (before the Cortex Vortex
	// table, which it replaces) and an armed Purple Gem Cup gauntlet leg.
	{
		struct MetaDataBOSS *venueMeta = AP_BossVenueAttackSchedule(1);
		if (venueMeta != NULL)
		{
			sdata->bossWeaponMeta = venueMeta;
			return;
		}
	}
#endif

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
	if (CustomTrack_OxideFinalServing(lev, GAME_TRACKER->bossID,
	                                  (GAME_TRACKER->gameMode1 & ADVENTURE_BOSS) != 0))
	{
		PickupBots_InitCortexVortexOxideWeapons(GAME_TRACKER);
		sdata->bossWeaponMeta = s_cortexVortexOxideWeapons;
		return;
	}
#endif

	if ((s16)hub > -1)
	{
		// set pointer to boss weapon meta
		sdata->bossWeaponMeta = GAME_BOSS_WEAPON_METADATA[(s16)hub];
	}
	return;
}

enum
{
	PICKUPBOTS_ITEM_NONE = HELD_ITEM_NONE,
	PICKUPBOTS_ITEM_INVALID = -1,
	PICKUPBOTS_ITEM_BOMB = HELD_ITEM_BOMB_1X,
	PICKUPBOTS_ITEM_MISSILE = HELD_ITEM_MISSILE_1X,
	PICKUPBOTS_ITEM_TNT = HELD_ITEM_TNT,
	PICKUPBOTS_ITEM_POTION = HELD_ITEM_POTION,
	PICKUPBOTS_SHOOT_ID_BOMB_MISSILE = 2,
	PICKUPBOTS_SHOOT_FLAG_RANDOM = 0x1,
	PICKUPBOTS_SHOOT_FLAG_BACKWARD = 0x2,
	PICKUPBOTS_VOICELINE_BOMB = 10,
	PICKUPBOTS_VOICELINE_MISSILE = 11,
	PICKUPBOTS_VOICELINE_MINE_DROP = 0xf,
	PICKUPBOTS_CLOSE_DIST_SQ_BIAS = 0x90001,
	PICKUPBOTS_CLOSE_DIST_SQ_RANGE = 0x13affff,
	PICKUPBOTS_COOLDOWN_RANDOM_MASK = 0xff,
	PICKUPBOTS_COOLDOWN_BASE_FRAMES = 0xf0,
	PICKUPBOTS_LEADING_ATTACK_ROLL_MOD = 200,
	PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_MOD = 100,
	PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_THRESHOLD = 0x32,
	PICKUPBOTS_TRAILING_ATTACK_ROLL_MOD = 800,
	PICKUPBOTS_TRAILING_ATTACK_MISSILE_THRESHOLD = 2,
	PICKUPBOTS_TRAILING_ATTACK_BOMB_THRESHOLD = 4,
	PICKUPBOTS_TRAILING_ATTACK_DISTANCE_TO_FINISH_MIN = 16000,
	PICKUPBOTS_BOSS_PATH_REQUEST_FRAMES = 0x1e,
	PICKUPBOTS_BOSS_JUICE_COUNTER_MAX = 5,
	PICKUPBOTS_BOSS_SPEED_MIN = 0x1f41,
	PICKUPBOTS_BOSS_COOLDOWN_RANDOM_MASK = 0x10,
	PICKUPBOTS_BOSS_COOLDOWN_BASE_FRAMES = 0xc,
	PICKUPBOTS_BOSS_LOSS_COOLDOWN_STEP = 4,
	PICKUPBOTS_BOSS_CHECKPOINT_DISTANCE_SHIFT = 3,
	PICKUPBOTS_UPDATE_START_DELAY = 0x4b00,
	PICKUPBOTS_UPDATE_NEGATIVE_MODE_START_DELAY = 0x12c0,
};


static inline void PickupBots_SetCooldown(struct Driver *bot)
{
	bot->botData.weaponCooldown = FPS_DOUBLE((MixRNG_Scramble() & PICKUPBOTS_COOLDOWN_RANDOM_MASK) + PICKUPBOTS_COOLDOWN_BASE_FRAMES);
}

static inline void PickupBots_PlayVoice(u32 voiceID, struct Driver *attacker, struct Driver *victim)
{
	Voiceline_RequestPlay(voiceID, GAME_CHARACTER_IDS[attacker->driverID], GAME_CHARACTER_IDS[victim->driverID]);
}

static inline void PickupBots_UpdateArcade(struct GameTracker *initialGT)
{
	register int i CTR_PSX_REGISTER("s4") = 0;

	if ((initialGT->numPlyrCurrGame == 0) || NativeDev_NoItems())
	{
		return;
	}

	do
	{
		struct Driver *player = GAME_TRACKER->drivers[i];

		if (player->driverRank != 0)
		{
			struct Driver *bot = GAME_TRACKER->driversInRaceOrder[player->driverRank - 1];

#if defined(CTR_NATIVE)
			// NOTE(aalhendi): Retail can read PS1 low memory when a rank slot is empty.
			if (bot != NULL)
			{
#endif
				if (((bot->actionsFlagSet & ACTION_BOT) != 0) && ((bot->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) == 0) &&
				    ((bot->actionsFlagSet & ACTION_RACE_FINISHED) == 0) && (bot->botData.weaponCooldown == 0) && (bot->instTntRecv == NULL) &&
				    (bot->clockReceive == 0))
				{
					register int playerX CTR_PSX_REGISTER("v1") = player->instSelf->matrix.t[0];
					register int botX CTR_PSX_REGISTER("v0") = bot->instSelf->matrix.t[0];
					register int x CTR_PSX_REGISTER("v1") = playerX - botX;
					int xSquared = x * x;
					register int playerZ CTR_PSX_REGISTER("v1") = player->instSelf->matrix.t[2];
					register int botZ CTR_PSX_REGISTER("v0") = bot->instSelf->matrix.t[2];
					register int z CTR_PSX_REGISTER("v1") = playerZ - botZ;
					int zSquared = z * z;
					register u32 range CTR_PSX_REGISTER("v0") = PICKUPBOTS_CLOSE_DIST_SQ_RANGE - 1;
					register u32 negativeBias CTR_PSX_REGISTER("v1") = (u32)-PICKUPBOTS_CLOSE_DIST_SQ_BIAS;
					register u32 distance CTR_PSX_REGISTER("a0") = xSquared + zSquared;

					if (distance + negativeBias <= range)
					{
						int rng = MixRNG_Scramble() % PICKUPBOTS_LEADING_ATTACK_ROLL_MOD;
						register struct Driver *shootBot CTR_PSX_REGISTER("a0") = bot;

						if (rng == 0)
						{
							int weaponID;

							if ((bot->lapIndex != 0) &&
							    (MixRNG_Scramble() % PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_MOD < PICKUPBOTS_LEADING_ATTACK_POWERED_ROLL_THRESHOLD))
							{
								bot->numWumpas = DRIVER_WUMPA_JUICED_COUNT;
							}

							if ((GAME_TRACKER->elapsedEventTime & 1) != 0)
							{
								register struct Driver *voiceBot CTR_PSX_REGISTER("a0") = bot;
								bot->heldItemID = PICKUPBOTS_ITEM_TNT;

								if ((player->actionsFlagSet & ACTION_BOT) == 0)
								{
									CTR_PSX_KEEP_VALUE(voiceBot);
									PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MINE_DROP, bot, player);
									{
										register struct Driver *afterVoiceBot CTR_PSX_REGISTER("a0") = bot;
										CTR_PSX_KEEP_VALUE(afterVoiceBot);
									}
								}

								weaponID = PICKUPBOTS_ITEM_TNT;
							}
							else
							{
								register struct Driver *voiceBot CTR_PSX_REGISTER("a0") = bot;
								bot->heldItemID = PICKUPBOTS_ITEM_POTION;

								if ((player->actionsFlagSet & ACTION_BOT) == 0)
								{
									CTR_PSX_KEEP_VALUE(voiceBot);
									PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MINE_DROP, bot, player);
									{
										register struct Driver *afterVoiceBot CTR_PSX_REGISTER("a0") = bot;
										CTR_PSX_KEEP_VALUE(afterVoiceBot);
									}
								}
								weaponID = PICKUPBOTS_ITEM_POTION;
							}

							VehPickupItem_ShootNow(shootBot, weaponID, 0);
							bot->numWumpas = 0;
							PickupBots_SetCooldown(bot);
						}
						else if (rng == 1)
						{
							bot->heldItemID = PICKUPBOTS_ITEM_BOMB;

							if ((player->actionsFlagSet & ACTION_BOT) == 0)
							{
								PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_BOMB, bot, player);
							}

							VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, 0);
							PickupBots_SetCooldown(bot);
						}
						else if (rng == 2)
						{
							bot->heldItemID = PICKUPBOTS_ITEM_MISSILE;

							if ((player->actionsFlagSet & ACTION_BOT) == 0)
							{
								PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MISSILE, bot, player);
							}

							VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, 0);
							PickupBots_SetCooldown(bot);
						}

						bot->heldItemID = PICKUPBOTS_ITEM_NONE;
					}
				}
#if defined(CTR_NATIVE)
			}
#endif
		}

		if (player->driverRank < 3)
		{
			struct Driver *bot = GAME_TRACKER->driversInRaceOrder[player->driverRank + 1];

#if defined(CTR_NATIVE)
			if (bot != NULL)
			{
#endif
				if (((bot->actionsFlagSet & ACTION_BOT) != 0) && ((bot->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) == 0) &&
				    ((bot->actionsFlagSet & ACTION_RACE_FINISHED) == 0) && (bot->botData.weaponCooldown == 0) && (bot->instTntRecv == NULL) &&
				    (bot->clockReceive == 0) &&
				    (((int)player->lapIndex < (int)GAME_TRACKER->numLaps) ||
				     ((s32)player->distanceToFinish_curr > PICKUPBOTS_TRAILING_ATTACK_DISTANCE_TO_FINISH_MIN)))
				{
					register int playerX CTR_PSX_REGISTER("v1") = player->instSelf->matrix.t[0];
					register int botX CTR_PSX_REGISTER("v0") = bot->instSelf->matrix.t[0];
					register int x CTR_PSX_REGISTER("v1") = playerX - botX;
					int xSquared = x * x;
					register int playerZ CTR_PSX_REGISTER("v1") = player->instSelf->matrix.t[2];
					register int botZ CTR_PSX_REGISTER("v0") = bot->instSelf->matrix.t[2];
					register int z CTR_PSX_REGISTER("v1") = playerZ - botZ;
					int zSquared = z * z;
					register u32 range CTR_PSX_REGISTER("v0") = PICKUPBOTS_CLOSE_DIST_SQ_RANGE - 1;
					register u32 negativeBias CTR_PSX_REGISTER("v1") = (u32)-PICKUPBOTS_CLOSE_DIST_SQ_BIAS;
					register u32 distance CTR_PSX_REGISTER("a0") = xSquared + zSquared;

					if (distance + negativeBias <= range)
					{
						int rng = MixRNG_Scramble() % PICKUPBOTS_TRAILING_ATTACK_ROLL_MOD;
						register int weaponID CTR_PSX_REGISTER("v0");

						if (rng < PICKUPBOTS_TRAILING_ATTACK_MISSILE_THRESHOLD)
						{
							if (bot->lapIndex != (GAME_TRACKER->numLaps - 1))
							{
								weaponID = PICKUPBOTS_ITEM_MISSILE;
								goto TrailingShoot;
							}
						}
						if (rng >= PICKUPBOTS_TRAILING_ATTACK_BOMB_THRESHOLD)
						{
							goto TrailingNoShot;
						}
						weaponID = PICKUPBOTS_ITEM_BOMB;
					TrailingShoot:
						bot->heldItemID = weaponID;

						if ((player->actionsFlagSet & ACTION_BOT) == 0)
						{
							PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MISSILE, bot, player);
						}

						VehPickupItem_ShootNow(bot, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, 0);
						PickupBots_SetCooldown(bot);

					TrailingNoShot:
						bot->heldItemID = PICKUPBOTS_ITEM_NONE;
					}
				}
#if defined(CTR_NATIVE)
			}
#endif
		}
		i++;
	} while (i < GAME_TRACKER_RELOAD()->numPlyrCurrGame);
}

#ifdef CTR_AP
// #169 tier 3: boss_loss_easing off keeps a composed kit's pressure the
// same on every try; the retail per-loss bonus applies everywhere else.
// J03: a gauntlet leg is no garage race and has no loss count.
static inline int PickupBots_BossLossCount(void)
{
	if (!AP_BossLossEasingNow() || (AP_BossGauntletAttacker() >= 0))
	{
		return 0;
	}
	return (s8)GAME_ADV_PROGRESS.timesLostBossRace[GAME_TRACKER->bossID];
}
#define PICKUPBOTS_BOSS_LOSS_COUNT() PickupBots_BossLossCount()
#else
#define PICKUPBOTS_BOSS_LOSS_COUNT() ((s8)GAME_ADV_PROGRESS.timesLostBossRace[GAME_TRACKER->bossID])
#endif

static inline void PickupBots_SetBossCooldown(struct MetaDataBOSS *bossMeta)
{
	*(volatile s16 *)&sdata->bossWeaponCooldown = FPS_DOUBLE((RngDeadCoed(&GAME_ADV_RNG) & PICKUPBOTS_BOSS_COOLDOWN_RANDOM_MASK) +
	                                                         (bossMeta->weaponCooldown + PICKUPBOTS_BOSS_COOLDOWN_BASE_FRAMES) +
	                                                         (PICKUPBOTS_BOSS_LOSS_COUNT() * PICKUPBOTS_BOSS_LOSS_COOLDOWN_STEP));
}

static inline struct MetaDataBOSS *PickupBots_GetInitialBossMeta(void)
{
	struct GameTracker *gGT = GAME_TRACKER;

#ifdef CTR_AP
	{
		struct MetaDataBOSS *venueMeta = AP_BossVenueAttackSchedule(0);
		if (venueMeta != NULL)
			return venueMeta;
	}
#endif

#if defined(CTR_AP) && defined(CTR_CUSTOM_TRACKS)
	if (CustomTrack_OxideFinalServing((int)gGT->levelID, gGT->bossID,
	                                  (gGT->gameMode1 & ADVENTURE_BOSS) != 0))
		return s_cortexVortexOxideWeapons;
#endif

	if (gGT->levelID == OXIDE_STATION)
	{
		return GAME_BOSS_WEAPON_METADATA[0];
	}

	return GAME_BOSS_WEAPON_METADATA[GAME_LEVEL_METADATA[gGT->levelID].hubID];
}

#ifdef CTR_AP
// J03 boss adapter: the advanced families (decision 6, Wayfinder D2).
// Bots never run VehPhysProc, so the timers it keeps for a human (mask flag,
// invisibility, super engine) are kept here for the one driver the adapter
// armed. The decisions are ap/ap_boss_adapter.h.

// The schedule row the boss is on and the last real row, from the live table.
static void PickupBots_AdapterSegment(struct MetaDataBOSS *bossMeta, int *segment, int *lastSegment)
{
	struct MetaDataBOSS *base = PickupBots_GetInitialBossMeta();
	int i;

	*segment = 0;
	*lastSegment = 0;
	if (base == NULL)
		return;
	for (i = 0; i < 16 && base[i].throwFlag != 0; i++)
		*lastSegment = i;
	if (bossMeta >= base && bossMeta - base < 16)
		*segment = (int)(bossMeta - base);
}

// Is a mask (Aku or Uka) attached to the driver and alive?
static int PickupBots_AdapterMaskAlive(struct Driver *d)
{
	struct Thread *t;

	if (d->instSelf == NULL || d->instSelf->thread == NULL)
		return 0;
	for (t = d->instSelf->thread->childThread; t != NULL; t = t->siblingThread)
		if (((u32)(t->modelIndex - STATIC_AKUAKU) < 2) && (t->flags & THREAD_FLAG_DEAD) == 0)
			return 1;
	return 0;
}

static struct MaskHeadWeapon *PickupBots_AdapterMaskObject(struct Driver *d)
{
	struct Thread *t;

	if (d->instSelf == NULL || d->instSelf->thread == NULL)
		return NULL;
	for (t = d->instSelf->thread->childThread; t != NULL; t = t->siblingThread)
		if (((u32)(t->modelIndex - STATIC_AKUAKU) < 2) && (t->flags & THREAD_FLAG_DEAD) == 0)
			return (struct MaskHeadWeapon *)t->object;
	return NULL;
}

static void PickupBots_AdapterEndInvisibility(struct Driver *d)
{
	if (d->invisibleTimer == 0)
		return;
	d->invisibleTimer = 0;
	d->instSelf->flags = d->instFlagsBackup;
	d->instSelf->alphaScale = 0;
	OtherFX_Play(0x62, 1);
}

// End every self-state of the armed driver: finish, race over, race exit.
// A held shield is popped here too: its own thread only pops it once its
// driver finishes, and the race can end while the boss is still driving
// (review round 1). A reload frees it with the level.
static void PickupBots_AdapterEndAll(struct Driver *d)
{
	struct MaskHeadWeapon *mask;

	if (d->instBubbleHold != NULL && d->instBubbleHold->thread != NULL)
	{
		struct Shield *shield = (struct Shield *)d->instBubbleHold->thread->object;
		if (shield != NULL)
			shield->flags |= SHIELD_FLAG_POP_ON_DAMAGE; // the retail pop VehPickState uses
		d->instBubbleHold = NULL;
	}

	PickupBots_AdapterEndInvisibility(d);
	d->superEngineTimer = 0;
	s_apBossAdapter.superMs = 0;
	if (s_apBossAdapter.maskOn)
	{
		mask = PickupBots_AdapterMaskObject(d);
		if (mask != NULL)
			mask->duration = 0; // RB_MaskWeapon_ThTick fades it away
		d->actionsFlagSet &= ~ACTION_MASK_WEAPON;
		s_apBossAdapter.maskOn = 0;
	}
	d->invincibleTimer = 0;
}

// Fire one advanced segment (called with the boss's cooldown already reset).
static void PickupBots_FireAdvanced(struct Driver *boss, struct Driver *player, struct MetaDataBOSS *bossMeta)
{
	struct GameTracker *gGT = GAME_TRACKER;
	AP_BossAdapterFacts f;
	int decision;
	u8 oldWumpa;

	PickupBots_AdapterSegment(bossMeta, &f.segment, &f.lastSegment);
	f.weapon = bossMeta->weaponType;
	f.lap = boss->lapIndex;
	f.numLaps = (s8)gGT->numLaps;
	AP_BossAdapterEnter(&s_apBossAdapter, f.lap, f.segment);
	if (s_apBossAdapter.used)
		return;
	// Only one driver is armed per race.
	if (s_apBossAdapterDriver != NULL && s_apBossAdapterDriver != boss)
		return;
	f.bossRank = boss->driverRank;
	f.playerRank = player->driverRank;
	f.playerClocked = player->clockReceive != 0;
	f.warpOrbInPlay = (gGT->gameMode1 & WARPBALL_HELD) != 0;
	f.shieldHeld = boss->instBubbleHold != NULL;
	f.maskActive = s_apBossAdapter.maskOn && PickupBots_AdapterMaskAlive(boss);
	f.invisible = boss->invisibleTimer != 0;
	f.superEngine = boss->superEngineTimer != 0;
	f.superAllowed = AP_BossFinalComposedNow();
	decision = AP_BossAdapterDecide(&s_apBossAdapter, &f);
	AP_BossAdapterCommit(&s_apBossAdapter, &f, decision);
	if (decision != AP_BA_FIRE)
		return;
	s_apBossAdapterDriver = boss;

	// The juice bit picks the retail juiced duration (invisibility only).
	oldWumpa = boss->numWumpas;
	boss->numWumpas = ((bossMeta->juiceFlag & BOSS_WEAPON_JUICED) != 0) ? DRIVER_WUMPA_JUICED_COUNT : 0;
	boss->heldItemID = (u8)f.weapon;

	switch (f.weapon)
	{
	case AP_BA_MASK:
	{
		// Retail MaskUseWeapon picks Aku or Uka from the racer's side and is
		// silent for a bot; its activation sound is the telegraph here.
		int goodGuy = VehPickupItem_MaskBoolGoodGuy(boss);
		OtherFX_Play((STATIC_UKAUKA - goodGuy) + 0x1A, 1);
		VehPickupItem_ShootNow(boss, f.weapon, 0);
		if (PickupBots_AdapterMaskAlive(boss))
			boss->actionsFlagSet |= ACTION_MASK_WEAPON;
		else
			s_apBossAdapter.maskOn = 0; // pool full: no mask, nothing to keep
		break;
	}
	default:
		// turbo, shield, clock, warp orb, invisibility, super engine: the retail
		// item with its own retail cue (fire, bubble, clock, orb, fade sound);
		// decision 6 asks no extra warning for the clock.
		VehPickupItem_ShootNow(boss, f.weapon, 0);
		break;
	}

	boss->heldItemID = PICKUPBOTS_ITEM_NONE;
	boss->numWumpas = oldWumpa;
}

// Every unpaused race frame: keep and expire the armed driver's self-states.
static void PickupBots_AdapterTick(void)
{
	struct GameTracker *gGT = GAME_TRACKER;
	struct Driver *d = s_apBossAdapterDriver;
	int ms, rule, timer;
	int live = 0, i;

	if (!s_apBossAdapter.armed || d == NULL)
		return;
	for (i = 0; i < 8; i++)
		if (gGT->drivers[i] == d)
			live = 1;
	if (!live || d->instSelf == NULL)
	{
		// The driver left the race (exit or reload): nothing to restore.
		AP_BossAdapterReset(&s_apBossAdapter);
		s_apBossAdapterDriver = NULL;
		return;
	}
	ms = gGT->elapsedTimeMS;
	rule = AP_BossAdapterEndRule((d->actionsFlagSet & ACTION_RACE_FINISHED) != 0,
	                             (gGT->gameMode1 & END_OF_RACE) != 0,
	                             (d->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) != 0);
	if (rule == AP_BA_END_ALL)
	{
		PickupBots_AdapterEndAll(d);
		return;
	}
	if (rule == AP_BA_END_INVISIBILITY)
		PickupBots_AdapterEndInvisibility(d);

	// Invisibility: retail countdown and restore (VehPhysProc for a human).
	if (d->invisibleTimer != 0)
	{
		timer = d->invisibleTimer;
		if (AP_BossAdapterCountdown(&timer, ms))
		{
			d->invisibleTimer = 1;
			PickupBots_AdapterEndInvisibility(d);
		}
		else
			d->invisibleTimer = timer;
	}

	// Super engine: retail 0.12 s reserves top-up while the timer runs.
	if (d->superEngineTimer != 0)
	{
		timer = d->superEngineTimer;
		AP_BossAdapterCountdown(&timer, ms);
		d->superEngineTimer = (s16)timer;
		if (d->superEngineTimer != 0 && d->botData.aiPhysics.speedLinear > 0)
			VehFire_Increment(d, 120, (TURBO_PAD | SUPER_ENGINE), 0x80);
	}

	// Mask: invincible (PickupBots_AdapterProtect), damages on contact
	// (ACTION_MASK_WEAPON, VehPhysCrash_Attack) and faster (single turbo
	// reserves while it lasts). The mask thread owns the retail duration.
	if (s_apBossAdapter.maskOn)
	{
		if (PickupBots_AdapterMaskAlive(d))
		{
			d->actionsFlagSet |= ACTION_MASK_WEAPON;
			if (d->reserves < 0x40)
				VehFire_Increment(d, 0x78, 1, 0x80);
		}
		else
		{
			d->actionsFlagSet &= ~ACTION_MASK_WEAPON;
			s_apBossAdapter.maskOn = 0;
		}
	}

	// Shield pop invincibility (retail VehPickState sets 0x2a0 ms).
	if (d->invincibleTimer != 0)
	{
		timer = d->invincibleTimer;
		AP_BossAdapterCountdown(&timer, ms);
		d->invincibleTimer = timer;
	}
}

// RB_Hazard_HurtDriver, bot branch: retail VehPickState protection for the
// armed driver only. Returns 1 when the hit is absorbed (no damage, no Hit
// check, no DeathLink).
int PickupBots_AdapterProtect(struct Driver *victim)
{
	if (!s_apBossAdapter.armed || victim == NULL || victim != s_apBossAdapterDriver)
		return 0;
	if (victim->kartState == KS_MASK_GRABBED)
		return 0;
	if ((s_apBossAdapter.maskOn && (victim->actionsFlagSet & ACTION_MASK_WEAPON) != 0) || victim->invincibleTimer != 0)
	{
		Voiceline_RequestPlay(2, GAME_CHARACTER_IDS[victim->driverID], 0x10);
		return 1;
	}
	if (victim->instBubbleHold != NULL)
	{
		struct Shield *shieldObj = victim->instBubbleHold->thread->object;
		shieldObj->flags |= SHIELD_FLAG_POP_ON_DAMAGE;
		victim->invincibleTimer = 0x2a0;
		victim->instBubbleHold = NULL;
		Voiceline_RequestPlay(2, GAME_CHARACTER_IDS[victim->driverID], 0x10);
		return 1;
	}
	return 0;
}

// The armed gauntlet leg's attacker: the AI racing the leg's drawn racer,
// else the first AI.
static struct Driver *PickupBots_GauntletAttacker(void)
{
	struct GameTracker *gGT = GAME_TRACKER;
	int character = AP_BossGauntletAttacker();
	int i;

	if (character < 0)
		return NULL;
	for (i = 1; i < 8; i++)
	{
		struct Driver *d = gGT->drivers[i];
		if (d != NULL && (d->actionsFlagSet & ACTION_BOT) != 0 && GAME_CHARACTER_IDS[d->driverID] == character)
			return d;
	}
	if (gGT->drivers[1] != NULL && (gGT->drivers[1]->actionsFlagSet & ACTION_BOT) != 0)
		return gGT->drivers[1];
	return NULL;
}
#endif


#ifdef CTR_AP
// J03: a Purple Gem Cup gauntlet leg runs this for its attacker, not for
// drivers[1].
static inline void PickupBots_UpdateBoss(struct GameTracker *gGT, struct Driver *boss)
{
#else
static inline void PickupBots_UpdateBoss(struct GameTracker *gGT)
{
	struct Driver *boss = gGT->drivers[1];
#endif
	struct Driver *player = gGT->drivers[0];
	register struct MetaDataBOSS *bossMeta CTR_PSX_REGISTER("s1") = sdata->bossWeaponMeta;
	register int weaponID CTR_PSX_REGISTER("s0");
	register int encodedWeaponType CTR_PSX_REGISTER("v1");
	int newWumpa;
	int throwFlag;
	int weaponFlags;

	if (((boss->botData.botFlags & BOT_FLAG_DAMAGE_ACTIVE) != 0) || ((boss->actionsFlagSet & ACTION_RACE_FINISHED) != 0) || (boss->instTntRecv != NULL) ||
	    (boss->clockReceive != 0) || (boss->botData.aiPhysics.speedLinear < PICKUPBOTS_BOSS_SPEED_MIN))
	{
		goto BossCooldown;
	}

	{
		struct MetaDataBOSS *nextMeta = &bossMeta[1];

		if (nextMeta->throwFlag == 0)
		{
			int threshold = gGT->level1->ptr_restart_points[bossMeta->trackCheckpoint].distToFinish << PICKUPBOTS_BOSS_CHECKPOINT_DISTANCE_SHIFT;

			if (threshold < (int)boss->distanceToFinish_curr)
			{
				s16 preservedThrow = -1;

				if (((bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION) || (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)) &&
				    (sdata->bossJuiceCounter == PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
				{
					preservedThrow = bossMeta->throwFlag;
				}

				bossMeta = PickupBots_GetInitialBossMeta();

				if (preservedThrow != -1)
				{
					bossMeta->throwFlag = preservedThrow;
				}
			}
		}
		else
		{
			int threshold = gGT->level1->ptr_restart_points[nextMeta->trackCheckpoint].distToFinish << PICKUPBOTS_BOSS_CHECKPOINT_DISTANCE_SHIFT;

			if ((int)boss->distanceToFinish_curr < threshold)
			{
				s16 preservedThrow = -1;

				if (((bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION) || (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)) &&
				    (sdata->bossJuiceCounter == PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
				{
					preservedThrow = bossMeta->throwFlag;
				}

				bossMeta = nextMeta;

				if (preservedThrow != -1)
				{
					bossMeta->throwFlag = preservedThrow;
				}
			}
		}

		sdata->bossWeaponMeta = bossMeta;
	}

#ifdef CTR_AP
	// J03: the boss lane requests belong to the garage boss race only; a
	// gauntlet attacker keeps its ordinary race lanes.
	if ((bossMeta->pathChangeDisabled == 0) && ((gGT->gameMode1 & ADVENTURE_BOSS) != 0))
#else
	if (bossMeta->pathChangeDisabled == 0)
#endif
	{
		s16 pathTimer = sdata->bossPathRequestTimer;
		u16 pathTimerUnsigned = sdata->bossPathRequestTimer;

		if (pathTimer == PICKUPBOTS_BOSS_PATH_REQUEST_FRAMES)
		{
			if ((*(volatile u32 *)&boss->botData.botFlags & BOT_FLAG_BOSS_PATH_ACTIVE) != 0)
			{
				goto PathRequestDone;
			}

			if (sdata->bossPathRequestPhase != 0)
			{
				if (boss->botData.botPath == 0)
				{
					goto RequestPathOne;
				}
				if (boss->botData.botPath == 1)
				{
					u32 flags = boss->botData.botFlags;
					boss->botData.desiredPath_BossOnly = 2;
					sdata->bossPathRequestTimer = 0;
					// NOTE(aalhendi): Keep this reset before the shared flag-store tail.
					*(volatile s16 *)&sdata->bossPathRequestPhase = 0;
					boss->botData.botFlags = flags | BOT_FLAG_BOSS_PATH_REQUESTED;
				}
				goto PathRequestDone;
			}

			if (boss->botData.botPath != 2)
			{
				goto RequestPathZero;
			}

		RequestPathOne:
		{
			u32 flags = *(volatile u32 *)&boss->botData.botFlags;
			boss->botData.desiredPath_BossOnly = 1;
			sdata->bossPathRequestTimer = 0;
			boss->botData.botFlags = flags | BOT_FLAG_BOSS_PATH_REQUESTED;
		}
			goto PathRequestDone;

		RequestPathZero:
		{
			register s32 path CTR_PSX_REGISTER("v1") = boss->botData.botPath;
			if (path != 1)
			{
				goto PathRequestDone;
			}
			{
				u32 flags = *(volatile u32 *)&boss->botData.botFlags;
				boss->botData.desiredPath_BossOnly = 0;
				sdata->bossPathRequestTimer = 0;
				sdata->bossPathRequestPhase = path;
				boss->botData.botFlags = flags | BOT_FLAG_BOSS_PATH_REQUESTED;
			}
		}
		}
		else if ((boss->botData.botFlags & BOT_FLAG_BOSS_PATH_REQUESTED) == 0)
		{
			sdata->bossPathRequestTimer = pathTimerUnsigned + 1;
		}

	PathRequestDone:;
	}

	{
		s16 cooldown = sdata->bossWeaponCooldown;
		u16 cooldownUnsigned = sdata->bossWeaponCooldown;

		if (cooldown > 0)
		{
			*(volatile s16 *)&sdata->bossWeaponCooldown = cooldownUnsigned - 1;
			return;
		}
	}

	weaponFlags = 0;
	sdata->bossWeaponCooldown = FPS_DOUBLE((RngDeadCoed(&GAME_ADV_RNG) & PICKUPBOTS_BOSS_COOLDOWN_RANDOM_MASK) +
	                                       (bossMeta->weaponCooldown + PICKUPBOTS_BOSS_COOLDOWN_BASE_FRAMES) +
	                                       (PICKUPBOTS_BOSS_LOSS_COUNT() * PICKUPBOTS_BOSS_LOSS_COOLDOWN_STEP));

#ifdef CTR_AP
	// J03: the advanced families go through the boss adapter, never the
	// projectile branch below. Retail tables never carry these codes.
	if (AP_BossAdapterIsAdvanced(bossMeta->weaponType))
	{
		PickupBots_FireAdvanced(boss, player, bossMeta);
		return;
	}
#endif

	weaponID = bossMeta->weaponType;
	encodedWeaponType = weaponID;
	if (encodedWeaponType == BOSS_WEAPON_ENCODED_TNT)
	{
		weaponID = PICKUPBOTS_ITEM_TNT;
	}
	else if (encodedWeaponType == BOSS_WEAPON_ENCODED_BOMB)
	{
		weaponID = PICKUPBOTS_ITEM_BOMB;
	}
	else if (encodedWeaponType == BOSS_WEAPON_ENCODED_POTION)
	{
		weaponID = PICKUPBOTS_ITEM_POTION;
	}
	else if (encodedWeaponType == BOSS_WEAPON_NONE)
	{
		weaponID = PICKUPBOTS_ITEM_INVALID;
	}
	{
		u16 juiceFlag = bossMeta->juiceFlag;
		s16 juiceCounter;
		u16 juiceCounterUnsigned;

		if ((juiceFlag & BOSS_WEAPON_RANDOM_JUICE) == 0)
		{
			goto BossJuiceNoRandom;
		}

		juiceCounter = sdata->bossJuiceCounter;
		juiceCounterUnsigned = sdata->bossJuiceCounter;
		if (juiceCounter < PICKUPBOTS_BOSS_JUICE_COUNTER_MAX)
		{
			goto BossJuiceIncrement;
		}

		if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_TNT)
		{
#if defined(CTR_NATIVE)
			if (bossMeta->throwFlag != BOSS_WEAPON_NORMAL)
			{
				weaponID = PICKUPBOTS_ITEM_TNT;
#else
			// NOTE(aalhendi): Retail sets the TNT ID in this branch's delay slot.
			// GCC 2.8.1 places the juice copy there instead, so this one PSX
			// branch targets the reset block while the native path stays in C.
			__asm__ volatile(
				"lbu $3,%1\n\t"
				"li $2,%2\n\t"
				".set\tnoreorder\n\t"
				"beq $3,$2,1f\n\t"
				"addiu %0,$0,%3\n\t"
				".set\treorder"
				: "=r"(weaponID)
				: "m"(bossMeta->throwFlag), "I"(BOSS_WEAPON_NORMAL), "I"(PICKUPBOTS_ITEM_TNT)
				: "v0", "v1", "memory");
			{
#endif
				register u16 newJuice CTR_PSX_REGISTER("v0") = juiceFlag;
				register s16 counter CTR_PSX_REGISTER("v1");
				CTR_PSX_KEEP_VALUE_RELAXED(newJuice);
				counter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
				newJuice |= BOSS_WEAPON_JUICED;
				bossMeta->throwFlag = weaponID;
				sdata->bossJuiceCounter = counter;
				bossMeta->juiceFlag = newJuice;
				goto BossJuiceDone;
			}
		}
		else if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_BOMB)
		{
			if ((juiceFlag & BOSS_WEAPON_JUICED) == 0)
			{
				bossMeta->juiceFlag = juiceFlag | BOSS_WEAPON_JUICED;
				*(volatile s16 *)&sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
				weaponID = PICKUPBOTS_ITEM_TNT;
				goto BossJuiceDone;
			}

			weaponID = PICKUPBOTS_ITEM_BOMB;
			bossMeta->juiceFlag = juiceFlag & ~BOSS_WEAPON_JUICED;
			sdata->bossJuiceCounter = 0;
			goto BossJuiceDone;
		}
		else if (bossMeta->weaponType == BOSS_WEAPON_ENCODED_POTION)
		{
			weaponID = PICKUPBOTS_ITEM_POTION;

			if (bossMeta->throwFlag != BOSS_WEAPON_NORMAL)
			{
				u16 currentJuice = *(volatile u16 *)&bossMeta->juiceFlag;
				bossMeta->throwFlag = BOSS_WEAPON_NORMAL;
				sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
				bossMeta->juiceFlag = currentJuice | BOSS_WEAPON_JUICED;
				goto BossJuiceDone;
			}
		}
		else
		{
			goto BossJuiceDone;
		}

		{
			u16 resetJuice;
#if !defined(CTR_NATIVE)
			// NOTE(aalhendi): Local label 1 is the TNT branch target and stays
			// valid if the enclosing inline function is ever emitted twice.
			__asm__ volatile("1:");
#endif
			// NOTE(aalhendi): Reload here because the PSX branch above also
			// reaches this block outside the compiler's C control-flow graph.
			resetJuice = *(volatile u16 *)&bossMeta->juiceFlag;
			bossMeta->throwFlag = BOSS_WEAPON_THROW;
			sdata->bossJuiceCounter = 0;
			bossMeta->juiceFlag = resetJuice & ~BOSS_WEAPON_JUICED;
		}
		goto BossJuiceDone;

BossJuiceIncrement:
		*(volatile s16 *)&sdata->bossJuiceCounter = juiceCounterUnsigned + 1;
		goto BossJuiceDone;

	BossJuiceNoRandom:
		sdata->bossJuiceCounter = 0;
	}

BossJuiceDone:
	throwFlag = bossMeta->throwFlag;
	if (throwFlag == BOSS_WEAPON_THROW)
	{
		weaponFlags = PICKUPBOTS_SHOOT_FLAG_RANDOM;
	}

	newWumpa = 0;
	if ((s16)weaponID >= 0)
	{
		register u8 rawWumpa CTR_PSX_REGISTER("v1") = *(volatile u8 *)&boss->numWumpas;
		register int oldWumpa CTR_PSX_REGISTER("s4") = (s8)rawWumpa;
		if ((bossMeta->juiceFlag & BOSS_WEAPON_JUICED) != 0)
		{
			newWumpa = DRIVER_WUMPA_JUICED_COUNT;
		}
		boss->numWumpas = newWumpa;
		boss->heldItemID = weaponID;

		if ((u16)(weaponID - PICKUPBOTS_ITEM_TNT) < 2)
		{
			PickupBots_PlayVoice(PICKUPBOTS_VOICELINE_MINE_DROP, boss, player);
		}
		else
		{
			register u32 bombVoice CTR_PSX_REGISTER("a0") = PICKUPBOTS_VOICELINE_BOMB;
			register const s16 *characterIDs CTR_PSX_REGISTER("a2") = GAME_CHARACTER_IDS;
			register u32 bossID CTR_PSX_REGISTER("v1") = boss->driverID;
			register u32 playerID CTR_PSX_REGISTER("v0") = player->driverID;
			CTR_PSX_KEEP_VALUE(bombVoice);
			CTR_PSX_KEEP_VALUE(bossID);
			CTR_PSX_KEEP_VALUE(playerID);
			weaponFlags |= PICKUPBOTS_SHOOT_FLAG_BACKWARD;
			Voiceline_RequestPlay(bombVoice, characterIDs[bossID], characterIDs[playerID]);
		}

		if (boss->heldItemID == PICKUPBOTS_ITEM_BOMB)
		{
			VehPickupItem_ShootNow(boss, PICKUPBOTS_SHOOT_ID_BOMB_MISSILE, (s16)weaponFlags);
		}
#ifdef CTR_AP
		// #169: Oxide's double potion belongs to Oxide, not to a garage boss
		// whose drawn venue is Oxide Station.
		else if ((boss->heldItemID == PICKUPBOTS_ITEM_POTION) && ((s16)weaponFlags == PICKUPBOTS_SHOOT_FLAG_RANDOM) &&
		         ((GAME_TRACKER->gameMode1 & ADVENTURE_BOSS) != 0) &&
		         (AP_BossRetailRulesLevel(GAME_TRACKER->levelID) == OXIDE_STATION))
#else
		else if ((boss->heldItemID == PICKUPBOTS_ITEM_POTION) && ((s16)weaponFlags == PICKUPBOTS_SHOOT_FLAG_RANDOM) &&
		         (GAME_TRACKER_RELOAD()->levelID == OXIDE_STATION))
#endif
		{
			register s16 repeatWeaponID CTR_PSX_REGISTER("s0") = weaponID;
			VehPickupItem_ShootNow(boss, repeatWeaponID, PICKUPBOTS_SHOOT_FLAG_RANDOM);
			VehPickupItem_ShootNow(boss, repeatWeaponID, PICKUPBOTS_SHOOT_FLAG_RANDOM);
		}
		else
		{
			VehPickupItem_ShootNow(boss, (s16)weaponID, (s16)weaponFlags);

			if ((boss->heldItemID == PICKUPBOTS_ITEM_TNT) && (bossMeta->throwFlag == BOSS_WEAPON_NORMAL) &&
			    (sdata->bossJuiceCounter != PICKUPBOTS_BOSS_JUICE_COUNTER_MAX))
			{
				sdata->bossJuiceCounter = PICKUPBOTS_BOSS_JUICE_COUNTER_MAX;
			}
		}

		boss->heldItemID = PICKUPBOTS_ITEM_NONE;
		boss->numWumpas = oldWumpa;
	}
	return;

BossCooldown:
	PickupBots_SetBossCooldown(bossMeta);
}

void PickupBots_Update(void)
{
	register struct GameTracker *initialGT CTR_PSX_REGISTER("v1") = GAME_TRACKER;
	struct GameTracker *gGT;

#ifdef CTR_AP
	// J03: the boss adapter's self-state timers run every unpaused race frame,
	// before any early return, so expiry and cleanup never wait on a weapon.
	PickupBots_AdapterTick();
#endif

	if ((initialGT->numBotsNextGame == 0) || (initialGT->elapsedEventTime < PICKUPBOTS_UPDATE_START_DELAY))
	{
		if (initialGT->gameMode1 >= 0)
		{
			return;
		}

		if (initialGT->elapsedEventTime < PICKUPBOTS_UPDATE_NEGATIVE_MODE_START_DELAY)
		{
			return;
		}
	}

	gGT = GAME_TRACKER_RELOAD();
	if ((gGT->gameMode1 & (ADVENTURE_BOSS | END_OF_RACE)) == ADVENTURE_BOSS)
	{
#ifdef CTR_AP
		PickupBots_UpdateBoss(gGT, gGT->drivers[1]);
#else
		PickupBots_UpdateBoss(gGT);
#endif
	}
	else
	{
#ifdef CTR_AP
		// J03: an armed Purple Gem Cup gauntlet leg adds one boss-style
		// attacker. Its ordinary AI weapons are held back while the leg
		// is armed, so it never fires two systems in one frame (review
		// round 1): a nonzero bot cooldown fails the arcade readiness test.
		// Same gates as the arcade pass: players present, items not off.
		struct Driver *attacker = NULL;
		if (((gGT->gameMode1 & END_OF_RACE) == 0) && (gGT->numPlyrCurrGame != 0) && !NativeDev_NoItems())
		{
			attacker = PickupBots_GauntletAttacker();
		}
		if ((attacker != NULL) && (attacker->botData.weaponCooldown == 0))
		{
			attacker->botData.weaponCooldown = 1;
		}
		PickupBots_UpdateArcade(gGT);
		if (attacker != NULL)
		{
			PickupBots_UpdateBoss(gGT, attacker);
		}
#else
		PickupBots_UpdateArcade(gGT);
#endif
	}
}
