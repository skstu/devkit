#if !defined(LIBUVBRG_H_)
#define LIBUVBRG_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *uvbrg_handle_t;
typedef void *uvbrg_listener_t;
typedef void *uvbrg_session_t;

typedef struct uvbrg_internal_connection_s {
  uint64_t id;
  uint64_t generation;
} uvbrg_internal_connection_t;

/* Optional asynchronous byte-stream upstream. Install before starting listeners.
 * Callbacks run on the bridge loop and must not block. Return 0 when accepted.
 * data is borrowed for the callback only; upstream must copy it. Each data
 * callback pauses browser reads until UVBRG_TUNNEL_WRITABLE. Delivery to the
 * browser is acknowledged by consumed after the local socket write completes.
 * IDs include a generation: late events cannot address a replacement session.
 * No DNS or direct TCP fallback is performed when this adapter is installed. */
typedef struct uvbrg_tunnel_callbacks_s {
  size_t size;
  void *user_data;
  int (*open)(void *, uvbrg_internal_connection_t, const char *, uint16_t);
  int (*data)(void *, uvbrg_internal_connection_t, const char *, size_t);
  void (*eof)(void *, uvbrg_internal_connection_t);
  void (*closed)(void *, uvbrg_internal_connection_t);
  void (*consumed)(void *, uvbrg_internal_connection_t, size_t);
} uvbrg_tunnel_callbacks_t;
enum {
  UVBRG_TUNNEL_CONNECTED = 1, UVBRG_TUNNEL_DATA = 2,
  UVBRG_TUNNEL_WRITABLE = 3, UVBRG_TUNNEL_EOF = 4, UVBRG_TUNNEL_CLOSE = 5
};
int uvbrg_set_tunnel_callbacks(uvbrg_handle_t, const uvbrg_tunnel_callbacks_t *);
/* Thread-safe, bounded; may wait for bridge loop. Never call after destroy. */
int uvbrg_tunnel_event(uvbrg_handle_t, uvbrg_internal_connection_t,
                       int event, const char *data, size_t size);

enum {
  UVBRG_INTERNAL_HTTP_COMPLETE = 0,
  UVBRG_INTERNAL_HTTP_DEFERRED = 1,
};

typedef void (*uvbrg_on_new_session_cb)(uvbrg_handle_t h, uvbrg_session_t s,
                                        const char *session_id,
                                        const char *instance_id);
typedef void (*uvbrg_on_fingerprint_cb)(uvbrg_handle_t h, uvbrg_session_t s,
                                        const char *fp_json);
typedef void (*uvbrg_on_features_cb)(uvbrg_handle_t h, uvbrg_session_t s,
                                     const char *feat_json);
typedef void (*uvbrg_on_error_cb)(uvbrg_handle_t h, uvbrg_session_t s,
                                  int code, const char *msg);
typedef void (*uvbrg_on_http_fingerprint_cb)(uvbrg_handle_t h,
                                             uvbrg_session_t s,
                                             const char *fp_json);
typedef void (*uvbrg_on_tcp_fingerprint_cb)(uvbrg_handle_t h,
                                            uvbrg_session_t s,
                                            const char *fp_json);
typedef void (*uvbrg_on_proxy_fingerprint_cb)(uvbrg_handle_t h,
                                              uvbrg_session_t s,
                                              const char *fp_json);
typedef void (*uvbrg_on_route_step_cb)(uvbrg_handle_t h, uvbrg_session_t s,
                                       const char *step_json);
typedef int (*uvbrg_on_security_decision_cb)(uvbrg_handle_t h,
                                             uvbrg_session_t s,
                                             const char *decision_json,
                                             char *redirect_url,
                                             size_t redirect_url_cap);
typedef int (*uvbrg_on_security_resource_allow_cb)(uvbrg_handle_t h,
                                                   uvbrg_session_t s,
                                                   const char *host,
                                                   uint16_t port,
                                                   const char *protocol);

typedef struct uvbrg_callbacks_s {
  uvbrg_on_new_session_cb on_new_session;
  uvbrg_on_fingerprint_cb on_fingerprint;
  uvbrg_on_features_cb on_features;
  uvbrg_on_error_cb on_error;
  uvbrg_on_http_fingerprint_cb on_http_fingerprint;
  uvbrg_on_tcp_fingerprint_cb on_tcp_fingerprint;
  uvbrg_on_proxy_fingerprint_cb on_proxy_fingerprint;
  uvbrg_on_route_step_cb on_route_step;
  uvbrg_on_security_decision_cb on_security_decision;
  uvbrg_on_security_resource_allow_cb on_security_resource_allow;
} uvbrg_callbacks_t;

