#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <uv.h>
namespace libsys {
    using SourceIdentity = std::array<std::uint64_t, 7>;
    bool SyncReceivedFile(const std::filesystem::path& path);
    bool SyncPublicationDirectory(const std::filesystem::path& path);
    SourceIdentity SourceStamp(const uv_stat_t& stat);
    bool SourceStamp(const std::filesystem::path& path, SourceIdentity& output, bool follow = true);
}
