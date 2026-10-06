#include <libsys/native_file.h>
#include "runtime_file_sink.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <limits>
#include <spdlog/details/null_mutex.h>
#include <spdlog/sinks/base_sink.h>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace sovkit::log::runtime_detail {

std::uint64_t ProcessId() noexcept {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return static_cast<std::uint64_t>(getpid());
#endif
}

std::string UtcTimestamp(bool filename) {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::floor<std::chrono::seconds>(now);
  const auto raw = std::chrono::system_clock::to_time_t(seconds);
  std::tm utc{};
#if defined(_WIN32)
  if (gmtime_s(&utc, &raw) != 0)
#else
  if (!gmtime_r(&raw, &utc))
#endif
    throw FileFailure{"clock_unavailable"};
  char buffer[40]{};
  if (filename) {
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc);
    return buffer;
  }
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &utc);
  char suffix[12]{};
  std::snprintf(
      suffix, sizeof(suffix), ".%03dZ",
      static_cast<int>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds)
              .count()));
  return std::string(buffer) + suffix;
}

#if !defined(_WIN32)
namespace {
    using Fd = libsys::NativeFile;

    bool PrivateFile(const struct stat& s) {
        return S_ISREG(s.st_mode) && s.st_nlink == 1 && s.st_uid == geteuid() &&
               (s.st_mode & 0777) == 0600;
    }
bool SameFile(int root, const std::string &name, const struct stat &s) {
  struct stat current{};
  return fstatat(root, name.c_str(), &current, AT_SYMLINK_NOFOLLOW) == 0 &&
         current.st_dev == s.st_dev && current.st_ino == s.st_ino &&
         PrivateFile(current);
}
int OpenRoot(const std::filesystem::path &path) {
#if defined(__APPLE__)
  // Keep the same descriptor-walk policy as libdb::PrivateFiles. App Sandbox
  // may permit ancestor searches but deny enumeration of '/' or '/private'.
  // O_SEARCH acquires an openat anchor without requesting directory reads;
  // the retention scanner opens '.' for reading only inside the final root.
  constexpr int directory_access = O_SEARCH;
#elif defined(O_PATH)
  // Linux/Android's descriptor-only equivalent also avoids demanding read
  // access to ancestors such as /data. Directory reads are confined to L.
  constexpr int directory_access = O_PATH | O_DIRECTORY;
#else
  constexpr int directory_access = O_RDONLY | O_DIRECTORY;
#endif
  Fd parent(open("/", directory_access | O_CLOEXEC));
  if (parent.get() < 0)
    throw FileFailure{"directory_unavailable"};
  for (const auto &part : path.relative_path()) {
    const auto name = part.string();
    if (name.empty() || name == "." || name == "..")
      throw FileFailure{"unsafe_directory"};
    int next = openat(parent.get(), name.c_str(),
                      directory_access | O_NOFOLLOW | O_CLOEXEC);
    if (next < 0 && errno == ENOENT) {
      if (mkdirat(parent.get(), name.c_str(), 0700) != 0 && errno != EEXIST)
        throw FileFailure{"directory_unavailable"};
      next = openat(parent.get(), name.c_str(),
                    directory_access | O_NOFOLLOW | O_CLOEXEC);
    }
    if (next < 0)
      throw FileFailure{"unsafe_directory"};
    parent.reset(next);
  }
  struct stat s{};
  if (fstat(parent.get(), &s) != 0 || !S_ISDIR(s.st_mode) ||
      s.st_uid != geteuid() || (s.st_mode & 0777) != 0700)
    throw FileFailure{"directory_permissions"};
  const int result = dup(parent.get());
  if (result < 0)
    throw FileFailure{"directory_unavailable"};
  fcntl(result, F_SETFD, FD_CLOEXEC);
  return result;
}

// Exact grammar deliberately excludes exports, legacy files and other roles.
bool ManagedName(std::string_view name, const std::string &role) {
  const std::string prefix = role + "-";
  if (!name.starts_with(prefix))
    return false;
  name.remove_prefix(prefix.size());
  if (name.size() < 16 || name[8] != 'T' || name[15] != 'Z')
    return false;
  for (std::size_t i = 0; i < 16; ++i)
    if (i != 8 && i != 15 && (name[i] < '0' || name[i] > '9'))
      return false;
  name.remove_prefix(16);
  if (name.empty() || name.front() != '-')
    return false;
  name.remove_prefix(1);
  const auto dash = name.find('-');
  if (dash == 0 || dash > 20 || dash == std::string_view::npos)
    return false;
  for (char c : name.substr(0, dash))
    if (c < '0' || c > '9')
      return false;
  name.remove_prefix(dash + 1);
  if (name.size() < 40)
    return false;
  for (char c : name.substr(0, 32))
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  name.remove_prefix(32);
  if (name.front() != '.')
    return false;
  name.remove_prefix(1);
  const auto dot = name.find('.');
  if (dot == 0 || dot > 20 || dot == std::string_view::npos)
    return false;
  for (char c : name.substr(0, dot))
    if (c < '0' || c > '9')
      return false;
  return name.substr(dot) == ".jsonl";
}

class RootLock {
public:
  explicit RootLock(int fd) : fd_(fd) {
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0)
      throw FileFailure{"retention_busy"};
  }
  ~RootLock() { flock(fd_, LOCK_UN); }

private:
  int fd_;
};

