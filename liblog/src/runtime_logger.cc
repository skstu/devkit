#include "liblog/runtime_logger.h"
#include "runtime_file_sink.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <spdlog/details/log_msg.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <thread>

namespace sovkit::log {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using runtime_detail::FileFailure;
constexpr std::size_t kRecordBytes = 8192;
constexpr std::size_t kInputBytes = 16384;
constexpr std::size_t kResponseBytes = 262144;
constexpr std::size_t kEnvelopeReserve = 4096;

struct InvalidInput {};
Json Parse(std::string_view data) {
  if (data.empty() || data.size() > kInputBytes)
    throw InvalidInput{};
  // Reject duplicates, excessive nesting and malformed UTF-8 before any
  // field is copied into a record. No last-key-wins interpretation at the ABI.
  std::array<std::set<std::string>, 6> keys;
  auto callback = [&](int depth, Json::parse_event_t event, Json &value) {
    if (depth < 0 || depth >= 5)
      throw InvalidInput{};
    if (event == Json::parse_event_t::object_start)
      keys[depth + 1].clear();
    if (event == Json::parse_event_t::key &&
        !keys[depth].insert(value.get<std::string>()).second)
      throw InvalidInput{};
    return true;
  };
  auto result = Json::parse(data.begin(), data.end(), callback);
  if (!result.is_object())
    throw InvalidInput{};
  return result;
}
bool HasOnly(const Json &j, std::initializer_list<std::string_view> keys) {
  for (auto it = j.begin(); it != j.end(); ++it)
    if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
      return false;
  return true;
}
bool OneOf(std::string_view value,
           std::initializer_list<std::string_view> options) {
  return std::find(options.begin(), options.end(), value) != options.end();
}
bool Unsigned(const Json &value, std::uint64_t maximum,
              std::uint64_t minimum = 0) {
  return value.is_number_unsigned() && value.get<std::uint64_t>() <= maximum &&
         value.get<std::uint64_t>() >= minimum;
}
bool Version(const Json &j) {
  return j.contains("version") && Unsigned(j["version"], 1, 1);
}
Level ParseLevel(const Json &value) {
  if (!value.is_string())
    throw InvalidInput{};
  const auto &name = value.get_ref<const std::string &>();
  for (unsigned i = 0; i <= static_cast<unsigned>(Level::Off); ++i)
    if (name == LevelName(static_cast<Level>(i)))
      return static_cast<Level>(i);
  throw InvalidInput{};
}
bool Decimal(std::string_view value, std::uint64_t &out) {
  if (value.empty() || value.size() > 20 ||
      (value.size() > 1 && value[0] == '0'))
    return false;
  const auto r =
      std::from_chars(value.data(), value.data() + value.size(), out);
  return r.ec == std::errc{} && r.ptr == value.data() + value.size();
}
bool HexSession(std::string_view value) {
  return value.size() == 32 &&
         std::all_of(value.begin(), value.end(), [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}
std::string RandomSession() {
  std::random_device random;
  std::string result;
  result.reserve(32);
  constexpr char hex[] = "0123456789abcdef";
  for (int i = 0; i < 4; ++i) {
    const auto word = static_cast<std::uint32_t>(random());
    for (int shift = 28; shift >= 0; shift -= 4)
      result += hex[(word >> shift) & 15];
  }
  return result;
}
std::int64_t MonotonicMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Clock::now().time_since_epoch())
      .count();
}

enum class FieldGroup {
  None,
  Startup,
  Phase,
  Error,
  Network,
  Message,
  Authorization
};
struct Template {
  const char *component;
  const char *event;
  const char *message;
  FieldGroup fields;
};
constexpr Template kTemplates[] = {
    {"sdk", "sdk.started", "SDK started", FieldGroup::Startup},
    {"sdk", "sdk.stopped", "SDK stopped", FieldGroup::Phase},
    {"sdk", "sdk.diagnostic", "SDK diagnostic", FieldGroup::None},
    {"logging", "logging.configured", "Runtime logging configured",
     FieldGroup::Startup},
    {"runtime", "runtime.started", "Runtime started", FieldGroup::None},
    {"runtime", "runtime.stopped", "Runtime stopped", FieldGroup::None},
    {"runtime", "runtime.failed", "Runtime operation failed",
     FieldGroup::Error},
    {"identity", "identity.created", "Identity created", FieldGroup::None},
    {"identity", "identity.imported", "Identity imported", FieldGroup::None},
    {"identity", "identity.device_rotated", "Device identity rotated",
     FieldGroup::None},
    {"identity", "identity.device_revoked", "Device identity revoked",
     FieldGroup::None},
    {"vault", "vault.unlock_result", "Vault unlock finished",
     FieldGroup::Phase},
    {"storage", "storage.opened", "Storage opened", FieldGroup::Phase},
    {"storage", "storage.closed", "Storage closed", FieldGroup::Phase},
    {"storage", "storage.migration_completed", "Storage migration completed",
     FieldGroup::Phase},
    {"storage", "storage.commit_failed", "Storage commit failed",
     FieldGroup::Error},
    {"pairing", "pairing.started", "Pairing started", FieldGroup::Phase},
    {"pairing", "pairing.completed", "Pairing completed", FieldGroup::Phase},
    {"pairing", "pairing.failed", "Pairing failed", FieldGroup::Error},
    {"pairing", "pairing.reconnect_accepted", "Reconnect accepted",
     FieldGroup::None},
    {"pairing", "pairing.load_failed", "Relationship load failed",
     FieldGroup::Error},
    {"discovery", "discovery.started", "Discovery started", FieldGroup::None},
    {"discovery", "discovery.stopped", "Discovery stopped", FieldGroup::None},
    {"transport", "transport.path_selected", "Direct transport path selected",
     FieldGroup::Network},
    {"transport", "transport.probe_completed", "Transport probe completed",
     FieldGroup::Network},
    {"transport", "transport.failed", "Transport failed", FieldGroup::Error},
    {"messaging", "messaging.committed", "Message committed",
     FieldGroup::Message},
    {"messaging", "messaging.delivered", "Message delivered",
     FieldGroup::Message},
    {"messaging", "messaging.load_failed", "Message load failed",
     FieldGroup::Error},
    {"transfer", "transfer.prepared", "Transfer prepared", FieldGroup::Phase},
    {"transfer", "transfer.phase_started", "Transfer phase started",
     FieldGroup::Phase},
    {"transfer", "transfer.phase_completed", "Transfer phase completed",
     FieldGroup::Phase},
    {"transfer", "transfer.failed", "Transfer failed", FieldGroup::Error},
    {"authorization", "authorization.granted", "Authorization granted",
     FieldGroup::Authorization},
    {"authorization", "authorization.denied", "Authorization denied",
     FieldGroup::Authorization},
    {"authorization", "authorization.revoked", "Authorization revoked",
     FieldGroup::Authorization},
    {"flutter", "flutter.exception", "Flutter exception reported",
     FieldGroup::Error},
    {"flutter", "flutter.framework_error", "Flutter framework error reported",
     FieldGroup::Error},
    {"flutter", "flutter.platform_error", "Flutter platform error reported",
     FieldGroup::Error},
    {"flutter", "flutter.zone_error", "Flutter zone error reported",
     FieldGroup::Error},
    {"flutter", "flutter.isolate_error", "Flutter isolate error reported",
     FieldGroup::Error},
    {"platform", "platform.error", "Platform operation failed",
     FieldGroup::Error},
    {"platform", "platform.background", "Application entered background",
     FieldGroup::None},
    {"platform", "platform.foreground", "Application entered foreground",
     FieldGroup::None},
};
const Template *FindTemplate(std::string_view component,
                             std::string_view event) {
  for (const auto &item : kTemplates)
    if (component == item.component && event == item.event)
      return &item;
  return nullptr;
}
bool ValidCode(std::string_view value) {
  return OneOf(value, {"OK",
                       "ERROR",
                       "UNKNOWN",
                       "INVALID_ARGUMENT",
                       "INVALID_STATE",
                       "INTERNAL",
                       "NOT_SUPPORTED",
                       "NOT_FOUND",
                       "ALREADY_EXISTS",
                       "AUTHENTICATION",
                       "BUFFER_TOO_SMALL",
                       "TIMEOUT",
                       "CANCELLED",
                       "PERMISSION_DENIED",
                       "IO_ERROR",
                       "DISK_FULL",
                       "BUSY",
                       "NETWORK_ERROR",
                       "CORRUPT",
                       "UNAVAILABLE"});
}
bool ValidField(FieldGroup group, std::string_view key, const Json &v) {
  if (group == FieldGroup::None)
    return false;
  if (key == "duration_ms")
    return Unsigned(v, 7ULL * 24 * 60 * 60 * 1000);
  if (key == "success")
    return v.is_boolean();
  if (group == FieldGroup::Startup)
    return OneOf(key,
                 {"app_version_major", "app_version_minor", "app_version_patch",
                  "sdk_version_major", "sdk_version_minor", "sdk_version_patch",
                  "abi", "build"}) &&
           Unsigned(v, 1000000000);
  if (group == FieldGroup::Phase || group == FieldGroup::Message) {
    if (OneOf(key, {"bytes", "count", "retry_count", "rows"}))
      return Unsigned(v, 9007199254740991ULL);
    if (key == "phase" && v.is_string())
      return OneOf(v.get_ref<const std::string &>(),
                   {"prepare", "send", "receive", "verify", "publish", "open",
                    "close", "migrate", "commit", "unlock", "pause", "cancel"});
    if (key == "direction" && v.is_string())
      return OneOf(v.get_ref<const std::string &>(), {"incoming", "outgoing"});
  }
  if (group == FieldGroup::Error) {
    if (key == "fatal" || key == "retryable")
      return v.is_boolean();
    if (key == "retry_count")
      return Unsigned(v, 1000000);
    if (key == "error_class" && v.is_string())
      return OneOf(v.get_ref<const std::string &>(),
                   {"unknown", "io", "permission", "timeout", "cancelled",
                    "authentication", "invalid_state", "network", "corrupt",
                    "busy", "resource"});
  }
  if (group == FieldGroup::Network && v.is_string()) {
    const auto &s = v.get_ref<const std::string &>();
    if (key == "family")
      return OneOf(s, {"ipv4", "ipv6", "dual", "unknown"});
    if (key == "path")
      return OneOf(s, {"lan", "ice", "direct", "unknown"});
    if (key == "protocol")
      return OneOf(s, {"tcp", "udp", "quic", "unknown"});
  }
  if (group == FieldGroup::Authorization && key == "scope" && v.is_string())
    return OneOf(v.get_ref<const std::string &>(),
                 {"pairing", "receive", "send", "publish", "relationship"});
  return false;
}
bool ValidConfig(const RuntimeConfig &c) {
  const auto path = c.directory.generic_string();
  if (!c.directory.is_absolute() || path.size() > 4096 ||
      path.find('\0') != std::string::npos ||
      c.directory == c.directory.root_path() ||
      !OneOf(c.role, {"app", "helper", "sdk", "sovkitd", "stun"}) ||
      c.minimum_level > Level::Off || c.file_level > Level::Off)
    return false;
  for (const auto &part : c.directory.relative_path())
    if (part == "." || part == ".." || part.empty())
      return false;
  return c.ring_entries >= 1 && c.ring_entries <= 2000 && c.ring_bytes >= 512 &&
         c.ring_bytes <= 2U * 1024U * 1024U && c.queue_entries >= 1 &&
         c.queue_entries <= 1024 && c.queue_bytes >= 512 &&
         c.queue_bytes <= 4U * 1024U * 1024U &&
         c.reserved_entries < c.queue_entries &&
         c.reserved_bytes < c.queue_bytes && c.segment_bytes >= kRecordBytes &&
         c.segment_bytes <= 10U * 1024U * 1024U &&
         c.total_file_bytes >= c.segment_bytes &&
         c.total_file_bytes <= 50U * 1024U * 1024U &&
         c.flush_interval.count() >= 1 && c.flush_interval.count() <= 2000 &&
         c.debug_lifetime.count() >= 1 &&
         c.debug_lifetime <= std::chrono::minutes(30);
}
} // namespace

