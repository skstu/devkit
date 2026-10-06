#pragma once

#include "libnet_ice.h"

namespace libnet {
struct TcpDirectStats {
  std::string phase = "disabled", error;
  std::uint64_t attempts = 0, accepted = 0, rejected = 0;
  bool coordinated = false;
};

// Optional record bearer below the SDK's signed invitations / Noise protocol.
// IPv4 v1: one explicitly bound source; at most host + observed TCP endpoint.
// Never predicts ports, relays records, changes network settings, or runs QUIC.
class TcpDirectAgent final {
public:
  TcpDirectAgent();
  ~TcpDirectAgent();
  static bool Supported();
  bool Start(const IceConfig &config);
  void Stop();
  bool BeginGather();
  bool Gathered() const;
  IceState state() const;
  std::string LocalDescription() const;
  bool SetRemoteDescription(std::string_view description);
  bool Send(std::string_view record);
  IceSelectedPath SelectedPath() const;
  std::vector<IceEvent> TakeEvents();
  IceGatherStats GatherStatistics() const;
  IceQueueStats QueueStatistics() const;
  IceCaptureEndpoints CaptureEndpoints() const;
  NetworkPathStatus NetworkPathStatistics() const;
  TcpDirectStats Statistics() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace libnet
