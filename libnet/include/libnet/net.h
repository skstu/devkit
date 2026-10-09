#ifndef DEVKIT_NET_H
#define DEVKIT_NET_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# define DKNET_CALL __cdecl
# if defined(DEVKIT_NET_BUILD)
#  define DKNET_API __declspec(dllexport)
# else
#  define DKNET_API __declspec(dllimport)
# endif
#else
# define DKNET_CALL
# define DKNET_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define DKNET_ABI_VERSION 1u
#define DKNET_OK 0
#define DKNET_INVALID -1
#define DKNET_STATE -2
#define DKNET_IO -3
#define DKNET_BUSY -4
#define DKNET_INTERNAL -5
/* Unauthenticated transport only. A ready QUIC bearer is NOT a trusted peer.
 * Every operation except wake is affine to the thread that called create.
 * Callbacks execute on that thread, must not throw or block, and may call
 * operations (except run/destroy). Event bytes/strings expire on return.
 * No callbacks after destroy returns. The caller must stop/join wake producers
 * before destroy; shutdown may race wake while the context still exists.
 * Accepted sends are copied; acceptance is not delivery or persistence. */
typedef struct dknet_context dknet_context;
typedef struct dknet_endpoint { char address[64]; uint16_t port; uint16_t reserved; } dknet_endpoint;
enum { DKNET_DATAGRAM=1, DKNET_WAKE=2, DKNET_TIMER=3, DKNET_QUIC_TICK=4,
       DKNET_REBOUND=5, DKNET_WRITABLE=6, DKNET_ERROR=7, DKNET_QUIC_EVENTS=8 };
enum { DKNET_QUIC_READY=1, DKNET_QUIC_RECORD=2, DKNET_QUIC_CLOSED=3,
       DKNET_QUIC_FAILED=4, DKNET_QUIC_STREAM_RECORD=5, DKNET_QUIC_STREAM_CLOSED=6 };
typedef struct dknet_event {
  uint32_t struct_size, type, slot;
  int32_t status;
  dknet_endpoint peer;
  const uint8_t *data;
  size_t size;
} dknet_event;
typedef struct dknet_quic_event {
  uint32_t struct_size, type;
  uint64_t connection;
  int64_t stream;
  dknet_endpoint peer;
  const uint8_t *data;
  size_t size;
  const char *detail;
} dknet_quic_event;
typedef void (DKNET_CALL *dknet_event_fn)(void *, const dknet_event *);
typedef void (DKNET_CALL *dknet_quic_event_fn)(void *, const dknet_quic_event *);
/* Optional QUIC datagram sender for a caller-owned bearer (e.g. ICE).
 * Return DKNET_OK only if copied/accepted. Called synchronously; do not call
 * QUIC methods from this callback. Null means use this context's UDP sockets. */
typedef int32_t (DKNET_CALL *dknet_send_fn)(void *, const dknet_endpoint *, const uint8_t *, size_t);
typedef struct dknet_config {
  uint32_t struct_size, abi_version;
  uint16_t port, reserved;
  uint32_t maximum_connections, maximum_pending_datagrams;
  size_t maximum_pending_bytes;
  uint32_t quic_tick_ms;
  const char *alpn; /* Required 1..255 bytes; copied. No product-specific default. */
  dknet_event_fn on_event;
  dknet_send_fn send_quic;
  void *user;
} dknet_config;
typedef struct dknet_quic_stats {
  uint32_t struct_size, reserved;
  uint64_t smoothed_rtt_us, congestion_window_bytes, bytes_in_flight,
      packets_sent, packets_lost, send_quantum_bytes, maximum_batch_datagrams,
      active_streams, stream_buffer_bytes;
} dknet_quic_stats;
typedef struct dknet_probe_result {
  uint32_t struct_size, handshake_completed, stream_completed, udp_socket_io, early_data;
  uint64_t packet_count;
  char alpn[256], cipher[128], error[256];
} dknet_probe_result;
/* Bounded local diagnostic, independent of a context; no remote host access. */
DKNET_API int32_t DKNET_CALL dknet_quic_probe(const char *alpn, const uint8_t *, size_t, uint32_t timeout_ms, dknet_probe_result *);
DKNET_API uint32_t DKNET_CALL dknet_abi_version(void);
DKNET_API const char * DKNET_CALL dknet_version(void);
DKNET_API const char * DKNET_CALL dknet_backend_versions(void);
DKNET_API int32_t DKNET_CALL dknet_endpoint_parse(const char *, uint16_t, dknet_endpoint *);
/* Creates a dual-stack listener, IPv4 required and IPv6 additive; no thread is
 * spawned. Failed creation leaves *output null, with all handles drained. */
