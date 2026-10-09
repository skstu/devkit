#pragma once
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace dkble::read_session {
using Epoch = std::array<uint8_t, 8>;
constexpr size_t header = 9;
constexpr uint8_t idle = 0, payload = 1, hello = 2, write = 3;
inline bool valid(const Epoch &epoch) {
  return std::any_of(epoch.begin(), epoch.end(), [](auto b) { return b != 0; });
}
inline std::vector<uint8_t> packet(uint8_t kind, const Epoch &epoch,
                                   std::span<const uint8_t> bytes = {}) {
  std::vector<uint8_t> result(header + bytes.size());
  result[0] = kind;
  std::copy(epoch.begin(), epoch.end(), result.begin() + 1);
  std::copy(bytes.begin(), bytes.end(), result.begin() + header);
  return result;
}
inline bool parse(std::span<const uint8_t> bytes, uint8_t &kind, Epoch &epoch) {
  if (bytes.size() < header || bytes.size() > 512)
    return false;
  kind = bytes[0];
  std::copy_n(bytes.begin() + 1, epoch.size(), epoch.begin());
  return valid(epoch) &&
      ((kind == idle || kind == hello) ? bytes.size() == header :
       (kind == payload || kind == write) && bytes.size() > header);
}
// Epochs separate byte streams, not identities. Retired epochs cannot reopen
// a stream on the same physical device. Bounds fail closed until Disconnect.
class Registry {
  struct Session { Epoch current{}; std::set<Epoch> seen; };
  std::map<std::string, Session> sessions;
public:
  enum Result { fresh, current, rejected };
  Result accept(const std::string &peer, const Epoch &epoch, size_t maximumPeers) {
    if (!valid(epoch)) return rejected;
    auto found = sessions.find(peer);
    if (found == sessions.end()) {
      if (sessions.size() >= maximumPeers) return rejected;
      found = sessions.emplace(peer, Session{}).first;
    }
    auto &session = found->second;
    if (epoch == session.current) return current;
    if (session.seen.count(epoch) || session.seen.size() >= 64) return rejected;
    session.seen.insert(epoch);
    session.current = epoch;
    return fresh;
  }
  void retire(const std::string &peer, const Epoch &epoch) {
    if (auto it = sessions.find(peer); it != sessions.end() && it->second.current == epoch)
      it->second.current = {};
  }
  void disconnected(const std::string &peer) { sessions.erase(peer); }
  void clear() { sessions.clear(); }
};
} // namespace dkble::read_session