/* Optional internal CONNECT execution fence, installed before listeners/run.
 * enter returns nonzero to allow and may retain a short host lock until leave.
 * leave is called exactly once for successful enter. Callbacks must not throw,
 * wait for the loop, or call libuvbrg. Disabled by default; not a public SDK ABI.
 * Covers TCP scheduling and CONNECT success publication, not every relay byte. */
enum { UVBRG_EXECUTE_DIAL = 1, UVBRG_EXECUTE_COMMIT = 2 };
typedef struct uvbrg_execution_guard_s {
  size_t size;
  void *user_data;
  int (*enter)(void *, uvbrg_session_t, int stage);
  void (*leave)(void *, uvbrg_session_t, int stage);
} uvbrg_execution_guard_t;
int uvbrg_set_execution_guard(uvbrg_handle_t, const uvbrg_execution_guard_t *);

typedef struct uvbrg_internal_http_request_s {
  size_t size;
  const char *method;
  const char *path;
  const char *query;
  const char *host;
  uint16_t port;
  const char *headers_json;
  const char *body;
  size_t body_size;
  uvbrg_internal_connection_t connection;
} uvbrg_internal_http_request_t;

typedef struct uvbrg_internal_http_response_s {
  size_t size;
  int status_code;
  char *content_type;
  size_t content_type_cap;
  char *body;
  size_t body_cap;
  size_t body_size;
} uvbrg_internal_http_response_t;

typedef int (*uvbrg_on_internal_http_cb)(
    uvbrg_handle_t h, uvbrg_session_t s,
    const uvbrg_internal_http_request_t *request,
    uvbrg_internal_http_response_t *response, void *user_data);

typedef int (*uvbrg_on_internal_ws_event_cb)(uvbrg_handle_t h,
                                             uvbrg_session_t s,
                                             const char *message_json,
                                             void *user_data);

typedef void (*uvbrg_on_internal_ws_open_cb)(
    uvbrg_handle_t h, uvbrg_internal_connection_t connection,
    void *user_data);
typedef int (*uvbrg_on_internal_ws_message_cb)(
    uvbrg_handle_t h, uvbrg_internal_connection_t connection,
    const char *message, size_t message_size, void *user_data);
typedef void (*uvbrg_on_internal_ws_close_cb)(
    uvbrg_handle_t h, uvbrg_internal_connection_t connection,
    const char *reason, void *user_data);

typedef struct uvbrg_internal_route_callbacks_s {
  size_t size;
  uvbrg_on_internal_http_cb on_http;
  void *user_data;
  uvbrg_on_internal_ws_event_cb on_ws_event;
  uvbrg_on_internal_ws_open_cb on_ws_open;
  uvbrg_on_internal_ws_message_cb on_ws_message;
  uvbrg_on_internal_ws_close_cb on_ws_close;
} uvbrg_internal_route_callbacks_t;

typedef struct uvbrg_stats_s {
  uint16_t listen_port;
  uint32_t listener_count;
  uint32_t active_sessions;
  uint32_t max_sessions;
  uint64_t accepted_sessions;
  uint64_t closed_sessions;
  uint64_t rejected_sessions;
  uint64_t pending_dns;
  uint64_t queued_write_bytes;
  uint64_t max_queued_write_bytes;
  uint64_t pending_write_requests;
  uint64_t max_pending_write_requests;
  uint64_t queued_commands;
  uint64_t queued_command_bytes;
  uint64_t max_queued_commands;
  uint64_t max_queued_command_bytes;
  uint64_t command_queue_rejections;
  uint64_t eof_events;
  uint64_t read_pause_events;
  uint64_t read_resume_events;
  uint64_t paused_read_sides;
  uint64_t listener_close_events;
  uint64_t loop_close_failures;
  uint32_t shared_reactor_index;
  int shared_reactor;
} uvbrg_stats_t;

typedef struct uvbrg_shared_reactor_stats_s {
  uint32_t reactor_count;
  uint64_t active_leases;
  uint64_t queued_tasks;
  uint64_t queued_bytes;
  uint64_t rejected_tasks;
  uint64_t post_failures;
  uint64_t loop_close_failures;
  int accepting;
  int quarantined;
} uvbrg_shared_reactor_stats_t;

typedef struct uvbrg_listener_stats_s {
  uint16_t listen_port;
  uint32_t active_sessions;
  uint64_t accepted_sessions;
  uint64_t rejected_sessions;
  int closing;
} uvbrg_listener_stats_t;

uvbrg_handle_t uvbrg_init(const char *config_json);
// Additive internal adapter used by BroSDK. Each handle keeps independent
// listeners, sessions, config, and callbacks while its libuv handles run on a
// shared 1..4 reactor selected by routing_key.
uvbrg_handle_t uvbrg_init_shared(const char *config_json,
                                 const char *routing_key,
                                 uint32_t reactor_count);
