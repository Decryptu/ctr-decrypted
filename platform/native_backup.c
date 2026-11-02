// Save backups. See include/platform/native_backup.h.

#include "platform/native_backup.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform/native_fs_utf8.h"

#define NATIVE_BACKUP_REL_MAX 192
#define NATIVE_BACKUP_PENDING_FILE "restore.txt"
#define NATIVE_BACKUP_INFO_FILE "backup.txt"

typedef struct
{
	char rel[NATIVE_BACKUP_FILES_MAX][NATIVE_BACKUP_REL_MAX];
	int count;
} NativeBackupFiles;

static char s_backupBase[NATIVE_BACKUP_PATH_MAX];
static time_t s_backupLastTaken;
// A backup prune never removes this one: the restore that is about to read it
// takes a backup (and prunes) first.
static const char *s_backupKeep;
// Two file lists are compared when a backup is taken; static, not on the stack.
static NativeBackupFiles s_backupLive;
static NativeBackupFiles s_backupOther;

static const char *const s_backupReasonKeys[] = {"launch", "play", "before-restore"};

// Where report lines go (NativeBackup_SetLog). Until it is set they are printed
// and kept here, since the launch backup and a restore run before the log
// file opens.
static void (*s_backupLog)(const char *text);
static char s_backupEarly[2048];

