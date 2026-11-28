// The app bundle installs the game folder. See include/platform/native_mac_install.h.

#include "platform/native_mac_install.h"

#ifdef __APPLE__

#include "platform/native_build_id.h"

#include <copyfile.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <removefile.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define NATIVE_MAC_INSTALL_FOLDER "/Library/Application Support/CTR Decrypted"
// Names the build of the app that last installed the game folder.
#define NATIVE_MAC_INSTALL_MARKER ".installed-by-app"
#define NATIVE_MAC_INSTALL_LOCK   ".install.lock"

static int NativeMacInstall_Fail(char *error, int errorSize, const char *what)
{
	snprintf(error, (size_t)errorSize, "%s (%s)", what, strerror(errno));
	return -1;
}

static int NativeMacInstall_Join(char *out, size_t size, const char *a, const char *b)
{
	return snprintf(out, size, "%s/%s", a, b) < (int)size;
}

// mkdir -p for the game folder's parents (Application Support exists on any
// Mac, but a fresh account may lack it).
static int NativeMacInstall_MakeDirs(char *path)
{
	for (char *p = path + 1; *p; p++)
	{
		if (*p != '/')
			continue;
		*p = '\0';
		const int failed = mkdir(path, 0755) != 0 && errno != EEXIST;
		*p = '/';
		if (failed)
			return 0;
	}
	return mkdir(path, 0755) == 0 || errno == EEXIST;
}

// The app build that last installed `folder`, or "" when none did.
static void NativeMacInstall_ReadMarker(const char *folder, char *out, size_t size)
{
	char path[PATH_MAX];
	FILE *f;

	out[0] = '\0';
	if (!NativeMacInstall_Join(path, sizeof path, folder, NATIVE_MAC_INSTALL_MARKER) || (f = fopen(path, "r")) == NULL)
		return;
	if (fgets(out, (int)size, f) == NULL)
		out[0] = '\0';
	fclose(f);
	out[strcspn(out, "\r\n")] = '\0';
}

static int NativeMacInstall_WriteMarker(const char *folder)
{
	char path[PATH_MAX];
	char temp[PATH_MAX];
	int fd;

	if (!NativeMacInstall_Join(path, sizeof path, folder, NATIVE_MAC_INSTALL_MARKER) ||
	    !NativeMacInstall_Join(temp, sizeof temp, folder, NATIVE_MAC_INSTALL_MARKER ".new"))
		return 0;
	fd = open(temp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd < 0)
		return 0;
	const size_t len = strlen(gNativeBuildId);
	const int ok = write(fd, gNativeBuildId, len) == (ssize_t)len && write(fd, "\n", 1) == 1 && fsync(fd) == 0;
	close(fd);
	return ok && rename(temp, path) == 0;
}

// One file: written as a new file and renamed into place. Rewriting a signed
// executable or library in place leaves the kernel's cached signature on the
// old bytes, and macOS kills the next process that maps it. Data and
// permissions only: extended attributes are not copied, so the quarantine
// flag stays on the app the player approved and never reaches the installed
// files.
static int NativeMacInstall_CopyFile(const char *from, const char *to)
{
	char temp[PATH_MAX];

	if (snprintf(temp, sizeof temp, "%s.new", to) >= (int)sizeof temp)
		return 0;
	unlink(temp);
	if (copyfile(from, temp, NULL, COPYFILE_DATA | COPYFILE_SECURITY) != 0)
		return 0;
	if (rename(temp, to) != 0)
	{
		unlink(temp);
		return 0;
	}
	return 1;
}

// Copies the tree `from` into the folder `to`, merging: what `to` holds and
// `from` does not stays. Links are copied as links (the solo runtime's Python
// has some).
static int NativeMacInstall_CopyTree(const char *from, const char *to)
{
	DIR *dir;
	struct dirent *entry;
	int ok = 1;

	if (mkdir(to, 0755) != 0 && errno != EEXIST)
		return 0;
	if ((dir = opendir(from)) == NULL)
		return 0;
	while (ok && (entry = readdir(dir)) != NULL)
	{
		char src[PATH_MAX];
		char dst[PATH_MAX];
		struct stat info;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (!NativeMacInstall_Join(src, sizeof src, from, entry->d_name) || !NativeMacInstall_Join(dst, sizeof dst, to, entry->d_name) ||
		    lstat(src, &info) != 0)
		{
			ok = 0;
			break;
		}
		if (S_ISDIR(info.st_mode))
			ok = NativeMacInstall_CopyTree(src, dst);
		else if (S_ISLNK(info.st_mode))
		{
			char link[PATH_MAX];
			const ssize_t n = readlink(src, link, sizeof link - 1);

			if (n < 0)
				ok = 0;
			else
			{
				link[n] = '\0';
				unlink(dst);
				ok = symlink(link, dst) == 0;
			}
		}
		else if (S_ISREG(info.st_mode))
			ok = NativeMacInstall_CopyFile(src, dst);
	}
	closedir(dir);
	return ok;
}