void uvbrg_set_callbacks(uvbrg_handle_t h, const uvbrg_callbacks_t *cbs);
int uvbrg_set_internal_route_callbacks(
    uvbrg_handle_t h, const uvbrg_internal_route_callbacks_t *cbs);
int uvbrg_internal_ws_send(uvbrg_handle_t h,
                           uvbrg_internal_connection_t connection,
                           const char *message, size_t message_size);
int uvbrg_internal_http_complete(uvbrg_handle_t h,
                                 uvbrg_internal_connection_t connection,
                                 int status_code, const char *content_type,
                                 const char *body, size_t body_size);

int uvbrg_register_instance(uvbrg_handle_t h, const char *instance_token,
                            const char *instance_meta_json);
int uvbrg_unregister_instance(uvbrg_handle_t h, const char *instance_token);

int uvbrg_start_listener(uvbrg_handle_t h, const char *addr, uint16_t port);
int uvbrg_start_listener_ex(uvbrg_handle_t h, const char *addr,
                            uint16_t requested_port, uint16_t *bound_port);
int uvbrg_start_listener_with_handle(uvbrg_handle_t h, const char *addr,
                                     uint16_t requested_port,
                                     uint16_t *bound_port,
                                     uvbrg_listener_t *listener);
int uvbrg_add_listener(uvbrg_handle_t h, const char *addr,
                       uint16_t requested_port, uint16_t *bound_port,
                       uvbrg_listener_t *listener);
int uvbrg_add_listener_with_config(uvbrg_handle_t h,
                                   const char *listener_config_json,
                                   uint16_t *bound_port,
                                   uvbrg_listener_t *listener);
int uvbrg_close_listener(uvbrg_handle_t h, uvbrg_listener_t listener);
// Update a listener's routing/security configuration without closing its
// listening socket.  The JSON uses the same schema accepted by uvbrg_init.
// When close_sessions is non-zero, established external sessions belonging to
// the listener are drained; internal MCP HTTP/WebSocket sessions remain alive.
// New sessions always use the new configuration.  The listener handle and
// bound port remain unchanged.  A zero/omitted listen port in the update JSON
// means "preserve the listener's current bound port".
int uvbrg_update_listener_config(uvbrg_handle_t h, uvbrg_listener_t listener,
                                 const char *listener_config_json,
                                 int close_sessions);
int uvbrg_run(uvbrg_handle_t h);
void uvbrg_stop(uvbrg_handle_t h);
int uvbrg_wait_stopped(uvbrg_handle_t h, uint32_t timeout_ms);
int uvbrg_is_shared(uvbrg_handle_t h);
int uvbrg_get_shared_reactor_stats(uvbrg_shared_reactor_stats_t *stats);
int uvbrg_shutdown_shared_reactors(uint32_t timeout_ms);

char *uvbrg_query_sessions(uvbrg_handle_t h, const char *filter_json);
int uvbrg_get_stats(uvbrg_handle_t h, uvbrg_stats_t *stats);
// Thread-safe dynamic admission ceiling. Zero pauses new accepts; existing
// sessions are untouched. Applies in addition to each listener's static limit.
int uvbrg_set_session_budget(uvbrg_handle_t h, uint32_t maximum);
int uvbrg_get_listener_stats(uvbrg_handle_t h, uvbrg_listener_t listener,
                             uvbrg_listener_stats_t *stats);
void uvbrg_free_string(char *s);
void uvbrg_destroy(uvbrg_handle_t h);

#ifdef __cplusplus
} // extern "C"

#include <memory>
#include <string>

struct ProxyBridgeImpl;

class UvBridge {
public:
  using Listener = uvbrg_listener_t;

  explicit UvBridge();
  ~UvBridge();

  bool init(const std::string &config_json);
  bool start_listener(const std::string &addr, uint16_t port);
  bool start_listener(const std::string &addr, uint16_t requested_port,
                      uint16_t &bound_port);
  bool add_listener(const std::string &addr, uint16_t requested_port,
                    uint16_t &bound_port, Listener &listener);
  bool close_listener(Listener listener);
  bool update_listener_config(Listener listener,
                              const std::string &config_json,
                              bool close_sessions = true);
  bool register_instance(const std::string &token,
                         const std::string &meta_json = "{}");
  bool unregister_instance(const std::string &token);
  void run();
  void stop();
  std::string query_sessions(const std::string &filter = {});

private:
  std::unique_ptr<ProxyBridgeImpl> impl_;
};

#endif // __cplusplus

#endif // LIBUVBRG_H_
