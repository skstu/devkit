#include <libsys/file_lock.h>

#include <utility>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace libsys {
    namespace {
#if defined(_WIN32)
        constexpr NativeFileHandle invalid_handle = nullptr;
        std::error_code LastError() noexcept {
            return {static_cast<int>(GetLastError()), std::system_category()};
        }
#else
        constexpr NativeFileHandle invalid_handle = -1;
        std::error_code LastError() noexcept {
            return {errno, std::generic_category()};
        }
#endif
        bool ValidPath(const std::filesystem::path& file) {
            if (file.empty() || !file.is_absolute() || file.filename().empty())
                return false;
            const auto& native = file.native();
            if (native.find(std::filesystem::path::value_type{}) != native.npos)
                return false;
            for (const auto& part : file.relative_path())
                if (part == "." || part == "..")
                    return false;
#if defined(_WIN32)
            // Local drive only; no UNC, device paths or alternate data streams.
            if (file.root_name().native().size() != 2 || file.root_name().native()[1] != L':')
                return false;
            for (const auto& part : file.relative_path()) {
                const auto value = part.native();
                if (!value.empty() && (value.back() == L'.' || value.back() == L' ' ||
                                       value.find_first_of(L"<>:\"|?*") != value.npos))
                    return false;
            }
#endif
            return true;
        }
    } // namespace

    bool TryLockFileHandle(NativeFileHandle handle, std::error_code& error) noexcept {
        error.clear();
#if defined(_WIN32)
        OVERLAPPED overlap{};
        if (LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap))
            return true;
        const auto code = GetLastError();
        error = code == ERROR_LOCK_VIOLATION ? std::make_error_code(std::errc::device_or_resource_busy)
                                             : std::error_code(static_cast<int>(code), std::system_category());
#else
        int result;
        do {
            result = flock(handle, LOCK_EX | LOCK_NB);
        } while (result != 0 && errno == EINTR);
        if (result == 0)
            return true;
        const int code = errno;
        error = code == EWOULDBLOCK || code == EAGAIN ? std::make_error_code(std::errc::device_or_resource_busy)
                                                      : std::error_code(code, std::generic_category());
#endif
        return false;
    }

    FileLock FileLock::TryAcquire(const std::filesystem::path& file, std::error_code& error) {
        error.clear();
        if (!ValidPath(file)) {
            error = std::make_error_code(std::errc::invalid_argument);
            return {};
        }
#if defined(_WIN32)
        auto path = std::filesystem::path(file).make_preferred().native();
        path.insert(0, L"\\\\?\\"); // Long Unicode paths; validated local drive above.
        const auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            error = LastError();
            return {};
        }
        FileLock result(handle);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info)) {
            error = LastError();
            return {};
        }
        if (GetFileType(handle) != FILE_TYPE_DISK || info.nNumberOfLinks != 1 ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
            error = std::make_error_code(std::errc::invalid_argument);
            return {};
        }
#else
        // O_NONBLOCK also avoids hanging on a malicious FIFO before fstat rejects it.
        int handle;
        do {
            handle = open(file.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        } while (handle < 0 && errno == EINTR);
        if (handle < 0) {
            error = LastError();
            return {};
        }
        FileLock result(handle);
        struct stat info{};
        if (fstat(handle, &info) != 0) {
            error = LastError();
            return {};
        }
        if (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_uid != geteuid() || (info.st_mode & 0077)) {
            error = std::make_error_code(std::errc::permission_denied);
            return {};
        }
#endif
        if (!TryLockFileHandle(handle, error))
            return {};
        return result;
    }

    FileLock::~FileLock() {
        Reset();
    }
    FileLock::FileLock(FileLock&& other) noexcept : handle_(std::exchange(other.handle_, invalid_handle)) {
    }
    FileLock& FileLock::operator=(FileLock&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = std::exchange(other.handle_, invalid_handle);
        }
        return *this;
    }
    bool FileLock::owns_lock() const noexcept {
        return handle_ != invalid_handle;
    }
    void FileLock::Reset() noexcept {
        const auto handle = std::exchange(handle_, invalid_handle);
        if (handle == invalid_handle)
            return;
        // Close without a separate unlock. In a forked child, LOCK_UN would also
        // release the parent's flock; close merely drops this process's reference.
#if defined(_WIN32)
        CloseHandle(handle);
#else
        close(handle); // Never retry close after EINTR: the fd may already be reused.
#endif
    }
} // namespace libsys
