#include <platform/native_racers.h>

// Racer library. Until the racer format lands this only serves the dev
// switch CTR_DEV_FAKE_RACERS=n: n placeholder racers that borrow the first
// eight retail characters in turn, to exercise the character select's pages.

static signed char s_playerRacer[4] = {-1, -1, -1, -1};

static int NativeRacers_FakeCount(void)
{
	static int s_count = -1;
	if (s_count < 0)
	{
		s_count = NativeDev_EnvInt("CTR_DEV_FAKE_RACERS", 0);
		if (s_count < 0)
			s_count = 0;
		if (s_count > 64)
			s_count = 64;
	}
	return s_count;
}

int NativeRacers_Count(void)
{
	return NativeRacers_FakeCount();
}

int NativeRacers_Template(int racer)
{
	return ((racer < 0) || (racer >= NativeRacers_Count())) ? 0 : (racer % 8);
}

const char *NativeRacers_Name(int racer)
{
	(void)racer;
	return NULL;
}

struct Icon *NativeRacers_Portrait(int racer)
{
	(void)racer;
	return NULL;
}

struct Model *NativeRacers_MenuModel(int racer)
{
	(void)racer;
	return NULL;
}

void NativeRacers_SetPlayer(int player, int racer)
{
	if ((player >= 0) && (player < 4))
		s_playerRacer[player] = (signed char)(((racer >= 0) && (racer < NativeRacers_Count())) ? racer : -1);
}

int NativeRacers_Player(int player)
{
	return ((player >= 0) && (player < 4)) ? s_playerRacer[player] : -1;
}
