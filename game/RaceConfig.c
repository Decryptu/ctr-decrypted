#include <common.h>

#ifndef RACECONFIG_OPTIONS_LOADED
#define RACECONFIG_OPTIONS_LOADED sdata->boolHasLoadedOptions
#endif

void RaceConfig_LoadGameOptions(void)
{
	struct GameSave *save;
	s16 i;

	if (RACECONFIG_OPTIONS_LOADED != 0)
	{
		return;
	}

	RACECONFIG_OPTIONS_LOADED = 1;
	i = 0;
	save = &GAME_SAVE;

	for (; i < GAME_OPTIONS_VOLUME_COUNT; i++)
	{
		// NOTE(aalhendi): Index-first addition preserves the retail address operands.
		howl_VolumeSet(i, (u8) * (i + save->options.volumes));
		// Retail refreshes the wheel settings after each volume channel.
		memcpy(&data.rwd[0], &save->options.rwd[0], sizeof(data.rwd));
	}

	GAME_TRACKER->gameMode1 |= GAME_SAVE.options.gameMode1_vibrationFlags & GAME_MODE_VIBRATION_MASK;
	howl_ModeSet((u8)GAME_SAVE.options.audioMode & 1);

	// config.ini audio (see platform/native_config.c) is authoritative over the
	// adventure-profile card: reapply the saved values so the memcard reconcile
	// just done above cannot stomp the user's config.ini choice. volFx < 0 means
	// no [Audio] was captured yet -- leave the card's values in place.
	if (g_config.volFx >= 0)
	{
		howl_VolumeSet(0, (u8)g_config.volFx);
		howl_VolumeSet(1, (u8)g_config.volMusic);
		howl_VolumeSet(2, (u8)g_config.volVoice);
		howl_ModeSet(g_config.stereo != 0);
	}
}

void RaceConfig_SaveGameOptions(void)
{
	s16 i = 0;
	struct GameSave *save = &GAME_SAVE;

	for (; i < GAME_OPTIONS_VOLUME_COUNT; i++)
	{
		*(i + save->options.volumes) = howl_VolumeGet(i) & 0xff;
	}

	memcpy(&GAME_SAVE.options.rwd[0], &data.rwd[0], sizeof(data.rwd));
	GAME_SAVE.options.gameMode1_vibrationFlags = GAME_TRACKER->gameMode1 & GAME_MODE_VIBRATION_MASK;
	GAME_SAVE.options.audioMode = (u8)howl_ModeGet() != 0;
}
