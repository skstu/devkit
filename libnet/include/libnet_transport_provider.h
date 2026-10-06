#ifndef LIBNET_TRANSPORT_PROVIDER_H_
#define LIBNET_TRANSPORT_PROVIDER_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace libnet {

enum class TransportKind : std::uint8_t { udp, quic };
enum class RouteScope : std::uint8_t { unknown, lan, wan };
enum class TrafficClass : std::uint8_t { control, interactive, bulk };

enum class TransportCapability : std::uint32_t {
  datagram = 1U << 0U,
  reliable_stream = 1U << 1U,
  ordered = 1U << 2U,
  congestion_control = 1U << 3U,
  path_migration = 1U << 4U,
  multipath = 1U << 5U,
  ipv4 = 1U << 6U,
  ipv6 = 1U << 7U,
};

constexpr std::uint32_t Capability(TransportCapability value) {
  return static_cast<std::uint32_t>(value);
}

struct TransportProviderDescriptor {
  std::string provider_id;
  TransportKind transport = TransportKind::udp;
  std::uint32_t capabilities = 0;
  std::size_t maximum_message_bytes = 0;
  std::size_t preferred_frame_bytes = 0;
  int priority = 0;
  bool available = false;
  bool production_ready = false;
  std::string status;

  bool Has(TransportCapability capability) const {
    return (capabilities & Capability(capability)) != 0;
  }
};

struct RouteRequirements {
  RouteScope scope = RouteScope::unknown;
  TrafficClass traffic = TrafficClass::control;
  bool require_reliable_stream = false;
  bool require_path_migration = false;
  bool allow_nonproduction = false;
};

struct TransportSelection {
  TransportProviderDescriptor provider;
  std::string profile;
  bool application_reliability = false;
};

/// A deterministic, side-effect-free capability catalog. It does not own
/// sockets and never sees SovKit identities, relationships, or plaintext.
/// Concrete providers must be available before selection; planned providers
/// remain visible for diagnostics but can never be selected accidentally.
class TransportProviderCatalog final {
public:
  bool Register(TransportProviderDescriptor descriptor);
  const std::vector<TransportProviderDescriptor> &providers() const {
    return providers_;
  }
  std::optional<TransportSelection>
  Select(const RouteRequirements &requirements) const;

private:
  std::vector<TransportProviderDescriptor> providers_;
};

TransportProviderCatalog BuiltinTransportProviderCatalog();
std::string_view TransportName(TransportKind value);
std::string_view RouteScopeName(RouteScope value);
std::string_view TrafficClassName(TrafficClass value);

} // namespace libnet

#endif // LIBNET_TRANSPORT_PROVIDER_H_
