#ifndef LIBNET_QUIC_H_
#define LIBNET_QUIC_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libnet_uv.h"

namespace libnet {

struct QuicLoopbackResult {
  bool handshake_completed = false;
  bool bidirectional_stream_completed = false;
  bool udp_socket_io = false;
  bool early_data_enabled = false;
  std::size_t packet_count = 0;
  std::string alpn;
  std::string cipher;
  std::string error;
};

/// Executes a real ngtcp2 + OpenSSL QUIC handshake and one echoed
/// bidirectional stream over two real loopback UDP sockets. This is a bounded
/// crypto/protocol/socket conformance probe, not a production network route.
QuicLoopbackResult RunQuicLoopbackProbe(
    std::string_view payload,
    std::chrono::milliseconds timeout = std::chrono::seconds(3));

QuicLoopbackResult RunQuicLoopbackProbeWithAlpn(std::string_view payload,
    std::chrono::milliseconds timeout, std::string_view alpn);

enum class QuicProviderEventType : std::uint8_t {
  bearer_ready,
  record,
  closed,
  failed,
  stream_record,
  stream_closed,
};

struct QuicProviderEvent {
  QuicProviderEventType type = QuicProviderEventType::failed;
  std::uint64_t connection_id = 0;
  Endpoint peer;
  std::vector<std::uint8_t> record;
  std::string detail;
  std::int64_t stream_id = -1;
};

struct QuicConnectionStats {
  std::uint64_t smoothed_rtt_us = 0;
  std::uint64_t congestion_window_bytes = 0;
  std::uint64_t bytes_in_flight = 0;
  std::uint64_t packets_sent = 0;
  std::uint64_t packets_lost = 0;
  std::uint64_t send_quantum_bytes = 0;
  std::uint64_t maximum_batch_datagrams = 0;
  std::size_t active_streams = 0;
  std::size_t stream_buffer_bytes = 0;
};

enum class QuicStreamSendResult { accepted, busy, unavailable };

/// Long-lived, loop-thread-affine QUIC bearer. The caller owns the UDP
/// listener and forwards non-SovKit datagrams elsewhere. Application records
/// retain the original ordered bidirectional stream; optional independent
/// framed streams are additive. This class deliberately knows nothing about
/// identities, relationships, Noise, or authorization.
class QuicProvider final {
public:
  using DatagramSender =
      std::function<bool(const Endpoint &, std::string_view)>;

  QuicProvider();
  ~QuicProvider();
  QuicProvider(const QuicProvider &) = delete;
  QuicProvider &operator=(const QuicProvider &) = delete;

  bool Start(Endpoint local_ipv4, Endpoint local_ipv6, DatagramSender sender,
             std::size_t maximum_connections = 16);
  bool StartWithAlpn(Endpoint local_ipv4, Endpoint local_ipv6, DatagramSender sender,
                     std::size_t maximum_connections, std::string_view alpn);
  std::size_t ConnectionCount() const;
  bool HasPendingEvents() const;
  void Stop();
  bool started() const;

  /// Starts one client connection. The returned identifier is process-local
  /// and never crosses the wire. Zero indicates rejection.
  std::uint64_t Connect(const Endpoint &peer);

  /// Consumes the datagram only when it belongs to an existing QUIC
  /// connection or is an admissible QUIC Initial. False lets the caller parse
  /// the datagram as another protocol on the shared UDP port.
  bool ReceiveDatagram(std::string_view datagram, const Endpoint &source);

  bool SendRecord(std::uint64_t connection_id, std::string_view record);
  /// Loop-thread-only preflight for optional traffic. Leaves half the pending
  /// record budget for control/file traffic; false does not close a connection.
  bool CanQueueOptionalRecord(std::uint64_t connection_id) const;
  // Additive streams, enabled only after the caller authenticates the bearer.
  // Original SendRecord framing and ordering are unchanged. All methods are
  // loop-thread-only. Stream data has no identity/authorization by itself.
  bool EnableStreams(std::uint64_t connection_id, std::size_t maximum_live);
  std::int64_t OpenStream(std::uint64_t connection_id);
  QuicStreamSendResult SendStreamRecord(std::uint64_t connection_id,
      std::int64_t stream_id, std::string_view record);
  void ConsumeStreamRecord(std::uint64_t connection_id, std::int64_t stream_id,
      std::size_t record_bytes);
  void ResetStream(std::uint64_t connection_id, std::int64_t stream_id);
  // Send FIN only after all accepted records; buffers survive until ACK/close.
  void FinishStream(std::uint64_t connection_id, std::int64_t stream_id);
  void Tick();
  // Loop-thread only, like SendRecord/Tick. SDK hosts must publish a copy.
  std::optional<QuicConnectionStats> Statistics(std::uint64_t connection_id) const;
  void Close(std::uint64_t connection_id, std::string detail = {});
  std::vector<QuicProviderEvent> TakeEvents();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace libnet

#endif // LIBNET_QUIC_H_
