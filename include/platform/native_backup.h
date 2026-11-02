#ifndef NATIVE_BACKUP_H
#define NATIVE_BACKUP_H

#include <stddef.h>
#include <time.h>

// Save backups (platform/native_backup.c). Copies of the player's progress are
// kept in backups/<name>/ next to the game, with the same relative layout as
// the files they copy:
//   memcards/<slot>/<file>        the memory card saves
//   solo/rooms/<room>/<x>.apsave  the Play Solo server saves (the progress of a
//                                 solo seed; a multiworld's lives on its server)
// A backup is taken when the game starts and, while playing, before a save is
// written when the newest backup is older than NATIVE_BACKUP_INTERVAL_SECONDS.
// One that would match the newest backup byte for byte is not taken. The
// newest NATIVE_BACKUP_KEEP are kept.
//
// A restore is asked for in the menu and done at the next start, before the
// game reads a save or starts a solo server: the running game holds its save in
// memory and would write it back. The state it replaces is backed up first.
//
// A backup is built in backups/<name>.tmp and renamed into place, so a listed
// backup is always complete. The folder name is the local time it was taken,
// YYYYMMDD-HHMMSS (-2, -3... when two fall in the same second), so names sort
// by age.

#define NATIVE_BACKUP_DIR "backups"
#define NATIVE_BACKUP_KEEP 10
#define NATIVE_BACKUP_INTERVAL_SECONDS (15 * 60)
#define NATIVE_BACKUP_NAME_MAX 32
#define NATIVE_BACKUP_PATH_MAX 600
#define NATIVE_BACKUP_FILES_MAX 128

typedef enum
{
	NATIVE_BACKUP_LAUNCH = 0,        // the game started
	NATIVE_BACKUP_PLAY = 1,          // before a save, while playing
	NATIVE_BACKUP_BEFORE_RESTORE = 2 // what a restore replaced
} NativeBackupReason;

typedef struct
{
	char name[NATIVE_BACKUP_NAME_MAX];
	NativeBackupReason reason;
	struct tm when; // from the name
	int saves;      // memory card files
	int soloSeeds;  // Play Solo saves
} NativeBackupInfo;

// The folder the backed-up paths are relative to; "" (the default) is the
// working folder, which main.c sets to the game folder. For the harness.
void NativeBackup_SetBaseDir(const char *dir);

// Take a backup now. Returns 1 when one was made, 0 when there was nothing to
// keep (no files, or the same as the newest backup), -1 on a file error.
int NativeBackup_Take(NativeBackupReason reason, time_t now);

// At start: apply a pending restore, then take the launch backup.
void NativeBackup_OnLaunch(time_t now);

// Before a save is written: a backup when the last one is older than the
// interval.
void NativeBackup_BeforeSave(time_t now);

// The backups, newest first. Returns how many were written to out.
int NativeBackup_List(NativeBackupInfo *out, int max);

// Restore this backup at the next start. Returns 1 when it was recorded.
int NativeBackup_RequestRestore(const char *name);
// The backup a restore waits for, or 0 when none does.
int NativeBackup_PendingRestore(char *name, size_t size);
void NativeBackup_CancelRestore(void);

#endif // NATIVE_BACKUP_H
