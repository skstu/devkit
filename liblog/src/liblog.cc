#include "liblog/liblog.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <deque>
#include <sstream>
#include <thread>
#include <utility>

#include <spdlog/async.h>
#include <spdlog/details/thread_pool.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace sovkit::log {
namespace {

std::atomic<std::uint64_t> g_logger_sequence{0};
std::atomic<std::uint64_t> g_record_sequence{0};

spdlog::level::level_enum ToSpdLevel(const Level level) noexcept {
  switch (level) {
  case Level::Trace:
    return spdlog::level::trace;
  case Level::Debug:
    return spdlog::level::debug;
  case Level::Info:
    return spdlog::level::info;
  case Level::Warn:
    return spdlog::level::warn;
  case Level::Error:
    return spdlog::level::err;
  case Level::Critical:
    return spdlog::level::critical;
  case Level::Off:
    return spdlog::level::off;
  }
  return spdlog::level::info;
}

Level FromSpdLevel(const spdlog::level::level_enum level) noexcept {
  switch (level) {
  case spdlog::level::trace:
    return Level::Trace;
  case spdlog::level::debug:
    return Level::Debug;
  case spdlog::level::info:
    return Level::Info;
  case spdlog::level::warn:
    return Level::Warn;
  case spdlog::level::err:
    return Level::Error;
  case spdlog::level::critical:
    return Level::Critical;
  case spdlog::level::off:
  case spdlog::level::n_levels:
    return Level::Off;
  }
  return Level::Info;
}

std::uint64_t TimestampMs(
    const std::chrono::system_clock::time_point time) noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          time.time_since_epoch())
          .count());
}

bool LocalTime(const std::time_t value, std::tm &result) noexcept {
  result = {};
#if defined(_WIN32)
  return localtime_s(&result, &value) == 0;
#else
  return localtime_r(&value, &result) != nullptr;
#endif
}

bool UtcTime(const std::time_t value, std::tm &result) noexcept {
  result = {};
#if defined(_WIN32)
  return gmtime_s(&result, &value) == 0;
#else
  return gmtime_r(&value, &result) != nullptr;
#endif
}

std::int64_t CalendarAsUtc(std::tm value) noexcept {
#if defined(_WIN32)
  return static_cast<std::int64_t>(_mkgmtime64(&value));
#else
  return static_cast<std::int64_t>(timegm(&value));
#endif
}

std::string FormatLocalTime(
    const std::chrono::system_clock::time_point time) {
  const auto whole_seconds = std::chrono::floor<std::chrono::seconds>(time);
  const std::time_t raw_time =
      std::chrono::system_clock::to_time_t(whole_seconds);
  std::tm local_time{};
  std::tm utc_time{};
  if (!LocalTime(raw_time, local_time) || !UtcTime(raw_time, utc_time))
    return "time-unavailable";
  const std::int64_t local_calendar = CalendarAsUtc(local_time);
  const std::int64_t utc_calendar = CalendarAsUtc(utc_time);
  const std::int64_t offset_seconds =
      local_calendar == -1 || utc_calendar == -1
          ? 0
          : local_calendar - utc_calendar;
  const std::int64_t offset_minutes = offset_seconds / 60;
  const std::int64_t absolute_offset =
      offset_minutes < 0 ? -offset_minutes : offset_minutes;
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(time -
                                                            whole_seconds);

  std::ostringstream output;
  output << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S") << '.'
         << std::setw(3) << std::setfill('0') << milliseconds.count()
         << (offset_minutes < 0 ? '-' : '+') << std::setw(2)
         << std::setfill('0') << absolute_offset / 60 << ':' << std::setw(2)
         << std::setfill('0') << absolute_offset % 60;
  return output.str();
}

const char *DisplayLevelName(const Level level) noexcept {
  switch (level) {
  case Level::Trace:
    return "TRACE";
  case Level::Debug:
    return "DEBUG";
  case Level::Info:
    return "INFO";
  case Level::Warn:
    return "WARN";
  case Level::Error:
    return "ERROR";
  case Level::Critical:
    return "FATAL";
  case Level::Off:
    return "OFF";
  }
  return "UNKNOWN";
}

spdlog::filename_t ToSpdlogFilename(const std::filesystem::path &path) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
  return path.wstring();
#else
  return path.string();
#endif
}

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const char c) {
    if (c >= 'A' && c <= 'Z')
      return static_cast<char>(c - 'A' + 'a');
    return c;
  });
  return value;
}

bool IsSensitiveKey(const std::string &key, const Config &config) {
  if (!config.redact_sensitive_fields)
    return false;
  const std::string lower_key = LowerAscii(key);
  return std::any_of(
      config.sensitive_key_fragments.begin(),
      config.sensitive_key_fragments.end(), [&](const std::string &fragment) {
        return !fragment.empty() &&
               lower_key.find(LowerAscii(fragment)) != std::string::npos;
      });
}

std::string SanitizeUtf8(std::string_view value);

