#include <libcompr.h>

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <sstream>
#include <string_view>

namespace {
constexpr int kArchiveBlockSize = 10240;

thread_local std::string g_archive_last_error;

void SetArchiveLastError(const std::string &message) {
  g_archive_last_error = message;
}

bool DecodeUtf8One(std::string_view input, size_t &offset,
                   uint32_t &codepoint) {
  if (offset >= input.size())
    return false;

  const auto lead = static_cast<unsigned char>(input[offset]);
  size_t extra = 0;
  uint32_t minimum = 0;

  if (lead < 0x80) {
    codepoint = lead;
    ++offset;
    return true;
  }

  if ((lead & 0xE0) == 0xC0) {
    codepoint = lead & 0x1Fu;
    extra = 1;
    minimum = 0x80;
  } else if ((lead & 0xF0) == 0xE0) {
    codepoint = lead & 0x0Fu;
    extra = 2;
    minimum = 0x800;
  } else if ((lead & 0xF8) == 0xF0) {
    codepoint = lead & 0x07u;
    extra = 3;
    minimum = 0x10000;
  } else {
    return false;
  }

  if (offset + extra >= input.size())
    return false;

  for (size_t i = 1; i <= extra; ++i) {
    const auto trail = static_cast<unsigned char>(input[offset + i]);
    if ((trail & 0xC0) != 0x80)
      return false;
    codepoint = (codepoint << 6) | (trail & 0x3Fu);
  }

  if (codepoint < minimum || codepoint > 0x10FFFF ||
      (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
    return false;
  }

  offset += extra + 1;
  return true;
}

bool IsValidUtf8(std::string_view value) {
  size_t offset = 0;
  while (offset < value.size()) {
    uint32_t codepoint = 0;
    if (!DecodeUtf8One(value, offset, codepoint))
      return false;
  }
  return true;
}

std::string ToLowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) {
                   return static_cast<char>(std::tolower(ch));
                 });
  return value;
}

bool EndsWithAsciiNoCase(const std::string &value, const char *suffix) {
  const std::string lower = ToLowerAscii(value);
  const std::string needle = ToLowerAscii(suffix ? suffix : "");
  return lower.size() >= needle.size() &&
         lower.compare(lower.size() - needle.size(), needle.size(), needle) ==
             0;
}

bool StartsWithBytes(const std::string &value,
                     std::initializer_list<unsigned char> bytes) {
  if (value.size() < bytes.size())
    return false;
  size_t index = 0;
  for (const unsigned char expected : bytes) {
    if (static_cast<unsigned char>(value[index++]) != expected)
      return false;
  }
  return true;
}

bool IsWindowsDrivePath(const std::string &entry) {
  return entry.size() >= 2 &&
         ((entry[0] >= 'A' && entry[0] <= 'Z') ||
          (entry[0] >= 'a' && entry[0] <= 'z')) &&
         entry[1] == ':';
}

bool HasUnsafeWindowsAds(const std::string &entry) {
#if defined(_WIN32)
  const auto slash = entry.find_last_of("/\\");
  const auto colon = entry.find(':', slash == std::string::npos ? 0 : slash + 1);
  return colon != std::string::npos;
#else
  (void)entry;
  return false;
#endif
}

bool IsArchiveSeparator(char ch) { return ch == '/' || ch == '\\'; }

bool IsUnsafeArchiveEntryName(const std::string &entry) {
  if (entry.empty() || entry[0] == '/' || entry[0] == '\\')
    return true;
  if (IsWindowsDrivePath(entry) || HasUnsafeWindowsAds(entry))
    return true;

  size_t end_size = entry.size();
  while (end_size > 0 && IsArchiveSeparator(entry[end_size - 1]))
    --end_size;
  if (end_size == 0)
    return true;

  size_t begin = 0;
  while (begin < end_size) {
    const size_t end = entry.find_first_of("/\\", begin);
    const size_t part_end =
        end == std::string::npos || end > end_size ? end_size : end;
    const std::string_view part =
        std::string_view(entry).substr(begin, part_end - begin);
    if (part.empty() || part == "." || part == "..")
      return true;
    if (end == std::string::npos || end >= end_size)
      break;
    begin = end + 1;
  }
  return false;
}

