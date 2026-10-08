// Extracted and generalized from tdbrg's node FIX transport (Apache-2.0).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <quickfix/Application.h>
#include <quickfix/Dictionary.h>
#include <quickfix/Log.h>
#include <quickfix/MessageStore.h>
#include <quickfix/Session.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace libfix {
enum class Role { initiator, acceptor };
struct SessionConfig {
  FIX::SessionID id;
  FIX::Dictionary settings;
};
struct Limits {
  size_t connections = 128;
  size_t frame_bytes = 65536;
  size_t peer_queue_bytes = 1024 * 1024;
  size_t total_queue_bytes = 32 * 1024 * 1024;
  size_t pending_tasks = 128;
  uint64_t connect_timeout_ms = 4000;
  uint64_t handshake_timeout_ms = 3000;
  uint64_t inbound_idle_ms = 30000;
  uint64_t session_tick_ms = 100;
};
struct Options {
  Role role = Role::initiator;
  std::string host = "127.0.0.1";
  uint16_t port = 0; // Acceptor may request an ephemeral port with zero.
  std::string certificate, private_key, certificate_authorities;
  std::string peer_name;       // Defaults to host; both DNS and IP identities are verified.
  std::string store_directory; // Absolute, exclusively leased when FileStore is used.
  std::map<std::string, std::string> dictionaries; // Basename -> reviewed XML bytes.
  std::vector<SessionConfig> sessions;
  std::shared_ptr<FIX::MessageStoreFactory> stores; // Optional consumer-owned store policy.
  std::shared_ptr<FIX::LogFactory> logs;
  Limits limits;
  uint64_t application_tick_ms = 0;
  uint64_t reconnect_min_ms = 0; // Disabled by default. Never queues business commands for replay.
  uint64_t reconnect_max_ms = 30000;
};
struct Event {
  std::string session; // Empty before a configured acceptor session is identified.
  std::string reason;  // Transport diagnostics contain no message bodies or credentials.
};
struct Callbacks {
  std::function<void(const Event &)> disconnected;
  std::function<void()> tick;
};
struct Metrics {
  uint64_t received_bytes = 0, sent_bytes = 0, received_messages = 0;
  uint64_t connections = 0, reconnects = 0, rejected_tasks = 0;
  size_t queued_bytes = 0, peak_queued_bytes = 0;
};

// One bounded reactor for all configured sessions. All FIX callbacks, session()
// access, and posted work execute on that reactor. No callbacks survive destruction.
// Application/store callbacks must return promptly; destruction is owner-thread only.
// send() acceptance is local admission, never remote receipt or business completion.
class Endpoint final {
public:
  Endpoint(FIX::Application &application, Options options, Callbacks callbacks = {});
  ~Endpoint();
  Endpoint(const Endpoint &) = delete;
  Endpoint &operator=(const Endpoint &) = delete;
  void start();
  void stop(); // Idempotent owner-thread join; call before releasing callback-owned pointers.
  bool post(std::function<void()> work);
  template <class F> auto call(F work) -> decltype(work()) {
    if (on_loop())
      return work();
    auto task = std::make_shared<std::packaged_task<decltype(work())()>>(std::move(work));
    auto result = task->get_future();
    if (!enqueue([task] { (*task)(); }, true))
      throw std::runtime_error("FIX reactor is full or closed");
    return result.get();
  }
  FIX::Session &session(const FIX::SessionID &id); // Reactor thread only.
  void disconnect(const FIX::SessionID &id, std::string reason = "local close"); // Reactor only.
  uint16_t port();
  Metrics metrics();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool on_loop() const;
  bool enqueue(std::function<void()> work, bool control);
};
} // namespace libfix
