#include <common.h>

enum
{
	UI_LAP_TIME_LAPS_PER_PLAYER = 7,
	UI_LAP_TIME_TICKS_PER_SECOND = 0x3c0,
	UI_LAP_TIME_TICKS_PER_TENTH_SECOND = 96,
	UI_LAP_TIME_TICKS_PER_TEN_SECONDS = 0x2580,
	UI_LAP_TIME_TICKS_PER_MINUTE = 0xe100,
	UI_LAP_TIME_MAX_MINUTES = 9,
	UI_LAP_TIME_SECONDS_TENS_MOD = 6,
	UI_LAP_TIME_DECIMAL_BASE = 10,
	UI_LAP_TIME_CENTISECOND_SCALE = 100,

	UI_REWARD_WUMPA_SHINE_CENTER = 0x80,
	UI_REWARD_WUMPA_SHINE_SHIFT = 4,
	UI_REWARD_PICKUP_ROT_SLOW = 0x40,
	UI_REWARD_PICKUP_ROT_FAST = 0x80,
	// NOTE(aalhendi): Retail reads the four adjacent HUD bytes as one word:
	// only the HUD-visible bit and zero demo byte decide visibility.
	UI_REWARD_HUD_VISIBLE_WORD_MASK = 0xff0100,
	UI_REWARD_HUD_VISIBLE_WORD_VALUE = 0x100,
	UI_REWARD_FADE_VISIBLE_MIN = 0xfff,
	UI_REWARD_CTR_LETTER_BASE_SCALE = 0x800,
	UI_REWARD_CTR_LETTER_NEGATIVE_SCALE_BIAS = 0x401,
	UI_REWARD_CTR_LETTER_ROT_SHIFT = 10,
	UI_REWARD_CTR_LETTER_ROT_STEP = 0x200,
};

#define UI_REWARD_PICKUP_COLOR 0xffff0000u

#ifdef CTR_NATIVE
// Retail keeps lap digits for two players and seven laps (14 slots), and
// gGT->lapTime has seven entries; a longer race wrote past both. Native
// keeps every lap time here, up to 9 laps (index 9 is the lap in progress
// after the finish line) for every driver, and writes the retail slots only
// when the index fits them.
enum
{
	UI_LAP_TIME_NATIVE_MAX_LAPS = 10,
	UI_LAP_TIME_NATIVE_MAX_DRIVERS = 8,
	UI_LAP_TIME_RETAIL_SLOTS = 2 * UI_LAP_TIME_LAPS_PER_PLAYER,
};

static s32 s_nativeLapTimes[UI_LAP_TIME_NATIVE_MAX_DRIVERS][UI_LAP_TIME_NATIVE_MAX_LAPS];

void UI_LapTime_Reset(void)
{
	memset(s_nativeLapTimes, 0, sizeof(s_nativeLapTimes));
}

s32 UI_LapTime_Get(s32 lapIndex, s16 driverID)
{
	if ((driverID < 0) || (driverID >= UI_LAP_TIME_NATIVE_MAX_DRIVERS) || (lapIndex < 0) || (lapIndex >= UI_LAP_TIME_NATIVE_MAX_LAPS))
	{
		return 0;
	}

	return s_nativeLapTimes[driverID][lapIndex];
}

// Same digits as UI_SaveLapTime, as " M:SS:CC".
void UI_LapTime_Format(char *text, s32 lapTime)
{
	s32 minutes = lapTime / UI_LAP_TIME_TICKS_PER_MINUTE;
	s32 tenSeconds = lapTime / UI_LAP_TIME_TICKS_PER_TEN_SECONDS;
	s32 seconds = lapTime / UI_LAP_TIME_TICKS_PER_SECOND;

	text[0] = ' ';
	text[2] = ':';
	text[5] = ':';
	if (UI_LAP_TIME_MAX_MINUTES < minutes)
	{
		text[1] = '0' + UI_LAP_TIME_MAX_MINUTES;
		text[3] = '0' + UI_LAP_TIME_SECONDS_TENS_MOD - 1;
		text[4] = '0' + UI_LAP_TIME_DECIMAL_BASE - 1;
		text[6] = '0' + UI_LAP_TIME_DECIMAL_BASE - 1;
		text[7] = '0' + UI_LAP_TIME_DECIMAL_BASE - 1;
		return;
	}

	text[1] = (char)('0' + minutes);
	text[3] = (char)('0' + tenSeconds - minutes * UI_LAP_TIME_SECONDS_TENS_MOD);
	text[4] = (char)('0' + seconds - tenSeconds * UI_LAP_TIME_DECIMAL_BASE);
	text[6] = (char)('0' + lapTime / UI_LAP_TIME_TICKS_PER_TENTH_SECOND - seconds * UI_LAP_TIME_DECIMAL_BASE);
	text[7] = (char)('0' + ((s32)((u32)lapTime * UI_LAP_TIME_CENTISECOND_SCALE) / UI_LAP_TIME_TICKS_PER_SECOND) % UI_LAP_TIME_DECIMAL_BASE);
}
#endif

