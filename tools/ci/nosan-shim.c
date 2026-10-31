// Compiler shim for running the harnesses on Windows (tools/ci/run-harnesses.py).
//
// MinGW ships no UBSan runtime, so every harness built with -fsanitize=undefined
// fails to link there, and that failure hides whether the harness itself passes.
// run-harnesses.py builds this program as cc.exe, gcc.exe and g++.exe in a folder
// it puts first on PATH. Each copy runs the real compiler of its own name from
// NOSAN_REAL_DIR with every -fsanitize / -fno-sanitize argument removed. CI runs
// on Linux with the sanitizer and never uses this.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Append one argument to a command line, quoted the way the C runtime parses it.
static void appendArg(char *line, size_t cap, const char *arg)
{
	size_t n = strlen(line);
	int quote = (*arg == '\0') || strpbrk(arg, " \t\"") != NULL;

	if (n > 0 && n + 1 < cap)
		line[n++] = ' ';
	if (quote && n + 1 < cap)
		line[n++] = '"';
	for (const char *p = arg; *p != '\0'; p++)
	{
		size_t slashes = 0;
		while (p[slashes] == '\\')
			slashes++;
		if (p[slashes] == '"' || (p[slashes] == '\0' && quote))
		{
			// Backslashes before a quote (or before the closing quote) double.
			for (size_t i = 0; i < slashes * 2 && n + 1 < cap; i++)
				line[n++] = '\\';
			p += slashes;
			if (*p == '\0')
				break;
			if (n + 2 < cap)
			{
				line[n++] = '\\';
				line[n++] = '"';
			}
			continue;
		}
		for (size_t i = 0; i < slashes && n + 1 < cap; i++)
			line[n++] = '\\';
		p += slashes;
		if (*p == '\0')
			break;
		if (n + 1 < cap)
			line[n++] = *p;
	}
	if (quote && n + 1 < cap)
		line[n++] = '"';
	line[n] = '\0';
}

int main(int argc, char **argv)
{
	static char line[65536];
	char real[MAX_PATH];
	const char *dir = getenv("NOSAN_REAL_DIR");
	const char *name = strrchr(argv[0], '\\');
	const char *slash = strrchr(argv[0], '/');
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	DWORD code = 1;

	if (slash != NULL && (name == NULL || slash > name))
		name = slash;
	name = (name != NULL) ? name + 1 : argv[0];
	if (dir == NULL)
	{
		fprintf(stderr, "nosan-shim: NOSAN_REAL_DIR is not set\n");
		return 1;
	}
	snprintf(real, sizeof real, "%s\\%s", dir, name);
	if (strstr(real, ".exe") == NULL)
		strncat(real, ".exe", sizeof real - strlen(real) - 1);

	line[0] = '\0';
	appendArg(line, sizeof line, real);
	for (int i = 1; i < argc; i++)
	{
		if (strncmp(argv[i], "-fsanitize", 10) == 0 || strncmp(argv[i], "-fno-sanitize", 13) == 0)
			continue;
		appendArg(line, sizeof line, argv[i]);
	}

	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	if (!CreateProcessA(real, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
	{
		fprintf(stderr, "nosan-shim: cannot run %s (error %lu)\n", real, GetLastError());
		return 1;
	}
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &code);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return (int)code;
}