struct RuntimeLogger::Impl {
  struct Record {
    std::uint64_t seq;
    Level level;
    std::string json;
    bool file, console;
    std::size_t Charge() const { return json.capacity() + sizeof(Record) + 64; }
  };
  struct Queued {
    std::shared_ptr<const Record> record;
    std::uint64_t ticket;
  };
  RuntimeConfig config;
  std::string session;
  const Clock::time_point started = Clock::now();
  mutable std::timed_mutex mutex;
  std::condition_variable_any changed;
  std::timed_mutex join_mutex;
  std::timed_mutex flush_mutex;
  std::deque<std::shared_ptr<const Record>> ring;
  std::deque<Queued> queue;
  std::size_t ring_bytes = 0, queue_bytes = 0;
  std::size_t high_entries = 0, high_bytes = 0;
  std::uint64_t sequence = 0, lost_until = 0, queued = 0, processed = 0;
  std::uint64_t flush_requested = 0, flush_observed = 0, flush_target = 0;
  bool done = false;
  std::atomic<bool> accepting{true}, closed{false};
  std::atomic<Level> minimum{Level::Info}, file_minimum{Level::Info};
  std::atomic<bool> console_enabled{false};
  std::atomic<std::int64_t> debug_expires{0};
  std::array<std::atomic<std::uint64_t>, 6> dropped_ring{}, dropped_queue{},
      dropped_file{}, dropped_console{};
  std::atomic<std::uint64_t> invalid{0}, filtered{0}, accepted{0},
      file_errors{0}, console_errors{0};
  std::atomic<std::uint64_t> last_flush{0}, file_written{0}, flush_timeouts{0},
      shutdown_timeouts{0};
  std::atomic<const char *> file_state{"starting"}, file_error{"none"};
  std::thread worker;
  // A timed-out owner cannot destroy a joinable worker or its sinks. Explicit
  // successful Shutdown breaks this cycle after joining. No detach is used.
  std::shared_ptr<Impl> lifetime;
#if defined(SOVKIT_RUNTIME_LOG_TESTING)
  std::function<void()> hook;
#endif