void UI_SaveLapTime(s32 lapIndex, s32 lapTime, s16 driverID)
{
	s32 *minuteSlots = GAME_LAP_MINUTE_UNITS;
	s32 playerLapIndex = ((s32)driverID * UI_LAP_TIME_LAPS_PER_PLAYER) + lapIndex;
	s32 numMinutes;
	s32 tenSeconds;
	s32 seconds;

#ifdef CTR_NATIVE
	if ((driverID >= 0) && (driverID < UI_LAP_TIME_NATIVE_MAX_DRIVERS) && (lapIndex >= 0) && (lapIndex < UI_LAP_TIME_NATIVE_MAX_LAPS))
	{
		s_nativeLapTimes[driverID][lapIndex] = lapTime;
	}

	if ((playerLapIndex < 0) || (playerLapIndex >= UI_LAP_TIME_RETAIL_SLOTS))
	{
		return;
	}
#endif

	numMinutes = (minuteSlots[playerLapIndex] = lapTime / UI_LAP_TIME_TICKS_PER_MINUTE);

	if (UI_LAP_TIME_MAX_MINUTES < numMinutes)
	{
		minuteSlots[playerLapIndex] = UI_LAP_TIME_MAX_MINUTES;
		GAME_LAP_SECOND_TENS[playerLapIndex] = UI_LAP_TIME_SECONDS_TENS_MOD - 1;
		GAME_LAP_SECOND_UNITS[playerLapIndex] = UI_LAP_TIME_DECIMAL_BASE - 1;
		GAME_LAP_CENTISECOND_TENS[playerLapIndex] = UI_LAP_TIME_DECIMAL_BASE - 1;
		GAME_LAP_CENTISECOND_UNITS[playerLapIndex] = UI_LAP_TIME_DECIMAL_BASE - 1;
		return;
	}

	// NOTE(aalhendi): Carry each quotient into the next digit. Scaling ticks
	// by ten first would wrap before division and change the retail result.
	tenSeconds = lapTime / UI_LAP_TIME_TICKS_PER_TEN_SECONDS;
	GAME_LAP_SECOND_TENS[playerLapIndex] = tenSeconds - numMinutes * UI_LAP_TIME_SECONDS_TENS_MOD;
	GAME_LAP_SECOND_UNITS[playerLapIndex] = (seconds = lapTime / UI_LAP_TIME_TICKS_PER_SECOND) - tenSeconds * UI_LAP_TIME_DECIMAL_BASE;
	GAME_LAP_CENTISECOND_TENS[playerLapIndex] = lapTime / UI_LAP_TIME_TICKS_PER_TENTH_SECOND - seconds * UI_LAP_TIME_DECIMAL_BASE;
	// NOTE(aalhendi): Keep the hundredfold product modulo 32 bits, then divide
	// as signed. The direct constant also lets old GCC synthesize shift/adds.
	GAME_LAP_CENTISECOND_UNITS[playerLapIndex] =
	    ((s32)((u32)lapTime * UI_LAP_TIME_CENTISECOND_SCALE) / UI_LAP_TIME_TICKS_PER_SECOND) % UI_LAP_TIME_DECIMAL_BASE;
}

