#pragma once
// Internal implementation structures for libuvbrg.
// Not part of the public API.

#include <libuvbrg.h>
extern "C" {
#include <uv.h>
}

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ── Constants
// ─────────────────────────────────────────────────────────────────
static constexpr size_t kReadBufSize = 65536;
static constexpr size_t kMaxHeaderBuf = 32768; // max buffered HTTP header
static constexpr uint64_t kDefaultWriteHighWatermark = 8ull * 1024 * 1024;
static constexpr uint64_t kDefaultWriteLowWatermark = 2ull * 1024 * 1024;
static constexpr uint32_t kDefaultUpstreamDnsTimeoutMs = 1500;
static constexpr uint32_t kDefaultTargetDnsTimeoutMs = 8000;
static constexpr uint32_t kDefaultTargetConnectTimeoutMs = 8000;
static constexpr uint32_t kDefaultUpstreamConnectTimeoutMs = 2000;
static constexpr uint32_t kDefaultUpstreamHandshakeTimeoutMs = 2500;
static constexpr uint32_t kDefaultCircuitBreakerOpenMs = 2000;
static constexpr uint32_t kDefaultCircuitBreakerFailureThreshold = 2;
static constexpr size_t kDefaultInternalRouteMaxBodyBytes = 1024 * 1024;
static constexpr size_t kMaxQueuedBridgeCommands = 4096;
static constexpr size_t kMaxQueuedBridgeCommandBytes = 64ull * 1024 * 1024;
static constexpr size_t kReservedBridgeCommands = 256;
static constexpr size_t kReservedBridgeCommandBytes = 4ull * 1024 * 1024;

// ── Forward declarations
// ──────────────────────────────────────────────────────
struct Listener;
struct Session;
struct ProxyBridgeImpl;
namespace uvbrg::internal {
class SharedLoopLease;
}

// ── Upstream proxy config
// ─────────────────────────────────────────────────────
enum class UpstreamType { None, HttpConnect, Socks5 };

struct UpstreamAuth {
  std::string type; // "basic" | "socks5"
  std::string user;
  std::string pass;
};

struct UpstreamConfig {
  UpstreamType type{UpstreamType::None};
  std::string addr;
  uint16_t port{0};
  UpstreamAuth auth;
};

// ── Bridge configuration
// ──────────────────────────────────────────────────────
enum class AuthMode { None, Token, Basic };

struct BridgeConfig {
  std::string listen_addr{"127.0.0.1"};
  uint16_t listen_port{1080};
  AuthMode auth_mode{AuthMode::None};
  uint32_t token_lifetime_sec{3600};
  std::vector<UpstreamConfig> upstreams;
  // Opt-in proxy-only route. Empty/invalid chains and bypasses are rejected.
  bool strict_proxy{false};
  std::string security_strategy;
  std::vector<std::string> security_whitelist;
  std::vector<std::string> security_blacklist;
  std::vector<std::string> security_bypass_list;
  std::vector<std::string> proxy_bypass_list;
  bool security_enable{false};
  bool security_auto_match_subdomains{false};
  bool tls_fingerprint{true};
  bool traffic_features{true};
  bool http_fingerprint{true};  // JA4H + OS detection
  bool tcp_fingerprint{true};   // TCP socket parameters
  bool proxy_fingerprint{true}; // upstream proxy analysis
  uint32_t max_sessions{0};     // 0 means unlimited
  uint32_t tunnel_connect_timeout_ms{15000};
  uint64_t write_high_watermark{kDefaultWriteHighWatermark};
  uint64_t write_low_watermark{kDefaultWriteLowWatermark};
  uint32_t upstream_dns_timeout_ms{kDefaultUpstreamDnsTimeoutMs};
  uint32_t target_dns_timeout_ms{kDefaultTargetDnsTimeoutMs};
  uint32_t target_connect_timeout_ms{kDefaultTargetConnectTimeoutMs};
  uint32_t upstream_connect_timeout_ms{kDefaultUpstreamConnectTimeoutMs};
  uint32_t upstream_handshake_timeout_ms{kDefaultUpstreamHandshakeTimeoutMs};
  uint32_t circuit_breaker_open_ms{kDefaultCircuitBreakerOpenMs};
  uint32_t circuit_breaker_failure_threshold{
      kDefaultCircuitBreakerFailureThreshold};
  bool internal_route_enable{false};
  std::vector<std::string> internal_route_hosts;
  std::string internal_route_path_prefix{"/extension"};
  std::string internal_route_websocket_path{"/extension/ws"};
  std::vector<std::string> internal_route_allowed_origins;
  bool internal_route_require_origin{true};
  size_t internal_route_max_body_bytes{kDefaultInternalRouteMaxBodyBytes};
  size_t internal_route_max_websocket_frame_bytes{
      kDefaultInternalRouteMaxBodyBytes};
};

