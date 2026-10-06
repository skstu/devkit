#pragma once
#include <libsys/file_lock.h>
#include <libsys/native_file.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif
#endif

namespace libsys::files {
    namespace fs = std::filesystem;
    using Handle = libsys::NativeFile;
    enum class PrivateFileError
    {
        unavailable,
        unsafe_path,
        busy,
        unsupported_format
    };
    struct PrivateFileFailure {
        PrivateFileError error;
    };
    inline void RequirePrivateFile(bool condition, PrivateFileError error = PrivateFileError::unavailable) {
        if (!condition)
            throw PrivateFileFailure{error};
    }
// Owner-private local files. Callers retain validated ancestor handles and
// own their layout and recovery policy. Read is bounded to a small marker.
#ifdef _WIN32

    struct PrivateDirectory {
        PrivateDirectory(const PrivateDirectory&) = delete;
        PrivateDirectory& operator=(const PrivateDirectory&) = delete;
        static constexpr DWORD directory_access = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY |
                                                  FILE_DELETE_CHILD | FILE_TRAVERSE;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        std::vector<unsigned char> token_user;
        PrivateDirectory() {
            Handle token;
            RequirePrivateFile(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value));
            DWORD size = 0;
            GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
            RequirePrivateFile(size != 0);
            token_user.resize(size);
            RequirePrivateFile(GetTokenInformation(token.value, TokenUser, token_user.data(), size, &size));
            LPWSTR sid = nullptr;
            RequirePrivateFile(ConvertSidToStringSidW(User(), &sid));
            const auto text = std::wstring(L"O:") + sid + L"D:P(A;OICI;FA;;;" + sid + L")(A;OICI;FA;;;SY)";
            LocalFree(sid);
            RequirePrivateFile(ConvertStringSecurityDescriptorToSecurityDescriptorW(text.c_str(), SDDL_REVISION_1, &descriptor, nullptr));
        }
        ~PrivateDirectory() {
            if (descriptor)
                LocalFree(descriptor);
        }
        PSID User() const {
            return reinterpret_cast<const TOKEN_USER*>(token_user.data())->User.Sid;
        }
        void Private(const Handle& file) const {
            PSID owner = nullptr;
            PACL acl = nullptr;
            PSECURITY_DESCRIPTOR security = nullptr;
            const auto result = GetSecurityInfo(file.value, SE_FILE_OBJECT,
                                                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                                &owner, nullptr, &acl, nullptr, &security);
            bool safe = result == ERROR_SUCCESS && owner && EqualSid(owner, User()) && acl;
            if (safe)
                for (DWORD i = 0; i < acl->AceCount; ++i) {
                    void* ace = nullptr;
                    if (!GetAce(acl, i, &ace)) {
                        safe = false;
                        break;
                    }
                    const auto header = static_cast<ACE_HEADER*>(ace);
                    if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                        const auto allowed = static_cast<ACCESS_ALLOWED_ACE*>(ace);
                        if (!EqualSid(&allowed->SidStart, User()) &&
                            !IsWellKnownSid(&allowed->SidStart, WinLocalSystemSid))
                            safe = false;
                    }
                    else if (header->AceType != ACCESS_DENIED_ACE_TYPE)
                        safe = false;
                }
            if (security)
                LocalFree(security);
            RequirePrivateFile(safe, PrivateFileError::unsafe_path);
        }
        Handle Open(const fs::path& path, bool directory, bool create, bool exclusive = false,
                    DWORD extra_access = 0) const {
            SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
            // Attribute-only handles do not deny directory rename on Windows.
            // LIST_DIRECTORY participates in sharing; deliberately omit SHARE_DELETE.
            Handle result(CreateFileW(path.c_str(), extra_access | (directory ? FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | READ_CONTROL : GENERIC_READ | GENERIC_WRITE | READ_CONTROL),
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                                      create ? (exclusive ? CREATE_NEW : OPEN_ALWAYS) : OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), nullptr));
            RequirePrivateFile(result.value != INVALID_HANDLE_VALUE);
            BY_HANDLE_FILE_INFORMATION info{};
            RequirePrivateFile(GetFileType(result.value) == FILE_TYPE_DISK && GetFileInformationByHandle(result.value, &info) &&
                                   !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                                   bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
                                   (directory || info.nNumberOfLinks == 1),
                               PrivateFileError::unsafe_path);
            return result;
        }
        bool Mkdir(const fs::path& path) const {
            SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
            if (CreateDirectoryW(path.c_str(), &attributes))
                return true;
            RequirePrivateFile(GetLastError() == ERROR_ALREADY_EXISTS);
            return false;
        }
        bool Same(const Handle& a, const Handle& b) const {
            BY_HANDLE_FILE_INFORMATION x{}, y{};
            return GetFileInformationByHandle(a.value, &x) && GetFileInformationByHandle(b.value, &y) &&
                   x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh &&
                   x.nFileIndexLow == y.nFileIndexLow;
        }
        void Lock(const Handle& file) const {
            std::error_code error;
            if (!libsys::TryLockFileHandle(file.value, error))
                throw PrivateFileFailure{error == std::errc::device_or_resource_busy ? PrivateFileError::busy : PrivateFileError::unavailable};
        }
        std::string Read(const Handle& file) const {
            std::array<char, 256> bytes{};
            DWORD count = 0;
            RequirePrivateFile(SetFilePointerEx(file.value, {}, nullptr, FILE_BEGIN));
            RequirePrivateFile(ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr));
            RequirePrivateFile(count < bytes.size(), PrivateFileError::unsupported_format);
            return {bytes.data(), count};
        }
        void Write(const Handle& file, const std::string& value) const {
            DWORD count = 0;
            RequirePrivateFile(SetFilePointerEx(file.value, {}, nullptr, FILE_BEGIN));
            RequirePrivateFile(WriteFile(file.value, value.data(), static_cast<DWORD>(value.size()), &count, nullptr) &&
                               count == value.size() && FlushFileBuffers(file.value));
        }
        // Mutate the validated file itself, keeping it pinned until completion.
        // These handles require DELETE; destination replacement is never allowed.
        void Rename(const Handle& file, const fs::path& destination) const {
            const auto name = destination.wstring();
            std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + name.size() * sizeof(wchar_t));
            auto* info = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
            info->ReplaceIfExists = FALSE;
            info->RootDirectory = nullptr;
            info->FileNameLength = static_cast<DWORD>(name.size() * sizeof(wchar_t));
            std::memcpy(info->FileName, name.data(), info->FileNameLength);
            RequirePrivateFile(SetFileInformationByHandle(file.value, FileRenameInfo, info,
                                                          static_cast<DWORD>(storage.size())));
        }
        void Remove(const Handle& file) const {
            FILE_DISPOSITION_INFO disposition{TRUE};
            RequirePrivateFile(SetFileInformationByHandle(file.value, FileDispositionInfo,
                                                          &disposition, sizeof(disposition)));
        }
    };
