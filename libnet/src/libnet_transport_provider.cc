#include "libnet_transport_provider.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace libnet {
namespace {

bool ValidProviderId(std::string_view value) {
  if (value.empty() || value.size() > 64)
    return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '-' ||
           character == '.';
  });
}

int Score(const TransportProviderDescriptor &provider,
          const RouteRequirements &requirements) {
  int score = provider.priority;
  if (provider.production_ready)
    score += 1000;
  if (requirements.traffic == TrafficClass::bulk) {
    if (provider.Has(TransportCapability::reliable_stream))
      score += 300;
    if (provider.Has(TransportCapability::congestion_control))
      score += 200;
  } else if (provider.Has(TransportCapability::datagram)) {
    score += 150;
  }
  if (requirements.scope == RouteScope::wan &&
      provider.Has(TransportCapability::path_migration))
    score += 180;
  if (provider.Has(TransportCapability::ipv6))
    score += 10;
  return score;
}

} // namespace

bool TransportProviderCatalog::Register(
    TransportProviderDescriptor descriptor) {
  if (!ValidProviderId(descriptor.provider_id) || descriptor.status.empty() ||
      (descriptor.available && descriptor.capabilities == 0) ||
      (descriptor.available && descriptor.maximum_message_bytes == 0))
    return false;
  const auto duplicate =
      std::find_if(providers_.begin(), providers_.end(), [&](const auto &item) {
        return item.provider_id == descriptor.provider_id;
      });
  if (duplicate != providers_.end())
    return false;
  providers_.push_back(std::move(descriptor));
  std::sort(providers_.begin(), providers_.end(),
            [](const auto &left, const auto &right) {
              return left.provider_id < right.provider_id;
            });
  return true;
}

std::optional<TransportSelection>
TransportProviderCatalog::Select(const RouteRequirements &requirements) const {
  const TransportProviderDescriptor *best = nullptr;
  int best_score = std::numeric_limits<int>::min();
  for (const TransportProviderDescriptor &provider : providers_) {
    if (!provider.available ||
        (!provider.production_ready && !requirements.allow_nonproduction) ||
        (requirements.require_reliable_stream &&
         !provider.Has(TransportCapability::reliable_stream)) ||
        (requirements.require_path_migration &&
         !provider.Has(TransportCapability::path_migration)))
      continue;
    const int score = Score(provider, requirements);
    if (best == nullptr || score > best_score ||
        (score == best_score && provider.provider_id < best->provider_id)) {
      best = &provider;
      best_score = score;
    }
  }
  if (best == nullptr)
    return std::nullopt;
  TransportSelection selection;
  selection.provider = *best;
  selection.profile = std::string(RouteScopeName(requirements.scope)) + "-" +
                      std::string(TrafficClassName(requirements.traffic)) +
                      "-" + std::string(TransportName(best->transport));
  selection.application_reliability =
      requirements.traffic == TrafficClass::bulk &&
      !best->Has(TransportCapability::reliable_stream);
  return selection;
}

TransportProviderCatalog BuiltinTransportProviderCatalog() {
  TransportProviderCatalog catalog;
  const std::uint32_t ip = Capability(TransportCapability::ipv4) |
                           Capability(TransportCapability::ipv6);
  catalog.Register({"libuv-udp", TransportKind::udp,
                    Capability(TransportCapability::datagram) | ip, 65507, 8192,
                    100, true, true, "ready"});
  // ngtcp2 is the selected prototype candidate because it has a C ABI, a
  // vcpkg port, and does not force a Rust toolchain into every SDK host. This
  // The provider is compiled and usable as an authenticated session upgrade.
  // It remains non-production until physical-device lifecycle, impairment,
  // migration, and benchmark gates pass; the selector therefore retains UDP
  // unless the live session has completed the relationship binding.
  catalog.Register({"ngtcp2-quic", TransportKind::quic,
                    Capability(TransportCapability::datagram) |
                        Capability(TransportCapability::reliable_stream) |
                        Capability(TransportCapability::ordered) |
                        Capability(TransportCapability::congestion_control) |
                        ip,
                    65556, 8192, 300, true, false, "authenticated_preview"});
  return catalog;
}

std::string_view TransportName(TransportKind value) {
  switch (value) {
  case TransportKind::udp:
    return "udp";
  case TransportKind::quic:
    return "quic";
  }
  return "unknown";
}

std::string_view RouteScopeName(RouteScope value) {
  switch (value) {
  case RouteScope::lan:
    return "lan";
  case RouteScope::wan:
    return "wan";
  case RouteScope::unknown:
    return "unknown";
  }
  return "unknown";
}

std::string_view TrafficClassName(TrafficClass value) {
  switch (value) {
  case TrafficClass::control:
    return "control";
  case TrafficClass::interactive:
    return "interactive";
  case TrafficClass::bulk:
    return "bulk";
  }
  return "control";
}

} // namespace libnet
