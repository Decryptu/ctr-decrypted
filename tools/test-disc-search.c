// Harness for the first-launch disc search (platform/native_disc_search.c):
// which folders it looks in, in what order, how deep, what it skips, and that
// only large enough .bin files reach the validator. It builds a folder tree in
// a temporary directory, with sparse files standing in for disc images, and a
// validator stub that accepts names containing "good".
//
//   cc -m32 -Wall -Wextra -D_DEFAULT_SOURCE -DCTR_NATIVE -I . -I include -o /tmp/test-disc-search tools/test-disc-search.c
//
// Exit 0 = every assertion held; failures are printed otherwise.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "platform/native_fs_utf8.c"
#include "platform/native_disc_search.c"

static int g_checks;
static int g_failures;
static int g_validated;

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
			mkdir(path, 0755);
			*p = '/';
		}
	}
	mkdir(path, 0755);
}

// A sparse file of the given size: no disk space, but the size the search sees.
static void file(const char *rel, long size)
{
	char path[600];
	FILE *f;
	snprintf(path, sizeof path, "%s/%s", g_root, rel);
	f = fopen(path, "wb");
	if (f == NULL)
		return;
	if (size > 0)
	{
		fseek(f, size - 1, SEEK_SET);
		fputc(0, f);
	}
	fclose(f);
}

static int acceptGood(void *ctx, const char *path)
{
	(void)ctx;
	g_validated++;
	return strstr(path, "good") != NULL;
}

static int search(const char *const *rels, int count, char *out, size_t size)
{
	char roots[8][600];
	const char *list[8];
	int i;
	for (i = 0; i < count; i++)
	{
		snprintf(roots[i], sizeof roots[i], "%s/%s", g_root, rels[i]);
		list[i] = roots[i];
	}
	g_validated = 0;
	return NativeDiscSearch_Walk(list, count, acceptGood, NULL, out, size);
}

static int endsWith(const char *s, const char *suffix)
{
	const size_t n = strlen(s), m = strlen(suffix);
	return n >= m && strcmp(s + n - m, suffix) == 0;
}

#define BIG (NATIVE_DISC_SEARCH_MIN_BYTES + 1)

int main(void)
{
	char out[NATIVE_DISC_SEARCH_PATH_MAX];

	snprintf(g_root, sizeof g_root, "/tmp/ctr-disc-search-XXXXXX");
	if (mkdtemp(g_root) == NULL)
	{
		printf("FAIL: no temporary directory\n");
		return 1;
	}

	// Downloads: a small .bin and a big one that is not the game, then the disc
	// two folders down. Only big .bin files reach the validator.
	mkdirs("home/Downloads/CTR/disc");
	file("home/Downloads/readme.bin", 4096);
	file("home/Downloads/other.bin", BIG);
	file("home/Downloads/CTR/disc/good.BIN", BIG);
	{
		const char *roots[] = {"home/Downloads"};
		expect(search(roots, 1, out, sizeof out) == 1, "found two folders down");
		expect(endsWith(out, "/CTR/disc/good.BIN"), "found path, upper-case extension");
		expect(g_validated == 2, "only big .bin files validated");
	}

	// A folder's files come before its subfolders.
	mkdirs("home/Games/sub");
	file("home/Games/sub/good-deep.bin", BIG);
	file("home/Games/good-top.bin", BIG);
	{
		const char *roots[] = {"home/Games"};
		expect(search(roots, 1, out, sizeof out) == 1 && endsWith(out, "/good-top.bin"), "files before subfolders");
	}

	// Roots are searched in order; a missing root is skipped.
	{
		const char *roots[] = {"missing", "home/Games", "home/Downloads"};
		expect(search(roots, 3, out, sizeof out) == 1 && endsWith(out, "/good-top.bin"), "roots in order, missing skipped");
	}

	// Three levels below a root, not four.
	mkdirs("deep/a/b/c/d");
	file("deep/a/b/c/d/good.bin", BIG);
	{
		const char *roots[] = {"deep"};
		expect(search(roots, 1, out, sizeof out) == 0 && out[0] == '\0', "four levels down is not searched");
	}
	mkdirs("deep3/a/b/c");
	file("deep3/a/b/c/good.bin", BIG);
	{
		const char *roots[] = {"deep3"};
		expect(search(roots, 1, out, sizeof out) == 1, "three levels down is searched");
	}

	// Hidden, AppData and node_modules folders are skipped.
	mkdirs("skip/.cache");
	mkdirs("skip/AppData");
	mkdirs("skip/node_modules");
	mkdirs("skip/$Recycle.Bin");
	file("skip/.cache/good.bin", BIG);
	file("skip/AppData/good.bin", BIG);
	file("skip/node_modules/good.bin", BIG);
	file("skip/$Recycle.Bin/good.bin", BIG);
	{
		const char *roots[] = {"skip"};
		expect(search(roots, 1, out, sizeof out) == 0, "skipped folders are not searched");
	}

	// Nothing accepted: 0 and an empty path.
	{
		const char *roots[] = {"home/Downloads/CTR"};
		file("home/Downloads/CTR/disc/good.BIN", 10);
		expect(search(roots, 1, out, sizeof out) == 0 && out[0] == '\0', "no disc: nothing found");
	}

	// The production roots: the game folder, the one above, then the home folders.
	{
		char roots[NATIVE_DISC_SEARCH_ROOTS_MAX][NATIVE_DISC_SEARCH_PATH_MAX];
		int n = NativeDiscSearch_Roots("/opt/games/ctr", "/home/p", roots, NATIVE_DISC_SEARCH_ROOTS_MAX);
		expect(n == 8, "roots: game, parent and six home folders");
		expect(n >= 3 && strcmp(roots[0], "/opt/games/ctr") == 0 && strcmp(roots[1], "/opt/games") == 0 &&
		       strcmp(roots[2], "/home/p/Downloads") == 0, "roots: order");
		expect(n == 8 && strcmp(roots[7], "/home/p/OneDrive/Documents") == 0, "roots: OneDrive last");
		expect(NativeDiscSearch_Roots(NULL, "/home/p", roots, NATIVE_DISC_SEARCH_ROOTS_MAX) == 6, "roots: no game folder");
		expect(NativeDiscSearch_Roots("/opt/games/ctr", NULL, roots, NATIVE_DISC_SEARCH_ROOTS_MAX) == 2, "roots: no home");
		expect(NativeDiscSearch_Roots("ctr", NULL, roots, NATIVE_DISC_SEARCH_ROOTS_MAX) == 1, "roots: relative game folder has no parent");
		expect(NativeDiscSearch_Roots("/opt/games/ctr", "/home/p", roots, 3) == 3, "roots: capped at max");
	}

	{
		char cmd[300];
		snprintf(cmd, sizeof cmd, "rm -rf '%s'", g_root);
		if (system(cmd) != 0)
			printf("note: could not remove %s\n", g_root);
	}

	printf("%d checks, %d failures\n", g_checks, g_failures);
	return g_failures == 0 ? 0 : 1;
}