#else

#ifdef __APPLE__
    constexpr int kSearch = O_SEARCH;
#elif defined(O_PATH)
    constexpr int kSearch = O_PATH;
#else
    constexpr int kSearch = O_RDONLY;
#endif
    struct PrivateDirectory {
        PrivateDirectory() = default;
        PrivateDirectory(const PrivateDirectory&) = delete;
        PrivateDirectory& operator=(const PrivateDirectory&) = delete;
        Handle Directory(int parent, const std::string& name, bool create, bool enumerable = false) const {
            bool created = false;
            if (create) {
                created = mkdirat(parent, name.c_str(), 0700) == 0;
                RequirePrivateFile(created || errno == EEXIST);
            }
            Handle result(openat(parent, name.c_str(), (enumerable ? O_RDONLY : kSearch) | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (result.value < 0)
                throw PrivateFileFailure{errno == ELOOP || errno == ENOTDIR ? PrivateFileError::unsafe_path : PrivateFileError::unavailable};
            if (created) {
                Private(result, true);
                // Persist each new entry before descending into it. O_PATH/O_SEARCH
                // descriptors cannot themselves be fsynced on every supported system.
                Handle writable_parent(openat(parent, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
                RequirePrivateFile(writable_parent.value >= 0 && fsync(writable_parent.value) == 0);
            }
            return result;
        }
        Handle File(int parent, const std::string& name, bool create, bool exclusive = false,
                    unsigned max_links = 1) const {
            Handle result(openat(parent, name.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT : 0) | (exclusive ? O_EXCL : 0), 0600));
            if (result.value < 0)
                throw PrivateFileFailure{errno == ELOOP ? PrivateFileError::unsafe_path : PrivateFileError::unavailable};
            Private(result, false, max_links);
            return result;
        }
        void Private(const Handle& file, bool directory, unsigned max_links = 1) const {
            struct stat info{};
            RequirePrivateFile(fstat(file.value, &info) == 0 && info.st_uid == geteuid() &&
                                   (directory ? S_ISDIR(info.st_mode) && (info.st_mode & 0777) == 0700 : S_ISREG(info.st_mode) && info.st_nlink >= 1 && info.st_nlink <= max_links && (info.st_mode & 0777) == 0600),
                               PrivateFileError::unsafe_path);
        }
        bool Same(const Handle& a, const Handle& b) const {
            struct stat x{}, y{};
            return fstat(a.value, &x) == 0 && fstat(b.value, &y) == 0 && x.st_dev == y.st_dev && x.st_ino == y.st_ino;
        }
        void Lock(const Handle& file) const {
            std::error_code error;
            if (!libsys::TryLockFileHandle(file.value, error))
                throw PrivateFileFailure{error == std::errc::device_or_resource_busy ? PrivateFileError::busy : PrivateFileError::unavailable};
        }
        std::string Read(const Handle& file) const {
            std::array<char, 256> bytes{};
            const auto size = pread(file.value, bytes.data(), bytes.size(), 0);
            RequirePrivateFile(size >= 0);
            RequirePrivateFile(static_cast<std::size_t>(size) < bytes.size(), PrivateFileError::unsupported_format);
            return {bytes.data(), static_cast<std::size_t>(size)};
        }
        void Write(const Handle& file, const std::string& value) const {
            std::size_t offset = 0;
            while (offset < value.size()) {
                const auto count = write(file.value, value.data() + offset, value.size() - offset);
                if (count < 0 && errno == EINTR)
                    continue;
                RequirePrivateFile(count > 0);
                offset += static_cast<std::size_t>(count);
            }
            RequirePrivateFile(fsync(file.value) == 0);
        }
    };
#endif
}
