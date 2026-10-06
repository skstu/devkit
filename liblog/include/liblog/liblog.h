#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sovkit::log {

enum class Level : std::uint8_t {
  Trace = 0,
  Debug,
  Info,
  Warn,
  Error,
  Critical,
  Off,
};

const char *LevelName(Level level) noexcept;

struct SourceLocation {
  const char *file = nullptr;
  const char *function = nullptr;
  std::uint32_t line = 0;
};

struct Field {
  std::string key;
  std::string value;
};

using Fields = std::vector<Field>;

struct Event {
  Level level = Level::Info;
  std::string category = "SOVKIT";
  std::string name = "message";
  std::string message;
  int code = 0;
  std::string code_name;
  std::string code_message;
  std::uint64_t process_id = 0;
  std::string operation_id;
  std::string env_id;
  Fields fields;
  SourceLocation source;
  std::string callback_text;
  bool emit_callback = true;
};

struct Config {
  std::string logger_name = "sovkit";
  Level minimum_level = Level::Info;
  bool console_enabled = true;
  bool file_enabled = true;
  std::filesystem::path log_directory = "logs";
  std::string file_name = "sovkit.jsonl";
  bool async_enabled = true;
  std::size_t async_queue_size = 8192;
  std::size_t rotate_size_bytes = 10U * 1024U * 1024U;
  std::size_t rotate_file_count = 5;
  std::uint32_t retention_days = 0;
  std::string retention_file_prefix;
  bool flush_on_error = true;
  std::chrono::milliseconds shutdown_timeout{3000};
  std::size_t max_message_bytes = 64U * 1024U;
  std::size_t max_field_count = 64;
  std::size_t max_field_value_bytes = 8U * 1024U;
  bool redact_sensitive_fields = true;
  std::vector<std::string> sensitive_key_fragments = {
      "authorization", "cookie", "password", "passwd", "proxy",
      "secret",        "token",  "api_key",  "apikey"};
};

struct CallbackRecord {
  std::uint64_t timestamp_ms = 0;
  Level level = Level::Info;
  std::string json_line;
};

struct PruneResult {
  std::size_t scanned = 0;
  std::size_t removed = 0;
  std::size_t failed = 0;
};

using Callback = std::function<void(const CallbackRecord &record)>;

class BoundLogger;

class Logger final : public std::enable_shared_from_this<Logger> {
public:
  static std::shared_ptr<Logger> Create(Config config,
                                        std::string *error = nullptr) noexcept;
  static std::shared_ptr<Logger> Create(Config config, Callback callback,
                                        std::string *error = nullptr) noexcept;

  ~Logger();

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
  Logger(Logger &&) = delete;
  Logger &operator=(Logger &&) = delete;

  bool Write(Event event) noexcept;
  bool Log(Level level, std::string_view category, std::string_view event_name,
           std::string_view message, Fields fields = {},
           SourceLocation source = {}) noexcept;

  bool Trace(std::string_view category, std::string_view event_name,
             std::string_view message, Fields fields = {},
             SourceLocation source = {}) noexcept;
  bool Debug(std::string_view category, std::string_view event_name,
             std::string_view message, Fields fields = {},
             SourceLocation source = {}) noexcept;
  bool Info(std::string_view category, std::string_view event_name,
            std::string_view message, Fields fields = {},
            SourceLocation source = {}) noexcept;
  bool Warn(std::string_view category, std::string_view event_name,
            std::string_view message, Fields fields = {},
            SourceLocation source = {}) noexcept;
  bool Error(std::string_view category, std::string_view event_name,
             std::string_view message, Fields fields = {},
             SourceLocation source = {}) noexcept;
  bool Critical(std::string_view category, std::string_view event_name,
                std::string_view message, Fields fields = {},
                SourceLocation source = {}) noexcept;

  BoundLogger Bind(std::string category, Fields context = {});

  bool SetCallback(Callback callback) noexcept;
  // Product mode keeps native callback delivery but suppresses legacy text
  // sinks, including records already waiting in the legacy async queue.
  void SetOutputsEnabled(bool enabled) noexcept;
  void SetLevel(Level level) noexcept;
  Level GetLevel() const noexcept;
  bool IsOpen() const noexcept;
  bool Flush(std::chrono::milliseconds timeout =
                 std::chrono::milliseconds{3000}) noexcept;
  bool Shutdown(std::chrono::milliseconds timeout =
                    std::chrono::milliseconds{3000}) noexcept;

  static bool WriteEmergency(const std::filesystem::path &path,
                             std::string_view text,
                             std::string *error = nullptr) noexcept;
  static PruneResult PruneExpiredFiles(
      const std::filesystem::path &directory, std::string_view file_name,
      std::uint32_t retention_days,
      std::string_view managed_prefix = {}) noexcept;

private:
  struct Impl;
  explicit Logger(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

class BoundLogger final {
public:
  BoundLogger() = default;

  bool Log(Level level, std::string_view event_name, std::string_view message,
           Fields fields = {}, SourceLocation source = {}) const noexcept;
  bool Trace(std::string_view event_name, std::string_view message,
             Fields fields = {}, SourceLocation source = {}) const noexcept;
  bool Debug(std::string_view event_name, std::string_view message,
             Fields fields = {}, SourceLocation source = {}) const noexcept;
  bool Info(std::string_view event_name, std::string_view message,
            Fields fields = {}, SourceLocation source = {}) const noexcept;
  bool Warn(std::string_view event_name, std::string_view message,
            Fields fields = {}, SourceLocation source = {}) const noexcept;
  bool Error(std::string_view event_name, std::string_view message,
             Fields fields = {}, SourceLocation source = {}) const noexcept;
  bool Critical(std::string_view event_name, std::string_view message,
                Fields fields = {}, SourceLocation source = {}) const noexcept;

private:
  friend class Logger;
  BoundLogger(std::shared_ptr<Logger> logger, std::string category,
              Fields context);

  std::shared_ptr<Logger> logger_;
  std::string category_;
  Fields context_;
};

} // namespace sovkit::log

#define SOVKIT_LOG_SOURCE_LOCATION                                            \
  ::sovkit::log::SourceLocation {                                             \
    __FILE__, __func__, static_cast<std::uint32_t>(__LINE__)                  \
  }