std::string FormatCallbackText(
    const std::chrono::system_clock::time_point time,
    const std::string_view text) {
  const std::string safe_text = SanitizeUtf8(text);
  const auto whole_seconds = std::chrono::floor<std::chrono::seconds>(time);
  const std::time_t raw_time =
      std::chrono::system_clock::to_time_t(whole_seconds);
  std::tm local_time{};
  if (!LocalTime(raw_time, local_time))
    return safe_text + '\n';
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(time -
                                                            whole_seconds);
  std::ostringstream output;
  output << std::put_time(&local_time, "%Y/%m/%d %H:%M:%S") << '/'
         << std::setw(3) << std::setfill('0') << milliseconds.count() << '\t'
         << safe_text << '\n';
  return output.str();
}

std::size_t Utf8SequenceLength(const std::string_view value,
                               const std::size_t index) noexcept {
  if (index >= value.size())
    return 0U;
  const auto first = static_cast<unsigned char>(value[index]);
  if (first <= 0x7fU)
    return 1U;

  std::size_t width = 0U;
  if (first >= 0xc2U && first <= 0xdfU)
    width = 2U;
  else if (first >= 0xe0U && first <= 0xefU)
    width = 3U;
  else if (first >= 0xf0U && first <= 0xf4U)
    width = 4U;
  else
    return 0U;
  if (width > value.size() - index)
    return 0U;
  for (std::size_t offset = 1U; offset < width; ++offset) {
    const auto byte = static_cast<unsigned char>(value[index + offset]);
    if ((byte & 0xc0U) != 0x80U)
      return 0U;
  }
  const auto second = static_cast<unsigned char>(value[index + 1U]);
  if ((first == 0xe0U && second < 0xa0U) ||
      (first == 0xedU && second >= 0xa0U) ||
      (first == 0xf0U && second < 0x90U) ||
      (first == 0xf4U && second >= 0x90U)) {
    return 0U;
  }
  return width;
}

std::size_t Utf8SafePrefixLength(const std::string_view value,
                                 const std::size_t limit) noexcept {
  std::size_t index = 0U;
  const std::size_t bounded_limit = std::min(value.size(), limit);
  while (index < bounded_limit) {
    std::size_t width = Utf8SequenceLength(value, index);
    if (width == 0U)
      width = 1U;
    if (width > bounded_limit - index)
      break;
    index += width;
  }
  return index;
}

std::string Bounded(std::string_view value, const std::size_t limit) {
  if (value.size() <= limit)
    return std::string(value);
  static constexpr std::string_view suffix = "...[truncated]";
  if (limit <= suffix.size()) {
    return std::string(
        value.substr(0, Utf8SafePrefixLength(value, limit)));
  }
  const std::size_t prefix_length =
      Utf8SafePrefixLength(value, limit - suffix.size());
  std::string result(value.substr(0, prefix_length));
  result.append(suffix);
  return result;
}