// ── Registered instance
// ───────────────────────────────────────────────────────
struct InstanceInfo {
  std::string instance_id; // same as token key
  std::string profile_id;
  std::string user_agent;
  uint64_t registered_ms{0};
  uint32_t lifetime_sec{3600};
};

// ── Session state
// ─────────────────────────────────────────────────────────────
enum class SessionState {
  ReadingRequest, // Accumulating initial bytes; detecting protocol
  // HTTP states
  HttpConnecting,        // Resolving / connecting for CONNECT tunnel
  HttpTunneling,         // CONNECT tunnel active — relay bytes passthrough
  HttpForwardConnecting, // Resolving / connecting for plain HTTP forward
  HttpForwarding,        // Forwarding plain HTTP request / response
  // SOCKS5 states
  Socks5Greeting,   // Read SOCKS5 VER+NMETHODS+METHODS
  Socks5Auth,       // Read SOCKS5 user/pass auth
  Socks5ConnectReq, // Read SOCKS5 CONNECT request
  Socks5Connecting, // Resolving / connecting for SOCKS5
  Socks5Tunneling,  // SOCKS5 tunnel active — relay bytes passthrough
  // Upstream handshake states (used after connecting to upstream proxy)
  UpstreamHttpConnect,   // Sent HTTP CONNECT to upstream; awaiting 200
  UpstreamSocks5Greet,   // Sent SOCKS5 greeting to upstream; awaiting method
  UpstreamSocks5Auth,    // Sent SOCKS5 auth to upstream; awaiting reply
  UpstreamSocks5Connect, // Sent SOCKS5 CONNECT to upstream; awaiting reply
  InternalHttp,          // Internal host HTTP fallback handled in-process
  InternalWebSocket,     // Internal host WebSocket handled in-process
  // Terminal
  Closing,
};

enum class ClientProtocol {
  Unknown,
  HttpConnect,
  HttpForward,
  Socks5,
};

// ── Heap-allocated write request ─────────────────────────────────────────────
struct WriteReq {
  uv_write_t req{};
  std::vector<char> data; // owns the buffer
  Session *session{nullptr};
  bool close_after_write{false};
  bool tunnel_delivery{false};

  static WriteReq *make(Session *session, const void *buf, size_t len,
                        bool close_after_write = false) {
    auto *wr = new WriteReq;
    wr->data.assign(static_cast<const char *>(buf),
                    static_cast<const char *>(buf) + len);
    wr->session = session;
    wr->close_after_write = close_after_write;
    wr->req.data = wr;
    return wr;
  }
};

// ── TLS fingerprint
// ───────────────────────────────────────────────────────────
struct TlsFingerprint {
  uint16_t version{0};
  std::vector<uint16_t> ciphers;
  std::vector<uint16_t> ext_types;
  std::vector<uint16_t> groups;    // supported_groups
  std::vector<uint8_t> ec_pt_fmts; // ec_point_formats
  std::vector<uint16_t> sig_algs;  // signature_algorithms (0x000d)
  std::string sni;
  std::vector<std::string> alpn;
  std::string ja3;
  std::string ja3_hash;
  std::string ja4;     // JA4 fingerprint
  std::string ja4_raw; // JA4_r (raw, unhashed values)
  bool extracted{false};