bool StartsWithArchiveSegment(const std::string &entry,
                              const char *segment) {
  const std::string_view value(entry);
  const std::string_view needle(segment ? segment : "");
  if (value == needle)
    return true;
  return value.size() > needle.size() &&
         value.compare(0, needle.size(), needle) == 0 &&
         IsArchiveSeparator(value[needle.size()]);
}

bool IsMacMetadataEntry(const std::string &entry) {
  if (StartsWithArchiveSegment(entry, "__MACOSX"))
    return true;

  size_t begin = 0;
  size_t end_size = entry.size();
  while (end_size > 0 && IsArchiveSeparator(entry[end_size - 1]))
    --end_size;
  while (begin < end_size) {
    const size_t end = entry.find_first_of("/\\", begin);
    const size_t part_end =
        end == std::string::npos || end > end_size ? end_size : end;
    const std::string_view part =
        std::string_view(entry).substr(begin, part_end - begin);
    if (part == ".DS_Store" ||
        (part.size() >= 2 && part[0] == '.' && part[1] == '_')) {
      return true;
    }
    if (end == std::string::npos || end >= end_size)
      break;
    begin = end + 1;
  }
  return false;
}

std::string StripTopLevel(std::string entry) {
  while (!entry.empty() && (entry.front() == '/' || entry.front() == '\\'))
    entry.erase(entry.begin());
  const auto slash = entry.find_first_of("/\\");
  if (slash == std::string::npos)
    return {};
  entry.erase(0, slash + 1);
  return entry;
}

std::string ArchiveError(archive *value, const char *fallback) {
  const char *message = value ? archive_error_string(value) : nullptr;
  if (message && *message)
    return message;
  return fallback ? fallback : "archive operation failed";
}

struct ArchiveReadCloser {
  void operator()(archive *value) const {
    if (value)
      archive_read_free(value);
  }
};

struct ArchiveWriteCloser {
  void operator()(archive *value) const {
    if (value)
      archive_write_free(value);
  }
};

using ReadArchivePtr = std::unique_ptr<archive, ArchiveReadCloser>;
using WriteArchivePtr = std::unique_ptr<archive, ArchiveWriteCloser>;

int OpenArchiveFile(archive *reader, const stl::path &path,
                    std::string *error) {
#if defined(_WIN32)
  (void)error;
  return archive_read_open_filename_w(reader, libpath::ToWide(path).c_str(),
                                      kArchiveBlockSize);
#else
  bool native_ok = false;
  const std::string native = libpath::NativeString(path, &native_ok);
  if (!native_ok || (native.empty() && !path.empty())) {
    if (error)
      *error = "archive path is not valid for current platform";
    return ARCHIVE_FATAL;
  }
  return archive_read_open_filename(reader, native.c_str(), kArchiveBlockSize);
#endif
}

std::string EntryPathnameUtf8(archive_entry *entry, bool *ok) {
  if (ok)
    *ok = true;

  const char *utf8_path = archive_entry_pathname_utf8(entry);
  if (utf8_path) {
    std::string result(utf8_path);
    if (IsValidUtf8(result))
      return result;
  }

#if defined(_WIN32)
  if (const wchar_t *wide = archive_entry_pathname_w(entry)) {
    bool converted = false;
    std::string result = libpath::detail::WideToUtf8(wide, &converted);
    if (ok)
      *ok = converted;
    return converted ? result : std::string();
  }
#endif

  const char *raw_path = archive_entry_pathname(entry);
  if (!raw_path)
    return {};
  std::string result(raw_path);
  const bool valid = IsValidUtf8(result);
  if (ok)
    *ok = valid;
  return valid ? result : std::string();
}

bool EntryEscapesTarget(const stl::path &target_dir, const stl::path &out_path) {
  const stl::path relative = out_path.lexically_relative(target_dir);
  if (relative.empty() || relative.is_absolute())
    return true;
  for (const auto &part : relative) {
    if (part.generic_string() == "..")
      return true;
  }
  return false;
}