  Level Effective(Level value) const {
    return value < Level::Info && MonotonicMs() >= debug_expires.load()
               ? Level::Info
               : value;
  }
  Json StatsLocked() const {
    auto counts = [](const auto &source) {
      Json j = Json::object();
      for (unsigned i = 0; i < 6; ++i)
        j[LevelName(static_cast<Level>(i))] = std::to_string(source[i].load());
      return j;
    };
    return {
        {"configured", true},
        {"session", session},
        {"state", closed.load()      ? "closed"
                  : accepting.load() ? "open"
                                     : "closing"},
        {"accepted", std::to_string(accepted.load())},
        {"invalid", std::to_string(invalid.load())},
        {"filtered", std::to_string(filtered.load())},
        {"minimumLevel", LevelName(Effective(minimum.load()))},
        {"fileLevel", LevelName(Effective(file_minimum.load()))},
        {"ring",
         {{"entries", ring.size()},
          {"bytes", ring_bytes},
          {"entryLimit", config.ring_entries},
          {"byteLimit", config.ring_bytes}}},
        {"queue",
         {{"entries", queue.size()},
          {"bytes", queue_bytes},
          {"entryLimit", config.queue_entries},
          {"byteLimit", config.queue_bytes},
          {"highWaterEntries", high_entries},
          {"highWaterBytes", high_bytes},
          {"pending", std::to_string(queued - processed)}}},
        {"dropped",
         {{"runtime", counts(dropped_ring)},
          {"queue", counts(dropped_queue)},
          {"file", counts(dropped_file)},
          {"console", counts(dropped_console)}}},
        {"file",
         {{"state", file_state.load()},
          {"enabled", config.file_enabled},
          {"healthy", config.file_enabled && std::string_view(file_error.load()) == "none" &&
                          std::string_view(file_state.load()) != "starting"},
          {"error", file_error.load()},
          {"errors", std::to_string(file_errors.load())},
          {"written", std::to_string(file_written.load())},
          {"lastSuccessfulFlushMs", std::to_string(last_flush.load())},
          {"segmentBytes", config.segment_bytes},
          {"totalBytesLimit", config.total_file_bytes},
          {"retentionDays", 7},
          {"role", config.role},
          {"durability", "flush_not_fsync"}}},
        {"consoleErrors", std::to_string(console_errors.load())},
        {"flushTimeouts", std::to_string(flush_timeouts.load())},
        {"shutdownTimeouts", std::to_string(shutdown_timeouts.load())},
        {"safeToUnload", closed.load()}};
  }
  RuntimeResult Submit(Level level, const Template &event,
                       const std::string &code, Json fields, bool truncated,
                       std::string *result) {
    if (!accepting.load())
      return RuntimeResult::Closed;
    const bool to_ring = level >= Effective(minimum.load());
    const bool to_file = config.file_enabled && level >= Effective(file_minimum.load());
    const bool to_console = console_enabled.load() && to_ring;
    if (!to_ring && !to_file && !to_console) {
      ++filtered;
      if (result)
        *result = Json({{"accepted", false},
                        {"reason", "filtered"},
                        {"session", session}})
                      .dump();
      return RuntimeResult::Ok;
    }
    const auto index = static_cast<unsigned>(level);
    // Producers never wait for readers, another producer, or file queue IO.
    std::unique_lock lock(mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
      if (to_ring)
        ++dropped_ring[index];
      if (to_file || to_console)
        ++dropped_queue[index];
      if (to_file)
        ++dropped_file[index];
      if (to_console)
        ++dropped_console[index];
      return RuntimeResult::Dropped;
    }
    if (!accepting.load())
      return RuntimeResult::Closed;
    if (sequence == std::numeric_limits<std::uint64_t>::max())
      return RuntimeResult::Closed;
    const auto seq = sequence + 1;
    Json j = {{"schema", 1},
              {"ts", runtime_detail::UtcTimestamp()},
              {"mono_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                              Clock::now() - started)
                              .count()},
              {"seq", std::to_string(seq)},
              {"level", LevelName(level)},
              {"component", event.component},
              {"event", event.event},
              {"message", event.message},
              {"session", session},
              {"operation", session + ":" + std::to_string(seq)},
              {"pid", runtime_detail::ProcessId()},
              {"tid", std::to_string(std::hash<std::thread::id>{}(
                          std::this_thread::get_id()))},
              {"code", code},
              {"fields", std::move(fields)},
              {"truncated", truncated}};
    auto record = std::make_shared<Record>(
        Record{seq, level, j.dump(), to_file, to_console});
    if (record->json.size() > kRecordBytes)
      throw InvalidInput{};
    const auto charge = record->Charge();
    sequence = seq;
    bool retained = false, enqueued = false;
    if (to_ring) {
      if (charge > config.ring_bytes) {
        ++dropped_ring[index];
        lost_until = seq;
      } else {
        while (!ring.empty() && (ring.size() >= config.ring_entries ||
                                 ring_bytes + charge > config.ring_bytes)) {
          const auto &old = ring.front();
          lost_until = std::max(lost_until, old->seq);
          ++dropped_ring[static_cast<unsigned>(old->level)];
          ring_bytes -= old->Charge();
          ring.pop_front();
        }
        ring.push_back(record);
        ring_bytes += charge;
        retained = true;
      }
    }
    if (to_file || to_console) {
      const auto entry_limit =
          config.queue_entries -
          (level < Level::Warn ? config.reserved_entries : 0);
      const auto byte_limit = config.queue_bytes -
                              (level < Level::Warn ? config.reserved_bytes : 0);
      if (queue.size() < entry_limit && charge <= byte_limit &&
          queue_bytes <= byte_limit - charge) {
        queue.push_back({record, queued + 1});
        ++queued;
        queue_bytes += charge;
        high_entries = std::max(high_entries, queue.size());
        high_bytes = std::max(high_bytes, queue_bytes);
        enqueued = true;
      } else {
        ++dropped_queue[index];
        if (to_file)
          ++dropped_file[index];
        if (to_console)
          ++dropped_console[index];
      }
    }
    if (retained || enqueued)
      ++accepted;
    lock.unlock();
    changed.notify_all();
    if (result)
      *result = Json({{"accepted", retained || enqueued},
                      {"runtime", retained},
                      {"queued", enqueued},
                      {"session", session},
                      {"seq", std::to_string(seq)}})
                    .dump();
    return retained || enqueued ? RuntimeResult::Ok : RuntimeResult::Dropped;
  }

