#include "libnet_network_path.h"
#include "libnet_uv.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <tuple>

#ifdef __ANDROID__
#include <android/multinetwork.h>
#elif !defined(_WIN32)
#include <net/if.h>
#else
#include <netioapi.h>
#endif

namespace libnet {

bool ListNetworkPathAddresses(std::vector<NetworkPathAddress> &output) {
  output.clear();
  uv_interface_address_t *interfaces = nullptr;
  int count = 0;
  if (uv_interface_addresses(&interfaces, &count) != 0) return false;
  for (int i = 0; i < count && output.size() < 256; ++i) {
    const auto &entry = interfaces[i];
    const auto *address = reinterpret_cast<const sockaddr *>(&entry.address);
    const int size = address->sa_family == AF_INET ? sizeof(sockaddr_in) :
                     address->sa_family == AF_INET6 ? sizeof(sockaddr_in6) : 0;
    if (!entry.name || !size) continue;
    const auto endpoint = Endpoint::FromSockaddr(address, size);
    if (!endpoint) continue;
    const auto text = endpoint->address();
    if (address->sa_family == AF_INET6) {
      const auto *v6 = reinterpret_cast<const sockaddr_in6 *>(address);
      if (IN6_IS_ADDR_LINKLOCAL(&v6->sin6_addr) || IN6_IS_ADDR_MULTICAST(&v6->sin6_addr) ||
          IN6_IS_ADDR_UNSPECIFIED(&v6->sin6_addr) || IN6_IS_ADDR_V4MAPPED(&v6->sin6_addr)) continue;
    } else {
      const auto ip = ntohl(reinterpret_cast<const sockaddr_in *>(address)->sin_addr.s_addr);
      if (!ip || ip >= 0xe0000000U || (ip >> 16) == 0xa9fe) continue;
    }
    output.push_back({entry.name, text, address->sa_family == AF_INET ? "ipv4" : "ipv6",
                      entry.is_internal != 0});
  }
  uv_free_interface_addresses(interfaces, count);
  std::sort(output.begin(), output.end(), [](const auto &a, const auto &b) {
    return std::tie(a.interface_name, a.family, a.local_address) <
           std::tie(b.interface_name, b.family, b.local_address);
  });
  output.erase(std::unique(output.begin(), output.end(), [](const auto &a, const auto &b) {
    return a.interface_name == b.interface_name && a.local_address == b.local_address;
  }), output.end());
  return true;
}

bool ValidateNetworkPath(const NetworkPath &path) {
  if (!path.selected()) return true;
  if (path.interface_name.empty() || path.interface_name.size() > 255 ||
      path.interface_name.find('\0') != std::string::npos || path.local_address.empty() ||
      path.local_address.size() > 64 || path.local_address.find('\0') != std::string::npos)
    return false;
#ifdef __ANDROID__
  if (!path.android_network) return false;
#else
  if (path.android_network) return false;
#endif
  const auto endpoint = Endpoint::Parse(path.local_address, 0);
  if (!endpoint) return false;
  std::vector<NetworkPathAddress> addresses;
  if (!ListNetworkPathAddresses(addresses)) return false;
  return std::any_of(addresses.begin(), addresses.end(), [&](const auto &address) {
    return address.interface_name == path.interface_name && address.local_address == endpoint->address();
  });
}

const char *NetworkPathBindingMethod() {
#if defined(__ANDROID__)
  return "android-network";
#elif defined(__APPLE__)
  return "bound-interface";
#elif defined(_WIN32)
  return "unicast-interface";
#elif defined(__linux__)
  return "bound-device";
#else
  return "unsupported";
#endif
}

bool PrepareNetworkPathSocket(const NetworkPath &path, std::uintptr_t socket, int family) {
  if (!path.selected()) return true;
  if (!ValidateNetworkPath(path)) return false;
  const auto address = Endpoint::Parse(path.local_address, 0);
  if (!address || (address->family() == AddressFamily::ipv4 ? AF_INET : AF_INET6) != family)
    return false;
#ifdef __ANDROID__
  if (socket > static_cast<std::uintptr_t>(std::numeric_limits<int>::max())) return false;
  return android_setsocknetwork(path.android_network, static_cast<int>(socket)) == 0;
#elif defined(__APPLE__)
  const auto index = if_nametoindex(path.interface_name.c_str());
  if (!index || socket > static_cast<std::uintptr_t>(std::numeric_limits<int>::max())) return false;
  return setsockopt(static_cast<int>(socket), family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                   family == AF_INET ? IP_BOUND_IF : IPV6_BOUND_IF, &index, sizeof(index)) == 0;
#elif defined(_WIN32)
  NET_LUID luid{};
  NET_IFINDEX index = 0;
  wchar_t alias[256]{};
  // libuv reports FriendlyName (an alias), not the internal adapter name.
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.interface_name.c_str(),
                          -1, alias, 256) ||
      ConvertInterfaceAliasToLuid(alias, &luid) != NO_ERROR ||
      ConvertInterfaceLuidToIndex(&luid, &index) != NO_ERROR || !index) return false;
  const DWORD value = family == AF_INET ? htonl(index) : index;
  return setsockopt(static_cast<SOCKET>(socket), family == AF_INET ? IPPROTO_IP : IPPROTO_IPV6,
                   family == AF_INET ? IP_UNICAST_IF : IPV6_UNICAST_IF,
                   reinterpret_cast<const char *>(&value), sizeof(value)) == 0;
#elif defined(__linux__)
  if (socket > static_cast<std::uintptr_t>(std::numeric_limits<int>::max())) return false;
  return setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_BINDTODEVICE,
                   path.interface_name.c_str(), static_cast<socklen_t>(path.interface_name.size() + 1)) == 0;
#else
  return false;
#endif
}

} // namespace libnet
