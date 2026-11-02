// Harness for save backups (platform/native_backup.c): what a backup holds,
// when one is not taken, how many are kept, the interval, and a restore at the
// next start. It plays a game folder in a temporary directory, through the
// same file helpers the game uses, so it also runs on Windows (MinGW), where
// the folder rename is MoveFileExW.
//
//   cc -m32 -Wall -Wextra -D_DEFAULT_SOURCE -DCTR_NATIVE -I . -I include -o /tmp/test-native-backup tools/test-native-backup.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include "platform/native_fs_utf8.c"
#include "platform/native_backup.c"

static int g_checks;
static int g_failures;

static void expect(int condition, const char *name)
{
	g_checks++;
	if (!condition)
	{
		g_failures++;
		printf("FAIL: %s\n", name);
	}
}

static char g_root[256];

static void mkdirs(const char *rel)
{
	char path[600];
	char *p;
	snprintf(path, sizeof path, "%s/%s", g_root, rel);
	for (p = path + strlen(g_root) + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = '\0';
			NativeFs_MakeDirectory(path);
			*p = '/';
		}
	}
	NativeFs_MakeDirectory(path);
}

static void put(const char *rel, const char *text)
{
	char path[600];
	FILE *f;
	snprintf(path, sizeof path, "%s/%s", g_root, rel);
	f = fopen(path, "wb");
	if (f == NULL)
		return;
	fputs(text, f);
	fclose(f);
}

// The file's text, or "" when it is missing.
static const char *get(const char *rel)
{
	static char text[256];
	char path[600];
	FILE *f;
	size_t n;
	snprintf(path, sizeof path, "%s/%s", g_root, rel);
	f = fopen(path, "rb");
	text[0] = '\0';
	if (f == NULL)
		return text;
	n = fread(text, 1, sizeof text - 1, f);
	text[n] = '\0';
	fclose(f);
	return text;
}

static int exists(const char *rel)
{
	char path[600];
	snprintf(path, sizeof path, "%s/%s", g_root, rel);
	return NativeFs_FileExists(path) || Backup_IsFolder(path);
}

// 2026-11-02 14:00:00 local, plus minutes.
static time_t at(int minutes)
{
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = 2026 - 1900;
	t.tm_mon = 10;
	t.tm_mday = 2;
	t.tm_hour = 14;
	t.tm_isdst = -1;
	return mktime(&t) + minutes * 60;
}