void AppendJsonString(std::string &out, const std::string_view value) {
  static constexpr char hex[] = "0123456789abcdef";
  out.push_back('"');
  for (std::size_t index = 0U; index < value.size();) {
    const auto c = static_cast<unsigned char>(value[index]);
    if (c >= 0x80U) {
      const std::size_t width = Utf8SequenceLength(value, index);
      if (width == 0U) {
        out.append("\\ufffd");
        ++index;
      } else {
        out.append(value.substr(index, width));
        index += width;
      }
      continue;
    }
    switch (c) {
    case '"':
      out.append("\\\"");
      break;
    case '\\':
      out.append("\\\\");
      break;
    case '\b':
      out.append("\\b");
      break;
    case '\f':
      out.append("\\f");
      break;
    case '\n':
      out.append("\\n");
      break;
    case '\r':
      out.append("\\r");
      break;
    case '\t':
      out.append("\\t");
      break;
    default:
      if (c < 0x20U) {
        out.append("\\u00");
        out.push_back(hex[(c >> 4U) & 0x0fU]);
        out.push_back(hex[c & 0x0fU]);
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
    ++index;
  }
  out.push_back('"');
}

std::string SanitizeUtf8(const std::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (std::size_t index = 0U; index < value.size();) {
    const auto byte = static_cast<unsigned char>(value[index]);
    if (byte <= 0x7fU) {
      output.push_back(static_cast<char>(byte));
      ++index;
      continue;
    }
    const std::size_t width = Utf8SequenceLength(value, index);
    if (width == 0U) {
      output.append("\xef\xbf\xbd");
      ++index;
      continue;
    }
    output.append(value.substr(index, width));
    index += width;
  }
  return output;
}

std::string SourceBasename(const char *file) {
  if (!file)
    return {};
  const std::filesystem::path path(file);
  return path.filename().string();
}

std::string FormatJson(const Event &event, const Config &config,
                       const std::chrono::system_clock::time_point time,
                       const std::uint64_t sequence) {
  std::string out;
  out.reserve(event.message.size() + 512U);
  out.append("{\"time\":");
  AppendJsonString(out, FormatLocalTime(time));
  out.append(",\"level\":");
  AppendJsonString(out, DisplayLevelName(event.level));
  out.append(",\"category\":");
  AppendJsonString(out, event.category.empty() ? "SOVKIT" : event.category);
  out.append(",\"event\":");
  AppendJsonString(out, event.name.empty() ? "message" : event.name);
  if (!event.code_name.empty()) {
    out.append(",\"codeName\":");
    AppendJsonString(out, event.code_name);
  }
  out.append(",\"code\":");
  out.append(std::to_string(event.code));
  out.append(",\"message\":");
  AppendJsonString(out, Bounded(event.message, config.max_message_bytes));
  if (!event.code_message.empty() && event.code != 0 &&
      event.code_message != event.message) {
    out.append(",\"codeMessage\":");
    AppendJsonString(out,
                     Bounded(event.code_message,
                             config.max_field_value_bytes));
  }

  if (!event.env_id.empty()) {
    out.append(",\"envId\":");
    AppendJsonString(out, event.env_id);
  }
  if (!event.operation_id.empty()) {
    out.append(",\"operationId\":");
    AppendJsonString(out, event.operation_id);
  }
  if (event.process_id != 0U) {
    out.append(",\"pid\":");
    out.append(std::to_string(event.process_id));
  }

  std::map<std::string, std::string> normalized_fields;
  const std::size_t count =
      std::min(event.fields.size(), config.max_field_count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto &field = event.fields[i];
    if (field.key.empty())
      continue;
    normalized_fields[field.key] =
        IsSensitiveKey(field.key, config)
            ? "[REDACTED]"
            : Bounded(field.value, config.max_field_value_bytes);
  }
  out.append(",\"sequence\":");
  out.append(std::to_string(sequence));
  out.append(",\"threadId\":");
  AppendJsonString(
      out, std::to_string(std::hash<std::thread::id>{}(
               std::this_thread::get_id())));
  if (event.source.file || event.source.function || event.source.line != 0U) {
    out.append(",\"source\":{\"file\":");
    AppendJsonString(out, SourceBasename(event.source.file));
    out.append(",\"line\":");
    out.append(std::to_string(event.source.line));
    out.append(",\"function\":");
    AppendJsonString(out,
                     event.source.function ? event.source.function : "");
    out.push_back('}');
  }
  if (!normalized_fields.empty()) {
    out.append(",\"fields\":{");
    bool first = true;
    for (const auto &[key, value] : normalized_fields) {
      if (!first)
        out.push_back(',');
      first = false;
      AppendJsonString(out, key);
      out.push_back(':');
      AppendJsonString(out, value);
    }
    out.push_back('}');
  }
  out.push_back('}');
  return out;
}

class DeliverySink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  static bool InCallback() noexcept { return in_callback_; }

  void SetCallback(Callback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = std::move(callback);
  }

  void EnqueueCallbackPolicy(const bool emit_callback,
                             std::string callback_text) {
    std::lock_guard<std::mutex> lock(policy_mutex_);
    callback_policy_.push_back(
        CallbackPolicy{emit_callback, std::move(callback_text)});
  }

  void CancelLastCallbackPolicy() {
    std::lock_guard<std::mutex> lock(policy_mutex_);
    if (!callback_policy_.empty())
      callback_policy_.pop_back();
  }

  std::uint64_t PrepareFlush() {
    std::lock_guard<std::mutex> lock(flush_mutex_);
    return ++flush_requested_;
  }

  bool WaitForFlush(const std::uint64_t generation,
                    const std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(flush_mutex_);
    return flush_condition_.wait_for(lock, timeout, [&] {
      return flush_observed_ >= generation;
    });
  }

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    CallbackPolicy policy;
    {
      std::lock_guard<std::mutex> lock(policy_mutex_);
      if (!callback_policy_.empty()) {
        policy = std::move(callback_policy_.front());
        callback_policy_.pop_front();
      }
    }
    if (!policy.emit)
      return;

    Callback callback;
    try {
      std::lock_guard<std::mutex> lock(callback_mutex_);
      callback = callback_;
    } catch (...) {
      return;
    }
    if (!callback)
      return;

    try {
      spdlog::memory_buf_t formatted;
      formatter_->format(message, formatted);
      CallbackRecord record;
      record.timestamp_ms = TimestampMs(message.time);
      record.level = FromSpdLevel(message.level);
      if (policy.text.empty()) {
        record.json_line.assign(formatted.data(), formatted.size());
      } else {
        record.json_line = FormatCallbackText(message.time, policy.text);
      }
      in_callback_ = true;
      callback(record);
    } catch (...) {
      // A consumer callback must never break the logging worker.
    }
    in_callback_ = false;
  }

  void flush_() override {
    {
      std::lock_guard<std::mutex> lock(flush_mutex_);
      flush_observed_ = flush_requested_;
    }
    flush_condition_.notify_all();
  }

private:
  struct CallbackPolicy {
    bool emit = true;
    std::string text;
  };

  std::mutex callback_mutex_;
  Callback callback_;
  std::mutex policy_mutex_;
  std::deque<CallbackPolicy> callback_policy_;
  std::mutex flush_mutex_;
  std::condition_variable flush_condition_;
  std::uint64_t flush_requested_ = 0;
  std::uint64_t flush_observed_ = 0;
  static thread_local bool in_callback_;
};