DKNET_API int32_t DKNET_CALL dknet_create(const dknet_config *, dknet_context **output);
DKNET_API int32_t DKNET_CALL dknet_local_endpoint(dknet_context *, uint32_t family, dknet_endpoint *);
DKNET_API int32_t DKNET_CALL dknet_run(dknet_context *);
DKNET_API int32_t DKNET_CALL dknet_shutdown(dknet_context *);
DKNET_API int32_t DKNET_CALL dknet_destroy(dknet_context *);
/* Up to 32 coalesced wake slots, and 8 independent timer slots. */
DKNET_API int32_t DKNET_CALL dknet_wake(dknet_context *, uint32_t slot);
DKNET_API int32_t DKNET_CALL dknet_timer_start(dknet_context *, uint32_t slot, uint64_t delay_ms, uint64_t repeat_ms);
DKNET_API int32_t DKNET_CALL dknet_timer_stop(dknet_context *, uint32_t slot);
DKNET_API int32_t DKNET_CALL dknet_send_datagram(dknet_context *, const dknet_endpoint *, const uint8_t *, size_t);
/* Keeps the port and protocol state. Emits exactly one REBOUND event unless
 * shutdown cancels it. No successful recovery is reported on partial failure. */
DKNET_API int32_t DKNET_CALL dknet_rebind(dknet_context *);
DKNET_API uint64_t DKNET_CALL dknet_quic_connect(dknet_context *, const dknet_endpoint *);
/* 1 consumed, 0 not QUIC, negative error. Call on raw datagrams you admit. */
DKNET_API int32_t DKNET_CALL dknet_quic_receive(dknet_context *, const dknet_endpoint *, const uint8_t *, size_t);
DKNET_API int32_t DKNET_CALL dknet_quic_send(dknet_context *, uint64_t, const uint8_t *, size_t);
DKNET_API int32_t DKNET_CALL dknet_quic_can_queue(dknet_context *, uint64_t);
DKNET_API int32_t DKNET_CALL dknet_quic_enable_streams(dknet_context *, uint64_t, uint32_t maximum);
DKNET_API int64_t DKNET_CALL dknet_quic_open_stream(dknet_context *, uint64_t);
DKNET_API int32_t DKNET_CALL dknet_quic_send_stream(dknet_context *, uint64_t, int64_t, const uint8_t *, size_t);
DKNET_API int32_t DKNET_CALL dknet_quic_consume_stream(dknet_context *, uint64_t, int64_t, size_t);
DKNET_API int32_t DKNET_CALL dknet_quic_reset_stream(dknet_context *, uint64_t, int64_t);
DKNET_API int32_t DKNET_CALL dknet_quic_finish_stream(dknet_context *, uint64_t, int64_t);
DKNET_API int32_t DKNET_CALL dknet_quic_close(dknet_context *, uint64_t, const char *detail);
/* Drains the current batch; newly generated events wait for the next drain.
 * Call after QUIC operations and QUIC_TICK/QUIC_EVENTS to bound event retention. */
DKNET_API int32_t DKNET_CALL dknet_quic_dispatch(dknet_context *, dknet_quic_event_fn, void *);
DKNET_API int32_t DKNET_CALL dknet_quic_statistics(dknet_context *, uint64_t, dknet_quic_stats *);
/* Additive LAN API, version 1. Original ABI-1 structures/functions are unchanged.
 * A LAN context owns its loop and requires no QUIC listener or ALPN. All calls
 * except lan_wake are owner-thread only. Callbacks execute only during lan_run;
 * data is borrowed until return. Do not block/throw, run or destroy in a callback.
 * Stop/join wake producers before destroy. Shutdown silences callbacks, cancels
 * sends and closes sockets; destroy drains closes. No callbacks after shutdown.
 * Socket IDs are never reused in a context. Receive callbacks have no SDK queue;
 * each socket has one fixed buffer. The host may pause receive for backpressure.
 * Accepted sends are copied and bounded across the entire LAN context. SENT
 * means OS completion, never delivery. Explicit socket close permits pending
 * SENT completions/cancellations, then exactly one CLOSED; no datagrams afterward.
 * Rebind keeps the bound port, options and memberships. It rejects sends until
 * REBOUND and never reports partial restoration as success. */
#define DKNET_LAN_VERSION 1u
#define DKNET_BUFFER_TOO_SMALL -6
#define DKNET_NOT_FOUND -7
typedef struct dknet_lan_context dknet_lan_context;
enum { DKNET_LAN_DATAGRAM=1, DKNET_LAN_SENT=2, DKNET_LAN_WRITABLE=3,
       DKNET_LAN_ERROR=4, DKNET_LAN_CLOSED=5, DKNET_LAN_INTERFACES=6,
       DKNET_LAN_WAKE=7, DKNET_LAN_TIMER=8, DKNET_LAN_REBOUND=9 };
enum { DKNET_UDP_REUSE_ADDRESS=1u, DKNET_UDP_IPV6_ONLY=2u,
       DKNET_UDP_BROADCAST=4u };
