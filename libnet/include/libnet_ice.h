#ifndef LIBNET_ICE_H_
#define LIBNET_ICE_H_

#include "libnet_network_path.h"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace libnet {

enum class IceState : std::uint8_t {
  disconnected,
  gathering,
  connecting,
  connected,
  completed,
  failed,
};

struct TurnServer {
  std::string host;
  std::uint16_t port = 3478;
  std::string username;
  std::string password;
};

enum class IceAddressPolicy : std::uint8_t { automatic, ipv4, ipv6 };

// Numeric IPv4, bound local interface, gateway on that interface's subnet.
bool IsOnLinkGateway(std::string_view local_address, std::string_view gateway);

struct IceConfig {
  // Explicit alternative bearer. UDP remains the default; TCP is not ICE-TCP.
  bool tcp_direct = false;
  // Local coordinator ticket/role, set by the SDK from the signed generation.
  std::string tcp_round;
  bool tcp_offerer = false;
  // Restricts advertised/imported candidates and permitted application paths.
  // This is not an OS-wide restriction on DNS or STUN server traffic.
  IceAddressPolicy address_policy = IceAddressPolicy::automatic;
  std::string stun_host;
  std::uint16_t stun_port = 3478;
  std::vector<TurnServer> turn_servers;
  std::string bind_address;
  // Opt-in source + interface/network binding for the actual ICE socket.
  // One selected source constrains this route to its address family.
  NetworkPath network_path;
  // Host supplies an inventory, never received from a peer. At most four
  // sockets are live; managed transports may rebuild them on inventory changes.
  bool manage_network_paths = false;
  std::vector<NetworkPath> network_paths;
  // Opt-in PCP v2 MAP for this bound IPv4 ICE socket, UDP 5351, 120s lease.
  // Explicit on-link gateway only; empty disables. No automatic gateway writes.
  std::string pcp_gateway;
  std::uint16_t port_range_begin = 0;
  std::uint16_t port_range_end = 0;
  bool relay_only = false;
  // Default false. Product relay requires an SDK-validated assistance ticket.
  bool allow_relay = false;
  std::string assistance_ticket;
  bool endpoint_diagnostics = false; // opt-in, bounded STUN endpoint aggregates
  // Explicit consent: one six-sample A/B/A/B/A/B run on this ICE UDP socket.
  // Zero disables. Requires IPv4-only, numeric STUN IPv4, distinct >=1024 ports.
  std::uint16_t mapping_second_port = 0;
  // Isolated diagnostic agent: no mapping probe or remote candidate import.
  std::uint16_t filtering_second_port = 0;
  // Called on libjuice's thread under the queue lock. Must only signal the
  // consumer (never re-enter this agent). Owner must guard wake-handle lifetime.
  std::function<void()> event_ready;
};

struct IceQueueStats {
  std::uint64_t dropped_datagrams = 0;
  std::size_t peak_events = 0;
};

// Counts only: no addresses, ICE credentials or SDP escape through diagnostics.
struct IceCandidateCounts {
  std::size_t host = 0, srflx = 0, prflx = 0, relay = 0;
  std::size_t ipv4 = 0, ipv6 = 0;
  std::size_t global_ipv4 = 0, global_ipv6 = 0, link_local = 0;
  std::size_t private_ipv4 = 0, unique_local_ipv6 = 0, other = 0;
};

struct IceCheckStats {
  bool available = false;
  std::uint64_t requests_sent = 0, send_errors = 0, requests_received = 0;
  std::uint64_t responses_received = 0, responses_sent = 0;
  std::uint64_t validation_failures = 0, check_timeouts = 0;
  std::uint64_t pairs_succeeded = 0, role_conflicts = 0, non_symmetric_responses = 0;
  std::uint64_t udp_datagrams_received = 0;
  std::uint64_t stun_datagrams_received = 0;
  std::uint64_t non_stun_datagrams_received = 0;
  std::uint64_t early_state_drops = 0;
  std::uint64_t malformed_stun_drops = 0;
  std::uint64_t binding_requests_observed = 0;
  std::uint64_t binding_responses_observed = 0;
  std::uint64_t unmatched_transaction_drops = 0;
  std::uint64_t unmatched_address_drops = 0;
  std::uint64_t missing_integrity_drops = 0;
  std::int64_t age_ms = -1, remote_description_age_ms = -1;
};

