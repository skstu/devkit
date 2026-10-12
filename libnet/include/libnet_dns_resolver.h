#pragma once
#include "libnet_network_path.h"
#include <chrono>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace libnet {
// Numeric resolvers from the local OS; never received from the remote peer.
std::vector<std::string> SystemDnsServers();
// RFC 2544 / RFC 5180 benchmark ranges commonly used by proxy fake DNS.
bool IsSyntheticDnsAddress(std::string_view address);
// A bounded UDP lookup on an explicitly bound local path. No process-wide DNS
// or proxy changes, cache across networks, TCP retry, or unbound fallback.
std::vector<std::string> ResolveDnsOnPath(
    std::string_view host, const NetworkPath& path,
    const std::vector<std::string>& servers, std::chrono::milliseconds budget,
    const std::function<bool()>& cancelled = {});
namespace dns {
bool MatchesResponse(std::span<const unsigned char> packet, std::string_view host,
                     unsigned short transaction, unsigned short type);
// Exposed for packet validation tests; accepts only the matching question and
// its answer/CNAME chain, not unrelated additional records.
std::vector<std::string> AddressAnswers(std::span<const unsigned char> packet,
    std::string_view host, unsigned short transaction, unsigned short type);
}
} // namespace libnet