static void Backup_Report(const char *fmt, ...)
{
	char line[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(line, sizeof line, fmt, args);
	va_end(args);
	if (s_backupLog != NULL)
	{
		s_backupLog(line);
		return;
	}
	fputs(line, stdout);
	if (strlen(s_backupEarly) + strlen(line) < sizeof s_backupEarly)
		strcat(s_backupEarly, line);
}

void NativeBackup_SetLog(void (*log)(const char *text))
{
	s_backupLog = log;
	if (log != NULL && s_backupEarly[0] != '\0')
		log(s_backupEarly);
	s_backupEarly[0] = '\0';
}

void NativeBackup_SetBaseDir(const char *dir)
{
	snprintf(s_backupBase, sizeof s_backupBase, "%s", dir != NULL ? dir : "");
	s_backupLastTaken = 0;
}

// base/rel, or rel when the base is the working folder.
static int Backup_Path(char *dst, size_t size, const char *rel)
{
	const int n = s_backupBase[0] != '\0' ? snprintf(dst, size, "%s/%s", s_backupBase, rel) : snprintf(dst, size, "%s", rel);
	return n > 0 && (size_t)n < size;
}

static int Backup_Join(char *dst, size_t size, const char *dir, const char *name)
{
	const int n = dir[0] != '\0' ? snprintf(dst, size, "%s/%s", dir, name) : snprintf(dst, size, "%s", name);
	return n > 0 && (size_t)n < size;
}

static int Backup_IsFolder(const char *path)
{
	NativeFsDir *dir = NativeFs_OpenDir(path);
	if (dir == NULL)
		return 0;
	NativeFs_CloseDir(dir);
	return 1;
}

static int Backup_EndsWith(const char *s, const char *suffix)
{
	const size_t n = strlen(s), m = strlen(suffix);
	return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Temporary and hidden names: a save being written (~name, the memory card's
// own temporary file), a .tmp of any writer.
static int Backup_SkipName(const char *name)
{
	return name[0] == '.' || name[0] == '~' || Backup_EndsWith(name, ".tmp");
}

// "YYYYMMDD-HHMMSS" or that with "-N". Fills when (local time) when given.
static int Backup_ParseName(const char *name, struct tm *when)
{
	int i;
	struct tm t;
	for (i = 0; i < 15; i++)
	{
		if (i == 8 ? name[i] != '-' : (name[i] < '0' || name[i] > '9'))
			return 0;
	}
	if (name[15] != '\0')
	{
		if (name[15] != '-' || name[16] < '2' || name[16] > '9' || name[17] != '\0')
			return 0;
	}
	memset(&t, 0, sizeof t);
	t.tm_year = (name[0] - '0') * 1000 + (name[1] - '0') * 100 + (name[2] - '0') * 10 + (name[3] - '0') - 1900;
	t.tm_mon = (name[4] - '0') * 10 + (name[5] - '0') - 1;
	t.tm_mday = (name[6] - '0') * 10 + (name[7] - '0');
	t.tm_hour = (name[9] - '0') * 10 + (name[10] - '0');
	t.tm_min = (name[11] - '0') * 10 + (name[12] - '0');
	t.tm_sec = (name[13] - '0') * 10 + (name[14] - '0');
	if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 || t.tm_hour > 23 || t.tm_min > 59 || t.tm_sec > 60)
		return 0;
	if (when != NULL)
		*when = t;
	return 1;
}

static void Backup_Add(NativeBackupFiles *files, const char *top, const char *sub, const char *name)
{
	if (files->count >= NATIVE_BACKUP_FILES_MAX)
		return;
	if (snprintf(files->rel[files->count], NATIVE_BACKUP_REL_MAX, "%s/%s/%s", top, sub, name) >= NATIVE_BACKUP_REL_MAX)
		return;
	files->count++;
}

// The files of one folder level under root/top/<sub>/: memcards/<slot>/<file>
// and solo/rooms/<room>/<file>.apsave.
static void Backup_CollectLevel(NativeBackupFiles *files, const char *root, const char *top, const char *suffix)
{
	char topPath[NATIVE_BACKUP_PATH_MAX];
	char subPath[NATIVE_BACKUP_PATH_MAX];
	char filePath[NATIVE_BACKUP_PATH_MAX];
	char sub[128];
	char name[128];
	NativeFsDir *topDir;

	if (!Backup_Join(topPath, sizeof topPath, root, top) || (topDir = NativeFs_OpenDir(topPath)) == NULL)
		return;
	while (NativeFs_ReadDir(topDir, sub, sizeof sub))
	{
		NativeFsDir *subDir;
		if (Backup_SkipName(sub) || !Backup_Join(subPath, sizeof subPath, topPath, sub))
			continue;
		if ((subDir = NativeFs_OpenDir(subPath)) == NULL)
			continue;
		while (NativeFs_ReadDir(subDir, name, sizeof name))
		{
			if (Backup_SkipName(name) || (suffix != NULL && !Backup_EndsWith(name, suffix)))
				continue;
			if (!Backup_Join(filePath, sizeof filePath, subPath, name) || Backup_IsFolder(filePath))
				continue;
			Backup_Add(files, top, sub, name);
		}
		NativeFs_CloseDir(subDir);
	}
	NativeFs_CloseDir(topDir);
}

static int Backup_CompareRel(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

// Every file a backup holds, under root ("" is the base folder itself), sorted.
static void Backup_Collect(NativeBackupFiles *files, const char *root)
{
	files->count = 0;
	Backup_CollectLevel(files, root, "memcards", NULL);
	Backup_CollectLevel(files, root, "solo/rooms", ".apsave");
	qsort(files->rel, (size_t)files->count, NATIVE_BACKUP_REL_MAX, Backup_CompareRel);
}

static int Backup_SameFile(const char *a, const char *b)
{
	unsigned char bufA[4096], bufB[4096];
	FILE *fa = NativeFs_OpenRead(a);
	FILE *fb = NativeFs_OpenRead(b);
	int same = fa != NULL && fb != NULL;

	while (same)
	{
		const size_t na = fread(bufA, 1, sizeof bufA, fa);
		const size_t nb = fread(bufB, 1, sizeof bufB, fb);
		if (na != nb || memcmp(bufA, bufB, na) != 0)
			same = 0;
		else if (na == 0)
			break;
	}
	if (fa != NULL)
		fclose(fa);
	if (fb != NULL)
		fclose(fb);
	return same;
}

// Copy into a path that must not exist yet.
static int Backup_CopyFile(const char *src, const char *dst)
{
	unsigned char buf[8192];
	FILE *in = NativeFs_OpenRead(src);
	FILE *out;
	int ok = 1;
	size_t n;

	if (in == NULL)
		return 0;
	out = NativeFs_CreateExclusive(dst);
	if (out == NULL)
	{
		fclose(in);
		return 0;
	}
	while ((n = fread(buf, 1, sizeof buf, in)) > 0)
	{
		if (fwrite(buf, 1, n, out) != n)
		{
			ok = 0;
			break;
		}
	}
	if (ferror(in))
		ok = 0;
	fclose(in);
	if (!NativeFs_FlushToDisk(out))
		ok = 0;
	if (fclose(out) != 0)
		ok = 0;
	if (!ok)
		NativeFs_Delete(dst);
	return ok;
}

// Create every folder of path's parent, from the first one under prefixLen.
static int Backup_MakeParents(const char *path, size_t prefixLen)
{
	char dir[NATIVE_BACKUP_PATH_MAX];
	size_t i;
	snprintf(dir, sizeof dir, "%s", path);
	for (i = prefixLen; dir[i] != '\0'; i++)
	{
		if (dir[i] == '/' && i > 0)
		{
			dir[i] = '\0';
			if (!NativeFs_MakeDirectory(dir))
				return 0;
			dir[i] = '/';
		}
	}
	return 1;
}

static void Backup_RemoveTree(const char *path)
{
	char name[256];
	char child[NATIVE_BACKUP_PATH_MAX];
	NativeFsDir *dir = NativeFs_OpenDir(path);

	if (dir == NULL)
	{
		NativeFs_Delete(path);
		return;
	}
	while (NativeFs_ReadDir(dir, name, sizeof name))
	{
		if (!Backup_Join(child, sizeof child, path, name))
			continue;
		if (Backup_IsFolder(child))
			Backup_RemoveTree(child);
		else
			NativeFs_Delete(child);
	}
	NativeFs_CloseDir(dir);
	NativeFs_RemoveDirectory(path);
}

static int Backup_CompareNameDesc(const void *a, const void *b)
{
	return strcmp((const char *)b, (const char *)a);
}

// Backup folder names, newest first.
static int Backup_Names(char (*names)[NATIVE_BACKUP_NAME_MAX], int max)
{
	char dirPath[NATIVE_BACKUP_PATH_MAX];
	char path[NATIVE_BACKUP_PATH_MAX];
	char name[128];
	NativeFsDir *dir;
	int count = 0;

	if (!Backup_Path(dirPath, sizeof dirPath, NATIVE_BACKUP_DIR) || (dir = NativeFs_OpenDir(dirPath)) == NULL)
		return 0;
	while (count < max && NativeFs_ReadDir(dir, name, sizeof name))
	{
		if (strlen(name) >= NATIVE_BACKUP_NAME_MAX || !Backup_ParseName(name, NULL))
			continue;
		if (!Backup_Join(path, sizeof path, dirPath, name) || !Backup_IsFolder(path))
			continue;
		memcpy(names[count++], name, strlen(name) + 1);
	}
	NativeFs_CloseDir(dir);
	qsort(names, (size_t)count, NATIVE_BACKUP_NAME_MAX, Backup_CompareNameDesc);
	return count;
}

#define NATIVE_BACKUP_SCAN_MAX 64

static void Backup_Prune(void)
{
	char names[NATIVE_BACKUP_SCAN_MAX][NATIVE_BACKUP_NAME_MAX];
	char rel[NATIVE_BACKUP_PATH_MAX];
	char path[NATIVE_BACKUP_PATH_MAX];
	const int count = Backup_Names(names, NATIVE_BACKUP_SCAN_MAX);
	int i;

	for (i = NATIVE_BACKUP_KEEP; i < count; i++)
	{
		if (s_backupKeep != NULL && strcmp(names[i], s_backupKeep) == 0)
			continue;
		snprintf(rel, sizeof rel, NATIVE_BACKUP_DIR "/%s", names[i]);
		if (Backup_Path(path, sizeof path, rel))
			Backup_RemoveTree(path);
	}
}

// A backup interrupted before its rename leaves name.tmp behind.
static void Backup_RemoveUnfinished(void)
{
	char dirPath[NATIVE_BACKUP_PATH_MAX];
	char path[NATIVE_BACKUP_PATH_MAX];
	char name[128];
	NativeFsDir *dir;

	if (!Backup_Path(dirPath, sizeof dirPath, NATIVE_BACKUP_DIR) || (dir = NativeFs_OpenDir(dirPath)) == NULL)
		return;
	while (NativeFs_ReadDir(dir, name, sizeof name))
	{
		if (Backup_EndsWith(name, ".tmp") && Backup_Join(path, sizeof path, dirPath, name))
			Backup_RemoveTree(path);
	}
	NativeFs_CloseDir(dir);
}

// The live files are the same as the files of this backup, byte for byte.
static int Backup_MatchesLive(const char *name)
{
	char root[NATIVE_BACKUP_PATH_MAX];
	char a[NATIVE_BACKUP_PATH_MAX];
	char b[NATIVE_BACKUP_PATH_MAX];
	char rootRel[NATIVE_BACKUP_PATH_MAX];
	int i;

	snprintf(rootRel, sizeof rootRel, NATIVE_BACKUP_DIR "/%s", name);
	if (!Backup_Path(root, sizeof root, rootRel))
		return 0;
	Backup_Collect(&s_backupOther, root);
	if (s_backupOther.count != s_backupLive.count)
		return 0;
	for (i = 0; i < s_backupLive.count; i++)
	{
		if (strcmp(s_backupLive.rel[i], s_backupOther.rel[i]) != 0)
			return 0;
		if (!Backup_Path(a, sizeof a, s_backupLive.rel[i]) || !Backup_Join(b, sizeof b, root, s_backupOther.rel[i]))
			return 0;
		if (!Backup_SameFile(a, b))
			return 0;
	}
	return 1;
}

int NativeBackup_Take(NativeBackupReason reason, time_t now)
{
	char names[1][NATIVE_BACKUP_NAME_MAX];
	char name[NATIVE_BACKUP_NAME_MAX];
	char rel[NATIVE_BACKUP_PATH_MAX];
	char tmp[NATIVE_BACKUP_PATH_MAX + 8];
	char final[NATIVE_BACKUP_PATH_MAX];
	char src[NATIVE_BACKUP_PATH_MAX];
	char dst[NATIVE_BACKUP_PATH_MAX];
	struct tm local;
	struct tm *lt;
	FILE *info;
	int i;
	int suffix;

	s_backupLastTaken = now;
	Backup_Collect(&s_backupLive, s_backupBase);
	if (s_backupLive.count == 0)
		return 0;
	if (Backup_Names(names, 1) == 1 && Backup_MatchesLive(names[0]))
		return 0;

	lt = localtime(&now);
	if (lt == NULL)
		return -1;
	local = *lt;
	for (suffix = 1; suffix <= 9; suffix++)
	{
		char stamp[24];
		strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
		if (suffix == 1)
			snprintf(name, sizeof name, "%s", stamp);
		else
			snprintf(name, sizeof name, "%s-%d", stamp, suffix);
		snprintf(rel, sizeof rel, NATIVE_BACKUP_DIR "/%s", name);
		if (!Backup_Path(final, sizeof final, rel))
			return -1;
		if (!Backup_IsFolder(final))
			break;
	}
	if (suffix > 9)
		return -1;
	snprintf(tmp, sizeof tmp, "%s.tmp", final);

	if (!Backup_Path(src, sizeof src, NATIVE_BACKUP_DIR) || !NativeFs_MakeDirectory(src))
		return -1;
	Backup_RemoveTree(tmp);
	if (!NativeFs_MakeDirectory(tmp))
		return -1;

	for (i = 0; i < s_backupLive.count; i++)
	{
		if (!Backup_Path(src, sizeof src, s_backupLive.rel[i]) || !Backup_Join(dst, sizeof dst, tmp, s_backupLive.rel[i]) ||
		    !Backup_MakeParents(dst, strlen(tmp) + 1) || !Backup_CopyFile(src, dst))
		{
			Backup_Report("[CTR Native] backup failed: could not copy %s\n", s_backupLive.rel[i]);
			Backup_RemoveTree(tmp);
			return -1;
		}
	}

	if (!Backup_Join(dst, sizeof dst, tmp, NATIVE_BACKUP_INFO_FILE) || (info = NativeFs_CreateExclusive(dst)) == NULL)
	{
		Backup_RemoveTree(tmp);
		return -1;
	}
	fprintf(info, "reason=%s\n", s_backupReasonKeys[reason]);
	fclose(info);

	if (!NativeFs_Replace(tmp, final))
	{
		Backup_RemoveTree(tmp);
		return -1;
	}
	Backup_Report("[CTR Native] backup %s (%s, %d files)\n", name, s_backupReasonKeys[reason], s_backupLive.count);
	Backup_Prune();
	return 1;
}

static int Backup_PendingPath(char *dst, size_t size)
{
	return Backup_Path(dst, size, NATIVE_BACKUP_DIR "/" NATIVE_BACKUP_PENDING_FILE);
}

static int Backup_Exists(const char *name)
{
	char rel[NATIVE_BACKUP_PATH_MAX];
	char path[NATIVE_BACKUP_PATH_MAX];
	if (strlen(name) >= NATIVE_BACKUP_NAME_MAX || !Backup_ParseName(name, NULL))
		return 0;
	snprintf(rel, sizeof rel, NATIVE_BACKUP_DIR "/%s", name);
	return Backup_Path(path, sizeof path, rel) && Backup_IsFolder(path);
}

int NativeBackup_PendingRestore(char *name, size_t size)
{
	char path[NATIVE_BACKUP_PATH_MAX];
	char line[64];
	FILE *f;
	size_t n;

	if (size > 0)
		name[0] = '\0';
	if (!Backup_PendingPath(path, sizeof path) || (f = NativeFs_OpenRead(path)) == NULL)
		return 0;
	n = fread(line, 1, sizeof line - 1, f);
	fclose(f);
	line[n] = '\0';
	while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' '))
		line[--n] = '\0';
	if (!Backup_Exists(line) || n >= size)
		return 0;
	memcpy(name, line, n + 1);
	return 1;
}

int NativeBackup_RequestRestore(const char *name)
{
	char path[NATIVE_BACKUP_PATH_MAX];
	char tmp[NATIVE_BACKUP_PATH_MAX + 8];
	FILE *f;
	int ok;

	if (name == NULL || !Backup_Exists(name) || !Backup_PendingPath(path, sizeof path))
		return 0;
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	NativeFs_Delete(tmp);
	if ((f = NativeFs_CreateExclusive(tmp)) == NULL)
		return 0;
	ok = fprintf(f, "%s\n", name) > 0;
	ok = NativeFs_FlushToDisk(f) && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok || !NativeFs_Replace(tmp, path))
	{
		NativeFs_Delete(tmp);
		return 0;
	}
	Backup_Report("[CTR Native] backup %s will be restored at the next start\n", name);
	return 1;
}

