#pragma once

/* Platform detection */
#if !defined(__OS_DETECTED__)
/* Windows (including MinGW/Cygwin) */
#if defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__) ||               \
    defined(__MINGW32__) || defined(__MINGW64__)
#define __OSWIN__
/* Android */
#elif defined(__ANDROID__)
#define __OSANDROID__
/* Apple platforms */
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#define __OSAPPLE__
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#define __OSIOS__
#elif defined(TARGET_OS_MAC) && TARGET_OS_MAC
#define __OSMAC__
#else
/* generic apple */
#endif
/* Linux */
#elif defined(__linux__)
#define __OSLINUX__
/* BSD variants */
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) ||   \
    defined(__DragonFly__)
#define __OSBSD__
/* Generic UNIX/POSIX */
#elif defined(__unix__) || defined(__posix)
#define __OSUNIX__
#else
#define __OSUNKNOWN__
#endif
#define __OS_DETECTED__ 1
#endif