void UI_ThTick_CountPickup(struct Thread *bucket)
{
	struct UiElement3D *obj = bucket->object;
	struct Instance *inst = bucket->inst;
	MATRIX *mat;
	u32 drawOtagState;

	{
		struct GameTracker *gGT = GAME_TRACKER;

		inst->colorRGBA = UI_REWARD_PICKUP_COLOR;

		if ((gGT->numPlyrCurrGame == 1) && (inst->model->id != STATIC_TIME_CRATE_01))
		{
			if (gGT->drivers[0]->numWumpas >= DRIVER_WUMPA_JUICED_COUNT)
			{
				inst->alphaScale = ((s32)CTR_ReadU16LE(&sdata->wumpaShineResult) - UI_REWARD_WUMPA_SHINE_CENTER) * (1 << UI_REWARD_WUMPA_SHINE_SHIFT);
			}
			else
			{
				inst->alphaScale = 0;
			}
		}
	}

	if (inst->model->id == STATIC_TIME_CRATE_01)
	{
		obj->rot.y = (u16)obj->rot.y + FPS_HALF(UI_REWARD_PICKUP_ROT_SLOW);
	}
	else
	{
		obj->rot.y = (u16)obj->rot.y + FPS_HALF(UI_REWARD_PICKUP_ROT_FAST);
	}

	mat = &inst->matrix;
	ConvertRotToMatrix(mat, &obj->rot);
	MatrixRotate(mat, &obj->m, mat);

	drawOtagState = CTR_ReadU32LE(&GAME_TRACKER->bool_DrawOTag_InProgress);
	if ((drawOtagState & UI_REWARD_HUD_VISIBLE_WORD_MASK) != UI_REWARD_HUD_VISIBLE_WORD_VALUE)
	{
		inst->flags |= HIDE_MODEL;
	}
	else
	{
		inst->flags &= ~HIDE_MODEL;
	}
#ifdef CTR_NATIVE
	if (inst->model->id == STATIC_FRUITDISP && GAME_TRACKER->numPlyrCurrGame > 1)
	{
		for (int i = 0; i < GAME_TRACKER->numPlyrCurrGame && i < 4; i++)
		{
			struct Driver *owner = GAME_TRACKER->drivers[i];
			if (!owner || owner->instFruitDisp != inst) continue;
			inst->alphaScale = owner->numWumpas >= DRIVER_WUMPA_JUICED_COUNT ?
			    ((s32)CTR_ReadU16LE(&sdata->wumpaShineResult) - UI_REWARD_WUMPA_SHINE_CENTER) * (1 << UI_REWARD_WUMPA_SHINE_SHIFT) : 0;
			if ((owner->actionsFlagSet & ACTION_RACE_FINISHED) || (GAME_TRACKER->gameMode1 & END_OF_RACE))
				inst->flags |= HIDE_MODEL;
			break;
		}
	}
#endif

}

void UI_ThTick_Reward(struct Thread *bucket)
{
	struct Instance *inst = bucket->inst;
	struct UiElement3D *obj = bucket->object;
	struct GameTracker *gGT;
	MATRIX *mat;
	u32 drawOtagState;

#ifdef CTR_AP
	// AP: re-tint the hub RELIC counter icon to match the tier its count is
	// cycling through (Sapphire -> Gold -> Platinum, in sync with UI_DrawNumRelic's
	// 0x3C/tier timer). ONLY the relic icon; key/trophy/crystal/token/menu-crystal
	// share this tick and are left untouched. Sapphire tint = the vanilla HUD relic
	// colour (0x060a5ff0), so tier 0 looks identical to stock.
	if (ctr_cfg_active() && inst->model->id == STATIC_RELIC)
	{
		static const u32 s_hudRelicTierTint[3] = {0x060a5ff0, 0x0ffc6290, 0x0ebebf50};
		inst->colorRGBA = s_hudRelicTierTint[(GAME_TRACKER->timer / FPS_DOUBLE(0x3C)) % 3];
	}
#endif

	obj->rot.y += FPS_HALF(UI_REWARD_PICKUP_ROT_SLOW);
	Vector_SpecLightSpin2D(inst, &obj->rot, &obj->lightDir);

	mat = &inst->matrix;
	ConvertRotToMatrix(mat, &obj->rot);
	MatrixRotate(mat, &obj->m, mat);

	gGT = GAME_TRACKER;
	drawOtagState = CTR_ReadU32LE(&gGT->bool_DrawOTag_InProgress);
	if (((drawOtagState & UI_REWARD_HUD_VISIBLE_WORD_MASK) != UI_REWARD_HUD_VISIBLE_WORD_VALUE) ||
	    (gGT->pushBuffer_UI.fadeFromBlack_currentValue <= UI_REWARD_FADE_VISIBLE_MIN)
#ifdef CTR_AP
	    // The hub picker owns the whole screen. Its 2D counter calls already
	    // stand down, but these reward icons are persistent 3D instances whose
	    // own thread clears HIDE_MODEL every frame. Gate that authoritative
	    // visibility decision too, or relic/key/trophy icons leak through the
	    // picker even though their numbers are gone.
	    || AP_CharSwap_HubHudHidden()
#endif
	)
	{
		inst->flags |= HIDE_MODEL;
	}
	else
	{
		inst->flags &= ~HIDE_MODEL;
	}
}

