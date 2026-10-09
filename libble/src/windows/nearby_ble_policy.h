#ifndef DEVKIT_BLE_WINDOWS_POLICY_HPP_
#define DEVKIT_BLE_WINDOWS_POLICY_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nearby_ble {
constexpr size_t kMaximumFrame = 65560;
constexpr size_t kMaximumLinks = 4;
constexpr size_t kMaximumCandidates = 64;

inline size_t FragmentSize(uint16_t pdu, size_t remaining) {
  return std::min(
      {remaining, size_t{512},
       static_cast<size_t>(std::max(23, static_cast<int>(pdu)) - 3)});
}

// A public advertisement may locate a lost peer, never revoke a live link.
inline bool CanMoveHint(const std::string &old_peer,
                        const std::string &new_peer, bool old_link_ready) {
  return old_peer != new_peer && !old_link_ready;
}
} // namespace nearby_ble
#endif