void NativeBackup_CancelRestore(void)
{
	char path[NATIVE_BACKUP_PATH_MAX];
	if (Backup_PendingPath(path, sizeof path))
		NativeFs_Delete(path);
}

// Put one backed-up file back: a temporary copy beside it, then a rename.
static int Backup_RestoreFile(const char *root, const char *rel)
{
	char src[NATIVE_BACKUP_PATH_MAX];
	char dst[NATIVE_BACKUP_PATH_MAX];
	char tmp[NATIVE_BACKUP_PATH_MAX + 8];
	size_t prefix = s_backupBase[0] != '\0' ? strlen(s_backupBase) + 1 : 0;

	if (!Backup_Join(src, sizeof src, root, rel) || !Backup_Path(dst, sizeof dst, rel))
		return 0;
	if (!Backup_MakeParents(dst, prefix))
		return 0;
	snprintf(tmp, sizeof tmp, "%s.tmp", dst);
	NativeFs_Delete(tmp);
	if (!Backup_CopyFile(src, tmp))
		return 0;
	if (!NativeFs_Replace(tmp, dst))
	{
		NativeFs_Delete(tmp);
		return 0;
	}
	return 1;
}

// A solo save goes back only into a room that still exists: without its room
// the save is of no use.
static int Backup_RoomExists(const char *rel)
{
	char roomRel[NATIVE_BACKUP_PATH_MAX];
	char path[NATIVE_BACKUP_PATH_MAX];
	char *slash;
	if (strncmp(rel, "solo/", 5) != 0)
		return 1;
	snprintf(roomRel, sizeof roomRel, "%s", rel);
	slash = strrchr(roomRel, '/');
	if (slash == NULL)
		return 0;
	*slash = '\0';
	return Backup_Path(path, sizeof path, roomRel) && Backup_IsFolder(path);
}

