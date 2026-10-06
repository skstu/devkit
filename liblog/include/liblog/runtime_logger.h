#pragma once

#include "liblog/liblog.h"

namespace sovkit::log {

// Product logging is opt-in. All JSON parsing, privacy policy, queueing and
// spdlog sinks live in liblog; the SDK only owns the logger and ABI buffers.
struct RuntimeConfig {
  std::filesystem::path directory;
  std::string role = "app";
  bool console = false;
  bool file_enabled = true;
  Level minimum_level = Level::Info;
  Level file_level = Level::Info;
  std::size_t ring_entries = 2000;
  std::size_t ring_bytes = 2U * 1024U * 1024U;
  std::size_t queue_entries = 1024;
  std::size_t queue_bytes = 4U * 1024U * 1024U;
  std::size_t reserved_entries = 128;
  std::size_t reserved_bytes = 512U * 1024U;
  std::size_t segment_bytes = 10U * 1024U * 1024U;
  std::size_t total_file_bytes = 50U * 1024U * 1024U;
  std::chrono::milliseconds flush_interval{2000};
  std::chrono::milliseconds debug_lifetime{30 * 60 * 1000};
};

enum class RuntimeResult { Ok, InvalidArgument, Closed, Dropped, Internal };

class RuntimeLogger final {
public:
  static bool ParseConfig(std::string_view json, RuntimeConfig &out) noexcept;
  static std::shared_ptr<RuntimeLogger> Create(RuntimeConfig config) noexcept;
  ~RuntimeLogger();
  RuntimeLogger(const RuntimeLogger &) = delete;
  RuntimeLogger &operator=(const RuntimeLogger &) = delete;

  // Strict versioned event contract; host message/source/operation/ID strings
  // are rejected. Returned seq is decimal and belongs only to this session.
  RuntimeResult Emit(std::string_view json, std::string &result) noexcept;
  // Legacy adaptation never reads/copies message, source, or arbitrary fields.
  bool WriteSafe(Level level, std::string_view category, std::string_view event,
                 int code, const Fields &fields) noexcept;
  RuntimeResult Read(std::string_view query,
                     std::string &result) const noexcept;
  std::string Stats() const;
  const std::string &Session() const noexcept;
  bool IsOpen() const noexcept;
  bool IsClosed() const noexcept;
  // Only levels/console may change during a session. DEBUG/TRACE expire.
  bool Reconfigure(const RuntimeConfig &config) noexcept;
  bool Flush(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                 3000}) noexcept;
  // A false result can mean closed with file errors OR still draining. Check
  // IsClosed()/stats. Retain ownership and retry before unloading the SDK.
  bool Shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds{
                    3000}) noexcept;

#if defined(SOVKIT_RUNTIME_LOG_TESTING)
  // Compiled only into standalone tests; executed on the worker, outside all
  // producer locks. Allows deterministic IO stalls and failure injection.
  void SetWorkerHook(std::function<void()> hook);
#endif

private:
  struct Impl;
  explicit RuntimeLogger(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

} // namespace sovkit::log