  std::string to_json() const;
};

// ── Session
// ───────────────────────────────────────────────────────────────────
// IMPORTANT: `client` must be the first member so that a Session* and a
// pointer to its uv_tcp_t client handle are interchangeable via
// reinterpret_cast.
struct Session {
  uv_tcp_t client{};           // client <-> bridge socket  [MUST BE FIRST]
  uv_tcp_t remote{};           // bridge <-> target/upstream socket
  uv_getaddrinfo_t resolver{}; // DNS lookup handle
  uv_connect_t conn_req{};     // outbound TCP connect request
  uv_timer_t phase_timer{};    // bounds DNS/connect/upstream handshake latency
  uv_shutdown_t client_shutdown_req{};
  uv_shutdown_t remote_shutdown_req{};

  ProxyBridgeImpl *bridge{nullptr};
  Listener *listener{nullptr};
  // Configuration is captured when the client is accepted.  Listener
  // reconfiguration must never mutate the policy observed by an in-flight
  // handshake/tunnel; this also makes the snapshot safe when old sessions are
  // drained asynchronously after a route switch.
  BridgeConfig config;
  SessionState state{SessionState::ReadingRequest};
  ClientProtocol client_protocol{ClientProtocol::Unknown};
  std::string session_id;
  std::string instance_id;
  std::string profile_id;
  uvbrg_internal_connection_t internal_connection{};

  // Client-origin request bytes. Never reuse for remote-origin reply tails.
  std::vector<uint8_t> client_buf;
  // Current upstream reply, then any bytes belonging to the next hop or final
  // target. HTTP headers are limited separately from trailing business data.
  std::vector<uint8_t> upstream_buf;
  // Data to replay to the remote after an upstream handshake completes
  // (only relevant for plain HTTP forward proxy via upstream)
  std::vector<uint8_t> replay_buf;

  // Resolution target (may differ from final target when using upstream proxy)
  std::string target_host;
  uint16_t target_port{0};

  // TLS fingerprint (extracted passively inside CONNECT / SOCKS5 tunnels)
  TlsFingerprint fingerprint;

  // Traffic statistics
  uint64_t bytes_up{0};
  uint64_t bytes_down{0};
  uint64_t pkt_count_up{0};
  uint64_t pkt_count_down{0};

  // HTTP request fields (plain HTTP forward proxy)
  std::string http_method;
  std::string http_version;
  std::string http_path; // path+query (relative form after stripping host)
  std::vector<std::pair<std::string, std::string>> http_headers;
  std::vector<uint8_t> http_body_tail; // bytes after \r\n\r\n in first read

  // Per-session auth
  bool auth_ok{false};

  // SOCKS5 negotiation
  bool socks5_need_auth{false}; // true when we demanded user/pass
  uint8_t socks5_atyp{0};

  // Lifecycle flags
  bool remote_initialized{false};
  bool tunnel{false};
  bool tunnel_waiting{false};
  bool resolver_pending{false};
  bool phase_timer_initialized{false};
  bool phase_timer_closed{false};
  bool client_reading{false};
  bool remote_reading{false};
  bool client_paused_for_backpressure{false};
  bool remote_paused_for_backpressure{false};
  // Relay EOF is propagated only after writes queued for the opposite stream
  // complete; embedded shutdown requests keep callback ownership in Session.
  size_t pending_client_writes{0};
  size_t pending_remote_writes{0};
  bool client_read_eof{false};
  bool remote_read_eof{false};
  bool client_shutdown_pending{false};
  bool remote_shutdown_pending{false};
  bool client_shutdown_done{false};
  bool remote_shutdown_done{false};
  bool client_closed{false};
  bool remote_closed{false};
  bool closing{false};
  bool close_deferred{false};
  bool internal_http_deferred{false};
  bool internal_ws_open_notified{false};
  bool internal_ws_close_notified{false};
  std::string internal_http_origin;
  std::string internal_close_reason;