// Returns 1 when a restore was applied.
static int Backup_ApplyPending(time_t now)
{
	char name[NATIVE_BACKUP_NAME_MAX];
	char rel[NATIVE_BACKUP_PATH_MAX];
	char root[NATIVE_BACKUP_PATH_MAX];
	int i, restored = 0, failed = 0;

	if (!NativeBackup_PendingRestore(name, sizeof name))
	{
		NativeBackup_CancelRestore(); // a request naming no backup
		return 0;
	}
	// Removed first: a restore that keeps failing must not run at every start.
	NativeBackup_CancelRestore();

	s_backupKeep = name;
	if (NativeBackup_Take(NATIVE_BACKUP_BEFORE_RESTORE, now) < 0)
	{
		s_backupKeep = NULL;
		Backup_Report("[CTR Native] restore of %s cancelled: the current progress could not be backed up\n", name);
		return 0;
	}
	s_backupKeep = NULL;

	snprintf(rel, sizeof rel, NATIVE_BACKUP_DIR "/%s", name);
	if (!Backup_Path(root, sizeof root, rel))
		return 0;
	Backup_Collect(&s_backupOther, root);
	for (i = 0; i < s_backupOther.count; i++)
	{
		if (!Backup_RoomExists(s_backupOther.rel[i]))
			continue;
		if (Backup_RestoreFile(root, s_backupOther.rel[i]))
			restored++;
		else
		{
			failed++;
			Backup_Report("[CTR Native] restore: could not put back %s\n", s_backupOther.rel[i]);
		}
	}
	Backup_Report("[CTR Native] restored backup %s (%d files, %d failed)\n", name, restored, failed);
	return 1;
}

