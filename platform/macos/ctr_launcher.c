// The executable of CTR Decrypted.app (docs/MACOS.md). It installs the
// bundle's game folder into ~/Library/Application Support/CTR Decrypted/ when
// needed and replaces itself with the game there, launched at its linked
// address. The game never runs from inside the bundle: the bundle is signed,
// may be read-only, and macOS runs a quarantined app that is not notarized
// from a random read-only copy until the player moves it.

#include "platform/native_mac_install.h"

#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
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

// Tells the game it already runs at its linked address
// (NativeRelaunch_Startup, platform/native_relaunch.c).
#define CTR_FIXED_BASE_ENV "CTR_FIXED_BASE_EXEC"

#if !defined(CTR_LAUNCHER_GAME) || !defined(CTR_LAUNCHER_FOLDER)
#error "CTR_LAUNCHER_GAME names the game executable, CTR_LAUNCHER_FOLDER its folder (CMakeLists.txt)"
#endif

static void Launcher_Alert(const char *message)
{
	CFStringRef text = CFStringCreateWithCString(NULL, message, kCFStringEncodingUTF8);

	fprintf(stderr, "[CTR] %s\n", message);
	CFUserNotificationDisplayAlert(0, kCFUserNotificationStopAlertLevel, NULL, NULL, NULL, CFSTR(CTR_LAUNCHER_FOLDER), text, NULL, NULL,
	                               NULL, NULL);
	if (text != NULL)
		CFRelease(text);
}

int main(int argc, char **argv)
{
	char self[PATH_MAX];
	char launcher[PATH_MAX];
	char exe[PATH_MAX];
	char error[PATH_MAX + 128];
	uint32_t size = sizeof self;
	posix_spawnattr_t attr;

	(void)argc;
	if (_NSGetExecutablePath(self, &size) != 0 || realpath(self, launcher) == NULL)
	{
		Launcher_Alert("Could not find where " CTR_LAUNCHER_FOLDER ".app is.");
		return 1;
	}
	switch (NativeMacInstall_Prepare(launcher, CTR_LAUNCHER_FOLDER, CTR_LAUNCHER_GAME, exe, (int)sizeof exe, error, (int)sizeof error))
	{
	case 1:
		break;
	case 0:
		Launcher_Alert("This copy of " CTR_LAUNCHER_FOLDER ".app is incomplete: it has no game folder. Download it again.");
		return 1;
	default:
		snprintf(exe, sizeof exe, "Could not install the game into your Application Support folder: %s.", error);
		Launcher_Alert(exe);
		return 1;
	}
	// POSIX_SPAWN_SETEXEC replaces this process, as exec does: same process,
	// arguments, environment and open files.
	setenv(CTR_FIXED_BASE_ENV, "1", 1);
	if (posix_spawnattr_init(&attr) == 0)
	{
		posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETEXEC | _POSIX_SPAWN_DISABLE_ASLR);
		argv[0] = exe;
		posix_spawn(NULL, exe, NULL, &attr, argv, environ);
		posix_spawnattr_destroy(&attr);
	}
	snprintf(error, sizeof error, "Could not start the game in %s.", exe);
	Launcher_Alert(error);
	return 1;
}
