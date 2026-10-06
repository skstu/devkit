#pragma once

// Compatibility-only OS includes. New public headers must not include this file.
#include <libsys/platform.h>

#if defined(__OSWIN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <shellapi.h>
#include <winioctl.h>
#include <intrin.h>
#include <shlobj.h>
#include <objbase.h>
#include <gdiplus.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <fcntl.h>
#include <io.h>
#pragma comment(lib, "ws2_32.lib")
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#if defined(max)
#undef max
#endif
#if defined(min)
#undef min
#endif

#elif defined(__OSLINUX__)
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>    // file locks
#include <netinet/in.h>
#include <poll.h>
#include <spawn.h>    // posix_spawn
#include <signal.h>   // kill
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h> // flock
#include <sys/wait.h> // waitpid
#include <unistd.h>
#include <pwd.h>      // getpwuid
#include <dlfcn.h>
#elif defined(__OSMAC__)
#include <arpa/inet.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>
#include <ImageIO/ImageIO.h>
#include <Security/Security.h> //coolies
#include <dlfcn.h>
#include <fcntl.h>
#include <libproc.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <spawn.h>
#include <sys/file.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>
#elif defined(__OSANDROID__) || defined(__OSIOS__)
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
#endif