  void FileFailed(const char *code, spdlog::sink_ptr &sink) {
    file_error.store(code);
    ++file_errors;
    file_state.store("unavailable");
    sink.reset();
  }
  void Run() noexcept {
    spdlog::sink_ptr file, console;
    try {
      if (config.file_enabled) {
        file = runtime_detail::MakeFileSink(config, session);
        file_state.store("healthy");
      } else {
        file_state.store("disabled");
      }
    } catch (const FileFailure &e) {
      FileFailed(e.code, file);
    } catch (...) {
      FileFailed("open_failed", file);
    }
    auto next_flush = Clock::now() + config.flush_interval;
    auto flush = [&] {
      if (file) {
        try {
          file->flush();
          last_flush.store(static_cast<std::uint64_t>(
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch())
                  .count()));
        } catch (const FileFailure &e) {
          FileFailed(e.code, file);
        } catch (...) {
          FileFailed("flush_failed", file);
        }
      }
      if (console) {
        try {
          console->flush();
        } catch (...) {
          ++console_errors;
        }
      }
      next_flush = Clock::now() + config.flush_interval;
    };
    try {
      for (;;) {
        Queued item{};
        std::uint64_t generation = 0;
        bool stop = false;
        {
          std::unique_lock lock(mutex);
          changed.wait_until(lock, next_flush, [&] {
            return !queue.empty() || !accepting.load() ||
                   flush_requested > flush_observed;
          });
          // Control barriers are separate scalar state, never queued/evicted.
          if (flush_requested > flush_observed && processed >= flush_target)
            generation = flush_requested;
          stop = !accepting.load() && queue.empty();
          if (!generation && !stop && !queue.empty()) {
            // Keep the in-flight record charged to the queue until IO ends.
            item = queue.front();
          }
        }
        if (item.record) {
#if defined(SOVKIT_RUNTIME_LOG_TESTING)
          std::function<void()> before;
          {
            std::lock_guard lock(mutex);
            before = hook;
          }
          if (before)
            before();
#endif
          const auto &record = *item.record;
          const auto index = static_cast<unsigned>(record.level);
          // Synchronous spdlog sinks are called only by this owned worker.
          const spdlog::details::log_msg message(
              spdlog::string_view_t("sovkit-runtime"),
              static_cast<spdlog::level::level_enum>(record.level),
              spdlog::string_view_t(record.json.data(), record.json.size()));
          if (record.file) {
            if (file) {
              try {
                file->log(message);
                ++file_written;
              } catch (const FileFailure &e) {
                FileFailed(e.code, file);
                ++dropped_file[index];
              } catch (...) {
                FileFailed("write_failed", file);
                ++dropped_file[index];
              }
            } else
              ++dropped_file[index];
          }
          if (record.console) {
            try {
              if (!console) {
                console = std::make_shared<spdlog::sinks::stdout_sink_st>();
                console->set_pattern("%v");
              }
              console->log(message);
            } catch (...) {
              ++console_errors;
              ++dropped_console[index];
            }
          }
          if (record.level >= Level::Error)
            flush();
          {
            std::lock_guard lock(mutex);
            processed = item.ticket;
            queue_bytes -= queue.front().record->Charge();
            queue.pop_front();
          }
        }
        if (generation || stop || Clock::now() >= next_flush)
          flush();
        if (generation) {
          {
            std::lock_guard lock(mutex);
            flush_observed = generation;
          }
          changed.notify_all();
        }
        if (stop)
          break;
      }
    } catch (...) {
      FileFailed("worker_failed", file);
      std::lock_guard lock(mutex);
      for (const auto &item : queue) {
        const auto index = static_cast<unsigned>(item.record->level);
        if (item.record->file)
          ++dropped_file[index];
        if (item.record->console)
          ++dropped_console[index];
      }
      queue.clear();
      queue_bytes = 0;
      processed = queued;
      accepting.store(false);
    }
    // Close OS handles/sinks before publishing completion or permitting unload.
    file.reset();
    console.reset();
    if (config.file_enabled && std::string_view(file_error.load()) == "none")
      file_state.store("closed");
    {
      std::lock_guard lock(mutex);
      done = true;
    }
    changed.notify_all();
  }
};