void UI_ThTick_CtrLetters(struct Thread *bucket)
{
	struct Instance *inst = bucket->inst;
	struct UiElement3D *obj = bucket->object;
	SVec3 rot;
	MATRIX *mat;

	obj->rot.y += FPS_HALF(UI_REWARD_PICKUP_ROT_SLOW);

	Vector_SpecLightSpin2D(inst, &obj->rot, &obj->lightDir);

	if (((GAME_TRACKER->gameMode1 & END_OF_RACE) != 0) && RaceFlag_IsTransitioning())
	{
		inst->scale.x = 0;
		inst->scale.y = 0;
		inst->scale.z = 0;
	}

	// NOTE(aalhendi): Initialize the complete rotation in each branch; old
	// GCC merges the zero-Z tail before preparing the matrix-call arguments.
	if (inst->scale.x != UI_REWARD_CTR_LETTER_BASE_SCALE)
	{
		s32 endOfRaceTransition;
		rot.x = 0;
		endOfRaceTransition = (s32)inst->scale.x - UI_REWARD_CTR_LETTER_BASE_SCALE;
		if (endOfRaceTransition < 0)
		{
			endOfRaceTransition = (s32)inst->scale.x - UI_REWARD_CTR_LETTER_NEGATIVE_SCALE_BIAS;
		}
		rot.y = ((endOfRaceTransition >> UI_REWARD_CTR_LETTER_ROT_SHIFT) + 1) * UI_REWARD_CTR_LETTER_ROT_STEP;
		rot.z = 0;
	}
	else
	{
		rot.x = 0;
		rot.y = 0;
		rot.z = 0;
	}

	mat = &inst->matrix;
	ConvertRotToMatrix(mat, &rot);

	MatrixRotate(mat, &obj->m, mat);
}

void UI_ThTick_big1(struct Thread *bucket)
{
	struct UiElement3D *obj = bucket->object;
	struct Instance *inst = bucket->inst;
	struct GameTracker *gGT;
	u16 scale;
	u32 drawOtagState;

	// NOTE(aalhendi): Scale is sampled for each diagonal store group. Clear
	// individual halfwords so negative scale never fills an off-diagonal.
	scale = (u16)obj->scale;
	inst->matrix.m[0][1] = 0;
	inst->matrix.m[0][2] = 0;
	inst->matrix.m[1][0] = 0;
	inst->matrix.m[0][0] = scale;

	scale = (u16)obj->scale;
	inst->matrix.m[1][2] = 0;
	inst->matrix.m[2][0] = 0;
	inst->matrix.m[2][1] = 0;
	inst->matrix.m[1][1] = scale;

	scale = (u16)obj->scale;
	inst->matrix.m[2][2] = scale;

	MatrixRotate(&inst->matrix, &obj->m, &inst->matrix);

	gGT = GAME_TRACKER;
	drawOtagState = CTR_ReadU32LE(&gGT->bool_DrawOTag_InProgress);
	if ((drawOtagState & UI_REWARD_HUD_VISIBLE_WORD_MASK) != UI_REWARD_HUD_VISIBLE_WORD_VALUE)
	{
		inst->flags |= HIDE_MODEL;
	}
	else
	{
		inst->flags &= ~HIDE_MODEL;
	}
}
