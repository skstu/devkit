#include <libsys/file_io.h>
#include <fcntl.h>
namespace libsys {
    namespace fs = std::filesystem;
    bool SyncReceivedFile(const fs::path& path) {
        uv_fs_t request{};
        const auto name = path.u8string();
        const int descriptor = uv_fs_open(
            nullptr, &request, reinterpret_cast<const char*>(name.c_str()), O_RDWR,
            0, nullptr);
        uv_fs_req_cleanup(&request);
        if (descriptor < 0)
            return false;
        const int synced = uv_fs_fsync(nullptr, &request, descriptor, nullptr);
        uv_fs_req_cleanup(&request);
        const int closed = uv_fs_close(nullptr, &request, descriptor, nullptr);
        uv_fs_req_cleanup(&request);
        return synced == 0 && closed == 0;
    }
    bool SyncPublicationDirectory([[maybe_unused]] const fs::path& path) {
#if defined(_WIN32)
        // MoveFileExW uses MOVEFILE_WRITE_THROUGH. libuv cannot open directory handles
        // for fsync on Windows; file contents are flushed separately before intent.
        return true;
#else
        uv_fs_t request{};
        const auto name = path.u8string();
        const int fd = uv_fs_open(nullptr, &request, reinterpret_cast<const char*>(name.c_str()), O_RDONLY, 0, nullptr);
        uv_fs_req_cleanup(&request);
        if (fd < 0)
            return false;
        const int synced = uv_fs_fsync(nullptr, &request, fd, nullptr);
        uv_fs_req_cleanup(&request);
        const int closed = uv_fs_close(nullptr, &request, fd, nullptr);
        uv_fs_req_cleanup(&request);
        return synced == 0 && closed == 0;
#endif
    }
    SourceIdentity SourceStamp(const uv_stat_t& value) {
        return {value.st_dev, value.st_ino, value.st_size,
                static_cast<std::uint64_t>(value.st_mtim.tv_sec), static_cast<std::uint64_t>(value.st_mtim.tv_nsec),
                static_cast<std::uint64_t>(value.st_ctim.tv_sec), static_cast<std::uint64_t>(value.st_ctim.tv_nsec)};
    }
    bool SourceStamp(const fs::path& path, SourceIdentity& output, bool follow) {
        uv_fs_t request{};
        const auto name = path.u8string();
        const int status = follow ? uv_fs_stat(nullptr, &request, reinterpret_cast<const char*>(name.c_str()), nullptr)
                                  : uv_fs_lstat(nullptr, &request, reinterpret_cast<const char*>(name.c_str()), nullptr);
        if (status == 0)
            output = SourceStamp(request.statbuf);
        uv_fs_req_cleanup(&request);
        return status == 0;
    }
}
