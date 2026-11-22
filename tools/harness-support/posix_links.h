#ifndef HARNESS_POSIX_LINKS_H
#define HARNESS_POSIX_LINKS_H

/* Links in the host harnesses, on POSIX and on Windows (MinGW). It includes
   <windows.h> there, so a harness that compiles game headers cannot use it.

   A hard link needs no rights on either system. A symbolic link on Windows
   needs Developer Mode (or admin): harness_symlink returns -1 there without
   it, and a harness skips that case with a message rather than fail. A link to
   a folder (harness_dirlink) is a junction on Windows, which any user can make,
   so it is the link a player's install can really hold. */
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
/* 16-bit pointer qualifiers <windows.h> still defines away; harnesses use the names. */
#undef near
#undef far
#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif
static inline int link(const char *existing, const char *created)
{
	return CreateHardLinkA(created, existing, NULL) ? 0 : -1;
}
static inline int harness_symlink(const char *target, const char *created)
{
	const DWORD attributes = GetFileAttributesA(target);
	DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
	if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY))
		flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
	return CreateSymbolicLinkA(created, target, flags) ? 0 : -1;
}
/* lstat + S_ISLNK, or the reparse-point attribute. */
static inline int harness_is_symlink(const char *path)
{
	const DWORD attributes = GetFileAttributesA(path);
	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
static inline int harness_dirlink(const char *target, const char *created)
{
	char command[2048];
	int n = snprintf(command, sizeof command, "cmd /c mklink /J \"%s\" \"%s\" >NUL", created, target);
	if (n < 0 || (size_t)n >= sizeof command)
		return -1;
	for (char *p = command + 16; *p; p++) /* the paths, not the switches */
		if (*p == '/')
			*p = '\\';
	return system(command) == 0 ? 0 : -1;
}
/* Remove a link itself: a folder link is a directory entry on Windows. */
static inline int harness_unlink_link(const char *path)
{
	return (RemoveDirectoryA(path) || DeleteFileA(path)) ? 0 : -1;
}
#else
#include <sys/stat.h>
#include <unistd.h>
static inline int harness_symlink(const char *target, const char *created)
{
	return symlink(target, created);
}
static inline int harness_is_symlink(const char *path)
{
	struct stat info;
	return lstat(path, &info) == 0 && S_ISLNK(info.st_mode);
}
static inline int harness_dirlink(const char *target, const char *created)
{
	return symlink(target, created);
}
static inline int harness_unlink_link(const char *path)
{
	return unlink(path);
}
#endif

#endif
