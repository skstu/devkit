#pragma once

#include <filesystem>
#include <system_error>

namespace libsys {
#if defined(_WIN32)
    using NativeFileHandle = void*;
#else
    using NativeFileHandle = int;
#endif

    // Nonblocking exclusive lock on an already opened, validated regular file.
    // Does not own/close the handle. Its owner must retain it for the entire lease.
    // Contention reports errc::device_or_resource_busy; other errors retain OS codes.
    // POSIX: flock. Windows: LockFileEx on byte zero, including an empty file.
    bool TryLockFileHandle(NativeFileHandle handle, std::error_code& error) noexcept;

    // Move-only process lock. All participants must use the same persistent file.
    // The parent directory must already exist and remain under the host's control.
    // Rejects symlink/reparse-point and multiply-linked lock files; never truncates,
    // deletes or replaces the file. This is not a workspace permission validator.
    class FileLock final {
    public:
        FileLock() noexcept = default;
        ~FileLock();
        FileLock(const FileLock&) = delete;
        FileLock& operator=(const FileLock&) = delete;
        FileLock(FileLock&& other) noexcept;
        FileLock& operator=(FileLock&& other) noexcept;

        // Absolute local file path. Mobile hosts supply a path inside their app
        // container (or an explicitly shared container), not HOME or cwd.
        static FileLock TryAcquire(const std::filesystem::path& file, std::error_code& error);
        bool owns_lock() const noexcept;
        explicit operator bool() const noexcept {
            return owns_lock();
        }
        void Reset() noexcept;

    private:
        explicit FileLock(NativeFileHandle handle) noexcept : handle_(handle) {
        }
#if defined(_WIN32)
        NativeFileHandle handle_ = nullptr;
#else
        NativeFileHandle handle_ = -1;
#endif
    };
} // namespace libsys
