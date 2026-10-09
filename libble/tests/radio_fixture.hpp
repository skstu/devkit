#pragma once
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <libble/ble.h>
#include <map>
#include <set>
#include <string>
#include <vector>
// Explicitly invoked synthetic transport fixture, separate from product data.
// This unique test profile cannot discover/communicate with Sovkit clients.
namespace blelab {
inline uint64_t hash(const std::string &value) {
  uint64_t h = 14695981039346656037ull;
  for (unsigned char b : value) {
    h ^= b;
    h *= 1099511628211ull;
  }
  return h;
}
class Fixture {
  struct Peer {
    bool live = true, busy = false;
    std::string remote, route;
    std::vector<uint8_t> input;
    std::deque<std::vector<uint8_t>> output;
    std::set<std::string> expected, acknowledged, received;
  };
  std::string name, nonce;
  std::function<void(const std::string &)> log;
  std::map<std::string, Peer> peers;
  uint64_t request = 0;
  bool failed = false;
  unsigned warmResets = 0;
  std::chrono::steady_clock::time_point resetAt{};
  void record(const std::string &value) { log(value); }
  void error(const std::string &value) {
    failed = true;
    record("FAIL " + value);
  }
  void queue(Peer &p, const std::string &text) {
    if (p.output.size() >= 32 || text.size() > 65556) {
      error("fixture-queue-bound");
      return;
    }
    auto size = static_cast<uint32_t>(text.size());
    std::vector<uint8_t> data = {
        static_cast<uint8_t>(size >> 24), static_cast<uint8_t>(size >> 16),
        static_cast<uint8_t>(size >> 8), static_cast<uint8_t>(size)};
    data.insert(data.end(), text.begin(), text.end());
    p.output.push_back(std::move(data));
  }
  void frame(const std::string &id, Peer &p, const std::string &text) {
    const std::string prefix = "BLE-TEST-" + nonce + "|";
    if (!text.starts_with(prefix)) {
      error("unexpected-test-payload");
      return;
    }
    const auto rest = text.substr(prefix.size());
    if (rest.starts_with("SYN|")) {
      auto separator = rest.find('|', 4);
      if (separator == std::string::npos) {
        error("bad-syn");
        return;
      }
      auto remote = rest.substr(4, separator - 4),
           route = rest.substr(separator + 1);
      const std::set<std::string> nodes = {"macos", "ios", "android", "windows",
                                           "linux"};
      if (!nodes.count(remote) || remote == name ||
          (!p.route.empty() && p.route != route)) {
        error("cross-route-syn");
        return;
      }
      p.remote = remote;
      p.route = route;
      record("PEER " + remote + " link=" + id);
      return;
    }
    if (rest.starts_with("ACK|")) {
      auto digest = rest.substr(4);
      if (!p.expected.count(digest)) {
        error("cross-route-or-unknown-ack");
        return;
      }
      p.acknowledged.insert(digest);
      record("ACK " + p.remote + " hash=" + digest);
      return;
    }
    if (!rest.starts_with("DATA|" + p.route + "|") || p.route.empty()) {
      error("cross-route-data");
      return;
    }
    auto digest = std::to_string(hash(text));
    if (!p.received.insert(digest).second) {
      error("duplicate-test-payload");
      return;
    }
    record("RECEIVE " + p.remote + " bytes=" + std::to_string(text.size()) +
           " hash=" + digest);
    queue(p, prefix + "ACK|" + digest);
  }
  static void callback(void *user, const dkble_event *e) {
    static_cast<Fixture *>(user)->event(e);
  }
  void event(const dkble_event *e) {
    if (e->type == DKBLE_EVENT_DIAGNOSTIC)
      record(std::string("DIAGNOSTIC ") + e->detail);
    if (e->type == DKBLE_EVENT_STATE)
      record(std::string("STATE ") + e->detail);
    if (e->type == DKBLE_EVENT_ERROR) {
      error(std::string("native-error ") + e->detail +
            " status=" + std::to_string(e->status));
      return;
    }
    if (e->type == DKBLE_EVENT_CANDIDATE) {
      auto result = dkble_connect(context, e->peer);
      if (result && result != DKBLE_BUSY)
        record("CONNECT-RESULT " + std::to_string(result));
    }
    if (e->type == DKBLE_EVENT_LINK) {
      Peer p;
      auto &entry = peers[e->link] = std::move(p);
      std::string prefix = "BLE-TEST-" + nonce + "|";
      queue(entry, prefix + "SYN|" + name + "|" + e->link);
      for (auto size : {128u, 512u, 1024u}) {
        std::string text = prefix + "DATA|" + e->link + "|蓝牙短消息|" +
                           std::to_string(size) + "|";
        if (text.size() < size)
          text.resize(size, 'x');
        entry.expected.insert(std::to_string(hash(text)));
        queue(entry, text);
      }
      record(std::string("LINK ") + (e->initiator ? "central" : "peripheral") +
             " id=" + e->link);
    }
    auto found = peers.find(e->link);
    if (found == peers.end())
      return;
    auto &p = found->second;
    if (e->type == DKBLE_EVENT_DISCONNECTED) {
      p.live = false;
      record("DISCONNECTED " + p.remote);
      return;
    }
    if (e->type == DKBLE_EVENT_SEND_COMPLETE) {
      p.busy = false;
      if (e->status)
        error("send-failed " + std::to_string(e->status));
    }
    if (e->type == DKBLE_EVENT_DATA) {
      if (!e->size || p.input.size() + e->size > 131120) {
        error("fixture-ingress-bound");
        return;
      }
      p.input.insert(p.input.end(), e->data, e->data + e->size);
      while (p.input.size() >= 4) {
        auto n = (uint32_t(p.input[0]) << 24) | (uint32_t(p.input[1]) << 16) |
                 (uint32_t(p.input[2]) << 8) | p.input[3];
        if (n < 1 || n > 65556) {
          error("bad-frame-length");
          return;
        }
        if (p.input.size() < n + 4)
          break;
        std::string text(p.input.begin() + 4, p.input.begin() + 4 + n);
        p.input.erase(p.input.begin(), p.input.begin() + 4 + n);
        frame(e->link, p, text);
      }
    }
  }

public:
  dkble_context *context = nullptr;
  Fixture(std::string node, std::string round,
          std::function<void(const std::string &)> output, unsigned resets = 0)
      : name(std::move(node)), nonce(std::move(round)), log(std::move(output)), warmResets(resets) {
  }
  ~Fixture() {
    if (context)
      dkble_destroy(context);
  }
  bool start(bool advertise) {
    auto roundId = nonce.substr(nonce.rfind('-') + 1);
    if (roundId.size() < 2 || roundId[0] != 'r') {
      error("invalid-round");
      return false;
    }
    auto suffix = std::to_string(std::stoul(roundId.substr(1)));
    if (suffix.size() > 8) {
      error("invalid-round");
      return false;
    }
    suffix = std::string(8 - suffix.size(), '0') + suffix;
    const auto service = "9fc46100-1e87-467a-8cda-f1be" + suffix;
    const auto receive = "9fc46101-1e87-467a-8cda-f1be" + suffix;
    const auto notify = "9fc46102-1e87-467a-8cda-f1be" + suffix;
    dkble_config config{
        sizeof(config), 1,       0,      0, service.c_str(), receive.c_str(),
        notify.c_str(), nullptr, nullptr};
    int status = dkble_create(&config, &context);
    if (status) {
      error("create " + std::to_string(status));
      return false;
    }
    dkble_options options{sizeof(options), 1, 20000, 25000, 3000, 30000, 4, 64};
    if (dkble_set_options(context, &options) != 0) {
      error("configure");
      return false;
    }
    status = dkble_start(context,
                         advertise ? DKBLE_MODE_ADVERTISE : DKBLE_MODE_SCAN);
    if (status) {
      error("start " + std::to_string(status));
      return false;
    }
    record("START node=" + name +
           " role=" + (advertise ? "advertise" : "scan") + " round=" + nonce);
    return true;
  }
  void step() {
    if (!context)
      return;
    auto count = dkble_dispatch(context, 256, callback, this);
    if (count < 0)
      error("dispatch " + std::to_string(count));
    for (auto &[id, p] : peers)
      if (p.live && !p.busy && !p.output.empty()) {
        auto &data = p.output.front();
        auto result = dkble_send(context, id.c_str(), data.data(), data.size(),
                                 ++request);
        if (result == 0) {
          p.busy = true;
          p.output.pop_front();
        } else if (result != DKBLE_BUSY)
          error("send-admission " + std::to_string(result));
      }
  }
    // Reset only completed synthetic sessions, after a grace turn for peer
    // dispatch. No product identities or BLE implementation are embedded here.
  void warmStep() {
    if (!warmResets || failed) return;
    std::vector<std::string> routes;
    for (auto &[id, p] : peers) if (p.live) {
      if (p.expected != p.acknowledged || p.received.size() != 3 ||
          !p.output.empty() || p.busy) return;
      routes.push_back(id);
    }
    if (routes.empty()) return;
    const auto now = std::chrono::steady_clock::now();
    if (resetAt == std::chrono::steady_clock::time_point{}) {
      resetAt = now + std::chrono::milliseconds(500); return;
    }
    if (now < resetAt) return;
    --warmResets;
    resetAt = {};
    for (auto &id : routes) {
      const auto status = dkble_disconnect(context, id.c_str(), 1);
      record("WARM_RESET link=" + id + " status=" + std::to_string(status));
      if (status) error("warm-reset-admission");
    }
  }
  int finish() {
    size_t passed = 0;
    for (auto &[id, p] : peers) {
      bool ok = p.expected == p.acknowledged && p.received.size() == 3 &&
                p.output.empty() && !p.busy;
      record(std::string(ok ? "PASS" : "INCOMPLETE") + " peer=" + p.remote +
             " sentAck=" + std::to_string(p.acknowledged.size()) +
             " received=" + std::to_string(p.received.size()));
      if (ok)
        ++passed;
    }
    if (context) {
      dkble_stop(context);
      dkble_dispatch(context, 256, callback, this);
      dkble_destroy(context);
      context = nullptr;
    }
    record("RESULT node=" + name + " passed=" + std::to_string(passed) +
           " links=" + std::to_string(peers.size()) +
           " failed=" + (failed ? "1" : "0"));
    if (warmResets) record("INCOMPLETE warm-resets=" + std::to_string(warmResets));
    return failed || !passed || passed != peers.size() || warmResets ? 1 : 0;
  }
};
} // namespace blelab