  // Index of the upstream proxy currently being handshaked in the chain.
  // upstreams[0] is the first hop (jump); last entry is the target proxy.
  // Incremented by advance_upstream() after each hop completes.
  size_t upstream_idx{0};
  bool proxy_bypass{false};
  std::string phase_timeout_reason;
  std::string phase_upstream_key;

  uint64_t start_ms{0};
};

struct CircuitBreakerEntry {
  uint32_t consecutive_failures{0};
  uint64_t open_until_ms{0};
  std::string reason;
  bool ever_succeeded{false};
  uint64_t last_success_ms{0};
};

struct Listener {
  ProxyBridgeImpl *bridge{nullptr};
  uint64_t id{0};
  uv_tcp_t server{};
  BridgeConfig config;
  std::atomic<uint16_t> port{0};
  std::atomic_bool listening{false};
  std::atomic_bool closing{false};
  std::atomic_bool server_closed{false};
  std::atomic<uint64_t> accepted_sessions{0};
  std::atomic<uint64_t> rejected_sessions{0};
};

struct BridgeCommand {
  enum class State { Queued, Executing, Completed, Canceled };
  enum class Kind {
    AddListener,
    CloseListener,
    UpdateListenerConfig,
    InternalWsSend,
    InternalHttpComplete,
    TunnelEvent,
  };

  Kind kind{Kind::AddListener};
  std::string addr;
  std::string config_json;
  uint16_t requested_port{0};
  Listener *listener{nullptr};
  uint64_t listener_id{0};
  bool make_default{false};
  bool close_sessions{true};
  uint16_t bound_port{0};
  uvbrg_internal_connection_t connection{};
  int status_code{0};
  std::string content_type;
  std::string payload;
  std::atomic<int> result{0};
  std::atomic<State> state{State::Queued};
  bool done{false};
  std::mutex done_mu;
  std::condition_variable done_cv;

  size_t estimated_bytes() const {
    return 256 + addr.size() + config_json.size() +
           content_type.size() + payload.size();
  }

  bool is_control() const {
    return kind == Kind::CloseListener || kind == Kind::InternalHttpComplete;
  }
};

enum class BridgeLifecycleState { New, Running, Stopping, Stopped, Destroyed };

// ── Utility: millisecond timestamp ───────────────────────────────────────────
inline uint64_t now_ms() noexcept {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

// ── Utility: simple base64 decode ────────────────────────────────────────────
inline std::string base64_decode(const std::string &in) {
  static const int8_t kTbl[256] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 0-15
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, // 16-31
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, // 32-47
      52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, // 48-63
      -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, // 64-79
      15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, // 80-95
      -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, // 96-111
      41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, // 112-127
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  };
  std::string out;
  out.reserve(in.size() * 3 / 4);
  uint32_t acc = 0;
  int bits = 0;
  for (unsigned char c : in) {
    if (c == '=')
      break;
    int8_t v = kTbl[c];
    if (v < 0)
      continue;
    acc = (acc << 6) | (uint8_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((acc >> bits) & 0xff);
    }
  }
  return out;
}

// ── ProxyBridgeImpl
// ───────────────────────────────────────────────────────────
struct ProxyBridgeImpl {
  std::atomic<uint32_t> session_budget{UINT32_MAX};
  uv_loop_t owned_loop{};
  uv_loop_t *loop{&owned_loop};
  uv_async_t stop_async{}; // thread-safe stop signal
  uv_async_t command_async{};

  BridgeConfig config;
  uvbrg_callbacks_t callbacks{};
  uvbrg_execution_guard_t execution_guard{};
  uvbrg_internal_route_callbacks_t internal_route_callbacks{};
  uvbrg_tunnel_callbacks_t tunnel_callbacks{};