enum { DKNET_INTERFACE_INTERNAL=1u, DKNET_INTERFACE_LINK_LOCAL=2u };
typedef struct dknet_interface {
  uint32_t struct_size, family, index, scope_id, flags, reserved;
  char name[256], address[64], netmask[64], broadcast[64];
} dknet_interface;
typedef struct dknet_lan_event {
  uint32_t struct_size, type;
  uint64_t socket, request, generation;
  int32_t status, native_status; /* native_status is the original libuv diagnostic. */
  dknet_endpoint peer;
  const uint8_t *data;
  size_t size;
} dknet_lan_event;
typedef void (DKNET_CALL *dknet_lan_event_fn)(void *, const dknet_lan_event *);
typedef struct dknet_lan_config {
  uint32_t struct_size, version, maximum_sockets, maximum_memberships;
  uint32_t maximum_pending_datagrams, reserved;
  size_t maximum_pending_bytes;
  dknet_lan_event_fn on_event;
  void *user;
} dknet_lan_config;
typedef struct dknet_udp_config {
  uint32_t struct_size, version, flags, maximum_receive_bytes;
  uint32_t multicast_ttl, multicast_loop;
  dknet_endpoint bind;
} dknet_udp_config;
typedef struct dknet_udp_stats {
  uint32_t struct_size, reserved;
  size_t pending_datagrams, pending_bytes;
  uint64_t received_datagrams, dropped_datagrams, sent_datagrams, failed_sends;
} dknet_udp_stats;
DKNET_API uint32_t DKNET_CALL dknet_lan_version(void);
DKNET_API int32_t DKNET_CALL dknet_lan_create(const dknet_lan_config *, dknet_lan_context **);
DKNET_API int32_t DKNET_CALL dknet_lan_run(dknet_lan_context *);
DKNET_API int32_t DKNET_CALL dknet_lan_shutdown(dknet_lan_context *);
DKNET_API int32_t DKNET_CALL dknet_lan_destroy(dknet_lan_context *);
DKNET_API int32_t DKNET_CALL dknet_lan_wake(dknet_lan_context *, uint32_t slot);
DKNET_API int32_t DKNET_CALL dknet_lan_timer_start(dknet_lan_context *, uint32_t slot, uint64_t delay_ms, uint64_t repeat_ms);
DKNET_API int32_t DKNET_CALL dknet_lan_timer_stop(dknet_lan_context *, uint32_t slot);
/* Poll assigned IPv4/IPv6 addresses, not link/media/route reachability. No
 * silent truncation: count is required capacity; an undersized output is left
 * untouched. Null output/capacity=0 queries count. Output structs are SDK-filled.
 * Hard cap 1024 address records; overflow fails rather than returning a subset. */
DKNET_API int32_t DKNET_CALL dknet_interface_list(dknet_interface *, size_t capacity, size_t *count);
/* Configurable portable polling (250..60000 ms), 0 stops automatic watching.
 * Refresh coalesces on the loop. A successful initial/changed snapshot emits
 * INTERFACES with a generation; unchanged polls emit nothing. Failed refresh
 * emits ERROR and retains the last good snapshot. No routing/auto-rebind policy. */
DKNET_API int32_t DKNET_CALL dknet_lan_watch_interfaces(dknet_lan_context *, uint32_t interval_ms);
DKNET_API int32_t DKNET_CALL dknet_lan_refresh_interfaces(dknet_lan_context *);
DKNET_API int32_t DKNET_CALL dknet_lan_interfaces(dknet_lan_context *, dknet_interface *, size_t capacity, size_t *count, uint64_t *generation);
DKNET_API int32_t DKNET_CALL dknet_lan_last_native_error(dknet_lan_context *);
DKNET_API int32_t DKNET_CALL dknet_udp_open(dknet_lan_context *, const dknet_udp_config *, uint64_t *socket);
DKNET_API int32_t DKNET_CALL dknet_udp_local_endpoint(dknet_lan_context *, uint64_t socket, dknet_endpoint *);
DKNET_API int32_t DKNET_CALL dknet_udp_receive_start(dknet_lan_context *, uint64_t socket);
DKNET_API int32_t DKNET_CALL dknet_udp_receive_stop(dknet_lan_context *, uint64_t socket);
DKNET_API int32_t DKNET_CALL dknet_udp_send(dknet_lan_context *, uint64_t socket, const dknet_endpoint *, const uint8_t *, size_t, uint64_t request);
/* group is numeric multicast IPv4/IPv6, interface is a numeric same-family
 * local address (IPv6 may include %scope), or null for OS selection. Repeated
 * joins are idempotent; an unknown leave is NOT_FOUND. Memberships are bounded.
 * IPv6 discovery should specify scope, e.g. interface "::%5". */
DKNET_API int32_t DKNET_CALL dknet_udp_membership(dknet_lan_context *, uint64_t socket, const char *group, const char *interface_address, uint32_t join);
DKNET_API int32_t DKNET_CALL dknet_udp_multicast_interface(dknet_lan_context *, uint64_t socket, const char *interface_address);
DKNET_API int32_t DKNET_CALL dknet_udp_rebind(dknet_lan_context *, uint64_t socket);
DKNET_API int32_t DKNET_CALL dknet_udp_close(dknet_lan_context *, uint64_t socket);
DKNET_API int32_t DKNET_CALL dknet_udp_statistics(dknet_lan_context *, uint64_t socket, dknet_udp_stats *);

#ifdef __cplusplus
}
#endif
#endif