struct IceGatherStats {
  IceCandidateCounts local, remote;
  bool complete = false;
  bool signaling_ready = false;
  bool remote_description_set = false;
};

// Explicit local troubleshooting only. Never include these addresses in the
// default redacted report. No SDP credentials or arbitrary attributes escape.
struct IceCandidateEndpoint {
  std::string address, type;
  std::uint16_t port = 0;
};
struct IceCaptureEndpoints {
  std::vector<IceCandidateEndpoint> local, remote;
};

struct IceEndpointObservation {
  std::string address, family, kind;
  std::uint16_t port = 0;
  int send_status = 0;
  std::uint64_t count = 0, bytes = 0, first_ms = 0, last_ms = 0;
};
struct IceEndpointStats {
  bool enabled = false;
  std::uint64_t dropped = 0;
  std::vector<IceEndpointObservation> entries;
};

struct IceMappingSample {
  std::array<std::uint8_t, 12> transaction{}; // internal; export only SHA-256 tag
  std::string state, mapped_address;
  std::uint16_t server_port = 0, mapped_port = 0;
  std::uint8_t sends = 0;
  int send_status = 0;
  std::uint64_t sent_ms = 0, received_ms = 0;
};
struct IceMappingStats {
  bool enabled = false;
  std::string state = "not-started";
  std::uint64_t age_ms = 0, rejected_responses = 0, duplicate_responses = 0;
  std::vector<IceMappingSample> samples;
};

struct IceSelectedPath {
  std::string local_family = "unknown";
  std::string remote_family = "unknown";
  std::string local_candidate;
  std::string remote_candidate;
  std::string local_address;
  std::string remote_address;
  std::string local_type;
  std::string remote_type;
  bool relayed = false;
};

struct IceGatewayMappingStats {
  bool enabled = false;
  std::string state = "not-started";
  int result_code = 0, send_status = 0;
  std::uint16_t internal_port = 0, external_port = 0;
  std::uint32_t lifetime_seconds = 0;
  std::uint64_t remaining_ms = 0, requests_sent = 0, rejected_responses = 0, renewals = 0;
};

enum class IceEventType : std::uint8_t {
  state_changed,
  candidate,
  gathering_done,
  datagram,
};

struct IceEvent {
  IceEventType type = IceEventType::state_changed;
  IceState state = IceState::disconnected;
  std::string detail;
  std::vector<std::uint8_t> datagram;
};

/// One RFC 8445 ICE component over UDP. It gathers host, STUN reflexive and
/// TURN relay candidates and remains alive for consent freshness and TURN
/// allocation refresh. The caller's authenticated protocol stays above it.
class IceAgent final {
public:
  IceAgent();
  ~IceAgent();
  IceAgent(const IceAgent &) = delete;
  IceAgent &operator=(const IceAgent &) = delete;

  bool Start(const IceConfig &config);
  // Before gathering only. Used by sockets belonging to one ICE generation.
  bool SetLocalCredentials(std::string_view ufrag, std::string_view password);
  void Stop();
  bool started() const;
  IceState state() const;

  bool Gather(std::chrono::milliseconds timeout);
  // Start libjuice's asynchronous gathering without waiting for STUN replies.
  bool BeginGather();
  bool Gathered() const;
  // A direct STUN mapping can be used while an unreachable server address
  // continues retrying. Gathered() still means every branch completed.
  bool ReadyForSignaling() const;
  IceGatherStats GatherStatistics() const;
  IceCheckStats CheckStatistics() const;
  IceCaptureEndpoints CaptureEndpoints() const;
  IceEndpointStats EndpointStatistics() const;
  IceMappingStats MappingStatistics() const;
  IceMappingStats FilteringStatistics() const;
  IceGatewayMappingStats GatewayMappingStatistics() const;
  NetworkPathStatus NetworkPathStatistics() const;
  std::string LocalDescription() const;
  bool SetRemoteDescription(std::string_view sdp);
  bool Send(std::string_view datagram);
  IceSelectedPath SelectedPath() const;
  // False until both selected candidate types are known and permitted.
  bool SelectedPathAllowed() const;
  std::vector<IceEvent> TakeEvents();
  IceQueueStats QueueStatistics() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace libnet

#endif // LIBNET_ICE_H_