void SetFailure(Compress::ArchiveExtractResult &result,
                const std::string &error) {
  result.ok = false;
  result.state = "failed";
  result.error = error;
  SetArchiveLastError(error);
}

bool ReportProgress(const Compress::tfArchiveExtractProgressCb &progress_cb,
                    Compress::ArchiveExtractResult &result) {
  if (!progress_cb)
    return true;
  Compress::ArchiveExtractResult snapshot = result;
  snapshot.progress = true;
  return progress_cb(snapshot);
}

void UpdateArchiveProgress(archive *reader,
                           Compress::ArchiveExtractResult &result) {
  const la_int64_t consumed = archive_filter_bytes(reader, -1);
  if (consumed >= 0)
    result.archive_bytes = static_cast<uint64_t>(consumed);
}

bool WouldExceed(uint64_t current, uint64_t added, uint64_t limit) {
  return limit != 0 && (current > limit || added > limit - current);
}

bool ExceedsCompressionRatio(const Compress::ArchiveExtractOptions &options,
                             const Compress::ArchiveExtractResult &result) {
  if (options.max_compression_ratio == 0 || result.archive_bytes == 0)
    return false;
  return result.bytes / result.archive_bytes > options.max_compression_ratio;
}

} // namespace

bool Compress::archiveExtract(const stl::path &archivePath,
                              const stl::path &targetDir,
                              const ArchiveExtractOptions &options,
                              const tfArchiveExtractProgressCb &progress_cb,
                              ArchiveExtractResult *result_out) {
  ArchiveExtractResult result;
  result.archive_path = archivePath;
  result.target_dir = targetDir;
  result.state = "running";
  result.archive_total_bytes =
      static_cast<uint64_t>(libpath::FileSize(archivePath));

  auto finish = [&](bool ok) {
    if (result_out)
      *result_out = result;
    if (!ok && result.error.empty())
      SetArchiveLastError("archive extraction failed");
    else if (ok)
      SetArchiveLastError({});
    return ok;
  };

  if (archivePath.empty()) {
    SetFailure(result, "archive path is empty");
    return finish(false);
  }
  if (targetDir.empty()) {
    SetFailure(result, "archive target directory is empty");
    return finish(false);
  }

  const stl::path target_dir = targetDir.lexically_normal();
  result.target_dir = target_dir;

  std::error_code ec;
  libpath::CreateDirectories(target_dir, &ec);
  if (ec) {
    SetFailure(result, "create target directory failed: " + ec.message());
    return finish(false);
  }

  ReadArchivePtr reader(archive_read_new());
  if (!reader) {
    SetFailure(result, "archive_read_new failed");
    return finish(false);
  }
  archive_read_support_filter_all(reader.get());
  archive_read_support_format_all(reader.get());

  std::string open_error;
  if (OpenArchiveFile(reader.get(), archivePath, &open_error) != ARCHIVE_OK) {
    SetFailure(result, open_error.empty()
                           ? ArchiveError(reader.get(), "failed to open archive")
                           : open_error);
    return finish(false);
  }

  WriteArchivePtr writer(archive_write_disk_new());
  if (!writer) {
    SetFailure(result, "archive_write_disk_new failed");
    return finish(false);
  }

  int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_SECURE_NODOTDOT;
  if (options.secure_symlinks)
    flags |= ARCHIVE_EXTRACT_SECURE_SYMLINKS;
  if (options.overwrite)
    flags |= ARCHIVE_EXTRACT_UNLINK;
  if (options.preserve_permissions)
    flags |= ARCHIVE_EXTRACT_PERM;
  archive_write_disk_set_options(writer.get(), flags);
  archive_write_disk_set_standard_lookup(writer.get());

  uint64_t last_report_bytes = 0;
  if (!ReportProgress(progress_cb, result)) {
    SetFailure(result, "archive extraction cancelled by progress callback");
    return finish(false);
  }

  archive_entry *entry = nullptr;
  for (;;) {
    const int header_rc = archive_read_next_header(reader.get(), &entry);
    if (header_rc == ARCHIVE_EOF)
      break;
    if (header_rc != ARCHIVE_OK) {
      SetFailure(result,
                 ArchiveError(reader.get(), "failed to read archive entry"));
      return finish(false);
    }

    ++result.entries;
    if (options.max_entries != 0 && result.entries > options.max_entries) {
      SetFailure(result, "archive exceeds maximum entry count");
      return finish(false);
    }

    bool entry_ok = false;
    std::string entry_name = EntryPathnameUtf8(entry, &entry_ok);
    if (!entry_ok) {
      SetFailure(result, "archive entry path is not valid UTF-8");
      return finish(false);
    }
    if (options.strip_top_level_dir)
      entry_name = StripTopLevel(std::move(entry_name));
    result.current_entry = entry_name;

    if (entry_name.empty()) {
      ++result.skipped;
      archive_read_data_skip(reader.get());
      continue;
    }
    if (IsUnsafeArchiveEntryName(entry_name)) {
      SetFailure(result, "archive contains unsafe entry: " + entry_name);
      return finish(false);
    }
    if (options.skip_macos_metadata && IsMacMetadataEntry(entry_name)) {
      ++result.skipped;
      archive_read_data_skip(reader.get());
      UpdateArchiveProgress(reader.get(), result);
      if (!ReportProgress(progress_cb, result)) {
        SetFailure(result, "archive extraction cancelled by progress callback");
        return finish(false);
      }
      continue;
    }

    bool entry_path_ok = false;
    const stl::path entry_path =
        libpath::FromUtf8(entry_name, &entry_path_ok);
    if (!entry_path_ok) {
      SetFailure(result,
                 "archive entry path is not valid UTF-8: " + entry_name);
      return finish(false);
    }

    const stl::path out_path = (target_dir / entry_path).lexically_normal();
    if (EntryEscapesTarget(target_dir, out_path)) {
      SetFailure(result,
                 "archive entry escapes target directory: " + entry_name);
      return finish(false);
    }

    const int filetype = archive_entry_filetype(entry);
    if (filetype == AE_IFLNK || filetype == AE_IFSOCK ||
        filetype == AE_IFCHR || filetype == AE_IFBLK || filetype == AE_IFIFO) {
      if (!options.allow_symlinks || filetype != AE_IFLNK) {
        ++result.skipped;
        archive_read_data_skip(reader.get());
        continue;
      }
    }

    const la_int64_t declared_size = archive_entry_size(entry);
    if (declared_size > 0) {
      const uint64_t size = static_cast<uint64_t>(declared_size);
      if (WouldExceed(0, size, options.max_file_bytes)) {
        SetFailure(result, "archive entry exceeds maximum file size: " +
                               entry_name);
        return finish(false);
      }
      if (filetype != AE_IFDIR &&
          WouldExceed(result.bytes, size, options.max_total_bytes)) {
        SetFailure(result, "archive exceeds maximum extracted size");
        return finish(false);
      }
    }

#if defined(_WIN32)
    archive_entry_copy_pathname_w(entry, libpath::ToWide(out_path).c_str());
#else
    const std::string out_path_utf8 = libpath::ToUtf8(out_path);
    archive_entry_set_pathname(entry, out_path_utf8.c_str());
#endif

    const int write_header_rc = archive_write_header(writer.get(), entry);
    if (write_header_rc != ARCHIVE_OK) {
      SetFailure(result,
                 ArchiveError(writer.get(), "failed to create archive entry"));
      return finish(false);
    }

    if (filetype == AE_IFDIR) {
      ++result.directories;
    } else {
      const void *buffer = nullptr;
      size_t size = 0;
      la_int64_t offset = 0;
      result.current_file_bytes = 0;
      for (;;) {
        const int data_rc =
            archive_read_data_block(reader.get(), &buffer, &size, &offset);
        if (data_rc == ARCHIVE_EOF)
          break;
        if (data_rc != ARCHIVE_OK) {
          SetFailure(result,
                     ArchiveError(reader.get(), "failed to read entry data"));
          return finish(false);
        }
        const uint64_t block_size = static_cast<uint64_t>(size);
        if (WouldExceed(result.current_file_bytes, block_size,
                        options.max_file_bytes)) {
          SetFailure(result, "archive entry exceeds maximum file size: " +
                                 entry_name);
          return finish(false);
        }
        if (WouldExceed(result.bytes, block_size,
                        options.max_total_bytes)) {
          SetFailure(result, "archive exceeds maximum extracted size");
          return finish(false);
        }
        const la_ssize_t write_rc =
            archive_write_data_block(writer.get(), buffer, size, offset);
        if (write_rc < ARCHIVE_OK) {
          SetFailure(result,
                     ArchiveError(writer.get(), "failed to write entry data"));
          return finish(false);
        }
        result.bytes += static_cast<uint64_t>(size);
        result.current_file_bytes += static_cast<uint64_t>(size);
        UpdateArchiveProgress(reader.get(), result);
        if (ExceedsCompressionRatio(options, result)) {
          SetFailure(result, "archive exceeds maximum compression ratio");
          return finish(false);
        }

        const uint64_t interval =
            options.progress_interval_bytes == 0
                ? ArchiveExtractOptions{}.progress_interval_bytes
                : options.progress_interval_bytes;
        if (result.bytes >= last_report_bytes + interval) {
          last_report_bytes = result.bytes;
          if (!ReportProgress(progress_cb, result)) {
            SetFailure(result,
                       "archive extraction cancelled by progress callback");
            return finish(false);
          }
        }
      }
      ++result.files;
    }

    const int close_rc = archive_write_finish_entry(writer.get());
    if (close_rc != ARCHIVE_OK) {
      SetFailure(result,
                 ArchiveError(writer.get(), "failed to finalize archive entry"));
      return finish(false);
    }
    UpdateArchiveProgress(reader.get(), result);
    if (!ReportProgress(progress_cb, result)) {
      SetFailure(result, "archive extraction cancelled by progress callback");
      return finish(false);
    }
  }

  result.ok = true;
  result.progress = false;
  result.state = "succeeded";
  result.current_entry.clear();
  result.current_file_bytes = 0;
  if (result.archive_total_bytes > 0)
    result.archive_bytes = result.archive_total_bytes;
  return finish(true);
}