RuntimeLogger::RuntimeLogger(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}
RuntimeLogger::~RuntimeLogger() { Shutdown(std::chrono::milliseconds{0}); }

bool RuntimeLogger::ParseConfig(std::string_view input,
                                RuntimeConfig &out) noexcept {
  try {
    const auto j = Parse(input);
    if (!Version(j) ||
        !HasOnly(j, {"version", "directory", "role", "console", "minimumLevel",
                     "fileLevel", "fileEnabled"}) ||
        !j.contains("directory") || !j["directory"].is_string())
      return false;
    RuntimeConfig config;
    const auto path = j["directory"].get<std::string>();
    config.directory = std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t *>(path.data()), path.size()));
    if (j.contains("role"))
      config.role = j["role"].get<std::string>();
    if (j.contains("console"))
      config.console = j["console"].get<bool>();
    if (j.contains("fileEnabled"))
      config.file_enabled = j["fileEnabled"].get<bool>();
    if (j.contains("minimumLevel"))
      config.minimum_level = ParseLevel(j["minimumLevel"]);
    if (j.contains("fileLevel"))
      config.file_level = ParseLevel(j["fileLevel"]);
    if (!ValidConfig(config))
      return false;
    out = std::move(config);
    return true;
  } catch (...) {
    return false;
  }
}
std::shared_ptr<RuntimeLogger>
RuntimeLogger::Create(RuntimeConfig config) noexcept {
  try {
    if (!ValidConfig(config))
      return {};
    auto impl = std::make_shared<Impl>();
    impl->config = std::move(config);
    impl->session = RandomSession();
    impl->minimum.store(impl->config.minimum_level);
    impl->file_minimum.store(impl->config.file_level);
    impl->console_enabled.store(impl->config.console);
    impl->debug_expires.store(MonotonicMs() +
                              impl->config.debug_lifetime.count());
    auto logger = std::shared_ptr<RuntimeLogger>(new RuntimeLogger(impl));
    impl->lifetime = impl;
    try {
      impl->worker = std::thread([state = impl] { state->Run(); });
    } catch (...) {
      impl->lifetime.reset();
      impl->done = true;
      impl->closed.store(true);
      return {};
    }
    return logger;
  } catch (...) {
    return {};
  }
}