thread_local bool DeliverySink::in_callback_ = false;

class FanoutSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
  FanoutSink(std::vector<spdlog::sink_ptr> outputs,
             std::shared_ptr<DeliverySink> delivery)
      : outputs_(std::move(outputs)), delivery_(std::move(delivery)) {}

  void SetOutputsEnabled(bool enabled) noexcept { outputs_enabled_.store(enabled); }

protected:
  void sink_it_(const spdlog::details::log_msg &message) override {
    for (const auto &sink : outputs_) {
      if (!outputs_enabled_.load()) break;
      try {
        sink->log(message);
      } catch (...) {
        // Callback delivery is still part of the record contract when a
        // console or file sink is temporarily unavailable.
      }
    }
    try {
      if (delivery_)
        delivery_->log(message);
    } catch (...) {
      // Logging must never escape into spdlog's default stderr error handler.
    }
  }

  void flush_() override {
    for (const auto &sink : outputs_) {
      try {
        sink->flush();
      } catch (...) {
      }
    }
    try {
      if (delivery_)
        delivery_->flush();
    } catch (...) {
    }
  }

private:
  std::vector<spdlog::sink_ptr> outputs_;
  std::shared_ptr<DeliverySink> delivery_;
  std::atomic<bool> outputs_enabled_{true};
};

bool ValidateConfig(const Config &config, std::string &error) {
  if (config.logger_name.empty()) {
    error = "logger_name must not be empty";
    return false;
  }
  if (config.file_enabled) {
    if (config.log_directory.empty()) {
      error = "log_directory must not be empty when file logging is enabled";
      return false;
    }
    if (config.file_name.empty() ||
        std::filesystem::path(config.file_name).is_absolute() ||
        std::filesystem::path(config.file_name).filename() !=
            std::filesystem::path(config.file_name)) {
      error = "file_name must be a non-empty file name without directories";
      return false;
    }
    if (config.rotate_size_bytes == 0U || config.rotate_file_count == 0U) {
      error = "rotation size and file count must be greater than zero";
      return false;
    }
  }
  if (config.async_enabled && config.async_queue_size == 0U) {
    error = "async_queue_size must be greater than zero";
    return false;
  }
  if (config.max_message_bytes == 0U ||
      config.max_field_value_bytes == 0U) {
    error = "message and field limits must be greater than zero";
    return false;
  }
  return true;
}

bool IsManagedLogFile(const std::filesystem::path &candidate,
                      const std::filesystem::path &configured_name,
                      const std::string_view managed_prefix) {
  const std::string candidate_name = candidate.filename().string();
  const std::string configured_file = configured_name.filename().string();
  if (candidate_name == configured_file)
    return true;

  const std::string configured_stem = configured_name.stem().string();
  const std::string configured_extension = configured_name.extension().string();
  std::string stem = candidate.stem().string();
  if (!managed_prefix.empty()) {
    if (!stem.starts_with(managed_prefix))
      return false;
    stem.erase(0, managed_prefix.size());
    const auto rotation_separator = stem.find('.');
    const std::string_view owner =
        rotation_separator == std::string::npos
            ? std::string_view(stem)
            : std::string_view(stem).substr(0, rotation_separator);
    const std::string_view rotation =
        rotation_separator == std::string::npos
            ? std::string_view{}
            : std::string_view(stem).substr(rotation_separator + 1U);
    const auto digits = [](const std::string_view value) {
      return !value.empty() &&
             std::all_of(value.begin(), value.end(), [](const unsigned char c) {
               return c >= '0' && c <= '9';
             });
    };
    return digits(owner) &&
           (rotation_separator == std::string::npos || digits(rotation)) &&
           candidate.extension().string() == configured_extension;
  }

  const std::string rotation_prefix = configured_stem + ".";
  if (!stem.starts_with(rotation_prefix))
    return false;
  stem.erase(0, rotation_prefix.size());
  return !stem.empty() &&
         std::all_of(stem.begin(), stem.end(), [](const unsigned char c) {
           return c >= '0' && c <= '9';
         }) &&
         candidate.extension().string() == configured_extension;
}