  std::mutex instances_mu;
  std::unordered_map<std::string /*token*/, InstanceInfo> instances;

  std::mutex sessions_mu;
  std::unordered_map<std::string /*session_id*/, Session *> sessions;

  mutable std::mutex state_mu;
  std::vector<std::unique_ptr<Listener>> listeners;
  Listener *default_listener{nullptr};
  std::thread::id loop_thread_id;
  std::unordered_map<std::string, CircuitBreakerEntry> circuit_breakers;

  bool loop_initialized{false};
  bool shared_reactor{false};
  uint32_t shared_reactor_index{0};
  std::shared_ptr<uvbrg::internal::SharedLoopLease> shared_loop_lease;
  std::atomic_bool stop_async_initialized{false};
  std::atomic_bool command_async_initialized{false};
  std::atomic_bool running{false};
  std::atomic_bool destroy_when_stopped{false};
  mutable std::mutex lifecycle_mu;
  BridgeLifecycleState lifecycle_state{BridgeLifecycleState::New};
  std::mutex commands_mu;
  std::deque<std::shared_ptr<BridgeCommand>> commands;
  std::atomic<uint64_t> session_counter{0};
  std::atomic<uint64_t> listener_counter{0};
  std::atomic<uint64_t> internal_connection_counter{0};
  uint64_t internal_connection_generation{1};
  std::atomic<uint64_t> accepted_sessions{0};
  std::atomic<uint64_t> closed_sessions{0};
  std::atomic<uint64_t> rejected_sessions{0};
  std::atomic<uint64_t> pending_dns{0};
  std::atomic<uint64_t> queued_write_bytes{0};
  std::atomic<uint64_t> max_queued_write_bytes{0};
  std::atomic<uint64_t> pending_write_requests{0};
  std::atomic<uint64_t> max_pending_write_requests{0};
  std::atomic<uint64_t> queued_commands{0};
  std::atomic<uint64_t> queued_command_bytes{0};
  std::atomic<uint64_t> max_queued_commands{0};
  std::atomic<uint64_t> max_queued_command_bytes{0};
  std::atomic<uint64_t> command_queue_rejections{0};
  std::atomic<uint64_t> eof_events{0};
  std::atomic<uint64_t> read_pause_events{0};
  std::atomic<uint64_t> read_resume_events{0};
  std::atomic<uint64_t> paused_read_sides{0};
  std::atomic<uint64_t> listener_close_events{0};
  std::atomic<uint64_t> loop_close_failures{0};
  mutable std::mutex stopped_mu;
  std::condition_variable stopped_cv;
  bool stopped_signaled{false};

  // Thread-safe stop request
  void request_stop() {
    if (stop_async_initialized.load(std::memory_order_acquire)) {
      uv_async_send(&stop_async);
    }
  }

  std::string next_session_id() {
    return "s" + std::to_string(++session_counter);
  }

  void observe_queued_write_bytes(uint64_t value) {
    uint64_t current = max_queued_write_bytes.load(std::memory_order_relaxed);
    while (current < value &&
           !max_queued_write_bytes.compare_exchange_weak(
               current, value, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
  }

  static void observe_max(std::atomic<uint64_t> &target, uint64_t value) {
    uint64_t current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(current, value,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed)) {
    }
  }

  // Lookup instance by token (NOT thread-safe; caller must hold instances_mu).
  // Returns nullptr if not found or expired.
  const InstanceInfo *find_instance_locked(const std::string &token) const {
    auto it = instances.find(token);
    if (it == instances.end())
      return nullptr;
    if (it->second.lifetime_sec > 0) {
      uint64_t age = (now_ms() - it->second.registered_ms) / 1000;
      if (age > it->second.lifetime_sec)
        return nullptr;
    }
    return &it->second;
  }

  // Serialize active sessions as a JSON array (caller holds sessions_mu).
  std::string sessions_json_locked() const;
};