RuntimeResult RuntimeLogger::Emit(std::string_view input,
                                  std::string &result) noexcept {
  result.clear();
  try {
    const auto j = Parse(input);
    if (!Version(j) || !HasOnly(j, {"version", "level", "component", "event",
                                    "code", "fields"}))
      throw InvalidInput{};
    const auto level = ParseLevel(j.at("level"));
    if (level == Level::Off)
      throw InvalidInput{};
    const auto &component = j.at("component").get_ref<const std::string &>();
    const auto &event = j.at("event").get_ref<const std::string &>();
    const auto *definition = FindTemplate(component, event);
    if (!definition)
      throw InvalidInput{};
    const auto code = j.value("code", std::string("OK"));
    if (!ValidCode(code))
      throw InvalidInput{};
    Json fields = j.value("fields", Json::object());
    if (!fields.is_object() || fields.size() > 24)
      throw InvalidInput{};
    for (auto it = fields.begin(); it != fields.end(); ++it)
      if (!ValidField(definition->fields, it.key(), it.value()))
        throw InvalidInput{};
    return impl_->Submit(level, *definition, code, std::move(fields), false,
                         &result);
  } catch (const std::bad_alloc &) {
    return RuntimeResult::Internal;
  } catch (...) {
    ++impl_->invalid;
    return RuntimeResult::InvalidArgument;
  }
}

