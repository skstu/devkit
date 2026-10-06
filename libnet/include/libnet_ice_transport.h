#pragma once

#include "libnet_ice.h"
#include "libnet_tcp_direct.h"

namespace libnet {

struct IceRecoveryStats {
  bool enabled = false;
  std::string state = "disabled";
  std::size_t available_paths = 0, live_paths = 0;
  std::uint64_t revision = 0, attempts = 0, switches = 0;
};

// One logical ICE generation, with at most four independently bound sockets.
// New sources after signaling require a fresh, authenticated ICE generation.
// A surviving verified socket can carry that negotiation; no unbound fallback.
class IceTransport final {
public:
  IceTransport();
  ~IceTransport();
  bool Start(const IceConfig &config);
  void Stop();
  bool BeginGather();
  bool Gather(std::chrono::milliseconds timeout);
  bool ReadyForSignaling() const;
  bool Gathered() const;
  IceState state() const;
  std::string LocalDescription() const;
  bool SetRemoteDescription(std::string_view sdp);
  bool Send(std::string_view datagram);
  IceSelectedPath SelectedPath() const;
  bool SelectedPathAllowed() const;
  std::vector<IceEvent> TakeEvents();
  // Empty means offline. Called only with local host inventory, never SDP.
  void UpdateNetworkPaths(const std::vector<NetworkPath> &paths);
  IceRecoveryStats RecoveryStatistics() const;
  bool PeerSupportsRecovery() const;
  IceQueueStats QueueStatistics() const;
  IceGatherStats GatherStatistics() const;
  IceCheckStats CheckStatistics() const;
  IceCaptureEndpoints CaptureEndpoints() const;
  IceEndpointStats EndpointStatistics() const;
  IceMappingStats MappingStatistics() const;
  IceMappingStats FilteringStatistics() const;
  IceGatewayMappingStats GatewayMappingStatistics() const;
  NetworkPathStatus NetworkPathStatistics() const;
  TcpDirectStats TcpStatistics() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace libnet
