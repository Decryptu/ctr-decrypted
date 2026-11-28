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

#elif defined(__APPLE__)

#include <mach-o/dyld.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern char **environ;

// Not in the public headers; the flag Apple's debugger launches with.
#ifndef _POSIX_SPAWN_DISABLE_ASLR
#define _POSIX_SPAWN_DISABLE_ASLR 0x0100
#endif

#define NATIVE_RELAUNCH_FIXED_BASE_ENV "CTR_FIXED_BASE_EXEC"

int NativeRelaunch_Self(void)
{
	char exe[4096];
	uint32_t size = sizeof exe;
	char *argv[2];
	pid_t pid;

	if (_NSGetExecutablePath(exe, &size) != 0)
		return 0;
	argv[0] = exe;
	argv[1] = NULL;
	return posix_spawn(&pid, exe, NULL, NULL, argv, environ) == 0;
}

void NativeRelaunch_FixedBase(int argc, char **argv)
{
	char exe[4096];
	uint32_t size = sizeof exe;
	posix_spawnattr_t attr;

	(void)argc;
	// Set before the exec below: if the loader still slid the image, the new
	// image sees it and runs where it is instead of trying again.
	if (getenv(NATIVE_RELAUNCH_FIXED_BASE_ENV) != NULL)
	{
		unsetenv(NATIVE_RELAUNCH_FIXED_BASE_ENV);
		return;
	}
	if (_dyld_get_image_vmaddr_slide(0) == 0 || _NSGetExecutablePath(exe, &size) != 0)
		return;
	if (posix_spawnattr_init(&attr) != 0)
		return;
	setenv(NATIVE_RELAUNCH_FIXED_BASE_ENV, "1", 1);
	// POSIX_SPAWN_SETEXEC replaces this process, as exec does: same process,
	// same arguments, environment and open files, now at the linked address.
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETEXEC | _POSIX_SPAWN_DISABLE_ASLR);
	posix_spawn(NULL, exe, NULL, &attr, argv, environ);
	// Only reached when the exec failed: run slid; states then load only in
	// this launch (native_checkpoint.c refuses the others).
	posix_spawnattr_destroy(&attr);
	unsetenv(NATIVE_RELAUNCH_FIXED_BASE_ENV);
	fprintf(stderr, "[CTR] could not restart at the fixed load address; savestates and replays load only in this launch\n");
}

#else

int NativeRelaunch_Self(void)
{
	return 0;
}

#endif

#if !defined(__APPLE__)
void NativeRelaunch_FixedBase(int argc, char **argv)
{
	(void)argc;
	(void)argv;
}
#endif
