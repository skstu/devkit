#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace libnet {

// A local, explicitly selected source. Empty fields preserve OS routing.
// Selections are ephemeral: resolve again after a network/address change.
struct NetworkPath {
  std::string interface_name;
  std::string local_address;
  // Android Network.getNetworkHandle(), supplied by the platform host.
  // Zero on other platforms. Never a process-wide network binding.
  std::uint64_t android_network = 0;
  bool selected() const { return !interface_name.empty() || !local_address.empty() || android_network; }
};

struct NetworkPathAddress {
  std::string interface_name, local_address, family;
  bool internal = false;
};

struct NetworkPathStatus {
  bool selected = false;
  std::string state = "system", family = "auto", binding_method = "system";
};

// Local inspection only; callers must not put addresses in redacted reports.
bool ListNetworkPathAddresses(std::vector<NetworkPathAddress> &output);
bool ValidateNetworkPath(const NetworkPath &path);
// Runs before bind/connect/send on an owned socket. Failure forbids fallback.
bool PrepareNetworkPathSocket(const NetworkPath &path, std::uintptr_t socket, int family);
const char *NetworkPathBindingMethod();

} // namespace libnet
