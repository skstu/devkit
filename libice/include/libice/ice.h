#ifndef DEVKIT_ICE_H
#define DEVKIT_ICE_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# define DKICE_CALL __cdecl
# if defined(DEVKIT_ICE_BUILD)
#  define DKICE_API __declspec(dllexport)
# else
#  define DKICE_API __declspec(dllimport)
# endif
#else
# define DKICE_CALL
# define DKICE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define DKICE_ABI_VERSION 1u
#define DKICE_OK 0
#define DKICE_INVALID -1
#define DKICE_STATE -2
#define DKICE_IO -3
#define DKICE_BUSY -4
#define DKICE_INTERNAL -5
#define DKICE_THREAD -6
#define DKICE_BUFFER -7
#define DKICE_ALLOW_RELAY 1u
#define DKICE_RELAY_ONLY 2u
#define DKICE_DISCONNECTED 0u
#define DKICE_GATHERING 1u
#define DKICE_CONNECTING 2u
#define DKICE_CONNECTED 3u
#define DKICE_COMPLETED 4u
#define DKICE_FAILED 5u
#define DKICE_EVENT_STATE 1u
#define DKICE_EVENT_CANDIDATE 2u
#define DKICE_EVENT_GATHERED 3u
#define DKICE_EVENT_DATAGRAM 4u
/* One UDP ICE component. Owner-thread API; libjuice owns its network worker.
 * wake runs on a provider thread OR synchronously during an API operation.
 * It must only signal the owner; it must not block, throw or call ANY dkice
 * function. dispatch callbacks run on the owner; they may call operations
 * except dispatch/destroy. No callbacks after stop/destroy returns.
 * Caller must serialize handle lifetime, never use a freed handle.
 * ICE connectivity is not application identity authentication or encryption. */
typedef struct dkice_context dkice_context;
typedef void (DKICE_CALL *dkice_wake_fn)(void *);
typedef struct dkice_turn_server {
  const char *host, *username, *password;
  uint16_t port, reserved;
} dkice_turn_server;
typedef struct dkice_config {
  uint32_t struct_size, abi_version, flags, address_family; /* family: 0,4,6 */
  const char *bind_address; /* numeric, NULL = OS routing */
  const char *interface_name; /* optional; requires bind_address; fail closed */
  const char *stun_address; /* numeric; NULL disables server-assisted gathering */
  uint16_t stun_port, port_begin, port_end, reserved;
  const dkice_turn_server *turn_servers; /* numeric hosts; copied; <=8 */
  uint32_t turn_server_count, reserved2;
  dkice_wake_fn wake;
  void *user;
} dkice_config;
typedef struct dkice_event {
  uint32_t struct_size, type, state, reserved;
  const char *detail; /* candidate/SDP fields may contain sensitive endpoints */
  const uint8_t *data;
  size_t size;
} dkice_event;
typedef void (DKICE_CALL *dkice_event_fn)(void *, const dkice_event *);
typedef struct dkice_status {
  uint32_t struct_size, state, gathering_complete, signaling_ready;
  uint32_t path_allowed, relayed, local_candidates, remote_candidates;
  uint64_t dropped_datagrams, peak_queued_events;
  char local_address[128], remote_address[128];
  char local_type[16], remote_type[16];
} dkice_status;
DKICE_API uint32_t DKICE_CALL dkice_abi_version(void);
DKICE_API const char * DKICE_CALL dkice_version(void);
DKICE_API const char * DKICE_CALL dkice_backend_version(void);
/* Configuration strings copied; no default external STUN/TURN or coordinator.
 * Creates an agent; gathering starts explicitly. flags=0 forbids relay
 * candidates including mixed remote descriptions. STOP is terminal; recreate
 * for restart with new ICE credentials and freshly authenticated signaling. */
DKICE_API int32_t DKICE_CALL dkice_create(const dkice_config *, dkice_context **);
DKICE_API int32_t DKICE_CALL dkice_gather(dkice_context *);
/* length includes terminating NUL. NULL/0 queries required size and returns
 * BUFFER. The application transports this description via its OWN signaling;
 * the SDK does not publish it, sign it or associate it with an account. */
DKICE_API int32_t DKICE_CALL dkice_local_description(dkice_context *, char *, size_t capacity, size_t *required);
DKICE_API int32_t DKICE_CALL dkice_remote_description(dkice_context *, const char *, size_t size);
/* Sends through the selected ICE socket/pair; do not reopen a UDP socket using
 * returned addresses. 1..65507 bytes, unreliable datagram; OK is send acceptance
 * and not remote delivery. The caller handles MTU/congestion/reliability (e.g.
 * via libnet QUIC with send_quic/receive callbacks on this bearer). */
DKICE_API int32_t DKICE_CALL dkice_send(dkice_context *, const uint8_t *, size_t);
/* One bounded snapshot (up to 256 provider events); callback pointers expire
 * on return. UDP queue pressure drops datagrams, exposed in status, never a
 * claim of reliable buffering. The host should drain when wake signals. */
DKICE_API int32_t DKICE_CALL dkice_dispatch(dkice_context *, dkice_event_fn, void *);
DKICE_API int32_t DKICE_CALL dkice_get_status(dkice_context *, dkice_status *);
DKICE_API int32_t DKICE_CALL dkice_stop(dkice_context *);
DKICE_API int32_t DKICE_CALL dkice_destroy(dkice_context *);
#ifdef __cplusplus
}
#endif
#endif
