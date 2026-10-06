#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
namespace libcompr {
    using Cancel = std::function<bool()>;
    using Chunk = std::function<bool(std::span<const std::uint8_t>)>;
    // Caller supplies memory, cancellation and optional digest/progress observer.
    inline bool CopyExactly(std::istream& source, std::ostream& destination,
                            std::uint64_t size, std::span<std::uint8_t> buffer,
                            const Chunk& observer = {}, const Cancel& canceled = {}) {
        if (buffer.empty() || buffer.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
            return false;
        while (size) {
            if (canceled && canceled())
                return false;
            const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(buffer.size(), size));
            source.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count));
            if (source.gcount() != static_cast<std::streamsize>(count))
                return false;
            if (observer && !observer(buffer.first(count)))
                return false;
            destination.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(count));
            if (!destination)
                return false;
            size -= count;
        }
        return true;
    }
    inline bool ReadChunks(std::istream& source, std::span<std::uint8_t> buffer,
                           const Chunk& observer, const Cancel& canceled = {}) {
        if (!observer || buffer.empty() || buffer.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)()))
            return false;
        while (source) {
            if (canceled && canceled())
                return false;
            source.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
            const auto count = source.gcount();
            if (count > 0 && !observer(buffer.first(static_cast<std::size_t>(count))))
                return false;
        }
        return source.eof() && !source.bad();
    }
    enum class WalkStatus
    {
        Ok,
        InvalidSource,
        Canceled,
        Rejected
    };
    // Reject links and special files, never skip inaccessible subtrees. Visitor owns
    // entry/byte limits, portable naming and ordering; traversal allocates no list.
    inline WalkStatus WalkTree(const std::filesystem::path& root,
                               const std::function<bool(const std::filesystem::directory_entry&, bool)>& visitor,
                               const Cancel& canceled = {}) {
        namespace fs = std::filesystem;
        std::error_code error;
        // is_directory(path) follows a symlink at the root, unlike the entry
        // checks below. Apply the same no-link contract to the source itself.
        // Trailing separators and '/.' also force a symlink to be followed by
        // the OS; remove only those suffixes, without resolving '..' or links.
        auto source = root;
        while (source.has_parent_path() && source != source.root_path() &&
               (source.filename().empty() || source.filename() == "."))
            source = source.parent_path();
        const auto root_status = fs::symlink_status(source, error);
        if (!visitor || error || !fs::is_directory(root_status))
            return WalkStatus::InvalidSource;
        fs::recursive_directory_iterator iterator(source, fs::directory_options::none, error), end;
        if (error)
            return WalkStatus::InvalidSource;
        while (iterator != end) {
            if (canceled && canceled())
                return WalkStatus::Canceled;
            const auto status = iterator->symlink_status(error);
            if (error || fs::is_symlink(status) || (!fs::is_directory(status) && !fs::is_regular_file(status)))
                return WalkStatus::InvalidSource;
            if (!visitor(*iterator, fs::is_directory(status)))
                return WalkStatus::Rejected;
            iterator.increment(error);
            if (error)
                return WalkStatus::InvalidSource;
        }
        return WalkStatus::Ok;
    }
}
