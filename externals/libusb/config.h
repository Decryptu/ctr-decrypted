/* config.h for building libusb's Windows backend with MinGW as part of CTR Decrypted. */

#define DEFAULT_VISIBILITY __attribute__((visibility("default")))

/* Define to 1 to enable message logging. */
#define ENABLE_LOGGING 1

/* Define to 1 if compiling for a Windows platform. */
#define PLATFORM_WINDOWS 1

/* Define to the attribute for enabling parameter checks on printf-like functions. */
#define PRINTF_FORMAT(a, b) __attribute__((__format__(__printf__, a, b)))

/* MinGW provides struct timespec and clock_gettime. */
#define HAVE_CLOCK_GETTIME 1
#define HAVE_STRUCT_TIMESPEC 1
#define HAVE_SYS_TIME_H 1

#define _GNU_SOURCE 1