int main(void)
{
	NativeBackupInfo list[NATIVE_BACKUP_KEEP + 4];
	char pending[NATIVE_BACKUP_NAME_MAX];
	int n, i;

	{
		const char *tmp = getenv("TMPDIR") != NULL ? getenv("TMPDIR") : getenv("TEMP") != NULL ? getenv("TEMP") : "/tmp";
		snprintf(g_root, sizeof g_root, "%s/ctr-backup-%d", tmp, (int)getpid());
	}
	Backup_RemoveTree(g_root);
	if (!NativeFs_MakeDirectory(g_root))
	{
		printf("FAIL: no temporary directory\n");
		return 1;
	}
	NativeBackup_SetBaseDir(g_root);

	// Nothing to back up yet: no backup, no folder.
	NativeBackup_OnLaunch(at(0));
	expect(NativeBackup_List(list, NATIVE_BACKUP_KEEP) == 0 && !exists("backups/20261102-140000"), "no files: no backup");

	// A save, a solo room with its save, and the files a backup leaves out.
	mkdirs("memcards/slot0");
	mkdirs("solo/rooms/AB12");
	put("memcards/slot0/BASCUS-94426-SLOTS", "save v1");
	put("memcards/slot0/~BASCUS-94426-SLOTS", "half-written");
	put("solo/rooms/AB12/AP_1.apsave", "solo v1");
	put("solo/rooms/AB12/AP_1.apsave.bak", "solo bak");
	put("solo/rooms/AB12/AP_1.archipelago", "room");

	NativeBackup_OnLaunch(at(0));
	n = NativeBackup_List(list, NATIVE_BACKUP_KEEP);
	expect(n == 1 && strcmp(list[0].name, "20261102-140000") == 0, "launch backup named by its time");
	expect(n == 1 && list[0].reason == NATIVE_BACKUP_LAUNCH && list[0].saves == 1 && list[0].soloSeeds == 1, "launch backup: one save, one solo seed");
	expect(n == 1 && list[0].when.tm_hour == 14 && list[0].when.tm_mday == 2 && list[0].when.tm_mon == 10, "time read back from the name");
	expect(strcmp(get("backups/20261102-140000/memcards/slot0/BASCUS-94426-SLOTS"), "save v1") == 0, "save copied");
	expect(strcmp(get("backups/20261102-140000/solo/rooms/AB12/AP_1.apsave"), "solo v1") == 0, "solo save copied");
	expect(!exists("backups/20261102-140000/memcards/slot0/~BASCUS-94426-SLOTS"), "temporary save left out");
	expect(!exists("backups/20261102-140000/solo/rooms/AB12/AP_1.apsave.bak") && !exists("backups/20261102-140000/solo/rooms/AB12/AP_1.archipelago"),
	       "only .apsave from a room");

	// Nothing changed: the next launch takes none.
	NativeBackup_OnLaunch(at(1));
	expect(NativeBackup_List(list, NATIVE_BACKUP_KEEP) == 1, "unchanged: no second backup");

	// Before a save: only once the interval has passed since the last backup.
	put("memcards/slot0/BASCUS-94426-SLOTS", "save v2");
	NativeBackup_BeforeSave(at(10));
	expect(NativeBackup_List(list, NATIVE_BACKUP_KEEP) == 1, "within the interval: no backup");
	NativeBackup_BeforeSave(at(16));
	n = NativeBackup_List(list, NATIVE_BACKUP_KEEP);
	expect(n == 2 && strcmp(list[0].name, "20261102-141600") == 0 && list[0].reason == NATIVE_BACKUP_PLAY, "after the interval: a play backup, newest first");

	// Two in the same second get a suffix.
	put("memcards/slot0/BASCUS-94426-SLOTS", "save v3");
	expect(NativeBackup_Take(NATIVE_BACKUP_PLAY, at(16)) == 1, "same second: taken");
	n = NativeBackup_List(list, NATIVE_BACKUP_KEEP);
	expect(n == 3 && strcmp(list[0].name, "20261102-141600-2") == 0, "same second: -2");

	// Only the newest are kept.
	for (i = 0; i < NATIVE_BACKUP_KEEP + 2; i++)
	{
		char text[32];
		snprintf(text, sizeof text, "save v%d", 10 + i);
		put("memcards/slot0/BASCUS-94426-SLOTS", text);
		NativeBackup_Take(NATIVE_BACKUP_PLAY, at(30 + i * 20));
	}
	n = NativeBackup_List(list, NATIVE_BACKUP_KEEP + 4);
	expect(n == NATIVE_BACKUP_KEEP, "pruned to the newest");
	expect(!exists("backups/20261102-140000"), "oldest removed");
	expect(strcmp(get("memcards/slot0/BASCUS-94426-SLOTS"), "save v21") == 0, "live save untouched by backups");

	// A restore waits for the next start, then puts the files back after a
	// backup of what it replaces. The oldest backup is chosen, so the backup
	// before the restore must not prune it.
	{
		char oldest[NATIVE_BACKUP_NAME_MAX];
		char oldestSave[96];
		snprintf(oldest, sizeof oldest, "%s", list[n - 1].name);
		snprintf(oldestSave, sizeof oldestSave, "backups/%s/memcards/slot0/BASCUS-94426-SLOTS", oldest);
		snprintf(pending, sizeof pending, "%s", get(oldestSave));

		expect(!NativeBackup_RequestRestore("20261102-999999"), "restore: unknown backup refused");
		expect(NativeBackup_RequestRestore(oldest), "restore: requested");
		{
			char name[NATIVE_BACKUP_NAME_MAX];
			expect(NativeBackup_PendingRestore(name, sizeof name) && strcmp(name, oldest) == 0, "restore: pending");
		}
		expect(strcmp(get("memcards/slot0/BASCUS-94426-SLOTS"), "save v21") == 0, "restore: nothing changes before the start");

		put("solo/rooms/AB12/AP_1.apsave", "solo v2");
		NativeBackup_OnLaunch(at(400));
		expect(strcmp(get("memcards/slot0/BASCUS-94426-SLOTS"), pending) == 0, "restore: save put back");
		expect(strcmp(get("solo/rooms/AB12/AP_1.apsave"), "solo v1") == 0, "restore: solo save put back");
		expect(!exists("memcards/slot0/BASCUS-94426-SLOTS.tmp"), "restore: no temporary left");
		{
			char name[NATIVE_BACKUP_NAME_MAX];
			expect(!NativeBackup_PendingRestore(name, sizeof name), "restore: done once");
		}
		n = NativeBackup_List(list, NATIVE_BACKUP_KEEP + 4);
		expect(n >= 1 && list[0].reason == NATIVE_BACKUP_BEFORE_RESTORE, "restore: the replaced state is the newest backup");
		expect(n >= 1 && strcmp(get("backups/20261102-204000/memcards/slot0/BASCUS-94426-SLOTS"), "save v21") == 0 &&
		       strcmp(get("backups/20261102-204000/solo/rooms/AB12/AP_1.apsave"), "solo v2") == 0, "restore: replaced files kept");
		expect(n == NATIVE_BACKUP_KEEP + 1 && strcmp(list[n - 1].name, oldest) == 0, "restore: chosen backup survived the prune");
	}

	// A solo save whose room is gone is not put back.
	{
		char name[NATIVE_BACKUP_NAME_MAX];
		char room[400];
		NativeBackup_List(list, NATIVE_BACKUP_KEEP + 4);
		snprintf(name, sizeof name, "%s", list[0].name);
		snprintf(room, sizeof room, "%s/solo/rooms/AB12", g_root);
		Backup_RemoveTree(room);
		expect(!exists("solo/rooms/AB12"), "room removed");
		expect(NativeBackup_RequestRestore(name), "restore 2: requested");
		NativeBackup_OnLaunch(at(500));
		expect(!exists("solo/rooms/AB12"), "restore 2: no room recreated");
		expect(strcmp(get("memcards/slot0/BASCUS-94426-SLOTS"), "save v21") == 0, "restore 2: save put back");
	}

	// A backup cut off before its rename is removed at the next start.
	mkdirs("backups/20261102-235900.tmp/memcards");
	NativeBackup_OnLaunch(at(600));
	expect(!exists("backups/20261102-235900.tmp"), "unfinished backup removed");

	Backup_RemoveTree(g_root);

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
