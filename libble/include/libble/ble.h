#ifndef DEVKIT_BLE_H
#define DEVKIT_BLE_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
# define DKBLE_CALL __cdecl
# if defined(DEVKIT_BLE_BUILD)
#  define DKBLE_API __declspec(dllexport)
# else
#  define DKBLE_API __declspec(dllimport)
# endif
#else
# define DKBLE_CALL
# define DKBLE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define DKBLE_ABI_VERSION 1u
#define DKBLE_OK 0
#define DKBLE_INVALID -1
#define DKBLE_STATE -2
#define DKBLE_BUSY -3
#define DKBLE_IO -4
#define DKBLE_THREAD -5
#define DKBLE_OVERFLOW -6
#define DKBLE_UNSUPPORTED -7
#define DKBLE_HINT_REBIND 1u
#define DKBLE_MODE_SCAN 1u
#define DKBLE_MODE_ADVERTISE 2u
#define DKBLE_EVENT_STATE 1u
#define DKBLE_EVENT_CANDIDATE 2u
#define DKBLE_EVENT_CANDIDATE_GONE 3u
#define DKBLE_EVENT_LINK 4u
#define DKBLE_EVENT_DISCONNECTED 5u
#define DKBLE_EVENT_DATA 6u
#define DKBLE_EVENT_PROBE_ENDED 7u
#define DKBLE_EVENT_SEND_COMPLETE 8u
#define DKBLE_EVENT_ERROR 9u
#define DKBLE_EVENT_DIAGNOSTIC 10u
/* All operations, including create/destroy/dispatch, require the Apple main
 * thread. The host runs its normal main run loop. No Flutter dependency.
 * Creation does NOT access the radio. Only explicit start constructs managers
 * and may prompt for permission; host supplies usage descriptions/entitlements.
 * wake only signals the host's event loop; it must not reenter this SDK.
 * dispatch invokes callbacks on the main thread, outside native delegates.
 * Callbacks may invoke operations except dispatch/destroy; they must not throw.
 * Callback strings/bytes expire on return. Accepted sends copy their bytes.
 * Stop invalidates the previous event generation. Destroy emits no callbacks.
 * A connected GATT peer is NOT an authenticated application identity. */
typedef struct dkble_context dkble_context;
typedef void (DKBLE_CALL *dkble_wake_fn)(void *);
typedef struct dkble_config {
  uint32_t struct_size, abi_version, flags, reserved;
  const char *service_uuid, *receive_uuid, *notify_uuid; /* distinct canonical 128-bit UUIDs; copied */
  dkble_wake_fn wake;
  void *user;
} dkble_config;
typedef struct dkble_event {
  uint32_t struct_size, type;
  uint64_t generation, request_id;
  int32_t status, rssi;
  uint32_t initiator, reserved;
  const char *link, *peer, *probe, *detail;
  const uint8_t *data;
  size_t size;
} dkble_event;
typedef void (DKBLE_CALL *dkble_event_fn)(void *, const dkble_event *);
#ifndef DKBLE_TYPES_ONLY
DKBLE_API uint32_t DKBLE_CALL dkble_abi_version(void);
DKBLE_API const char * DKBLE_CALL dkble_version(void);
DKBLE_API int32_t DKBLE_CALL dkble_create(const dkble_config *, dkble_context **);
DKBLE_API int32_t DKBLE_CALL dkble_destroy(dkble_context *);
DKBLE_API int32_t DKBLE_CALL dkble_start(dkble_context *, uint32_t mode);
DKBLE_API int32_t DKBLE_CALL dkble_stop(dkble_context *);
/* Explicit connect requests retain a retry intent until disconnect/stop.
 * Retries apply to transport only (3s delay, 20s connection timeout, 4 links).
 * A one-shot probe never gains that intent until the caller adopts it after
 * authentication. Token is 1..64 UTF-8 bytes. Adoption may replace old_peer;
 * a ready old link cannot be displaced. Hints never prove identity. */
DKBLE_API int32_t DKBLE_CALL dkble_connect(dkble_context *, const char *peer);
DKBLE_API int32_t DKBLE_CALL dkble_probe(dkble_context *, const char *peer, const char *token);
DKBLE_API int32_t DKBLE_CALL dkble_cancel_probe(dkble_context *, const char *token);
DKBLE_API int32_t DKBLE_CALL dkble_adopt_probe(dkble_context *, const char *token, const char *old_peer);
/* retry=0 removes retry intent; retry=1 resets the transport, retaining intent.
 * Apple cannot physically disconnect an individual inbound central. Such a
 * peer is rejected until unsubscribe; other subscribed links remain intact. */
DKBLE_API int32_t DKBLE_CALL dkble_disconnect(dkble_context *, const char *link, uint32_t retry);
/* 1..65560 bytes, ONE pending send per link, busy returns without accepting.
 * Completion is an ATT write response / local notification enqueue, NOT remote
 * application receipt. Framing, encryption, delivery ACK and retries belong to
 * the consumer. 25s send timeout closes the route. request_id is caller-owned. */
DKBLE_API int32_t DKBLE_CALL dkble_send(dkble_context *, const char *link, const uint8_t *, size_t, uint64_t request_id);
DKBLE_API int32_t DKBLE_CALL dkble_recover(dkble_context *);
/* max_events is 1..256. Returns dispatch count or negative error. Bounded queue
 * (256 events / 256 KiB): overflow stops the radio, invalidates all links and
 * emits ERROR/OVERFLOW. Caller must explicitly start a fresh session. */
DKBLE_API int32_t DKBLE_CALL dkble_dispatch(dkble_context *, uint32_t max_events, dkble_event_fn, void *);
#endif
#ifdef __cplusplus
}
#endif
#endif