void NativeBackup_OnLaunch(time_t now)
{
	Backup_RemoveUnfinished();
	// After a restore the files are the chosen backup's, which is kept already.
	if (Backup_ApplyPending(now))
		return;
	NativeBackup_Take(NATIVE_BACKUP_LAUNCH, now);
}

void NativeBackup_BeforeSave(time_t now)
{
	if (s_backupLastTaken != 0 && now - s_backupLastTaken < NATIVE_BACKUP_INTERVAL_SECONDS)
		return;
	NativeBackup_Take(NATIVE_BACKUP_PLAY, now);
}

int NativeBackup_List(NativeBackupInfo *out, int max)
{
	char names[NATIVE_BACKUP_SCAN_MAX][NATIVE_BACKUP_NAME_MAX];
	char rel[NATIVE_BACKUP_PATH_MAX];
	char root[NATIVE_BACKUP_PATH_MAX];
	char path[NATIVE_BACKUP_PATH_MAX];
	const int count = Backup_Names(names, NATIVE_BACKUP_SCAN_MAX);
	int i, j, n = 0;

	for (i = 0; i < count && n < max; i++)
	{
		NativeBackupInfo *b = &out[n];
		char line[64];
		FILE *f;

		memset(b, 0, sizeof *b);
		snprintf(b->name, sizeof b->name, "%s", names[i]);
		Backup_ParseName(names[i], &b->when);
		b->reason = NATIVE_BACKUP_LAUNCH;
		snprintf(rel, sizeof rel, NATIVE_BACKUP_DIR "/%s", names[i]);
		if (!Backup_Path(root, sizeof root, rel))
			continue;
		if (Backup_Join(path, sizeof path, root, NATIVE_BACKUP_INFO_FILE) && (f = NativeFs_OpenRead(path)) != NULL)
		{
			while (fgets(line, sizeof line, f) != NULL)
			{
				line[strcspn(line, "\r\n")] = '\0';
				for (j = 0; j < 3; j++)
				{
					char want[40];
					snprintf(want, sizeof want, "reason=%s", s_backupReasonKeys[j]);
					if (strcmp(line, want) == 0)
						b->reason = (NativeBackupReason)j;
				}
			}
			fclose(f);
		}
		Backup_Collect(&s_backupOther, root);
		for (j = 0; j < s_backupOther.count; j++)
		{
			if (strncmp(s_backupOther.rel[j], "memcards/", 9) == 0)
				b->saves++;
			else
				b->soloSeeds++;
		}
		n++;
	}
	return n;
}