class PrivateRotatingSink final
    : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
public:
  PrivateRotatingSink(const RuntimeConfig &config, const std::string &session)
      : config_(config), root_(OpenRoot(config.directory)),
        prefix_(config.role + "-" + UtcTimestamp(true) + "-" +
                std::to_string(ProcessId()) + "-" + session) {
    lock_name_ = ".sovkit-log-" + config.role + ".lock";
    lock_.reset(openat(root_.get(), lock_name_.c_str(),
                       O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                       0600));
    struct stat s{};
    if (lock_.get() < 0 || fstat(lock_.get(), &s) != 0 || !PrivateFile(s) ||
        !SameFile(root_.get(), lock_name_, s))
      throw FileFailure{"unsafe_lock"};
    lock_identity_ = s;
    RootLock guard(lock_.get());
    EnforceBudget(0);
    NewSegment();
  }

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    const auto bytes = message.payload.size() + 1;
    if (bytes > config_.segment_bytes)
      throw FileFailure{"record_size"};
    if (!SameFile(root_.get(), lock_name_, lock_identity_))
      throw FileFailure{"unsafe_lock"};
    RootLock guard(lock_.get());
    // The directory descriptor anchors all operations even if an ancestor is
    // renamed. No path-based reopening, following links, or rename overwrite.
    struct stat root_status{};
    if (fstat(root_.get(), &root_status) != 0 || root_status.st_nlink == 0 ||
        root_status.st_uid != geteuid() || (root_status.st_mode & 0777) != 0700)
      throw FileFailure{"directory_permissions"};
    if (size_ + bytes > config_.segment_bytes) {
      file_.reset(); // Releases the active-file lock before pruning.
      EnforceBudget(bytes);
      NewSegment();
    } else {
      EnforceBudget(bytes);
    }
    struct stat current{};
    if (fstat(file_.get(), &current) != 0 || !PrivateFile(current) ||
        !SameFile(root_.get(), active_name_, current) ||
        static_cast<std::uint64_t>(current.st_size) != size_)
      throw FileFailure{"unsafe_file"};
    struct statvfs space{};
    if (fstatvfs(root_.get(), &space) != 0)
      throw FileFailure{"space_unavailable"};
    const auto unit = std::max<std::uint64_t>(space.f_frsize, 1);
    if (space.f_bavail < (bytes + 1024U * 1024U + unit - 1) / unit)
      throw FileFailure{"disk_full"};
    // A single bounded allocation; payload is already validated JSON. Direct
    // writes mean flush has no userspace file buffer and does not claim fsync.
    std::string line(message.payload.data(), message.payload.size());
    line.push_back('\n');
    std::size_t offset = 0;
    while (offset < line.size()) {
      const auto written =
          write(file_.get(), line.data() + offset, line.size() - offset);
      if (written < 0 && errno == EINTR)
        continue;
      if (written <= 0)
        throw FileFailure{errno == ENOSPC ? "disk_full" : "write_failed"};
      offset += static_cast<std::size_t>(written);
      size_ += static_cast<std::size_t>(written);
    }
  }
  void flush_() override {
    if (!SameFile(root_.get(), lock_name_, lock_identity_))
      throw FileFailure{"unsafe_lock"};
    RootLock guard(lock_.get());
    // Also enforce age while idle; the current segment's lock protects it.
    EnforceBudget(0);
  }