PruneResult PruneExpiredFilesImpl(
    const std::filesystem::path &directory, const std::string_view file_name,
    const std::uint32_t retention_days,
    const std::string_view managed_prefix,
    const bool preserve_configured_file) noexcept {
  PruneResult result;
  if (directory.empty() || file_name.empty() || retention_days == 0U)
    return result;

  try {
    const std::filesystem::path configured_name(file_name);
    const auto max_age = std::chrono::hours(24) * retention_days;
    const auto cutoff = std::filesystem::file_time_type::clock::now() - max_age;
    std::error_code iterator_error;
    for (std::filesystem::directory_iterator iterator(directory, iterator_error),
         end;
         !iterator_error && iterator != end; iterator.increment(iterator_error)) {
      std::error_code entry_error;
      if (iterator->is_symlink(entry_error) || entry_error ||
          !iterator->is_regular_file(entry_error) || entry_error ||
          (preserve_configured_file &&
           iterator->path().filename() == configured_name.filename()) ||
          !IsManagedLogFile(iterator->path(), configured_name,
                            managed_prefix)) {
        continue;
      }
      ++result.scanned;
      const auto write_time = iterator->last_write_time(entry_error);
      if (entry_error) {
        ++result.failed;
        continue;
      }
      if (write_time >= cutoff)
        continue;
      if (std::filesystem::remove(iterator->path(), entry_error) &&
          !entry_error) {
        ++result.removed;
      } else {
        ++result.failed;
      }
    }
    if (iterator_error)
      ++result.failed;
  } catch (...) {
    ++result.failed;
  }
  return result;
}

} // namespace

struct Logger::Impl {
  enum class State : std::uint8_t { Open, Closing, Closed };

  Config config;
  std::shared_ptr<spdlog::details::thread_pool> thread_pool;
  std::shared_ptr<DeliverySink> delivery_sink;
  std::weak_ptr<FanoutSink> fanout_sink;
  std::atomic<bool> outputs_enabled{true};
  std::shared_ptr<spdlog::logger> logger;
  std::vector<spdlog::sink_ptr> output_sinks;
  // Serializes the log/flush/shutdown submission boundary. A recursive mutex
  // keeps synchronous loggers safe when a callback emits a diagnostic record.
  mutable std::recursive_mutex submission_mutex;
  mutable std::mutex flush_call_mutex;
  mutable std::mutex operation_mutex;
  std::condition_variable state_condition;
  std::atomic<State> state{State::Open};
  std::atomic<Level> minimum_level{Level::Info};
  std::atomic<std::uint64_t> next_prune_at_ms{0};
};

const char *LevelName(const Level level) noexcept {
  switch (level) {
  case Level::Trace:
    return "trace";
  case Level::Debug:
    return "debug";
  case Level::Info:
    return "info";
  case Level::Warn:
    return "warn";
  case Level::Error:
    return "error";
  case Level::Critical:
    return "critical";
  case Level::Off:
    return "off";
  }
  return "unknown";
}

