#ifndef HARNESS_POSIX_COMPAT_H
#define HARNESS_POSIX_COMPAT_H

/* POSIX calls the host harnesses use, for the Windows (MinGW) build. On POSIX
   this header adds nothing. Include it before any other header: the system
   headers that declare MinGW's one-argument mkdir come first here, so the
   two-argument form below never collides with them. It pulls in no
   <windows.h>, whose macros clash with the game's headers; links are in
   posix_links.h. */
#include <stdio.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

static inline int harness_mkdir(const char *path, int mode)
{
	(void)mode; /* Windows folders have no POSIX mode. */
	return _mkdir(path);
}
#define mkdir(path, mode) harness_mkdir(path, mode)

static inline int setenv(const char *name, const char *value, int overwrite)
{
	if (!overwrite && getenv(name) != NULL)
		return 0;
	return _putenv_s(name, value);
}

static inline int unsetenv(const char *name)
{
	return _putenv_s(name, "");
}
#endif

/* File fixtures without shell tools (cp, dd, rm -rf), the same on both systems. */
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Copy a file; 0 on success. */
static inline int harness_copy_file(const char *from, const char *to)
{
	FILE *in = fopen(from, "rb"), *out = in ? fopen(to, "wb") : NULL;
	char buf[65536];
	size_t n;
	int ok = in != NULL && out != NULL;
	while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0)
		ok = fwrite(buf, 1, n, out) == n;
	if (in)
		fclose(in);
	if (out && fclose(out) != 0)
		ok = 0;
	return ok ? 0 : -1;
}

/* Overwrite one byte in place, the length unchanged; 0 on success. */
static inline int harness_poke_byte(const char *path, long offset, int value)
{
	FILE *f = fopen(path, "r+b");
	int ok = f != NULL && fseek(f, offset, SEEK_SET) == 0 && fputc(value, f) != EOF;
	if (f && fclose(f) != 0)
		ok = 0;
	return ok ? 0 : -1;
}

/* Delete a folder tree the harness made itself; 0 on success. It follows
   what stat sees, so it is not for a tree that holds links. */
static inline int harness_remove_tree(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *e;
	int ok = 1;
	if (stat(path, &st) != 0)
		return 0;
	if (!S_ISDIR(st.st_mode))
		return remove(path) == 0 ? 0 : -1;
	dir = opendir(path);
	if (dir == NULL)
		return -1;
	while ((e = readdir(dir)) != NULL)
	{
		char child[4096];
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child || harness_remove_tree(child) != 0)
			ok = 0;
	}
	closedir(dir);
	return ok && rmdir(path) == 0 ? 0 : -1;
}

/* 1 when chmod on a folder refuses access: POSIX, and not as root. Windows
   folders ignore POSIX modes, so a harness skips such a case there. */
#ifdef _WIN32
static inline int harness_folder_modes_apply(void)
{
	return 0;
}
#else
#include <unistd.h>
static inline int harness_folder_modes_apply(void)
{
	return geteuid() != 0;
}
#endif

#endif