private:
  void NewSegment() {
    active_name_ = prefix_ + "." + std::to_string(segment_++) + ".jsonl";
    file_.reset(openat(root_.get(), active_name_.c_str(),
                       O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                       0600));
    struct stat s{};
    if (file_.get() < 0 || fstat(file_.get(), &s) != 0 || !PrivateFile(s) ||
        !SameFile(root_.get(), active_name_, s) ||
        flock(file_.get(), LOCK_EX | LOCK_NB) != 0)
      throw FileFailure{"open_failed"};
    size_ = 0;
  }

  void EnforceBudget(std::size_t incoming) {
    struct Candidate {
      std::string name;
      struct stat status;
    };
    std::vector<Candidate> candidates;
    // Open a fresh directory description; dup would share readdir's offset.
    int scan = openat(root_.get(), ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (scan < 0)
      throw FileFailure{"retention_failed"};
    DIR *raw = fdopendir(scan);
    if (!raw) {
      close(scan);
      throw FileFailure{"retention_failed"};
    }
    std::unique_ptr<DIR, int (*)(DIR *)> entries(raw, closedir);
    std::uint64_t total = 0;
    std::size_t inspected = 0;
    for (;;) {
      errno = 0;
      const auto *entry = readdir(entries.get());
      if (!entry) {
        if (errno != 0)
          throw FileFailure{"retention_failed"};
        break;
      }
      if (++inspected > 4096)
        throw FileFailure{"retention_scan_limit"};
      const std::string name(entry->d_name);
      if (!ManagedName(name, config_.role))
        continue;
      struct stat s{};
      if (fstatat(root_.get(), name.c_str(), &s, AT_SYMLINK_NOFOLLOW) != 0 ||
          !PrivateFile(s) || s.st_size < 0)
        throw FileFailure{"unsafe_managed_file"};
      if (static_cast<std::uint64_t>(s.st_size) >
          std::numeric_limits<std::uint64_t>::max() - total)
        throw FileFailure{"quota_exceeded"};
      total += static_cast<std::uint64_t>(s.st_size);
      candidates.push_back({name, s});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b) {
                if (a.status.st_mtime != b.status.st_mtime)
                  return a.status.st_mtime < b.status.st_mtime;
                return a.name < b.name;
              });
    const auto now = std::time(nullptr);
    for (const auto &candidate : candidates) {
      const bool expired =
          now > candidate.status.st_mtime &&
          std::difftime(now, candidate.status.st_mtime) > 7 * 24 * 60 * 60;
      if (!expired && total <= config_.total_file_bytes - incoming)
        continue;
      if (file_.get() >= 0 && candidate.name == active_name_)
        continue;
      Fd target(openat(root_.get(), candidate.name.c_str(),
                       O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
      struct stat s{};
      if (target.get() < 0 || fstat(target.get(), &s) != 0 || !PrivateFile(s) ||
          s.st_dev != candidate.status.st_dev ||
          s.st_ino != candidate.status.st_ino)
        throw FileFailure{"retention_failed"};
      if (flock(target.get(), LOCK_EX | LOCK_NB) != 0)
        continue; // Active session.
      if (!SameFile(root_.get(), candidate.name, s) ||
          unlinkat(root_.get(), candidate.name.c_str(), 0) != 0)
        throw FileFailure{"retention_failed"};
      total -= static_cast<std::uint64_t>(candidate.status.st_size);
    }
    if (total > config_.total_file_bytes - incoming)
      throw FileFailure{"quota_exceeded"};
  }

  RuntimeConfig config_;
  Fd root_, lock_, file_;
  struct stat lock_identity_{};
  std::string prefix_, lock_name_, active_name_;
  std::uint64_t segment_ = 0;
  std::size_t size_ = 0;
};
} // namespace
#endif

spdlog::sink_ptr MakeFileSink(const RuntimeConfig &config,
                              const std::string &session) {
#if defined(_WIN32)
  // Fail closed until a handle-relative backend with private ACL validation
  // is available. std::filesystem + spdlog fopen cannot enforce this contract.
  (void)config;
  (void)session;
  throw FileFailure{"secure_file_backend_unavailable"};
#else
  return std::make_shared<PrivateRotatingSink>(config, session);
#endif
}
} // namespace sovkit::log::runtime_detail