Logger::Logger(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

Logger::~Logger() {
  if (!impl_)
    return;
  if (DeliverySink::InCallback()) {
    const auto timeout = impl_->config.shutdown_timeout;
    Impl *deferred_impl = impl_.release();
    try {
      std::thread([deferred_impl, timeout] {
        Logger deferred{std::unique_ptr<Impl>(deferred_impl)};
        deferred.Shutdown(timeout);
      }).detach();
    } catch (...) {
      // Leaking during an out-of-resource callback teardown is preferable to
      // destroying spdlog's thread pool from its own worker thread.
    }
    return;
  }
  Shutdown(impl_->config.shutdown_timeout);
}

std::shared_ptr<Logger> Logger::Create(Config config,
                                       std::string *error) noexcept {
  return Create(std::move(config), {}, error);
}

std::shared_ptr<Logger> Logger::Create(Config config, Callback callback,
                                       std::string *error) noexcept {
  if (error)
    error->clear();
  try {
    std::string validation_error;
    if (!ValidateConfig(config, validation_error)) {
      if (error)
        *error = std::move(validation_error);
      return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->config = std::move(config);
    impl->minimum_level.store(impl->config.minimum_level,
                              std::memory_order_release);
    impl->next_prune_at_ms.store(
        TimestampMs(std::chrono::system_clock::now()) +
            60U * 60U * 1000U,
        std::memory_order_release);

    std::vector<spdlog::sink_ptr> sinks;
    if (impl->config.console_enabled) {
      auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
      sink->set_pattern("%v");
      sinks.push_back(std::move(sink));
    }
    if (impl->config.file_enabled) {
      std::filesystem::create_directories(impl->config.log_directory);
      PruneExpiredFiles(impl->config.log_directory, impl->config.file_name,
                        impl->config.retention_days,
                        impl->config.retention_file_prefix);
      const auto path =
          impl->config.log_directory / impl->config.file_name;
      auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          ToSpdlogFilename(path), impl->config.rotate_size_bytes,
          impl->config.rotate_file_count, false);
      sink->set_pattern("%v");
      sinks.push_back(std::move(sink));
    }

    impl->output_sinks = sinks;
    impl->delivery_sink = std::make_shared<DeliverySink>();
    impl->delivery_sink->set_pattern("%v");
    impl->delivery_sink->SetCallback(std::move(callback));
    auto fanout =
        std::make_shared<FanoutSink>(impl->output_sinks, impl->delivery_sink);
    impl->fanout_sink = fanout;
    sinks.clear();
    sinks.push_back(std::move(fanout));

    const std::string instance_name =
        impl->config.logger_name + "-" +
        std::to_string(g_logger_sequence.fetch_add(1,
                                                    std::memory_order_relaxed));
    if (impl->config.async_enabled) {
      impl->thread_pool = std::make_shared<spdlog::details::thread_pool>(
          impl->config.async_queue_size, 1);
      impl->logger = std::make_shared<spdlog::async_logger>(
          instance_name, sinks.begin(), sinks.end(), impl->thread_pool,
          spdlog::async_overflow_policy::block);
    } else {
      impl->logger = std::make_shared<spdlog::logger>(
          instance_name, sinks.begin(), sinks.end());
    }
    impl->logger->set_level(spdlog::level::trace);
    // Error durability uses Logger::Flush(), which carries a real queue
    // barrier. spdlog's implicit flush cannot identify its generation.
    impl->logger->flush_on(spdlog::level::off);
    impl->logger->set_error_handler([](const std::string &) {});
    return std::shared_ptr<Logger>(new Logger(std::move(impl)));
  } catch (const std::exception &exception) {
    if (error)
      *error = exception.what();
  } catch (...) {
    if (error)
      *error = "unknown logger initialization failure";
  }
  return nullptr;
}

bool Logger::Write(Event event) noexcept {
  if (!impl_ || impl_->state.load(std::memory_order_acquire) !=
                    Impl::State::Open)
    return false;

  const Level minimum = impl_->minimum_level.load(std::memory_order_acquire);
  if (minimum == Level::Off || event.level == Level::Off ||
      static_cast<unsigned>(event.level) < static_cast<unsigned>(minimum))
    return false;

  try {
    const auto timestamp = std::chrono::system_clock::now();
    const std::uint64_t timestamp_ms = TimestampMs(timestamp);
    if (impl_->config.retention_days > 0U) {
      std::uint64_t next_prune =
          impl_->next_prune_at_ms.load(std::memory_order_acquire);
      if (timestamp_ms >= next_prune &&
          impl_->next_prune_at_ms.compare_exchange_strong(
              next_prune, timestamp_ms + 60U * 60U * 1000U,
              std::memory_order_acq_rel)) {
        PruneExpiredFilesImpl(impl_->config.log_directory,
                              impl_->config.file_name,
                              impl_->config.retention_days, {}, true);
      }
    }
    if (DeliverySink::InCallback()) {
      std::lock_guard<std::recursive_mutex> submission_lock(
          impl_->submission_mutex);
      std::vector<spdlog::sink_ptr> output_sinks;
      {
        std::lock_guard<std::mutex> lock(impl_->operation_mutex);
        if (impl_->state.load(std::memory_order_relaxed) != Impl::State::Open)
          return false;
        output_sinks = impl_->output_sinks;
      }
      const std::uint64_t sequence =
          g_record_sequence.fetch_add(1, std::memory_order_relaxed) + 1U;
      const std::string json =
          FormatJson(event, impl_->config, timestamp, sequence);
      const spdlog::details::log_msg message(
          timestamp, spdlog::source_loc{}, spdlog::string_view_t("", 0),
          ToSpdLevel(event.level),
          spdlog::string_view_t(json.data(), json.size()));
      for (const auto &sink : output_sinks)
        if (impl_->outputs_enabled.load()) sink->log(message);
      if (impl_->config.flush_on_error && event.level >= Level::Error) {
        for (const auto &sink : output_sinks)
          sink->flush();
      }
      return true;
    }

    {
      std::lock_guard<std::recursive_mutex> submission_lock(
          impl_->submission_mutex);
      std::shared_ptr<spdlog::logger> logger;
      std::shared_ptr<DeliverySink> delivery_sink;
      {
        std::lock_guard<std::mutex> lock(impl_->operation_mutex);
        if (impl_->state.load(std::memory_order_relaxed) != Impl::State::Open ||
            !impl_->logger || !impl_->delivery_sink)
          return false;
        logger = impl_->logger;
        delivery_sink = impl_->delivery_sink;
      }
      const std::uint64_t sequence =
          g_record_sequence.fetch_add(1, std::memory_order_relaxed) + 1U;
      const std::string json =
          FormatJson(event, impl_->config, timestamp, sequence);
      const bool emit_callback = event.emit_callback;
      delivery_sink->EnqueueCallbackPolicy(emit_callback,
                                           std::move(event.callback_text));
      try {
        logger->log(
            timestamp, spdlog::source_loc{}, ToSpdLevel(event.level),
            spdlog::string_view_t(json.data(), json.size()));
      } catch (...) {
        delivery_sink->CancelLastCallbackPolicy();
        throw;
      }
    }
    if (impl_->config.flush_on_error && event.level >= Level::Error)
      return Flush(impl_->config.shutdown_timeout);
    return true;
  } catch (...) {
    return false;
  }
}

bool Logger::Log(const Level level, const std::string_view category,
                 const std::string_view event_name,
                 const std::string_view message, Fields fields,
                 const SourceLocation source) noexcept {
  try {
    Event event;
    event.level = level;
    event.category.assign(category);
    event.name.assign(event_name);
    event.message.assign(message);
    event.fields = std::move(fields);
    event.source = source;
    return Write(std::move(event));
  } catch (...) {
    return false;
  }
}

#define SOVKIT_LOG_DEFINE_LEVEL_METHOD(method_name, enum_name)                \
  bool Logger::method_name(std::string_view category,                        \
                           std::string_view event_name,                      \
                           std::string_view message, Fields fields,          \
                           SourceLocation source) noexcept {                 \
    return Log(Level::enum_name, category, event_name, message,              \
               std::move(fields), source);                                   \
  }

SOVKIT_LOG_DEFINE_LEVEL_METHOD(Trace, Trace)
SOVKIT_LOG_DEFINE_LEVEL_METHOD(Debug, Debug)
SOVKIT_LOG_DEFINE_LEVEL_METHOD(Info, Info)
SOVKIT_LOG_DEFINE_LEVEL_METHOD(Warn, Warn)
SOVKIT_LOG_DEFINE_LEVEL_METHOD(Error, Error)
SOVKIT_LOG_DEFINE_LEVEL_METHOD(Critical, Critical)

#undef SOVKIT_LOG_DEFINE_LEVEL_METHOD

BoundLogger Logger::Bind(std::string category, Fields context) {
  return BoundLogger(shared_from_this(), std::move(category),
                     std::move(context));
}

bool Logger::SetCallback(Callback callback) noexcept {
  if (!impl_)
    return false;
  try {
    std::shared_ptr<DeliverySink> delivery_sink;
    std::shared_ptr<spdlog::logger> logger;
    if (DeliverySink::InCallback()) {
      std::lock_guard<std::mutex> lock(impl_->operation_mutex);
      if (impl_->delivery_sink) {
        impl_->delivery_sink->SetCallback(std::move(callback));
        return true;
      }
      return false;
    }
    std::lock_guard<std::mutex> flush_lock(impl_->flush_call_mutex);
    std::lock_guard<std::recursive_mutex> submission_lock(
        impl_->submission_mutex);
    {
      std::lock_guard<std::mutex> lock(impl_->operation_mutex);
      delivery_sink = impl_->delivery_sink;
      logger = impl_->logger;
    }
    if (delivery_sink && logger) {
      const auto generation = delivery_sink->PrepareFlush();
      logger->flush();
      if (!delivery_sink->WaitForFlush(generation,
                                       impl_->config.shutdown_timeout))
        return false;
      delivery_sink->SetCallback(std::move(callback));
      return true;
    }
  } catch (...) {
  }
  return false;
}

void Logger::SetOutputsEnabled(bool enabled) noexcept {
  if (!impl_) return;
  impl_->outputs_enabled.store(enabled);
  if (auto fanout = impl_->fanout_sink.lock()) fanout->SetOutputsEnabled(enabled);
}

void Logger::SetLevel(const Level level) noexcept {
  if (!impl_)
    return;
  impl_->minimum_level.store(level, std::memory_order_release);
}

Level Logger::GetLevel() const noexcept {
  return impl_ ? impl_->minimum_level.load(std::memory_order_acquire)
               : Level::Off;
}

bool Logger::IsOpen() const noexcept {
  return impl_ && impl_->state.load(std::memory_order_acquire) ==
                      Impl::State::Open;
}

bool Logger::Flush(const std::chrono::milliseconds timeout) noexcept {
  if (!impl_ || DeliverySink::InCallback())
    return false;
  try {
    std::lock_guard<std::mutex> flush_lock(impl_->flush_call_mutex);
    std::shared_ptr<spdlog::logger> logger;
    std::shared_ptr<DeliverySink> delivery_sink;
    std::uint64_t generation = 0;
    {
      std::lock_guard<std::recursive_mutex> submission_lock(
          impl_->submission_mutex);
      {
        std::lock_guard<std::mutex> lock(impl_->operation_mutex);
        if (!impl_->logger || !impl_->delivery_sink ||
            impl_->state.load(std::memory_order_relaxed) != Impl::State::Open)
          return false;
        logger = impl_->logger;
        delivery_sink = impl_->delivery_sink;
      }
      generation = delivery_sink->PrepareFlush();
      logger->flush();
    }
    return delivery_sink->WaitForFlush(generation, timeout);
  } catch (...) {
    return false;
  }
}

bool Logger::Shutdown(const std::chrono::milliseconds timeout) noexcept {
  if (!impl_)
    return true;
  if (DeliverySink::InCallback())
    return false;
  try {
    std::lock_guard<std::mutex> flush_lock(impl_->flush_call_mutex);
    std::shared_ptr<spdlog::logger> logger;
    std::shared_ptr<DeliverySink> delivery_sink;
    std::shared_ptr<spdlog::details::thread_pool> thread_pool;
    std::uint64_t generation = 0;
    {
      std::unique_lock<std::recursive_mutex> submission_lock(
          impl_->submission_mutex);
      std::unique_lock<std::mutex> lock(impl_->operation_mutex);
      const auto state = impl_->state.load(std::memory_order_relaxed);
      if (state == Impl::State::Closed)
        return true;
      if (state == Impl::State::Closing) {
        lock.unlock();
        submission_lock.unlock();
        lock.lock();
        return impl_->state_condition.wait_for(lock, timeout, [&] {
          return impl_->state.load(std::memory_order_acquire) ==
                 Impl::State::Closed;
        });
      }
      impl_->state.store(Impl::State::Closing, std::memory_order_release);
      logger = impl_->logger;
      delivery_sink = impl_->delivery_sink;
      if (logger && delivery_sink) {
        generation = delivery_sink->PrepareFlush();
        logger->flush();
      }
    }

    const bool drained = !delivery_sink ||
                         delivery_sink->WaitForFlush(generation, timeout);
    {
      std::lock_guard<std::mutex> lock(impl_->operation_mutex);
      impl_->logger.reset();
      impl_->delivery_sink.reset();
      impl_->output_sinks.clear();
      thread_pool.swap(impl_->thread_pool);
    }
    logger.reset();
    delivery_sink.reset();
    thread_pool.reset();
    {
      std::lock_guard<std::mutex> lock(impl_->operation_mutex);
      impl_->state.store(Impl::State::Closed, std::memory_order_release);
    }
    impl_->state_condition.notify_all();
    return drained;
  } catch (...) {
    {
      std::lock_guard<std::mutex> lock(impl_->operation_mutex);
      impl_->state.store(Impl::State::Closed, std::memory_order_release);
    }
    impl_->state_condition.notify_all();
    return false;
  }
}

bool Logger::WriteEmergency(const std::filesystem::path &path,
                            const std::string_view text,
                            std::string *error) noexcept {
  if (error)
    error->clear();
  try {
    if (path.empty()) {
      if (error)
        *error = "emergency log path must not be empty";
      return false;
    }
    if (path.has_parent_path())
      std::filesystem::create_directories(path.parent_path());
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
        ToSpdlogFilename(path), true);
    sink->set_pattern("%v");
    spdlog::logger logger(
        "sovkit-log-emergency-" +
            std::to_string(g_logger_sequence.fetch_add(
                1, std::memory_order_relaxed)),
        sink);
    logger.log(spdlog::source_loc{}, spdlog::level::critical,
               spdlog::string_view_t(text.data(), text.size()));
    logger.flush();
    return true;
  } catch (const std::exception &exception) {
    if (error)
      *error = exception.what();
  } catch (...) {
    if (error)
      *error = "unknown emergency log failure";
  }
  try {
    if (path.has_parent_path())
      std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream)
      return false;
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.put('\n');
    stream.flush();
    if (!stream)
      return false;
    if (error)
      error->clear();
    return true;
  } catch (...) {
    return false;
  }
}