bool RuntimeLogger::WriteSafe(Level level, std::string_view category,
                              std::string_view event, int code,
                              const Fields &input) noexcept {
  try {
    if (level >= Level::Off)
      return false;
    struct Mapping {
      const char *category;
      const char *legacy;
      const char *component;
      const char *event;
    };
    static constexpr Mapping mappings[] = {
        {"context", "started", "sdk", "sdk.started"},
        {"context", "stopped", "sdk", "sdk.stopped"},
        {"context", "start-failed", "runtime", "runtime.failed"},
        {"logging", "configured", "logging", "logging.configured"},
        {"runtime", "loop-started", "runtime", "runtime.started"},
        {"runtime", "loop-stopped", "runtime", "runtime.stopped"},
        {"runtime", "loop-init-failed", "runtime", "runtime.failed"},
        {"runtime", "task-async-init-failed", "runtime", "runtime.failed"},
        {"runtime", "stop-async-init-failed", "runtime", "runtime.failed"},
        {"identity", "created", "identity", "identity.created"},
        {"identity", "imported", "identity", "identity.imported"},
        {"identity", "device-rotated", "identity", "identity.device_rotated"},
        {"identity", "device-revoked", "identity", "identity.device_revoked"},
        {"discovery", "started", "discovery", "discovery.started"},
        {"discovery", "stopped", "discovery", "discovery.stopped"},
        {"transport", "probe-completed", "transport",
         "transport.probe_completed"},
        {"pairing", "session-failed", "pairing", "pairing.failed"},
        {"pairing", "relationship-load-failed", "pairing",
         "pairing.load_failed"},
        {"pairing", "reconnect-request-accepted", "pairing",
         "pairing.reconnect_accepted"},
        {"pairing", "reconnect-response-accepted", "pairing",
         "pairing.reconnect_accepted"},
        {"messaging", "message-load-failed", "messaging",
         "messaging.load_failed"},
    };
    const Template *definition = FindTemplate(category, event);
    if (!definition)
      for (const auto &mapping : mappings)
        if (category == mapping.category && event == mapping.legacy) {
          definition = FindTemplate(mapping.component, mapping.event);
          break;
        }
    bool truncated = !definition;
    if (!definition)
      definition = FindTemplate("sdk", "sdk.diagnostic");
    Json fields = Json::object();
    for (std::size_t i = 0; i < std::min<std::size_t>(input.size(), 24); ++i) {
      const auto &field = input[i];
      if (field.key.size() > 32 || field.value.size() > 32) {
        truncated = true;
        continue;
      }
      Json value = field.value;
      if (field.value == "true")
        value = true;
      else if (field.value == "false")
        value = false;
      else {
        std::uint64_t number = 0;
        if (Decimal(field.value, number))
          value = number;
      }
      if (ValidField(definition->fields, field.key, value))
        fields[field.key] = std::move(value);
      else
        truncated = true;
    }
    truncated = truncated || input.size() > 24;
    return impl_->Submit(level, *definition, code == 0 ? "OK" : "ERROR",
                         std::move(fields), truncated,
                         nullptr) == RuntimeResult::Ok;
  } catch (...) {
    ++impl_->invalid;
    return false;
  }
}