bool Compress::archiveLooksLike(const std::string &path) {
  return EndsWithAsciiNoCase(path, ".zip") || EndsWithAsciiNoCase(path, ".7z") ||
         EndsWithAsciiNoCase(path, ".tar") ||
         EndsWithAsciiNoCase(path, ".tar.gz") ||
         EndsWithAsciiNoCase(path, ".tgz") ||
         EndsWithAsciiNoCase(path, ".tar.xz") ||
         EndsWithAsciiNoCase(path, ".txz") ||
         EndsWithAsciiNoCase(path, ".tar.bz2") ||
         EndsWithAsciiNoCase(path, ".tbz2") ||
         EndsWithAsciiNoCase(path, ".gz") || EndsWithAsciiNoCase(path, ".xz") ||
         EndsWithAsciiNoCase(path, ".bz2") ||
         EndsWithAsciiNoCase(path, ".rar");
}

bool Compress::archiveLooksLikeHeader(const std::string &head) {
  if (StartsWithBytes(head, {'P', 'K', 0x03, 0x04}) ||
      StartsWithBytes(head, {'P', 'K', 0x05, 0x06}) ||
      StartsWithBytes(head, {'P', 'K', 0x07, 0x08}) ||
      StartsWithBytes(head, {0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C}) ||
      StartsWithBytes(head, {0x1F, 0x8B}) ||
      StartsWithBytes(head, {0xFD, '7', 'z', 'X', 'Z', 0x00}) ||
      StartsWithBytes(head, {'B', 'Z', 'h'}) ||
      StartsWithBytes(head, {'R', 'a', 'r', '!', 0x1A, 0x07}))
    return true;
  return head.size() > 262 && head.compare(257, 5, "ustar") == 0;
}

std::string Compress::archiveLastError() { return g_archive_last_error; }

std::string Compress::archiveVersion() { return archive_version_string(); }