// Copies the bundle's game folder over `folder`. Files only the player has
// (settings, saves, the disc, downloaded tracks) are not in it and stay.
// solo-runtime/ is replaced whole, as the updater replaces it.
static int NativeMacInstall_Copy(const char *resources, const char *bundleExe, const char *folder, const char *exeName,
                                 char *error, int errorSize)
{
	char path[PATH_MAX];
	char target[PATH_MAX];

	if (!NativeMacInstall_Join(path, sizeof path, folder, "solo-runtime"))
		return NativeMacInstall_Fail(error, errorSize, "game folder path too long");
	if (removefile(path, NULL, REMOVEFILE_RECURSIVE) != 0 && errno != ENOENT)
		return NativeMacInstall_Fail(error, errorSize, "could not remove the old solo-runtime");
	if (!NativeMacInstall_Join(path, sizeof path, resources, "game") || !NativeMacInstall_CopyTree(path, folder))
		return NativeMacInstall_Fail(error, errorSize, "could not copy the game files");
	if (!NativeMacInstall_Join(target, sizeof target, folder, exeName) || !NativeMacInstall_CopyFile(bundleExe, target))
		return NativeMacInstall_Fail(error, errorSize, "could not install the game executable");
	return 1;
}

int NativeMacInstall_Prepare(char *exe, int size, char *error, int errorSize)
{
	char self[PATH_MAX];
	char bundleExe[PATH_MAX];
	char resources[PATH_MAX];
	char folder[PATH_MAX];
	char lock[PATH_MAX];
	char installed[128];
	uint32_t selfSize = sizeof self;
	struct stat info;
	const char *home = getenv("HOME");

	if (_NSGetExecutablePath(self, &selfSize) != 0 || realpath(self, bundleExe) == NULL)
		return 0;
	// <bundle>.app/Contents/MacOS/<exe>: the game folder is
	// <bundle>.app/Contents/Resources/game.
	char *name = strrchr(bundleExe, '/');
	if (name == NULL || name - bundleExe < 15 || strncmp(name - 15, "/Contents/MacOS", 15) != 0)
		return 0;
	if (snprintf(resources, sizeof resources, "%.*s/Contents/Resources", (int)(name - 15 - bundleExe), bundleExe) >= (int)sizeof resources)
		return 0;
	if (!NativeMacInstall_Join(folder, sizeof folder, resources, "game") || stat(folder, &info) != 0 || !S_ISDIR(info.st_mode))
		return 0;
	if (home == NULL || home[0] != '/' || snprintf(folder, sizeof folder, "%s%s", home, NATIVE_MAC_INSTALL_FOLDER) >= (int)sizeof folder)
	{
		errno = ENOENT;
		return NativeMacInstall_Fail(error, errorSize, "no home folder");
	}
	if (!NativeMacInstall_MakeDirs(folder))
		return NativeMacInstall_Fail(error, errorSize, "could not create the game folder");
	if (!NativeMacInstall_Join(lock, sizeof lock, folder, NATIVE_MAC_INSTALL_LOCK) || !NativeMacInstall_Join(exe, (size_t)size, folder, name + 1))
		return NativeMacInstall_Fail(error, errorSize, "game folder path too long");

	// One launch installs at a time; a second waits, then finds it done. The
	// lock is released when this process execs the installed game.
	const int lockFd = open(lock, O_RDWR | O_CREAT | O_CLOEXEC, 0644);
	if (lockFd < 0 || flock(lockFd, LOCK_EX) != 0)
		return NativeMacInstall_Fail(error, errorSize, "could not lock the game folder");
	NativeMacInstall_ReadMarker(folder, installed, sizeof installed);
	// A different build of the app is a new download: install it, older or
	// newer. The same build leaves the folder alone, so an update the in-game
	// updater installed since is kept.
	int result = 1;
	if (strcmp(installed, gNativeBuildId) != 0 || access(exe, X_OK) != 0)
	{
		result = NativeMacInstall_Copy(resources, bundleExe, folder, name + 1, error, errorSize);
		if (result == 1 && !NativeMacInstall_WriteMarker(folder))
			result = NativeMacInstall_Fail(error, errorSize, "could not record the install");
	}
	if (result != 1)
		close(lockFd);
	return result;
}

#endif
