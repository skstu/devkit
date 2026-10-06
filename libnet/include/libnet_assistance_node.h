#pragma once
#include "libnet_network_path.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

struct juice_server;

namespace libnet {

// A bounded TURN server shared by every SDK host. Control methods serialize;
// the forwarding callback never calls a host callback or takes this mutex.
class AssistanceNode final {
public:
  struct Config {
    std::string bind_address;
    std::string external_address;
    NetworkPath network_path;
    std::uint16_t port = 0;
    std::uint64_t byte_limit = 64 * 1024 * 1024;
    std::uint64_t bytes_per_second = 128 * 1024;
    // Optional atomic-only SDK policy check. Never invoke a platform/keystore.
    std::function<bool()> can_forward;
  };
  struct Stats {
    bool running = false;
    std::uint16_t port = 0;
    std::uint64_t charged_bytes = 0;
    std::uint64_t dropped_packets = 0;
    bool exhausted = false;
    std::size_t grants = 0;
  };
  ~AssistanceNode();
  bool Start(const Config &config);
  void Stop();
  bool Grant(const std::string &username, const std::string &password,
             std::uint32_t lifetime_seconds);
  void Revoke(const std::string &username);
  Stats Snapshot() const;
  void SetAllowed(bool allowed) { allowed_ = allowed; }

private:
  static bool Charge(std::size_t bytes, void *self);
  static int Prepare(std::uintptr_t socket, int family, void *self);
  mutable std::mutex mutex_;
  juice_server *server_ = nullptr;
  std::uint16_t port_ = 0;
  Config config_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> grants_;
  std::atomic<std::uint64_t> charged_{0}, dropped_{0};
  std::atomic<bool> allowed_{true};
  // Accessed exclusively by the TURN thread after Start.
  std::chrono::steady_clock::time_point refill_;
  double tokens_ = 0;
};

} // namespace libnet
