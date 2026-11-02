// Start the game again. See include/platform/native_relaunch.h.

#include "platform/native_relaunch.h"

#if defined(_WIN32)

#include <platform/native_win32.h>

#include <wchar.h>

int NativeRelaunch_Self(void)
{
	wchar_t exe[32768];
	wchar_t cmd[32768 + 3];
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;
	const DWORD n = GetModuleFileNameW(NULL, exe, (DWORD)(sizeof exe / sizeof exe[0]));

	if (n == 0 || n >= sizeof exe / sizeof exe[0])
		return 0;
	swprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\"", exe);
	ZeroMemory(&si, sizeof si);
	ZeroMemory(&pi, sizeof pi);
	si.cb = sizeof si;
	if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
		return 0;
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 1;
}

#elif defined(__linux__)

#include <limits.h>
#include <spawn.h>
#include <unistd.h>

extern char **environ;

int NativeRelaunch_Self(void)
{
	char exe[PATH_MAX];
	char *argv[2];
	pid_t pid;
	const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);

	if (n <= 0)
		return 0;
	exe[n] = '\0';
	argv[0] = exe;
	argv[1] = NULL;
	return posix_spawn(&pid, exe, NULL, NULL, argv, environ) == 0;
}

#else

int NativeRelaunch_Self(void)
{
	return 0;
}

#endif