RuntimeResult RuntimeLogger::Read(std::string_view input,
                                  std::string &result) const noexcept {
  result.clear();
  try {
    const auto q = Parse(input);
    if (!HasOnly(
            q, {"session", "afterSeq", "minimumLevel", "maxCount", "maxBytes"}))
      throw InvalidInput{};
    const auto session = q.value("session", std::string());
    const auto cursor = q.value("afterSeq", std::string("0"));
    std::uint64_t after = 0;
    if ((!session.empty() && !HexSession(session)) || !Decimal(cursor, after) ||
        (session.empty() && after != 0))
      throw InvalidInput{};
    const Level minimum = q.contains("minimumLevel")
                              ? ParseLevel(q["minimumLevel"])
                              : Level::Info;
    const Json count_value = q.value("maxCount", Json(std::uint64_t{200}));
    const Json bytes_value =
        q.value("maxBytes", Json(std::uint64_t{kResponseBytes}));
    if (!Unsigned(count_value, 200, 1) ||
        !Unsigned(bytes_value, kResponseBytes, kRecordBytes + kEnvelopeReserve))
      throw InvalidInput{};
    const auto count = count_value.get<std::size_t>(),
               bytes = bytes_value.get<std::size_t>();
    bool gap = !session.empty() && session != impl_->session;
    std::vector<std::shared_ptr<const Impl::Record>> records;
    std::uint64_t next = 0;
    Json stats;
    {
      std::lock_guard lock(impl_->mutex);
      if (gap || after > impl_->sequence) {
        gap = true;
        after = 0;
      }
      gap = gap || after < impl_->lost_until;
      next = after;
      std::size_t payload_bytes = kEnvelopeReserve;
      bool stopped = false;
      for (const auto &record : impl_->ring) {
        if (record->seq <= after)
          continue;
        if (record->level >= minimum) {
          if (records.size() == count ||
              payload_bytes + record->json.size() + 1 > bytes) {
            stopped = true;
            break;
          }
          records.push_back(record);
          payload_bytes += record->json.size() + 1;
        }
        next = record->seq;
      }
      if (!stopped)
        next =
            impl_->sequence; // Advance over nonmatches and file-only records.
      stats = impl_->StatsLocked();
    }
    // Serialize after releasing the producer lock. Each consumer owns its
    // response and cursor; no shared drain and no borrowed callback pointers.
    Json response = {{"session", impl_->session},
                     {"records", Json::array()},
                     {"nextCursor", std::to_string(next)},
                     {"gap", gap},
                     {"stats", std::move(stats)}};
    for (const auto &record : records)
      response["records"].push_back(Json::parse(record->json));
    result = response.dump();
    if (result.size() > bytes) {
      result.clear();
      return RuntimeResult::Internal;
    }
    return RuntimeResult::Ok;
  } catch (const std::bad_alloc &) {
    return RuntimeResult::Internal;
  } catch (...) {
    return RuntimeResult::InvalidArgument;
  }
}
std::string RuntimeLogger::Stats() const {
  Json result;
  {
    std::lock_guard lock(impl_->mutex);
    result = impl_->StatsLocked();
  }
  return result.dump();
}
const std::string &RuntimeLogger::Session() const noexcept {
  return impl_->session;
}
bool RuntimeLogger::IsOpen() const noexcept { return impl_->accepting.load(); }
bool RuntimeLogger::IsClosed() const noexcept { return impl_->closed.load(); }
bool RuntimeLogger::Reconfigure(const RuntimeConfig &config) noexcept {
  try {
    if (!ValidConfig(config) || !IsOpen() ||
        config.directory != impl_->config.directory ||
        config.role != impl_->config.role ||
        config.file_enabled != impl_->config.file_enabled)
      return false;
    impl_->debug_expires.store(MonotonicMs() +
                               impl_->config.debug_lifetime.count());
    impl_->minimum.store(config.minimum_level);
    impl_->file_minimum.store(config.file_level);
    impl_->console_enabled.store(config.console);
    return true;
  } catch (...) {
    return false;
  }
}
bool RuntimeLogger::Flush(std::chrono::milliseconds timeout) noexcept {
  try {
    const auto deadline =
        Clock::now() + std::clamp(timeout, std::chrono::milliseconds{0},
                                  std::chrono::milliseconds{30000});
    std::unique_lock flush_lock(impl_->flush_mutex, std::defer_lock);
    if (!flush_lock.try_lock_until(deadline)) {
      ++impl_->flush_timeouts;
      return false;
    }
    std::unique_lock lock(impl_->mutex, std::defer_lock);
    if (!lock.try_lock_until(deadline)) {
      ++impl_->flush_timeouts;
      return false;
    }
    if (!impl_->accepting.load())
      return false;
    const auto generation = ++impl_->flush_requested;
    impl_->flush_target = impl_->queued;
    impl_->changed.notify_all();
    const bool observed = impl_->changed.wait_until(lock, deadline, [&] {
      return impl_->flush_observed >= generation || impl_->done;
    }) && impl_->flush_observed >= generation;
    if (!observed)
      ++impl_->flush_timeouts;
    return observed && impl_->file_errors.load() == 0 &&
           impl_->console_errors.load() == 0;
  } catch (...) {
    return false;
  }
}
bool RuntimeLogger::Shutdown(std::chrono::milliseconds timeout) noexcept {
  try {
    const auto deadline =
        Clock::now() + std::clamp(timeout, std::chrono::milliseconds{0},
                                  std::chrono::milliseconds{30000});
    impl_->accepting.store(false);
    impl_->changed.notify_all();
    std::unique_lock join_lock(impl_->join_mutex, std::defer_lock);
    if (!join_lock.try_lock_until(deadline)) {
      ++impl_->shutdown_timeouts;
      return false;
    }
    if (!impl_->closed.load()) {
      std::unique_lock lock(impl_->mutex, std::defer_lock);
      if (!lock.try_lock_until(deadline) ||
          !impl_->changed.wait_until(lock, deadline,
                                     [&] { return impl_->done; })) {
        ++impl_->shutdown_timeouts;
        return false;
      }
      lock.unlock();
      if (impl_->worker.joinable())
        impl_->worker.join();
      impl_->closed.store(true);
      impl_->lifetime.reset();
    }
    return impl_->file_errors.load() == 0 && impl_->console_errors.load() == 0;
  } catch (...) {
    return false;
  }
}
#if defined(SOVKIT_RUNTIME_LOG_TESTING)
void RuntimeLogger::SetWorkerHook(std::function<void()> hook) {
  std::lock_guard lock(impl_->mutex);
  impl_->hook = std::move(hook);
}
#endif
} // namespace sovkit::log