PruneResult Logger::PruneExpiredFiles(
    const std::filesystem::path &directory, const std::string_view file_name,
    const std::uint32_t retention_days,
    const std::string_view managed_prefix) noexcept {
  return PruneExpiredFilesImpl(directory, file_name, retention_days,
                               managed_prefix, false);
}

BoundLogger::BoundLogger(std::shared_ptr<Logger> logger, std::string category,
                         Fields context)
    : logger_(std::move(logger)), category_(std::move(category)),
      context_(std::move(context)) {}

bool BoundLogger::Log(const Level level, const std::string_view event_name,
                      const std::string_view message, Fields fields,
                      const SourceLocation source) const noexcept {
  try {
    if (!logger_)
      return false;
    Fields merged;
    merged.reserve(context_.size() + fields.size());
    merged.insert(merged.end(), context_.begin(), context_.end());
    merged.insert(merged.end(), std::make_move_iterator(fields.begin()),
                  std::make_move_iterator(fields.end()));
    return logger_->Log(level, category_, event_name, message,
                        std::move(merged), source);
  } catch (...) {
    return false;
  }
}

#define SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD(method_name, enum_name)          \
  bool BoundLogger::method_name(std::string_view event_name,                  \
                                std::string_view message, Fields fields,      \
                                SourceLocation source) const noexcept {       \
    return Log(Level::enum_name, event_name, message, std::move(fields),      \
               source);                                                       \
  }

SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD(Trace, Trace)
SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD(Debug, Debug)
SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD(Info, Info)
SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD(Warn, Warn)
SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD(Error, Error)
SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD(Critical, Critical)

#undef SOVKIT_LOG_DEFINE_BOUND_LEVEL_METHOD

} // namespace sovkit::log
