#ifndef NATIVE_FAKECRASH_H
#define NATIVE_FAKECRASH_H

#define NATIVE_FAKECRASH_PACK "FAKECRASH-JP.pack"
#define NATIVE_FAKECRASH_MAGIC "FCJP\1\0\0\0"
#define NATIVE_FAKECRASH_COUNT 10

#ifdef __cplusplus
extern "C" {
#endif

extern const int g_fakeCrashIndices[NATIVE_FAKECRASH_COUNT];

enum NativeFakeCrashStatus
{
	NATIVE_FAKECRASH_NO_DISC,
	NATIVE_FAKECRASH_READY,
	NATIVE_FAKECRASH_INSTALLED,
	NATIVE_FAKECRASH_RESTART,
	NATIVE_FAKECRASH_FAILED
};

enum NativeFakeCrashStatus NativeFakeCrash_Status(void);
enum NativeFakeCrashStatus NativeFakeCrash_Import(void);

#ifdef __cplusplus
}
#endif
#endif
