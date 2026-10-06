#pragma once

#include <filesystem>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/stdio.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace libsys {
    // Source and destination are siblings on the same filesystem. A name chosen
    // at consent is not a reservation: an unrelated file may appear before publish.
    // Fail closed when the platform cannot provide an atomic no-replace rename.
    inline bool PublishNoReplace(const std::filesystem::path& source,
                                 const std::filesystem::path& destination) {
#if defined(_WIN32)
        return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#elif defined(__APPLE__)
        return renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2)
        constexpr unsigned no_replace = 1; // RENAME_NOREPLACE, Linux UAPI.
        return syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD,
                       destination.c_str(), no_replace) == 0;
#else
        return false;
#endif
    }
}
