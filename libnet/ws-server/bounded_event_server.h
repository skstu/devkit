#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace libnet {
// Single-loop ownership. All methods except construction/destruction are called
// by tick, on the owning loop. No socket pointer or send closure escapes it.
class BoundedEventServer {
 public:
  struct Stats { unsigned connections, pending, tickets; size_t buffered, peak_buffered; };
  BoundedEventServer(int port, std::string origin);
  ~BoundedEventServer();
  int port() const;
  void run(std::function<void()> tick);
  void stop();
  void revoke();
  bool ticket(std::string key, std::string generation, int64_t deadline_ns, std::string initial);
  bool publish(const std::string& generation, const std::string& frame);
  Stats stats() const;
  static int64_t now();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
}
