// Dev runs only (CTR_DEV_FAST set, as tools/decrypted/devrun.ps1 does): when the
// game crashes, write the exception, the faulting address and every stack word
// that points into the executable's code to crash.txt in the working directory.
// Resolve them with addr2line -f -e <the exe> <addresses> (docs/DEV-TOOLS.md).
// Its own translation unit, so <windows.h> stays out of the unity build.
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>

static LONG CALLBACK NativeCrashLog_Handler(EXCEPTION_POINTERS *info)
{
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	// only faults that end the process; debug output and C++ exceptions pass
	if ((code != EXCEPTION_INT_DIVIDE_BY_ZERO) && (code != EXCEPTION_ACCESS_VIOLATION) && (code != EXCEPTION_ILLEGAL_INSTRUCTION) &&
		(code != EXCEPTION_STACK_OVERFLOW) && (code != EXCEPTION_INT_OVERFLOW))
		return EXCEPTION_CONTINUE_SEARCH;

	const HMODULE exe = GetModuleHandleA(NULL);
	const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)exe + ((const IMAGE_DOS_HEADER *)exe)->e_lfanew);
	const ULONG_PTR codeStart = (ULONG_PTR)exe + nt->OptionalHeader.BaseOfCode;
	const ULONG_PTR codeEnd = codeStart + nt->OptionalHeader.SizeOfCode;

	FILE *f = fopen("crash.txt", "w");
	if (f == NULL)
		return EXCEPTION_CONTINUE_SEARCH;
	fprintf(f, "exception 0x%08lx at 0x%p\n", (unsigned long)code, info->ExceptionRecord->ExceptionAddress);
#if defined(_M_IX86) || defined(__i386__)
	const ULONG_PTR *sp = (const ULONG_PTR *)info->ContextRecord->Esp;
#else
	const ULONG_PTR *sp = (const ULONG_PTR *)info->ContextRecord->Rsp;
#endif
	fprintf(f, "stack words into the code section:\n");
	MEMORY_BASIC_INFORMATION mbi;
	if ((VirtualQuery(sp, &mbi, sizeof mbi) != 0) && (mbi.State == MEM_COMMIT))
	{
		// the committed stack region above the faulting frame, at most 16 KiB of it
		const ULONG_PTR *end = (const ULONG_PTR *)((const char *)mbi.BaseAddress + mbi.RegionSize);
		if (end > sp + 4096)
			end = sp + 4096;
		for (const ULONG_PTR *p = sp; p < end; p++)
		{
			if ((*p >= codeStart) && (*p < codeEnd))
				fprintf(f, "0x%p\n", (void *)*p);
		}
	}
	fclose(f);
	return EXCEPTION_CONTINUE_SEARCH;
}

// abort() raises SIGABRT, not an exception: scan from this handler's own frame.
static void NativeCrashLog_Abort(int sig)
{
	const HMODULE exe = GetModuleHandleA(NULL);
	const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)exe + ((const IMAGE_DOS_HEADER *)exe)->e_lfanew);
	const ULONG_PTR codeStart = (ULONG_PTR)exe + nt->OptionalHeader.BaseOfCode;
	const ULONG_PTR codeEnd = codeStart + nt->OptionalHeader.SizeOfCode;
	const ULONG_PTR *sp = (const ULONG_PTR *)__builtin_frame_address(0);
	MEMORY_BASIC_INFORMATION mbi;
	FILE *f = fopen("crash.txt", "w");

	if (f != NULL)
	{
		fprintf(f, "abort (signal %d)\nstack words into the code section:\n", sig);
		if ((VirtualQuery(sp, &mbi, sizeof mbi) != 0) && (mbi.State == MEM_COMMIT))
		{
			const ULONG_PTR *end = (const ULONG_PTR *)((const char *)mbi.BaseAddress + mbi.RegionSize);
			if (end > sp + 4096)
				end = sp + 4096;
			for (const ULONG_PTR *p = sp; p < end; p++)
			{
				if ((*p >= codeStart) && (*p < codeEnd))
					fprintf(f, "0x%p\n", (void *)*p);
			}
		}
		fclose(f);
	}
	signal(SIGABRT, SIG_DFL);
	raise(SIGABRT);
}

__attribute__((constructor)) static void NativeCrashLog_Install(void)
{
	const char *fast = getenv("CTR_DEV_FAST");
	if ((fast != NULL) && (*fast != '\0') && (*fast != '0'))
	{
		AddVectoredExceptionHandler(1, NativeCrashLog_Handler);
		signal(SIGABRT, NativeCrashLog_Abort);
	}
}
#endif
