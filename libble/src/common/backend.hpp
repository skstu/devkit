#pragma once
#include <cstdint>
#include <functional>
#include <libble/ble.h>
#include <memory>
#include <string>
#include <vector>
namespace dkble {
struct Event {
  uint32_t type = 0;
  uint64_t generation = 0, request = 0;
  int32_t status = 0, rssi = 0;
  bool initiator = false;
  std::string link, peer, probe, detail;
  std::vector<uint8_t> data;
};
struct Config {
  std::string service, receive, notify;
  bool hints = false;
  uint32_t readInterval = 50;
  dkble_options options{
      sizeof(dkble_options), 1, 20000, 25000, 3000, 30000, 4, 64};
};
enum class Op {
  startScan,
  startAdvertise,
  stop,
  connect,
  probe,
  cancel,
  adopt,
  disconnect,
  reset,
  send,
  recover
};
struct Command {
  Op op;
  uint64_t generation = 0, request = 0;
  std::string value, extra;
  std::vector<uint8_t> data;
};
using Emit = std::function<void(Event)>;
using Wake = std::function<void()>;
class Backend {
public:
  virtual ~Backend() = default;
  // A successful stop is a completion barrier: native routes are retired,
  // queued work is fenced, and no old command can restart the transport.
  // Other commands may complete asynchronously through events.
  virtual int command(Command) = 0;
  virtual void poll() {} // bounded/nonblocking; owner thread
  virtual int set_read_interval(uint32_t) = 0;
  virtual int set_options(const dkble_options &) = 0;
  virtual void close() = 0;
};
std::unique_ptr<Backend> make_backend(Config, Emit, Wake);
} // namespace dkble
