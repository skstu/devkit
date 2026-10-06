// libbridge.cc — libuv + OpenSSL based Chromium proxy bridge
// Handles HTTP CONNECT tunnels, plain HTTP proxy, and SOCKS5.
// Passively extracts TLS JA3/JA4, HTTP JA4H, TCP socket, and proxy
// fingerprints.
#define TLS_FINGERPRINT_INCLUDED_VIA_BRIDGE_IMPL 1
#include "bridge_impl.h"
#include "tls_fingerprint.h"
#include "http_fingerprint.h"
#include "tcp_fingerprint.h"
#include "proxy_fingerprint.h"
#include "shared_reactor.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <random>
#include <string>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#if defined(min)
#undef min
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

// JSON configuration parsing
#include <nlohmann/json.hpp>
#include <limits>
/*
Single-threaded libuv event loop — no mutex overhead on the hot path
Protocol detection at first read: 0x05 → SOCKS5 state machine; CONNECT/GET/... →
HTTP handler HTTP CONNECT: tunnel established after 200 Connection established
sent, then passive TLS fingerprint on first relay bytes HTTP Forward Proxy:
strips proxy headers, buffers and replays request to remote SOCKS5: full
greeting/auth/connect state machine; username = instance_id, password =
registered token Upstream proxies: supports HTTP-CONNECT (Basic auth) and SOCKS5
(user/pass) via BridgeConfig.upstreams[] Auth modes: none, Bearer token header,
Basic proxy auth — all bind request to a registered instance_id Session
lifecycle safety: resolver_pending + dual close callbacks prevent use-after-free
during async cancellation
*/

/*2026/3/21 HangZhou: NOTE: This file is included in the single translation unit
of libbridge.cc; it is not intended to be compiled separately.  It can include
headers from src/ but should not include bridge_impl.h or other internal headers
directly. 实现摘要
1. JA4 TLS 指纹（扩展 tls_fingerprint.h）
在原有 JA3 基础上新增 JA4：

新字段：sig_algs（签名算法列表）、ja4、ja4_raw（明文版本）
格式：t{tls版本}{sni指示}{密码套件数:02d}{扩展数:02d}{ALPN前2字符}_{密码套件SHA256-12}_{扩展+签名算法SHA256-12}
自动过滤 GREASE 值，正确排序后再做哈希
on_fingerprint 回调的 JSON 现在同时携带 ja3、ja3_hash、ja4、ja4_raw
2. HTTP 流量指纹（新建 http_fingerprint.h）
JA4H 指纹 + 操作系统/浏览器识别，揭示你的 HTTP 流量泄露了什么信息：

detected_os：从 User-Agent 推断 Windows 10/11、macOS 14.x、Android 13、iOS
17、Linux 等 detected_browser：识别
Chrome、Firefox、Safari、Edge、curl、Python/requests 等 ja4h：标准 JA4H 格式，含
Header 顺序哈希、Cookie 字段哈希 触发时机：收到客户端 HTTP
CONNECT/请求后立即触发 on_http_fingerprint 回调
3. TCP 套接字指纹（新建 tcp_fingerprint.h）
读取已建立连接的内核 TCP 参数，刻画对端（上游代理/目标）的网络栈：

mss：协商后的最大分段大小
recv_buf/send_buf：内核缓冲区（Windows 默认 87380 = 未自动调整）
rtt_us/rtt_var_us（Linux）：实测往返延迟及抖动
cwnd_segs/retrans（Linux）：拥塞窗口和重传次数
platform_hint：基于以上数值的 OS 启发式推断（windows-classic / linux-large-buf /
macos-likely） 触发时机：与上游代理/目标 TCP 连接建立成功后触发
on_tcp_fingerprint 回调
4. 上游代理应答指纹（新建 proxy_fingerprint.h）
分析上游代理对 HTTP CONNECT 的响应头，判断代理软件和其操作系统：

识别
Squid、TinyProxy、CCProxy（Windows）、Nginx、Apache、HAProxy、Envoy、Cloudflare
CDN、AWS CloudFront 等 proxy_os_hint：推断代理运行在 linux、windows 还是 bsd
proxy_type：open-source/forward、commercial/forward、cdn 等
触发时机：收到上游代理 200 响应后、进入透明隧道模式之前触发 on_proxy_fingerprint
回调
5. API 变更（libbridge.h / bridge_impl.h）
pvb_callbacks_t 新增三个可选回调（零初始化即可禁用，向后兼容）：
pvb_on_http_fingerprint_cb  on_http_fingerprint;
pvb_on_tcp_fingerprint_cb   on_tcp_fingerprint;
pvb_on_proxy_fingerprint_cb on_proxy_fingerprint;

配置 JSON 中对应三个新开关（默认全开）:
"features": {
  "http_fingerprint":  true,
  "tcp_fingerprint":   true,
  "proxy_fingerprint": true
}
*/
// ═════════════════════════════════════════════════════════════════════════════
// Internal helper prototypes
// ═════════════════════════════════════════════════════════════════════════════
static void close_session(Session *s);
static void try_free_session(Session *s);
static void free_session(Session *s);
static void start_dns_resolve(Session *s, const std::string &host,
                              uint16_t port);
static void alloc_cb(uv_handle_t *h, size_t suggested, uv_buf_t *buf);
static int write_to(uv_stream_t *stream, const void *data, size_t len);
static int write_str(uv_stream_t *stream, const std::string &s);
static int write_to_and_close(uv_stream_t *stream, const void *data,
                              size_t len);
static int write_str_and_close(uv_stream_t *stream, const std::string &s);
static void start_relay(Session *s);
static void on_remote_read(uv_stream_t *stream, ssize_t nread,
                           const uv_buf_t *buf);
static void on_tunnel_client_read(uv_stream_t *stream, ssize_t nread,
                                  const uv_buf_t *buf);
static void on_forward_remote_read(uv_stream_t *stream, ssize_t nread,
                                   const uv_buf_t *buf);
static void on_forward_client_read(uv_stream_t *stream, ssize_t nread,
                                   const uv_buf_t *buf);
static void on_internal_client_read(uv_stream_t *stream, ssize_t nread,
                                    const uv_buf_t *buf);
static void on_internal_ws_client_read(uv_stream_t *stream, ssize_t nread,
                                       const uv_buf_t *buf);
static void on_internal_deferred_http_read(uv_stream_t *stream, ssize_t nread,
                                           const uv_buf_t *buf);
static int write_ws_frame(Session *s, uint8_t opcode,
                          const std::string &payload);
static std::string build_internal_http_response(
    int status, const std::string &content_type, const std::string &body,
    const std::string &origin);
static void on_new_connection(uv_stream_t *server, int status);
static bool parse_config(const char *json_str, BridgeConfig &cfg);
static const BridgeConfig &session_config(const Session *s);
static const std::vector<UpstreamConfig> &session_upstreams(const Session *s);
static void process_bridge_command(ProxyBridgeImpl *b,
                                   const std::shared_ptr<BridgeCommand> &cmd);
static void close_finished_listeners(ProxyBridgeImpl *b);
static void maybe_finish_shared_bridge(ProxyBridgeImpl *b);
static void on_listener_closed(uv_handle_t *h);
static void on_client_closed(uv_handle_t *h);
static void maybe_schedule_relay_shutdowns(Session *s);
static void maybe_finish_relay_half_close(Session *s);
static void emit_bridge_error(Session *s, int code, const char *reason,
                              const std::string &message, int hop_index = -1,
                              int http_status = 0, int socks5_reply = -1);
static void emit_proxy_bypass_step(Session *s,
                                   const std::string &matched_rule);
static void emit_resource_domain_allow_step(Session *s,
                                            const char *protocol_name);
static void arm_phase_timer(Session *s, uint32_t timeout_ms,
                            const char *reason);
static void stop_phase_timer(Session *s);
static void send_fast_failure_to_client(Session *s, const char *reason);
static void send_connect_established(Session *s);
static void start_http_forward_relay(Session *s);
static bool fail_fast_if_circuit_open(Session *s, size_t hop_index);
static void mark_upstream_failure(Session *s, const char *reason,
                                  int hop_index = -1);
static void mark_upstream_success(Session *s, size_t hop_index);
// Multi-hop proxy chain helpers (defined after the upstream send functions)
static void advance_upstream(Session *s);
static void get_connect_target(const Session *s, std::string &host,
                               uint16_t &port);

static std::string to_lower(std::string s) {
  for (char &c : s)
    c = static_cast<char>(tolower((unsigned char)c));
  return s;
}

static std::string json_escape(const std::string &s) {
  const auto quoted = nlohmann::json(s).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
  return quoted.substr(1, quoted.size() - 2);
}

static const char *upstream_type_name(UpstreamType type) {
  switch (type) {
  case UpstreamType::HttpConnect:
    return "http";
  case UpstreamType::Socks5:
    return "socks5";
  default:
    return "none";
  }
}

static const char *session_state_name(SessionState state) {
  switch (state) {
  case SessionState::ReadingRequest:
    return "ReadingRequest";
  case SessionState::HttpConnecting:
    return "HttpConnecting";
  case SessionState::HttpTunneling:
    return "HttpTunneling";
  case SessionState::HttpForwardConnecting:
    return "HttpForwardConnecting";
  case SessionState::HttpForwarding:
    return "HttpForwarding";
  case SessionState::Socks5Greeting:
    return "Socks5Greeting";
  case SessionState::Socks5Auth:
    return "Socks5Auth";
  case SessionState::Socks5ConnectReq:
    return "Socks5ConnectReq";
  case SessionState::Socks5Connecting:
    return "Socks5Connecting";
  case SessionState::Socks5Tunneling:
    return "Socks5Tunneling";
  case SessionState::UpstreamHttpConnect:
    return "UpstreamHttpConnect";
  case SessionState::UpstreamSocks5Greet:
    return "UpstreamSocks5Greet";
  case SessionState::UpstreamSocks5Auth:
    return "UpstreamSocks5Auth";
  case SessionState::UpstreamSocks5Connect:
    return "UpstreamSocks5Connect";
  case SessionState::InternalHttp:
    return "InternalHttp";
  case SessionState::InternalWebSocket:
    return "InternalWebSocket";
  case SessionState::Closing:
    return "Closing";
  default:
    return "Unknown";
  }
}

static std::string normalize_rule_host(std::string host) {
  host = to_lower(host);
  const auto scheme = host.find("://");
  if (scheme != std::string::npos)
    host = host.substr(scheme + 3);
  const auto path = host.find_first_of("/?#");
  if (path != std::string::npos)
    host.erase(path);
  if (!host.empty() && host.front() == '[') {
    const auto rb = host.find(']');
    if (rb != std::string::npos)
      host = host.substr(1, rb - 1);
  } else {
    const auto colon = host.rfind(':');
    if (colon != std::string::npos && host.find(':') == colon)
      host.erase(colon);
  }
  while (!host.empty() && host.back() == '.')
    host.pop_back();
  return host;
}

static bool is_local_host_name(const std::string &host) {
  const std::string h = normalize_rule_host(host);
  return h == "localhost" || h.find('.') == std::string::npos;
}

static bool wildcard_match(const std::string &text, const std::string &pattern,
                           size_t text_i = 0, size_t pattern_i = 0) {
  size_t star = std::string::npos;
  size_t match = 0;
  while (text_i < text.size()) {
    if (pattern_i < pattern.size() &&
        (pattern[pattern_i] == '?' || pattern[pattern_i] == text[text_i])) {
      ++text_i;
      ++pattern_i;
    } else if (pattern_i < pattern.size() && pattern[pattern_i] == '*') {
      star = pattern_i++;
      match = text_i;
    } else if (star != std::string::npos) {
      pattern_i = star + 1;
      text_i = ++match;
    } else {
      return false;
    }
  }
  while (pattern_i < pattern.size() && pattern[pattern_i] == '*')
    ++pattern_i;
  return pattern_i == pattern.size();
}

static bool parse_ipv4(const std::string &text, uint32_t &out) {
  in_addr addr{};
  if (inet_pton(AF_INET, text.c_str(), &addr) != 1)
    return false;
  out = ntohl(addr.s_addr);
  return true;
}

static bool ipv4_cidr_matches(const std::string &host,
                              const std::string &rule) {
  const auto slash = rule.find('/');
  if (slash == std::string::npos)
    return false;

  char *end = nullptr;
  const long bits = std::strtol(rule.c_str() + slash + 1, &end, 10);
  if (!end || *end != '\0' || bits < 0 || bits > 32)
    return false;

  uint32_t host_addr = 0;
  uint32_t net_addr = 0;
  if (!parse_ipv4(normalize_rule_host(host), host_addr) ||
      !parse_ipv4(normalize_rule_host(rule.substr(0, slash)), net_addr)) {
    return false;
  }

  const uint32_t mask =
      bits == 0 ? 0 : static_cast<uint32_t>(0xffffffffu << (32 - bits));
  return (host_addr & mask) == (net_addr & mask);
}

static bool host_matches_rule(const std::string &host,
                              const std::string &rule) {
  const std::string h = normalize_rule_host(host);
  if (h.empty())
    return false;
  const std::string raw_rule = to_lower(rule);
  if (ipv4_cidr_matches(h, raw_rule))
    return true;
  std::string r = normalize_rule_host(raw_rule);
  if (r.empty())
    return false;
  if (r == "*")
    return true;
  if (r == "<local>")
    return is_local_host_name(h);
  if (!r.empty() && r.front() == '.')
    r = "*" + r;
  if (r.find('*') != std::string::npos || r.find('?') != std::string::npos)
    return wildcard_match(h, r);
  return h == r || (h.size() > r.size() &&
                    h.compare(h.size() - r.size(), r.size(), r) == 0 &&
                    h[h.size() - r.size() - 1] == '.');
}

static bool host_matches_whitelist_rule(const std::string &host,
                                        const std::string &rule,
                                        bool auto_match_subdomains) {
  const std::string h = normalize_rule_host(host);
  std::string r = normalize_rule_host(to_lower(rule));
  if (h.empty() || r.empty())
    return false;
  if (r.size() > 2 && r[0] == '*' && r[1] == '.') {
    r.erase(0, 2);
    return h.size() > r.size() &&
           h.compare(h.size() - r.size(), r.size(), r) == 0 &&
           h[h.size() - r.size() - 1] == '.';
  }
  if (r.find('*') != std::string::npos || r.find('?') != std::string::npos)
    return false;
  return h == r ||
         (auto_match_subdomains && h.size() > r.size() &&
          h.compare(h.size() - r.size(), r.size(), r) == 0 &&
          h[h.size() - r.size() - 1] == '.');
}

static bool host_matches_security_rule(const std::string &host,
                                       const std::string &rule,
                                       bool auto_match_subdomains) {
  return host_matches_whitelist_rule(host, rule, auto_match_subdomains);
}

static bool list_matches_host(const std::vector<std::string> &rules,
                              const std::string &host,
                              std::string &matched_rule) {
  for (const auto &rule : rules) {
    if (host_matches_rule(host, rule)) {
      matched_rule = normalize_rule_host(rule);
      return true;
    }
  }
  return false;
}

static bool security_list_matches_host(const std::vector<std::string> &rules,
                                       const std::string &host,
                                       std::string &matched_rule,
                                       bool auto_match_subdomains) {
  for (const auto &rule : rules) {
    if (host_matches_security_rule(host, rule, auto_match_subdomains)) {
      matched_rule = normalize_rule_host(rule);
      return true;
    }
  }
  return false;
}

static bool whitelist_matches_host(const std::vector<std::string> &rules,
                                   const std::string &host,
                                   std::string &matched_rule,
                                   bool auto_match_subdomains) {
  for (const auto &rule : rules) {
    if (host_matches_whitelist_rule(host, rule, auto_match_subdomains)) {
      matched_rule = normalize_rule_host(rule);
      return true;
    }
  }
  return false;
}

static bool is_http_protocol(const Session *s) {
  return s && (s->client_protocol == ClientProtocol::HttpConnect ||
               s->client_protocol == ClientProtocol::HttpForward);
}

static bool maybe_redirect_or_block_security(Session *s,
                                             const char *protocol_name) {
  if (!s || !s->bridge)
    return false;
  const BridgeConfig &cfg = session_config(s);
  if (!cfg.security_enable)
    return false;
  const std::string strategy = to_lower(cfg.security_strategy);
  if (strategy != "whitelist" && strategy != "blacklist")
    return false;

  std::string matched_rule;
  std::string reason;
  bool blocked = false;
  if (security_list_matches_host(cfg.security_bypass_list, s->target_host,
                                 matched_rule,
                                 cfg.security_auto_match_subdomains)) {
    return false;
  }
  if (strategy == "whitelist") {
    if (!whitelist_matches_host(cfg.security_whitelist, s->target_host,
                                matched_rule,
                                cfg.security_auto_match_subdomains)) {
      blocked = true;
      reason = "blocked_by_whitelist";
    }
  } else if (strategy == "blacklist" &&
             security_list_matches_host(cfg.security_blacklist, s->target_host,
                                        matched_rule,
                                        cfg.security_auto_match_subdomains)) {
    blocked = true;
    reason = "blocked_by_blacklist";
  }
  if (!blocked)
    return false;

  if (strategy == "whitelist" && reason == "blocked_by_whitelist" &&
      s->bridge->callbacks.on_security_resource_allow &&
      s->bridge->callbacks.on_security_resource_allow(
          s->bridge, s, s->target_host.c_str(), s->target_port,
          protocol_name ? protocol_name : "") != 0) {
    emit_resource_domain_allow_step(s, protocol_name);
    return false;
  }

  std::string decision_json =
      std::string("{\"action\":\"block\",\"reason\":\"") +
      json_escape(reason) + "\",\"strategy\":\"" + json_escape(strategy) +
      "\",\"protocol\":\"" + json_escape(protocol_name ? protocol_name : "") +
      "\",\"host\":\"" + json_escape(s->target_host) +
      "\",\"port\":" + std::to_string(s->target_port) +
      ",\"matchedRule\":\"" + json_escape(matched_rule) +
      "\",\"sessionId\":\"" + json_escape(s->session_id) + "\"}";

  std::vector<char> redirect_url(64 * 1024, '\0');
  bool redirected = false;
  if (s->bridge->callbacks.on_security_decision) {
    redirected = s->bridge->callbacks.on_security_decision(
                     s->bridge, s, decision_json.c_str(), redirect_url.data(),
                     redirect_url.size()) != 0 &&
                 redirect_url[0] != '\0';
  }

  if (redirected && is_http_protocol(s)) {
    std::string response = "HTTP/1.1 302 Found\r\nLocation: ";
    response += redirect_url.data();
    response += "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                        response);
    return true;
  }

  if (redirected && s->client_protocol == ClientProtocol::Socks5 &&
      s->target_port == 80) {
    std::string response;
    const char ok[] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    response.append(ok, sizeof(ok));
    response += "HTTP/1.1 302 Found\r\nLocation: ";
    response += redirect_url.data();
    response += "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                        response);
    return true;
  }

  if (is_http_protocol(s)) {
    write_str_and_close(
        reinterpret_cast<uv_stream_t *>(&s->client),
        "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: "
        "close\r\n\r\n");
  } else {
    const uint8_t err[] = {0x05, 0x02, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
    write_to_and_close(reinterpret_cast<uv_stream_t *>(&s->client), err,
                       sizeof(err));
  }
  return true;
}

static bool apply_proxy_bypass(Session *s) {
  if (!s || s->proxy_bypass)
    return false;
  std::string matched_rule;
  if (!list_matches_host(session_config(s).proxy_bypass_list, s->target_host,
                         matched_rule)) {
    return false;
  }
  s->proxy_bypass = true;
  s->upstream_idx = 0;
  emit_proxy_bypass_step(s, matched_rule);
  return true;
}

static uint32_t clamp_timeout_ms(uint32_t value, uint32_t default_value) {
  if (value == 0)
    return default_value;
  return std::max<uint32_t>(100, std::min<uint32_t>(value, 30000));
}

static uint32_t clamp_circuit_breaker_threshold(uint32_t value) {
  if (value == 0)
    return kDefaultCircuitBreakerFailureThreshold;
  return std::max<uint32_t>(1, std::min<uint32_t>(value, 10));
}

static uint32_t circuit_breaker_warmup_threshold(uint32_t threshold) {
  const uint32_t warmed_threshold = clamp_circuit_breaker_threshold(threshold);
  return std::max<uint32_t>(5, std::min<uint32_t>(warmed_threshold * 3, 10));
}

static const char *socks5_reply_name(int code) {
  switch (code) {
  case 0x00:
    return "succeeded";
  case 0x01:
    return "general_failure";
  case 0x02:
    return "connection_not_allowed";
  case 0x03:
    return "network_unreachable";
  case 0x04:
    return "host_unreachable";
  case 0x05:
    return "connection_refused";
  case 0x06:
    return "ttl_expired";
  case 0x07:
    return "command_not_supported";
  case 0x08:
    return "address_type_not_supported";
  case 0xff:
    return "no_acceptable_methods";
  default:
    return "";
  }
}

static std::string upstream_circuit_key(const UpstreamConfig &up) {
  return std::string(upstream_type_name(up.type)) + "://" +
         to_lower(up.addr) + ":" + std::to_string(up.port) +
         (up.auth.user.empty() ? "" : "#auth");
}

static const UpstreamConfig *upstream_at(const Session *s, size_t hop_index) {
  if (!s)
    return nullptr;
  const auto &upstreams = session_upstreams(s);
  if (hop_index >= upstreams.size())
    return nullptr;
  return &upstreams[hop_index];
}

static bool fail_fast_if_circuit_open(Session *s, size_t hop_index) {
  if (!s || !s->bridge)
    return false;
  const UpstreamConfig *up = upstream_at(s, hop_index);
  if (!up)
    return false;
  const std::string key = upstream_circuit_key(*up);
  auto it = s->bridge->circuit_breakers.find(key);
  if (it == s->bridge->circuit_breakers.end())
    return false;
  const uint64_t now = now_ms();
  if (it->second.open_until_ms == 0)
    return false;
  if (it->second.open_until_ms <= now) {
    it->second.consecutive_failures = 0;
    it->second.open_until_ms = 0;
    it->second.reason.clear();
    return false;
  }

  s->upstream_idx = hop_index;
  const std::string reason = it->second.reason.empty()
                                 ? "upstream_circuit_open"
                                 : it->second.reason;
  emit_bridge_error(s, UV_ECONNREFUSED, "upstream_circuit_open",
                    std::string("Upstream circuit breaker is open: ") +
                        reason,
                    static_cast<int>(hop_index));
  send_fast_failure_to_client(s, "upstream_circuit_open");
  close_session(s);
  return true;
}

static void mark_upstream_failure(Session *s, const char *reason,
                                  int hop_index) {
  if (!s || !s->bridge)
    return;
  if (session_upstreams(s).empty())
    return;
  size_t resolved_hop = s->upstream_idx;
  if (hop_index >= 0)
    resolved_hop = static_cast<size_t>(hop_index);
  const UpstreamConfig *up = upstream_at(s, resolved_hop);
  if (!up)
    return;

  const BridgeConfig &cfg = session_config(s);
  CircuitBreakerEntry &entry =
      s->bridge->circuit_breakers[upstream_circuit_key(*up)];
  const uint32_t threshold =
      entry.ever_succeeded
          ? clamp_circuit_breaker_threshold(cfg.circuit_breaker_failure_threshold)
          : circuit_breaker_warmup_threshold(
                cfg.circuit_breaker_failure_threshold);
  entry.consecutive_failures =
      std::min<uint32_t>(entry.consecutive_failures + 1, threshold);
  entry.reason = reason ? reason : "upstream_failure";
  if (entry.consecutive_failures >= threshold) {
    entry.open_until_ms = now_ms() + cfg.circuit_breaker_open_ms;
  }
}

static void mark_upstream_success(Session *s, size_t hop_index) {
  if (!s || !s->bridge)
    return;
  const UpstreamConfig *up = upstream_at(s, hop_index);
  if (!up)
    return;
  CircuitBreakerEntry &entry =
      s->bridge->circuit_breakers[upstream_circuit_key(*up)];
  entry.consecutive_failures = 0;
  entry.open_until_ms = 0;
  entry.reason.clear();
  entry.ever_succeeded = true;
  entry.last_success_ms = now_ms();
}

static void emit_bridge_error(Session *s, int code, const char *reason,
                              const std::string &message, int hop_index,
                              int http_status, int socks5_reply) {
  if (!s || !s->bridge || !s->bridge->callbacks.on_error)
    return;

  const auto &upstreams = session_upstreams(s);
  std::string scheme = "direct";
  std::string host = s->target_host;
  uint16_t port = s->target_port;
  bool auth_present = false;

  int resolved_hop = hop_index;
  if (resolved_hop < 0 && !upstreams.empty() &&
      s->upstream_idx < upstreams.size()) {
    resolved_hop = static_cast<int>(s->upstream_idx);
  }
  if (resolved_hop >= 0 &&
      static_cast<size_t>(resolved_hop) < upstreams.size()) {
    const auto &up = upstreams[static_cast<size_t>(resolved_hop)];
    scheme = upstream_type_name(up.type);
    host = up.addr;
    port = up.port;
    auth_present = !up.auth.user.empty();
  }

  std::string json =
      std::string("{") + "\"kind\":\"route_error\"," +
      "\"stage\":\"runtime_route_error\"," + "\"sessionId\":\"" +
      json_escape(s->session_id) + "\"," + "\"state\":\"" +
      json_escape(session_state_name(s->state)) + "\"," +
      "\"code\":" + std::to_string(code) + "," + "\"reason\":\"" +
      json_escape(reason ? reason : "proxy_bridge_error") + "\"," +
      "\"message\":\"" + json_escape(message) + "\"," +
      "\"hopIndex\":" + std::to_string(resolved_hop) + "," + "\"scheme\":\"" +
      json_escape(scheme) + "\"," + "\"host\":\"" + json_escape(host) + "\"," +
      "\"port\":" + std::to_string(port) + "," +
      "\"authPresent\":" + std::string(auth_present ? "true" : "false");

  if (code < 0) {
    const char *uv_name = uv_err_name(code);
    const char *uv_msg = uv_strerror(code);
    if (uv_name && *uv_name) {
      json += ",\"uvErrorName\":\"";
      json += json_escape(uv_name);
      json += "\"";
    }
    if (uv_msg && *uv_msg) {
      json += ",\"uvError\":\"";
      json += json_escape(uv_msg);
      json += "\"";
    }
  }
  if (http_status > 0)
    json += ",\"httpStatus\":" + std::to_string(http_status);
  if (socks5_reply >= 0) {
    json += ",\"socks5Reply\":" + std::to_string(socks5_reply);
    const char *reply_name = socks5_reply_name(socks5_reply);
    if (reply_name && *reply_name) {
      json += ",\"socks5ReplyName\":\"";
      json += json_escape(reply_name);
      json += "\"";
    }
  }

  json += "}";
  s->bridge->callbacks.on_error(s->bridge, s, code, json.c_str());
}

static void emit_route_step(Session *s, size_t hop_index, const char *phase) {
  if (!s || !s->bridge || !s->bridge->callbacks.on_route_step)
    return;
  const auto &upstreams = session_upstreams(s);
  if (hop_index >= upstreams.size())
    return;
  const auto &up = upstreams[hop_index];
  const std::string json =
      std::string("{") + "\"kind\":\"route_step\"," + "\"phase\":\"" +
      json_escape(phase ? phase : "") + "\"," + "\"sessionId\":\"" +
      json_escape(s->session_id) + "\"," +
      "\"hopIndex\":" + std::to_string(hop_index) + "," + "\"scheme\":\"" +
      json_escape(upstream_type_name(up.type)) + "\"," + "\"host\":\"" +
      json_escape(up.addr) + "\"," + "\"port\":" + std::to_string(up.port) +
      "," + "\"authPresent\":" +
      std::string(up.auth.user.empty() ? "false" : "true") + "}";
  s->bridge->callbacks.on_route_step(s->bridge, s, json.c_str());
}

static void emit_proxy_bypass_step(Session *s, const std::string &matched_rule) {
  if (!s || !s->bridge || !s->bridge->callbacks.on_route_step)
    return;
  const std::string json =
      std::string("{") + "\"kind\":\"proxy_bypass\"," +
      "\"phase\":\"matched\"," + "\"sessionId\":\"" +
      json_escape(s->session_id) + "\"," + "\"host\":\"" +
      json_escape(s->target_host) + "\"," + "\"port\":" +
      std::to_string(s->target_port) + "," + "\"matchedRule\":\"" +
      json_escape(matched_rule) + "\"}";
  s->bridge->callbacks.on_route_step(s->bridge, s, json.c_str());
}

static void emit_resource_domain_allow_step(Session *s,
                                            const char *protocol_name) {
  if (!s || !s->bridge || !s->bridge->callbacks.on_route_step)
    return;
  const std::string json =
      std::string("{") + "\"kind\":\"resource_domain_allow\"," +
      "\"phase\":\"matched\"," + "\"sessionId\":\"" +
      json_escape(s->session_id) + "\"," + "\"protocol\":\"" +
      json_escape(protocol_name ? protocol_name : "") + "\"," + "\"host\":\"" +
      json_escape(s->target_host) + "\"," + "\"port\":" +
      std::to_string(s->target_port) + "}";
  s->bridge->callbacks.on_route_step(s->bridge, s, json.c_str());
}

static void send_fast_failure_to_client(Session *s, const char *reason) {
  if (!s || s->closing)
    return;
  if (s->client_protocol == ClientProtocol::HttpConnect ||
      s->client_protocol == ClientProtocol::HttpForward) {
    std::string response =
        "HTTP/1.1 504 Gateway Timeout\r\nContent-Length: 0\r\nConnection: "
        "close\r\n";
    if (reason && *reason) {
      response += "X-BroSDK-Bridge-Error: ";
      response += reason;
      response += "\r\n";
    }
    response += "\r\n";
    write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client), response);
    return;
  }

  const uint8_t err[] = {0x05, 0x04, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
  write_to_and_close(reinterpret_cast<uv_stream_t *>(&s->client), err,
                     sizeof(err));
}

static void on_phase_timer_closed(uv_handle_t *h) {
  Session *s = static_cast<Session *>(h->data);
  if (!s)
    return;
  s->phase_timer_closed = true;
  try_free_session(s);
}

static void on_phase_timeout(uv_timer_t *timer) {
  Session *s = static_cast<Session *>(timer->data);
  if (!s || s->closing)
    return;

  const std::string reason = s->phase_timeout_reason.empty()
                                 ? "proxy_runtime_timeout"
                                 : s->phase_timeout_reason;
  mark_upstream_failure(s, reason.c_str());
  emit_bridge_error(s, UV_ETIMEDOUT, reason.c_str(),
                    std::string("Proxy bridge phase timed out: ") + reason,
                    static_cast<int>(s->upstream_idx));
  send_fast_failure_to_client(s, reason.c_str());
  close_session(s);
}

static void arm_phase_timer(Session *s, uint32_t timeout_ms,
                            const char *reason) {
  if (!s || !s->bridge || s->closing || timeout_ms == 0)
    return;
  if (!s->phase_timer_initialized) {
    if (uv_timer_init(s->bridge->loop, &s->phase_timer) != 0)
      return;
    s->phase_timer.data = s;
    s->phase_timer_initialized = true;
    s->phase_timer_closed = false;
  }
  s->phase_timeout_reason = reason ? reason : "proxy_runtime_timeout";
  uv_timer_stop(&s->phase_timer);
  uv_timer_start(&s->phase_timer, on_phase_timeout, timeout_ms, 0);
}

static void stop_phase_timer(Session *s) {
  if (!s || !s->phase_timer_initialized || s->phase_timer_closed)
    return;
  if (uv_is_closing(reinterpret_cast<uv_handle_t *>(&s->phase_timer)))
    return;
  uv_timer_stop(&s->phase_timer);
  s->phase_timeout_reason.clear();
}

// Remove leading/trailing ASCII whitespace.
static std::string trim(const std::string &s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos)
    return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// ─── HTTP auth header helpers
// ─────────────────────────────────────────────────

// Return "user:pass" from "Basic dXNlcjpwYXNz" or empty on failure.
static std::string decode_basic_auth(const std::string &header_value) {
  std::string lv = to_lower(trim(header_value));
  if (lv.size() < 6 || lv.substr(0, 6) != "basic ")
    return {};
  return base64_decode(trim(header_value.substr(6)));
}

// Return bearer token from "Bearer <token>" or empty.
static std::string decode_bearer(const std::string &header_value) {
  std::string lv = to_lower(trim(header_value));
  if (lv.size() <= 7 || lv.substr(0, 7) != "bearer ")
    return {};
  return trim(header_value.substr(7));
}

static const BridgeConfig &session_config(const Session *s) {
  if (s)
    return s->config;
  static const BridgeConfig empty;
  return empty;
}

static const std::vector<UpstreamConfig> &session_upstreams(const Session *s) {
  static const std::vector<UpstreamConfig> empty;
  if (s && s->proxy_bypass)
    return empty;
  return session_config(s).upstreams;
}

static bool is_loop_thread(const ProxyBridgeImpl *b) {
  if (!b)
    return false;
  std::lock_guard<std::mutex> lk(b->lifecycle_mu);
  return b->running.load(std::memory_order_acquire) &&
         b->lifecycle_state == BridgeLifecycleState::Running &&
         b->loop_thread_id == std::this_thread::get_id();
}

static bool is_accepting_commands(ProxyBridgeImpl *b) {
  std::lock_guard<std::mutex> lk(b->lifecycle_mu);
  return b->lifecycle_state == BridgeLifecycleState::Running;
}

static void maybe_finish_shared_bridge(ProxyBridgeImpl *b) {
  if (!b || !b->shared_reactor)
    return;
  {
    std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
    if (b->lifecycle_state != BridgeLifecycleState::Stopping &&
        b->lifecycle_state != BridgeLifecycleState::Stopped) {
      return;
    }
  }
  bool listeners_empty = false;
  bool sessions_empty = false;
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    listeners_empty = b->listeners.empty();
  }
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    sessions_empty = b->sessions.empty();
  }
  if (!listeners_empty || !sessions_empty ||
      b->pending_dns.load(std::memory_order_acquire) != 0 ||
      b->queued_commands.load(std::memory_order_acquire) != 0 ||
      b->stop_async_initialized.load(std::memory_order_acquire) ||
      b->command_async_initialized.load(std::memory_order_acquire)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
    b->running.store(false, std::memory_order_release);
    b->loop_thread_id = {};
    if (b->lifecycle_state != BridgeLifecycleState::Destroyed)
      b->lifecycle_state = BridgeLifecycleState::Stopped;
  }
  if (b->shared_loop_lease)
    b->shared_loop_lease->Release();
  b->loop_initialized = false;
  {
    std::lock_guard<std::mutex> lk(b->stopped_mu);
    b->stopped_signaled = true;
  }
  b->stopped_cv.notify_all();
  if (b->destroy_when_stopped.load(std::memory_order_acquire)) {
    b->shared_loop_lease.reset();
    b->loop = nullptr;
    delete b;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Write helper
// ═════════════════════════════════════════════════════════════════════════════

static bool is_relay_state(const Session *s) {
  return s && (s->state == SessionState::HttpTunneling ||
               s->state == SessionState::Socks5Tunneling ||
               s->state == SessionState::HttpForwarding);
}

static void on_write_done(uv_write_t *req, int status) {
  // req is the first member of WriteReq, so delete wr also frees req itself.
  // Save the stream handle BEFORE freeing wr to avoid use-after-free.
  uv_stream_t *handle = req->handle;
  auto *wr = static_cast<WriteReq *>(req->data);
  Session *session = wr ? wr->session : nullptr;
  const size_t write_len = wr ? wr->data.size() : 0;
  const bool close_after_write = wr && wr->close_after_write;
  const bool tunnel_delivery = wr && wr->tunnel_delivery;
  if (session && session->bridge) {
    session->bridge->queued_write_bytes.fetch_sub(write_len,
                                                  std::memory_order_relaxed);
    session->bridge->pending_write_requests.fetch_sub(
        1, std::memory_order_relaxed);
  }
  if (session &&
      handle == reinterpret_cast<uv_stream_t *>(&session->client)) {
    assert(session->pending_client_writes > 0);
    --session->pending_client_writes;
  } else if (session &&
             handle == reinterpret_cast<uv_stream_t *>(&session->remote)) {
    assert(session->pending_remote_writes > 0);
    --session->pending_remote_writes;
  }
  delete wr;
  if (session && tunnel_delivery && status == 0 && !session->closing) {
    const auto &cb = session->bridge->tunnel_callbacks;
    if (cb.consumed) cb.consumed(cb.user_data, session->internal_connection, write_len);
  }
  if (session && close_after_write) {
    session->close_deferred = false;
    if (session->closing) {
      if (!session->client_closed &&
          !uv_is_closing(reinterpret_cast<uv_handle_t *>(&session->client))) {
        uv_close(reinterpret_cast<uv_handle_t *>(&session->client),
                 on_client_closed);
      }
      try_free_session(session);
    } else {
      close_session(session);
    }
    return;
  }
  if (session && !session->closing && handle && is_relay_state(session)) {
    const uint64_t low = session_config(session).write_low_watermark;
    if (handle == reinterpret_cast<uv_stream_t *>(&session->remote) &&
        session->client_paused_for_backpressure &&
        handle->write_queue_size <= low) {
      uv_read_cb read_cb = on_tunnel_client_read;
      if (session->state == SessionState::HttpForwarding)
        read_cb = on_forward_client_read;
      const int rc = uv_read_start(
          reinterpret_cast<uv_stream_t *>(&session->client), alloc_cb,
          read_cb);
      if (rc == 0) {
        session->client_reading = true;
        session->client_paused_for_backpressure = false;
        session->bridge->read_resume_events.fetch_add(
            1, std::memory_order_relaxed);
        session->bridge->paused_read_sides.fetch_sub(
            1, std::memory_order_relaxed);
      }
    } else if (handle == reinterpret_cast<uv_stream_t *>(&session->client) &&
               session->remote_paused_for_backpressure &&
               handle->write_queue_size <= low) {
      uv_read_cb read_cb = on_remote_read;
      if (session->state == SessionState::HttpForwarding)
        read_cb = on_forward_remote_read;
      const int rc = uv_read_start(
          reinterpret_cast<uv_stream_t *>(&session->remote), alloc_cb,
          read_cb);
      if (rc == 0) {
        session->remote_reading = true;
        session->remote_paused_for_backpressure = false;
        session->bridge->read_resume_events.fetch_add(
            1, std::memory_order_relaxed);
        session->bridge->paused_read_sides.fetch_sub(
            1, std::memory_order_relaxed);
      }
    }
  }
  if (status < 0) {
    if (session && !session->closing) {
      if (status != UV_ECANCELED) {
        emit_bridge_error(session, status, "bridge_write_failed",
                          std::string("Bridge write failed: ") +
                              uv_strerror(status));
      }
      close_session(session);
    }
  } else if (session && !session->closing) {
    maybe_schedule_relay_shutdowns(session);
  }
  if (session && session->closing)
    try_free_session(session);
}

static int write_to_impl(uv_stream_t *stream, const void *data, size_t len,
                         bool close_after_write, bool tunnel_delivery = false) {
  if (!len)
    return 0;
  Session *s =
      stream && stream->data ? static_cast<Session *>(stream->data) : nullptr;
  if (s && s->tunnel && stream == reinterpret_cast<uv_stream_t *>(&s->remote)) {
    const auto &cb = s->bridge->tunnel_callbacks;
    if (s->closing || s->tunnel_waiting || len > 32768) return UV_ENOBUFS;
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
    s->client_reading = false;
    s->tunnel_waiting = true;
    if (!cb.data || cb.data(cb.user_data, s->internal_connection,
                            static_cast<const char *>(data), len) != 0) {
      close_session(s);
      return UV_EPIPE;
    }
    return 0;
  }
  if (s && s->bridge) {
    const uint64_t queued =
        s->bridge->queued_write_bytes.fetch_add(len,
                                                std::memory_order_relaxed) +
        len;
    s->bridge->observe_queued_write_bytes(queued);

    const uint64_t high = session_config(s).write_high_watermark;
    if (is_relay_state(s) &&
        stream == reinterpret_cast<uv_stream_t *>(&s->remote) &&
        !s->client_paused_for_backpressure &&
        stream->write_queue_size + len >= high && s->client_reading) {
      uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
      s->client_reading = false;
      s->client_paused_for_backpressure = true;
      s->bridge->read_pause_events.fetch_add(1, std::memory_order_relaxed);
      s->bridge->paused_read_sides.fetch_add(1, std::memory_order_relaxed);
    } else if (is_relay_state(s) &&
               stream == reinterpret_cast<uv_stream_t *>(&s->client) &&
               !s->remote_paused_for_backpressure &&
               stream->write_queue_size + len >= high && s->remote_reading) {
      uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->remote));
      s->remote_reading = false;
      s->remote_paused_for_backpressure = true;
      s->bridge->read_pause_events.fetch_add(1, std::memory_order_relaxed);
      s->bridge->paused_read_sides.fetch_add(1, std::memory_order_relaxed);
    }
  }

  if (s && close_after_write)
    s->close_deferred = true;
  auto *wr = WriteReq::make(s, data, len, close_after_write);
  wr->tunnel_delivery = tunnel_delivery;
  if (s && stream == reinterpret_cast<uv_stream_t *>(&s->client))
    ++s->pending_client_writes;
  else if (s && stream == reinterpret_cast<uv_stream_t *>(&s->remote))
    ++s->pending_remote_writes;
  if (s && s->bridge) {
    const uint64_t pending = s->bridge->pending_write_requests.fetch_add(
                                 1, std::memory_order_relaxed) +
                             1;
    ProxyBridgeImpl::observe_max(s->bridge->max_pending_write_requests,
                                 pending);
  }
  uv_buf_t b = uv_buf_init(wr->data.data(), static_cast<unsigned>(len));
  int r = uv_write(&wr->req, stream, &b, 1, on_write_done);
  if (r != 0) {
    if (stream && stream->data) {
      Session *s = static_cast<Session *>(stream->data);
      if (!s->closing) {
        emit_bridge_error(s, r, "bridge_write_schedule_failed",
                          std::string("Scheduling bridge write failed: ") +
                              uv_strerror(r));
      }
    }
    if (s && s->bridge)
      s->bridge->queued_write_bytes.fetch_sub(len,
                                              std::memory_order_relaxed);
    if (s && s->bridge)
      s->bridge->pending_write_requests.fetch_sub(1,
                                                  std::memory_order_relaxed);
    if (s && close_after_write)
      s->close_deferred = false;
    if (s && stream == reinterpret_cast<uv_stream_t *>(&s->client)) {
      assert(s->pending_client_writes > 0);
      --s->pending_client_writes;
    } else if (s && stream == reinterpret_cast<uv_stream_t *>(&s->remote)) {
      assert(s->pending_remote_writes > 0);
      --s->pending_remote_writes;
    }
    delete wr;
    if (s && !s->closing)
      close_session(s);
  }
  return r;
}

static int write_to(uv_stream_t *stream, const void *data, size_t len) {
  return write_to_impl(stream, data, len, false);
}

static int write_str(uv_stream_t *stream, const std::string &s) {
  return write_to(stream, s.data(), s.size());
}

static int write_to_and_close(uv_stream_t *stream, const void *data,
                              size_t len) {
  return write_to_impl(stream, data, len, true);
}

static int write_str_and_close(uv_stream_t *stream, const std::string &s) {
  return write_to_and_close(stream, s.data(), s.size());
}

// ═════════════════════════════════════════════════════════════════════════════
// Session lifecycle
// ═════════════════════════════════════════════════════════════════════════════

static void emit_features(Session *s) {
  ProxyBridgeImpl *b = s->bridge;
  if (!session_config(s).traffic_features)
    return;
  if (!b->callbacks.on_features)
    return;

  uint64_t dur = now_ms() - s->start_ms;
  char buf[256];
  snprintf(
      buf, sizeof(buf),
      "{\"bytes_up\":%llu,\"bytes_down\":%llu,"
      "\"pkt_count_up\":%llu,\"pkt_count_down\":%llu,\"duration_ms\":%llu}",
      static_cast<unsigned long long>(s->bytes_up),
      static_cast<unsigned long long>(s->bytes_down),
      static_cast<unsigned long long>(s->pkt_count_up),
      static_cast<unsigned long long>(s->pkt_count_down),
      static_cast<unsigned long long>(dur));
  b->callbacks.on_features(b, s, buf);
}

static void free_session(Session *s) {
  emit_features(s);
  ProxyBridgeImpl *bridge = s->bridge;
  bridge->closed_sessions.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(bridge->sessions_mu);
    bridge->sessions.erase(s->session_id);
  }
  close_finished_listeners(bridge);
  delete s;
  maybe_finish_shared_bridge(bridge);
}

static void notify_internal_ws_close(Session *s) {
  if (!s || !s->bridge || !s->internal_ws_open_notified ||
      s->internal_ws_close_notified)
    return;
  s->internal_ws_close_notified = true;
  const auto &callbacks = s->bridge->internal_route_callbacks;
  if (callbacks.on_ws_close) {
    const std::string reason =
        s->internal_close_reason.empty() ? "closed" : s->internal_close_reason;
    callbacks.on_ws_close(s->bridge, s->internal_connection, reason.c_str(),
                          callbacks.user_data);
  }
}

static void try_free_session(Session *s) {
  const bool timer_done =
      !s->phase_timer_initialized || s->phase_timer_closed;
  const bool writes_done =
      s->pending_client_writes == 0 && s->pending_remote_writes == 0;
  const bool shutdowns_done =
      !s->client_shutdown_pending && !s->remote_shutdown_pending;
  if (s->client_closed && s->remote_closed && !s->resolver_pending &&
      timer_done && writes_done && shutdowns_done)
    free_session(s);
}

static void on_client_closed(uv_handle_t *h) {
  Session *s = static_cast<Session *>(h->data);
  s->client_closed = true;
  try_free_session(s);
}

static void on_remote_closed(uv_handle_t *h) {
  Session *s = static_cast<Session *>(h->data);
  s->remote_closed = true;
  try_free_session(s);
}

static void close_session(Session *s) {
  if (s->closing)
    return;
  s->closing = true;
  if (s->tunnel && s->bridge->tunnel_callbacks.closed)
    s->bridge->tunnel_callbacks.closed(
        s->bridge->tunnel_callbacks.user_data, s->internal_connection);
  if (s->bridge) {
    uint64_t paused = 0;
    if (s->client_paused_for_backpressure) {
      s->client_paused_for_backpressure = false;
      ++paused;
    }
    if (s->remote_paused_for_backpressure) {
      s->remote_paused_for_backpressure = false;
      ++paused;
    }
    if (paused != 0)
      s->bridge->paused_read_sides.fetch_sub(paused,
                                             std::memory_order_relaxed);
  }
  notify_internal_ws_close(s);

  stop_phase_timer(s);
  if (s->phase_timer_initialized && !s->phase_timer_closed &&
      !uv_is_closing(reinterpret_cast<uv_handle_t *>(&s->phase_timer))) {
    uv_close(reinterpret_cast<uv_handle_t *>(&s->phase_timer),
             on_phase_timer_closed);
  }

  // If the DNS resolver is active, cancel it — on_resolved_v2 will handle
  // marking remote_closed when the cancel callback fires.
  if (s->resolver_pending) {
    uv_cancel(reinterpret_cast<uv_req_t *>(&s->resolver));
    // remote_closed will be set inside on_resolved_v2
  } else if (!s->remote_initialized) {
    s->remote_closed = true;
  }

  if (s->client_reading) {
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
    s->client_reading = false;
  }
  if (s->remote_reading) {
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->remote));
    s->remote_reading = false;
  }

  // Guard against double-close: uv_walk (in pvb_destroy) may already be
  // closing these handles with a NULL callback.
  if (!s->close_deferred && !s->client_closed &&
      !uv_is_closing(reinterpret_cast<uv_handle_t *>(&s->client)))
    uv_close(reinterpret_cast<uv_handle_t *>(&s->client), on_client_closed);
  if (s->remote_initialized && !s->remote_closed &&
      !uv_is_closing(reinterpret_cast<uv_handle_t *>(&s->remote)))
    uv_close(reinterpret_cast<uv_handle_t *>(&s->remote), on_remote_closed);

  try_free_session(s);
}

static void close_all_sessions(ProxyBridgeImpl *b) {
  std::vector<Session *> sessions;
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    sessions.reserve(b->sessions.size());
    for (const auto &item : b->sessions)
      sessions.push_back(item.second);
  }
  for (Session *session : sessions)
    close_session(session);
}

static void close_listener_sessions(ProxyBridgeImpl *b, Listener *listener) {
  std::vector<Session *> sessions;
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    for (const auto &item : b->sessions) {
      if (item.second && item.second->listener == listener)
        sessions.push_back(item.second);
    }
  }
  for (Session *session : sessions)
    close_session(session);
}

// Route switches should not tear down the in-process MCP transport.  It is
// intentionally conservative for sessions that have not finished protocol
// classification yet: those sessions are external by default and will be
// retried by the browser after the switch.
static bool is_internal_transport_session(const Session *session) {
  return session &&
         (session->state == SessionState::InternalHttp ||
          session->state == SessionState::InternalWebSocket);
}

static bool internal_transport_config_changed(const BridgeConfig &before,
                                              const BridgeConfig &after) {
  return before.auth_mode != after.auth_mode ||
         before.token_lifetime_sec != after.token_lifetime_sec ||
         before.internal_route_enable != after.internal_route_enable ||
         before.internal_route_hosts != after.internal_route_hosts ||
         before.internal_route_path_prefix != after.internal_route_path_prefix ||
         before.internal_route_websocket_path !=
             after.internal_route_websocket_path ||
         before.internal_route_allowed_origins !=
             after.internal_route_allowed_origins ||
         before.internal_route_require_origin !=
             after.internal_route_require_origin ||
         before.internal_route_max_body_bytes !=
             after.internal_route_max_body_bytes ||
         before.internal_route_max_websocket_frame_bytes !=
             after.internal_route_max_websocket_frame_bytes;
}

static void close_listener_external_sessions(ProxyBridgeImpl *b,
                                             Listener *listener) {
  std::vector<Session *> sessions;
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    for (const auto &item : b->sessions) {
      Session *session = item.second;
      if (session && session->listener == listener &&
          !is_internal_transport_session(session))
        sessions.push_back(session);
    }
  }
  for (Session *session : sessions)
    close_session(session);
}

static int close_failed_listener_on_loop(ProxyBridgeImpl *b,
                                         std::unique_ptr<Listener> listener,
                                         int rc) {
  Listener *raw_listener = listener.get();
  raw_listener->closing.store(true, std::memory_order_release);
  raw_listener->listening.store(false, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    b->listeners.push_back(std::move(listener));
  }
  if (!uv_is_closing(reinterpret_cast<uv_handle_t *>(&raw_listener->server))) {
    uv_close(reinterpret_cast<uv_handle_t *>(&raw_listener->server),
             on_listener_closed);
  }
  return rc;
}

static int add_listener_on_loop(ProxyBridgeImpl *b,
                                const BridgeConfig &listener_config,
                                const char *addr, uint16_t port,
                                uint16_t *bound_port,
                                Listener **listener_out,
                                bool make_default = false) {
  if (listener_config.strict_proxy && b->tunnel_callbacks.open)
    return UV_EINVAL;
  auto listener = std::make_unique<Listener>();
  listener->bridge = b;
  listener->id = b->listener_counter.fetch_add(1, std::memory_order_relaxed) +
                 1;
  listener->config = listener_config;

  int r = uv_tcp_init(b->loop, &listener->server);
  if (r != 0)
    return r;
  listener->server.data = listener.get();

  struct sockaddr_storage ss{};
  r = uv_ip4_addr(addr, port, reinterpret_cast<struct sockaddr_in *>(&ss));
  if (r != 0)
    r = uv_ip6_addr(addr, port, reinterpret_cast<struct sockaddr_in6 *>(&ss));
  if (r != 0)
    return close_failed_listener_on_loop(b, std::move(listener), r);

  r = uv_tcp_bind(&listener->server, reinterpret_cast<struct sockaddr *>(&ss),
                  0);
  if (r != 0)
    return close_failed_listener_on_loop(b, std::move(listener), r);

  r = uv_listen(reinterpret_cast<uv_stream_t *>(&listener->server), 1024,
                on_new_connection);
  if (r != 0)
    return close_failed_listener_on_loop(b, std::move(listener), r);

  listener->listening.store(true, std::memory_order_release);
  struct sockaddr_storage bound_ss{};
  int bound_len = sizeof(bound_ss);
  r = uv_tcp_getsockname(&listener->server,
                         reinterpret_cast<struct sockaddr *>(&bound_ss),
                         &bound_len);
  if (r != 0)
    return close_failed_listener_on_loop(b, std::move(listener), r);

  uint16_t actual_port = port;
  if (bound_ss.ss_family == AF_INET) {
    actual_port =
        ntohs(reinterpret_cast<struct sockaddr_in *>(&bound_ss)->sin_port);
  } else if (bound_ss.ss_family == AF_INET6) {
    actual_port =
        ntohs(reinterpret_cast<struct sockaddr_in6 *>(&bound_ss)->sin6_port);
  }
  listener->port.store(actual_port, std::memory_order_relaxed);
  listener->config.listen_addr = addr;
  listener->config.listen_port = actual_port;
  if (bound_port)
    *bound_port = actual_port;
  Listener *raw_listener = listener.get();
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    b->listeners.push_back(std::move(listener));
    if (make_default) {
      b->default_listener = raw_listener;
      b->config = raw_listener->config;
    }
  }
  if (listener_out)
    *listener_out = raw_listener;
  return 0;
}

static void close_finished_listeners(ProxyBridgeImpl *b) {
  std::unordered_set<Listener *> active_listeners;
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    active_listeners.reserve(b->sessions.size());
    for (const auto &item : b->sessions) {
      if (item.second && item.second->listener)
        active_listeners.insert(item.second->listener);
    }
  }

  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    auto &listeners = b->listeners;
    for (auto it = listeners.begin(); it != listeners.end();) {
      Listener *listener = it->get();
      if (listener && listener->closing.load(std::memory_order_acquire) &&
          listener->server_closed.load(std::memory_order_acquire) &&
          active_listeners.find(listener) == active_listeners.end()) {
        if (b->default_listener == listener)
          b->default_listener = nullptr;
        it = listeners.erase(it);
      } else {
        ++it;
      }
    }
  }
  maybe_finish_shared_bridge(b);
}

static Listener *find_listener_locked(ProxyBridgeImpl *b,
                                      uvbrg_listener_t listener_handle,
                                      uint64_t listener_id = 0,
                                      bool require_active = false) {
  auto *candidate = static_cast<Listener *>(listener_handle);
  for (const auto &listener : b->listeners) {
    if (listener.get() == candidate &&
        (listener_id == 0 || listener->id == listener_id) &&
        (!require_active ||
         (listener->listening.load(std::memory_order_acquire) &&
          !listener->closing.load(std::memory_order_acquire))) )
      return candidate;
  }
  return nullptr;
}

static void on_listener_closed(uv_handle_t *h) {
  auto *listener = static_cast<Listener *>(h->data);
  if (!listener)
    return;
  listener->listening.store(false, std::memory_order_release);
  listener->server_closed.store(true, std::memory_order_release);
  if (listener->bridge) {
    listener->bridge->listener_close_events.fetch_add(
        1, std::memory_order_relaxed);
    close_finished_listeners(listener->bridge);
  }
}

static int close_listener_on_loop(ProxyBridgeImpl *b,
                                  uvbrg_listener_t listener_handle,
                                  uint64_t listener_id = 0) {
  Listener *listener = nullptr;
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    listener = find_listener_locked(b, listener_handle, listener_id, true);
  }
  if (!listener)
    return UV_EINVAL;
  listener->closing.store(true, std::memory_order_release);
  if (listener->listening.load(std::memory_order_acquire) &&
      !uv_is_closing(reinterpret_cast<uv_handle_t *>(&listener->server))) {
    listener->listening.store(false, std::memory_order_release);
    uv_close(reinterpret_cast<uv_handle_t *>(&listener->server),
             on_listener_closed);
  }
  close_listener_sessions(b, listener);
  close_finished_listeners(b);
  return 0;
}

static void complete_command(const std::shared_ptr<BridgeCommand> &cmd) {
  {
    std::lock_guard<std::mutex> lk(cmd->done_mu);
    cmd->done = true;
  }
  cmd->done_cv.notify_one();
}

static void process_bridge_command(ProxyBridgeImpl *b,
                                   const std::shared_ptr<BridgeCommand> &cmd) {
  const auto find_connection = [&]() -> Session * {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    for (const auto &item : b->sessions) {
      Session *session = item.second;
      if (session &&
          session->internal_connection.id == cmd->connection.id &&
          session->internal_connection.generation ==
              cmd->connection.generation) {
        return session;
      }
    }
    return nullptr;
  };
  switch (cmd->kind) {
  case BridgeCommand::Kind::TunnelEvent: {
    Session *s = find_connection();
    if (!s || s->closing || !s->tunnel) { cmd->result = UV_ENOTCONN; break; }
    const int event = cmd->status_code;
    if (event == UVBRG_TUNNEL_CLOSE) {
      if (!is_relay_state(s)) send_fast_failure_to_client(s, "tunnel_closed");
      close_session(s);
    } else if (event == UVBRG_TUNNEL_CONNECTED && !is_relay_state(s)) {
      uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
      s->client_reading = false;
      stop_phase_timer(s);
      if (s->state == SessionState::HttpForwardConnecting)
        start_http_forward_relay(s);
      else {
        send_connect_established(s);
        if (!s->client_buf.empty()) {
          write_to(reinterpret_cast<uv_stream_t *>(&s->remote),
                   s->client_buf.data(), s->client_buf.size());
          s->client_buf.clear();
        }
      }
    } else if (event == UVBRG_TUNNEL_DATA && is_relay_state(s) && !s->remote_read_eof) {
      if (s->pending_client_writes > 8) { cmd->result = UV_ENOBUFS; break; }
      s->bytes_down += cmd->payload.size();
      cmd->result = write_to_impl(reinterpret_cast<uv_stream_t *>(&s->client),
                                   cmd->payload.data(), cmd->payload.size(), false, true);
    } else if (event == UVBRG_TUNNEL_WRITABLE && is_relay_state(s)) {
      s->tunnel_waiting = false;
      if (!s->client_read_eof) {
        cmd->result = uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client),
                                    alloc_cb, on_tunnel_client_read);
        s->client_reading = cmd->result == 0;
      }
      maybe_schedule_relay_shutdowns(s);
    } else if (event == UVBRG_TUNNEL_EOF && is_relay_state(s)) {
      s->remote_read_eof = true;
      maybe_schedule_relay_shutdowns(s);
    } else cmd->result = UV_EINVAL;
    break;
  }
  case BridgeCommand::Kind::AddListener:
    {
      BridgeConfig listener_config;
      {
        std::lock_guard<std::mutex> lk(b->state_mu);
        listener_config = b->config;
      }
      if (!cmd->config_json.empty() &&
          !parse_config(cmd->config_json.c_str(), listener_config)) {
        cmd->result = UV_EINVAL;
        break;
      }
      const char *addr =
          cmd->addr.empty() ? listener_config.listen_addr.c_str()
                            : cmd->addr.c_str();
      const uint16_t port =
          cmd->requested_port != 0 ? cmd->requested_port
                                   : listener_config.listen_port;
      cmd->result = add_listener_on_loop(b, listener_config, addr, port,
                                         &cmd->bound_port, &cmd->listener,
                                         cmd->make_default);
      if (cmd->result == 0 && cmd->listener)
        cmd->listener_id = cmd->listener->id;
    }
    break;
  case BridgeCommand::Kind::CloseListener:
    cmd->result =
        close_listener_on_loop(b, cmd->listener, cmd->listener_id);
    break;
  case BridgeCommand::Kind::UpdateListenerConfig: {
    Listener *listener = nullptr;
    BridgeConfig next_config;
    BridgeConfig current_config;
    {
      std::lock_guard<std::mutex> lk(b->state_mu);
      listener = find_listener_locked(b, cmd->listener, cmd->listener_id,
                                      true);
      if (listener) {
        next_config = listener->config;
        current_config = listener->config;
      }
    }
    if (!listener || cmd->config_json.empty() ||
        !parse_config(cmd->config_json.c_str(), next_config) ||
        (next_config.strict_proxy && b->tunnel_callbacks.open)) {
      cmd->result = UV_EINVAL;
      break;
    }

    // The socket itself is deliberately immutable.  A caller that needs a
    // different address/port must create a separate listener and atomically
    // switch its own consumers; changing it here would invalidate Chromium's
    // already-injected loopback proxy argument.
    // A zero port is the public configuration spelling for an unspecified
    // listener port.  The socket is intentionally immutable during an
    // update, so preserve the bound port instead of treating it as a port
    // change.  This also keeps configs produced before the listener was bound
    // compatible with in-place reconfiguration.
    if (next_config.listen_port == 0)
      next_config.listen_port = current_config.listen_port;
    if (next_config.listen_addr.empty())
      next_config.listen_addr = current_config.listen_addr;
    if (next_config.listen_addr != current_config.listen_addr ||
        next_config.listen_port != current_config.listen_port) {
      cmd->result = UV_EINVAL;
      break;
    }

    if (cmd->close_sessions) {
      if (internal_transport_config_changed(current_config, next_config))
        close_listener_sessions(b, listener);
      else
        close_listener_external_sessions(b, listener);
    }

    {
      std::lock_guard<std::mutex> lk(b->state_mu);
      listener->config = next_config;
      if (b->default_listener == listener)
        b->config = next_config;
    }
    // Circuit-breaker state is route-specific.  Carrying failures from the
    // previous node into the new node would reject healthy connections.
    b->circuit_breakers.clear();
    cmd->result = 0;
    break;
  }
  case BridgeCommand::Kind::InternalWsSend: {
    Session *session = find_connection();
    if (!session || session->closing ||
        session->state != SessionState::InternalWebSocket) {
      cmd->result = UV_ENOTCONN;
      break;
    }
    if (cmd->payload.size() >
        session_config(session).internal_route_max_websocket_frame_bytes) {
      cmd->result = UV_EMSGSIZE;
      break;
    }
    if (session->bridge->queued_write_bytes.load(std::memory_order_relaxed) +
            cmd->payload.size() + 16 >
        session_config(session).write_high_watermark) {
      cmd->result = UV_ENOBUFS;
      break;
    }
    cmd->result = write_ws_frame(session, 0x1, cmd->payload);
    break;
  }
  case BridgeCommand::Kind::InternalHttpComplete: {
    Session *session = find_connection();
    if (!session || session->closing || !session->internal_http_deferred ||
        session->state != SessionState::InternalHttp) {
      cmd->result = UV_ENOTCONN;
      break;
    }
    if (cmd->payload.size() >
        session_config(session).internal_route_max_body_bytes) {
      cmd->result = UV_EMSGSIZE;
      break;
    }
    session->internal_http_deferred = false;
    cmd->result = write_str_and_close(
        reinterpret_cast<uv_stream_t *>(&session->client),
        build_internal_http_response(
            cmd->status_code,
            cmd->content_type.empty() ? "application/json" : cmd->content_type,
            cmd->payload, session->internal_http_origin));
    break;
  }
  }
}

static void on_command_requested(uv_async_t *h) {
  auto *b = static_cast<ProxyBridgeImpl *>(h->data);
  if (!b)
    return;

  std::deque<std::shared_ptr<BridgeCommand>> commands;
  {
    std::lock_guard<std::mutex> lk(b->commands_mu);
    commands.swap(b->commands);
    uint64_t bytes = 0;
    for (const auto &command : commands)
      bytes += command ? command->estimated_bytes() : 0;
    b->queued_commands.fetch_sub(commands.size(),
                                 std::memory_order_relaxed);
    b->queued_command_bytes.fetch_sub(bytes, std::memory_order_relaxed);
  }

  for (const auto &cmd : commands) {
    if (!is_accepting_commands(b)) {
      cmd->state.store(BridgeCommand::State::Canceled,
                       std::memory_order_release);
      cmd->result = UV_ECANCELED;
      complete_command(cmd);
      continue;
    }
    auto expected = BridgeCommand::State::Queued;
    if (!cmd->state.compare_exchange_strong(
            expected, BridgeCommand::State::Executing,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      // A timed-out caller can cancel only while the command is still queued;
      // acknowledge it without executing so a late config commit is
      // impossible after UV_ETIMEDOUT was returned.
      complete_command(cmd);
      continue;
    }
    process_bridge_command(b, cmd);
    cmd->state.store(BridgeCommand::State::Completed,
                     std::memory_order_release);
    complete_command(cmd);
  }
  maybe_finish_shared_bridge(b);
}

static int run_command_sync(ProxyBridgeImpl *b,
                            const std::shared_ptr<BridgeCommand> &cmd) {
  constexpr auto kCommandTimeout = std::chrono::seconds(5);

  if (is_loop_thread(b)) {
    if (!is_accepting_commands(b)) {
      cmd->result = UV_ECANCELED;
      return cmd->result;
    }
    cmd->state.store(BridgeCommand::State::Executing,
                     std::memory_order_release);
    process_bridge_command(b, cmd);
    cmd->state.store(BridgeCommand::State::Completed,
                     std::memory_order_release);
    return cmd->result;
  }

  if (!is_accepting_commands(b))
    return UV_ECANCELED;

  {
    std::lock_guard<std::mutex> lk(b->commands_mu);
    if (!is_accepting_commands(b))
      return UV_ECANCELED;
    const size_t bytes = cmd->estimated_bytes();
    const uint64_t queued =
        b->queued_commands.load(std::memory_order_relaxed);
    const uint64_t queued_bytes =
        b->queued_command_bytes.load(std::memory_order_relaxed);
    const uint64_t count_limit =
        cmd->is_control()
            ? kMaxQueuedBridgeCommands
            : kMaxQueuedBridgeCommands - kReservedBridgeCommands;
    const uint64_t bytes_limit =
        cmd->is_control()
            ? kMaxQueuedBridgeCommandBytes
            : kMaxQueuedBridgeCommandBytes - kReservedBridgeCommandBytes;
    if (queued >= count_limit || queued_bytes >= bytes_limit ||
        bytes > bytes_limit - queued_bytes) {
      b->command_queue_rejections.fetch_add(1,
                                            std::memory_order_relaxed);
      return UV_ENOBUFS;
    }
    b->commands.push_back(cmd);
    const uint64_t next_count =
        b->queued_commands.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint64_t next_bytes =
        b->queued_command_bytes.fetch_add(bytes, std::memory_order_relaxed) +
        bytes;
    ProxyBridgeImpl::observe_max(b->max_queued_commands, next_count);
    ProxyBridgeImpl::observe_max(b->max_queued_command_bytes, next_bytes);
  }
  const int post_rc = uv_async_send(&b->command_async);
  if (post_rc != 0) {
    std::lock_guard<std::mutex> queue_lk(b->commands_mu);
    auto it = std::find(b->commands.begin(), b->commands.end(), cmd);
    if (it != b->commands.end()) {
      b->queued_commands.fetch_sub(1, std::memory_order_relaxed);
      b->queued_command_bytes.fetch_sub(cmd->estimated_bytes(),
                                        std::memory_order_relaxed);
      b->commands.erase(it);
    }
    cmd->state.store(BridgeCommand::State::Canceled,
                     std::memory_order_release);
    cmd->result = post_rc;
    return post_rc;
  }

  std::unique_lock<std::mutex> lk(cmd->done_mu);
  if (!cmd->done_cv.wait_for(lk, kCommandTimeout,
                             [&cmd]() { return cmd->done; })) {
    auto expected = BridgeCommand::State::Queued;
    if (cmd->state.compare_exchange_strong(
            expected, BridgeCommand::State::Canceled,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
      cmd->result = UV_ETIMEDOUT;
      return UV_ETIMEDOUT;
    }
    // Execution already began before the deadline.  The operation is short
    // and no longer cancelable; wait for its authoritative result instead of
    // returning a timeout while it may still mutate bridge state.
    cmd->done_cv.wait(lk, [&cmd]() { return cmd->done; });
  }
  return cmd->result;
}

static void cancel_pending_commands(ProxyBridgeImpl *b) {
  std::deque<std::shared_ptr<BridgeCommand>> commands;
  {
    std::lock_guard<std::mutex> lk(b->commands_mu);
    commands.swap(b->commands);
    uint64_t bytes = 0;
    for (const auto &command : commands)
      bytes += command ? command->estimated_bytes() : 0;
    b->queued_commands.fetch_sub(commands.size(),
                                 std::memory_order_relaxed);
    b->queued_command_bytes.fetch_sub(bytes, std::memory_order_relaxed);
  }
  for (const auto &cmd : commands) {
    auto expected = BridgeCommand::State::Queued;
    cmd->state.compare_exchange_strong(
        expected, BridgeCommand::State::Canceled, std::memory_order_acq_rel,
        std::memory_order_acquire);
    cmd->result = UV_ECANCELED;
    complete_command(cmd);
  }
  maybe_finish_shared_bridge(b);
}

static void on_bridge_async_closed(uv_handle_t *handle) {
  auto *b = static_cast<ProxyBridgeImpl *>(handle->data);
  if (!b)
    return;
  if (handle == reinterpret_cast<uv_handle_t *>(&b->command_async))
    b->command_async_initialized.store(false, std::memory_order_release);
  if (handle == reinterpret_cast<uv_handle_t *>(&b->stop_async))
    b->stop_async_initialized.store(false, std::memory_order_release);
  maybe_finish_shared_bridge(b);
}

static void on_stop_requested(uv_async_t *h) {
  auto *b = static_cast<ProxyBridgeImpl *>(h->data);
  if (!b)
    return;

  std::vector<Listener *> listeners;
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    listeners.reserve(b->listeners.size());
    for (auto &listener : b->listeners)
      listeners.push_back(listener.get());
  }
  for (Listener *listener : listeners) {
    if (!listener)
      continue;
    listener->closing.store(true, std::memory_order_release);
    if (listener->listening.load(std::memory_order_acquire) &&
        !uv_is_closing(reinterpret_cast<uv_handle_t *>(&listener->server))) {
      listener->listening.store(false, std::memory_order_release);
      uv_close(reinterpret_cast<uv_handle_t *>(&listener->server),
               on_listener_closed);
    }
  }

  close_all_sessions(b);
  cancel_pending_commands(b);

  if (b->command_async_initialized.load(std::memory_order_acquire) &&
      !uv_is_closing(reinterpret_cast<uv_handle_t *>(&b->command_async))) {
    uv_close(reinterpret_cast<uv_handle_t *>(&b->command_async),
             on_bridge_async_closed);
  }

  if (b->stop_async_initialized.load(std::memory_order_acquire) &&
      !uv_is_closing(reinterpret_cast<uv_handle_t *>(&b->stop_async))) {
    uv_close(reinterpret_cast<uv_handle_t *>(&b->stop_async),
             on_bridge_async_closed);
  }
  maybe_finish_shared_bridge(b);
}

// ═════════════════════════════════════════════════════════════════════════════
// Buffer allocation callback (shared by all reads)
// ═════════════════════════════════════════════════════════════════════════════

static void alloc_cb(uv_handle_t *h, size_t suggested, uv_buf_t *buf) {
  const auto *s = static_cast<const Session *>(h->data);
  if (s && s->bridge->tunnel_callbacks.open)
    suggested = std::min(suggested, size_t(16384));
  if (s && s->bridge->execution_guard.enter)
    suggested = std::min(suggested, size_t(65536));
  if (s && (s->state == SessionState::UpstreamHttpConnect ||
            s->state == SessionState::UpstreamSocks5Greet ||
            s->state == SessionState::UpstreamSocks5Auth ||
            s->state == SessionState::UpstreamSocks5Connect))
    suggested = std::min(suggested, size_t(65536));
  buf->base = new char[suggested];
  buf->len = static_cast<unsigned>(suggested);
}

// ═════════════════════════════════════════════════════════════════════════════
// Relay (bidirectional passthrough)
// ═════════════════════════════════════════════════════════════════════════════

static void observe_eof(Session *session, ssize_t nread) {
  if (nread == UV_EOF && session && session->bridge)
    session->bridge->eof_events.fetch_add(1, std::memory_order_relaxed);
}

static void maybe_finish_relay_half_close(Session *s) {
  if (!s || s->closing || !is_relay_state(s))
    return;
  if (s->client_read_eof && s->remote_read_eof &&
      s->client_shutdown_done && s->remote_shutdown_done &&
      s->pending_client_writes == 0 && s->pending_remote_writes == 0) {
    close_session(s);
  }
}

static void on_relay_shutdown(uv_shutdown_t *req, int status) {
  Session *s = req ? static_cast<Session *>(req->data) : nullptr;
  if (!s)
    return;

  const bool client_side = req == &s->client_shutdown_req;
  if (client_side) {
    s->client_shutdown_pending = false;
    s->client_shutdown_done = status == 0;
  } else {
    assert(req == &s->remote_shutdown_req);
    s->remote_shutdown_pending = false;
    s->remote_shutdown_done = status == 0;
  }

  if (status < 0 && !s->closing) {
    if (status != UV_ECANCELED) {
      emit_bridge_error(
          s, status,
          client_side ? "relay_client_shutdown_failed"
                      : "relay_remote_shutdown_failed",
          std::string("Relay write-side shutdown failed: ") +
              uv_strerror(status));
    }
    close_session(s);
  } else if (!s->closing) {
    maybe_finish_relay_half_close(s);
  }

  if (s->closing)
    try_free_session(s);
}

static void schedule_relay_shutdown(Session *s, bool client_side) {
  if (!s || s->closing || !is_relay_state(s))
    return;

  bool &read_eof = client_side ? s->remote_read_eof : s->client_read_eof;
  bool &shutdown_pending = client_side ? s->client_shutdown_pending
                                       : s->remote_shutdown_pending;
  bool &shutdown_done =
      client_side ? s->client_shutdown_done : s->remote_shutdown_done;
  const size_t pending_writes = client_side ? s->pending_client_writes
                                            : s->pending_remote_writes;
  if (!read_eof || shutdown_pending || shutdown_done || pending_writes != 0)
    return;
  if (s->tunnel && !client_side) {
    if (s->tunnel_waiting) return;
    shutdown_done = true;
    const auto &cb = s->bridge->tunnel_callbacks;
    if (cb.eof) cb.eof(cb.user_data, s->internal_connection);
    return;
  }

  uv_stream_t *stream = client_side
                            ? reinterpret_cast<uv_stream_t *>(&s->client)
                            : reinterpret_cast<uv_stream_t *>(&s->remote);
  uv_shutdown_t *shutdown_req =
      client_side ? &s->client_shutdown_req : &s->remote_shutdown_req;
  if (uv_is_closing(reinterpret_cast<uv_handle_t *>(stream))) {
    close_session(s);
    return;
  }

  shutdown_req->data = s;
  shutdown_pending = true;
  const int rc = uv_shutdown(shutdown_req, stream, on_relay_shutdown);
  if (rc != 0) {
    shutdown_pending = false;
    emit_bridge_error(
        s, rc,
        client_side ? "relay_client_shutdown_schedule_failed"
                    : "relay_remote_shutdown_schedule_failed",
        std::string("Scheduling relay write-side shutdown failed: ") +
            uv_strerror(rc));
    close_session(s);
  }
}

static void maybe_schedule_relay_shutdowns(Session *s) {
  if (!s || s->closing || !is_relay_state(s))
    return;
  schedule_relay_shutdown(s, true);
  if (!s->closing)
    schedule_relay_shutdown(s, false);
  if (!s->closing)
    maybe_finish_relay_half_close(s);
}

static void on_relay_read_end(Session *s, uv_stream_t *stream, ssize_t nread) {
  if (!s || s->closing)
    return;
  if (nread != UV_EOF) {
    close_session(s);
    return;
  }

  observe_eof(s, nread);
  uv_read_stop(stream);
  if (stream == reinterpret_cast<uv_stream_t *>(&s->client)) {
    s->client_reading = false;
    s->client_read_eof = true;
  } else {
    assert(stream == reinterpret_cast<uv_stream_t *>(&s->remote));
    s->remote_reading = false;
    s->remote_read_eof = true;
  }
  maybe_schedule_relay_shutdowns(s);
}

// remote → client
static void on_remote_read(uv_stream_t *stream, ssize_t nread,
                           const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0) {
    s->bytes_down += static_cast<uint64_t>(nread);
    ++s->pkt_count_down;
    if (!s->closing)
      write_to(reinterpret_cast<uv_stream_t *>(&s->client), buf->base,
               static_cast<size_t>(nread));
  } else if (nread < 0) {
    on_relay_read_end(s, stream, nread);
  }
  delete[] buf->base;
}

// client → remote (also performs passive TLS ClientHello capture)
static void on_tunnel_client_read(uv_stream_t *stream, ssize_t nread,
                                  const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0) {
    auto *data = reinterpret_cast<const uint8_t *>(buf->base);
    size_t len = static_cast<size_t>(nread);

    if (!s->fingerprint.extracted && session_config(s).tls_fingerprint) {
      if (tls_fp::parse(data, len, s->fingerprint)) {
        if (s->bridge->callbacks.on_fingerprint) {
          std::string fp_json = s->fingerprint.to_json();
          s->bridge->callbacks.on_fingerprint(s->bridge, s, fp_json.c_str());
        }
      }
    }

    s->bytes_up += static_cast<uint64_t>(len);
    ++s->pkt_count_up;
    if (!s->closing)
      write_to(reinterpret_cast<uv_stream_t *>(&s->remote), buf->base, len);
  } else if (nread < 0) {
    on_relay_read_end(s, stream, nread);
  }
  delete[] buf->base;
}

static void start_relay(Session *s) {
  uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
  s->client_reading = false;

  uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                on_tunnel_client_read);
  s->client_reading = true;

  if (!s->tunnel) {
    uv_read_start(reinterpret_cast<uv_stream_t *>(&s->remote), alloc_cb,
                  on_remote_read);
    s->remote_reading = true;
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Plain HTTP forward relay
// ═════════════════════════════════════════════════════════════════════════════

static void on_forward_remote_read(uv_stream_t *stream, ssize_t nread,
                                   const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0) {
    s->bytes_down += static_cast<uint64_t>(nread);
    ++s->pkt_count_down;
    if (!s->closing)
      write_to(reinterpret_cast<uv_stream_t *>(&s->client), buf->base,
               static_cast<size_t>(nread));
  } else if (nread < 0) {
    on_relay_read_end(s, stream, nread);
  }
  delete[] buf->base;
}

static void on_forward_client_read(uv_stream_t *stream, ssize_t nread,
                                   const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0) {
    s->bytes_up += static_cast<uint64_t>(nread);
    ++s->pkt_count_up;
    if (!s->closing)
      write_to(reinterpret_cast<uv_stream_t *>(&s->remote), buf->base,
               static_cast<size_t>(nread));
  } else if (nread < 0) {
    on_relay_read_end(s, stream, nread);
  }
  delete[] buf->base;
}

static void start_http_forward_relay(Session *s) {
  s->state = SessionState::HttpForwarding;
  if (!s->replay_buf.empty()) {
    write_to(reinterpret_cast<uv_stream_t *>(&s->remote),
             s->replay_buf.data(), s->replay_buf.size());
    s->replay_buf.clear();
  }

  s->state = SessionState::HttpForwarding;
  if (s->tunnel) {
    if (!s->tunnel_waiting) {
      uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                    on_forward_client_read);
      s->client_reading = true;
    }
    return;
  }
  uv_read_start(reinterpret_cast<uv_stream_t *>(&s->remote), alloc_cb,
                on_forward_remote_read);
  s->remote_reading = true;
  uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
  s->client_reading = false;
  uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                on_forward_client_read);
  s->client_reading = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Upstream HTTP-CONNECT handshake
// ═════════════════════════════════════════════════════════════════════════════

static void send_connect_established(Session *s);

static void on_upstream_http_connect_read(uv_stream_t *stream, ssize_t nread,
                                          const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  auto &reply = s->upstream_buf;
  static constexpr char end_marker[] = "\r\n\r\n";
  auto end = std::search(reply.begin(), reply.end(), end_marker, end_marker + 4);
  size_t header_size = end == reply.end() ? 0 : size_t(end - reply.begin()) + 4;
  size_t consumed = 0;
  // Check each header byte before growing the buffer. A read containing a
  // short header and a large business tail is not an oversized header.
  while (!header_size && consumed < size_t(std::max<ssize_t>(0, nread)) &&
         reply.size() < kMaxHeaderBuf) {
    reply.push_back(static_cast<uint8_t>(buf->base[consumed++]));
    if (reply.size() >= 4 && std::equal(reply.end() - 4, reply.end(), end_marker))
      header_size = reply.size();
  }
  if (header_size > kMaxHeaderBuf || (!header_size && reply.size() >= kMaxHeaderBuf)) {
    delete[] buf->base;
    emit_bridge_error(s, -1, "upstream_http_header_too_large",
                      "Upstream HTTP CONNECT response header exceeds the limit.");
    send_fast_failure_to_client(s, "upstream_http_header_too_large");
    close_session(s);
    return;
  }
  if (header_size && nread > 0)
    reply.insert(reply.end(), buf->base + consumed, buf->base + nread);
  delete[] buf->base;

  observe_eof(s, nread);
  if (nread < 0 || s->closing) {
    if (!s->closing) {
      emit_bridge_error(
          s, static_cast<int>(nread), "upstream_http_connect_read_failed",
          std::string("Reading upstream HTTP CONNECT response failed: ") +
              uv_strerror(static_cast<int>(nread)));
      mark_upstream_failure(s, "upstream_http_connect_read_failed");
      send_fast_failure_to_client(s, "upstream_http_connect_read_failed");
      close_session(s);
    }
    return;
  }

  if (!header_size)
    return; // need more data
  const std::string resp(reply.begin(), reply.begin() + header_size);

  if (resp.size() < 12 || resp.substr(9, 3) != "200") {
    int http_status = 0;
    if (resp.size() >= 12) {
      try {
        http_status = std::stoi(resp.substr(9, 3));
      } catch (...) {
      }
    }
    emit_bridge_error(s, http_status > 0 ? http_status : -1,
                      "upstream_http_connect_rejected",
                      "Upstream HTTP CONNECT rejected.",
                      static_cast<int>(s->upstream_idx), http_status);
    // CONNECT rejection is scoped to this destination/policy. It does not
    // prove that the proxy endpoint is unavailable for other targets.
    send_fast_failure_to_client(s, "upstream_http_connect_rejected");
    close_session(s);
    return;
  }

  stop_phase_timer(s);
  uv_read_stop(stream);
  s->remote_reading = false;

  // ── Upstream proxy fingerprint ────────────────────────────────────────────
  // Analyse the proxy's HTTP CONNECT response headers before discarding them.
  if (session_config(s).proxy_fingerprint &&
      s->bridge->callbacks.on_proxy_fingerprint) {
    ProxyFingerprint pfp;
    if (proxy_fp::analyze_response(reply.data(), header_size,
                                   pfp)) {
      std::string j = pfp.to_json();
      s->bridge->callbacks.on_proxy_fingerprint(s->bridge, s, j.c_str());
    }
  }

  reply.erase(reply.begin(), reply.begin() + header_size);
  advance_upstream(s);
}

// Scope includes the actual synchronous scheduling/publication operation. The
// host may serialize this scope with generation changes without holding a lock
// across DNS, connect completion, or upstream handshakes.
class ExecutionFence {
public:
  ExecutionFence(Session *session, int stage) : s(session), phase(stage) {
    auto &guard = s->bridge->execution_guard;
    allowed = !guard.enter || guard.enter(guard.user_data, s, phase) != 0;
  }
  ~ExecutionFence() {
    auto &guard = s->bridge->execution_guard;
    if (allowed && guard.enter) guard.leave(guard.user_data, s, phase);
  }
  bool Allowed() const { return allowed; }
private:
  Session *s;
  int phase;
  bool allowed;
};

static void deliver_upstream_tail(Session *s) {
  if (s->closing || s->upstream_buf.empty())
    return;
  s->bytes_down += s->upstream_buf.size();
  ++s->pkt_count_down;
  write_to(reinterpret_cast<uv_stream_t *>(&s->client),
           s->upstream_buf.data(), s->upstream_buf.size());
  s->upstream_buf.clear();
}

static void send_connect_established(Session *s) {
  ExecutionFence fence(s, UVBRG_EXECUTE_COMMIT);
  if (!fence.Allowed()) {
    close_session(s);
    return;
  }
  if (s->client_protocol == ClientProtocol::HttpForward) {
    start_http_forward_relay(s);
    deliver_upstream_tail(s);
    return;
  }

  if (s->client_protocol == ClientProtocol::HttpConnect) {
    const char *ok = "HTTP/1.1 200 Connection established\r\n\r\n";
    write_str(reinterpret_cast<uv_stream_t *>(&s->client), ok);
    s->state = SessionState::HttpTunneling;
    start_relay(s);
    deliver_upstream_tail(s);
    return;
  }

  // Default to SOCKS5 for existing callers that entered through the SOCKS5
  // state machine before upstream handshakes replaced SessionState.
  const uint8_t ok[] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
  write_to(reinterpret_cast<uv_stream_t *>(&s->client), ok, sizeof(ok));
  s->state = SessionState::Socks5Tunneling;
  start_relay(s);
  deliver_upstream_tail(s);
}

// ═════════════════════════════════════════════════════════════════════════════
// Upstream SOCKS5 handshake
// ═════════════════════════════════════════════════════════════════════════════

static void on_upstream_socks5_read(uv_stream_t *stream, ssize_t nread,
                                    const uv_buf_t *buf);

// ─── Multi-hop upstream chain helpers ────────────────────────────────────────

// Returns the host:port this upstream hop should CONNECT to — either the next
// upstream proxy in the chain (if more hops remain) or the session's final
// destination.
static void get_connect_target(const Session *s, std::string &host,
                               uint16_t &port) {
  size_t next_idx = s->upstream_idx + 1;
  const auto &upstreams = session_upstreams(s);
  if (next_idx < upstreams.size()) {
    host = upstreams[next_idx].addr;
    port = upstreams[next_idx].port;
  } else {
    host = s->target_host;
    port = s->target_port;
  }
}

static void send_upstream_socks5_connect(Session *s) {
  std::string chost;
  uint16_t cport;
  get_connect_target(s, chost, cport);

  std::vector<uint8_t> req;
  req.push_back(0x05);
  req.push_back(0x01); // CMD=CONNECT
  req.push_back(0x00); // RSV
  req.push_back(0x03); // ATYP=domain
  uint8_t hlen = static_cast<uint8_t>(std::min(chost.size(), size_t(255)));
  req.push_back(hlen);
  req.insert(req.end(), chost.begin(), chost.begin() + hlen);
  req.push_back(static_cast<uint8_t>(cport >> 8));
  req.push_back(static_cast<uint8_t>(cport & 0xff));
  write_to(reinterpret_cast<uv_stream_t *>(&s->remote), req.data(), req.size());
  s->state = SessionState::UpstreamSocks5Connect;
  arm_phase_timer(s, session_config(s).upstream_handshake_timeout_ms,
                  "upstream_socks5_connect_timeout");
}

static void on_upstream_socks5_read(uv_stream_t *stream, ssize_t nread,
                                    const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0)
    s->upstream_buf.insert(s->upstream_buf.end(), buf->base, buf->base + nread);
  delete[] buf->base;

  observe_eof(s, nread);
  if (nread < 0 || s->closing) {
    if (!s->closing) {
      emit_bridge_error(
          s, static_cast<int>(nread), "upstream_socks5_read_failed",
          std::string("Reading upstream SOCKS5 response failed: ") +
              uv_strerror(static_cast<int>(nread)));
      mark_upstream_failure(s, "upstream_socks5_read_failed");
      send_fast_failure_to_client(s, "upstream_socks5_read_failed");
      close_session(s);
    }
    return;
  }

  auto &b = s->upstream_buf;
  const UpstreamConfig &up = session_upstreams(s)[s->upstream_idx];

  if (s->state == SessionState::UpstreamSocks5Greet) {
    if (b.size() < 2)
      return;
    if (b[0] != 0x05 || b[1] == 0xff) {
      const bool invalid_version = b[0] != 0x05;
      emit_bridge_error(
          s, static_cast<int>(b[1]),
          invalid_version ? "upstream_socks5_invalid_greeting"
                          : "upstream_socks5_no_acceptable_auth",
          invalid_version
              ? "Upstream SOCKS5 proxy returned an invalid greeting."
              : "Upstream SOCKS5 proxy has no acceptable authentication "
                "method.",
          static_cast<int>(s->upstream_idx), 0,
          invalid_version ? -1 : static_cast<int>(b[1]));
      mark_upstream_failure(s, invalid_version
                                   ? "upstream_socks5_invalid_greeting"
                                   : "upstream_socks5_no_acceptable_auth");
      send_fast_failure_to_client(s, invalid_version
                                         ? "upstream_socks5_invalid_greeting"
                                         : "upstream_socks5_no_acceptable_auth");
      close_session(s);
      return;
    }
    // Server selected user/pass (0x02) — send credentials if we have them.
    if (b[1] == 0x02 && !up.auth.user.empty()) {
      std::string auth;
      auth += '\x01';
      uint8_t ulen =
          static_cast<uint8_t>(std::min(up.auth.user.size(), size_t(255)));
      auth += static_cast<char>(ulen);
      auth += up.auth.user.substr(0, ulen);
      uint8_t plen =
          static_cast<uint8_t>(std::min(up.auth.pass.size(), size_t(255)));
      auth += static_cast<char>(plen);
      auth += up.auth.pass.substr(0, plen);
      write_to(reinterpret_cast<uv_stream_t *>(&s->remote), auth.data(),
               auth.size());
      s->state = SessionState::UpstreamSocks5Auth;
      arm_phase_timer(s, session_config(s).upstream_handshake_timeout_ms,
                      "upstream_socks5_auth_timeout");
      b.erase(b.begin(), b.begin() + 2);
    } else {
      b.erase(b.begin(), b.begin() + 2);
      send_upstream_socks5_connect(s);
    }
  } else if (s->state == SessionState::UpstreamSocks5Auth) {
    if (b.size() < 2)
      return;
    if (b[0] != 0x01 || b[1] != 0x00) {
      emit_bridge_error(
          s, static_cast<int>(b[1]), "upstream_socks5_auth_failed",
          std::string(
              "Upstream SOCKS5 authentication failed with reply code ") +
              std::to_string(static_cast<int>(b[1])) + ".",
          static_cast<int>(s->upstream_idx));
      mark_upstream_failure(s, "upstream_socks5_auth_failed");
      send_fast_failure_to_client(s, "upstream_socks5_auth_failed");
      close_session(s);
      return;
    }
    b.erase(b.begin(), b.begin() + 2);
    send_upstream_socks5_connect(s);
  } else if (s->state == SessionState::UpstreamSocks5Connect) {
    if (b.size() < 4)
      return;
    if (b[0] != 0x05 || b[1] != 0x00) {
      const char *reply_name = socks5_reply_name(static_cast<int>(b[1]));
      emit_bridge_error(
          s, static_cast<int>(b[1]), "upstream_socks5_connect_rejected",
          std::string("Upstream SOCKS5 CONNECT rejected") +
              ((reply_name && *reply_name)
                   ? std::string(": ").append(reply_name)
                   : std::string(".")),
          static_cast<int>(s->upstream_idx), 0, static_cast<int>(b[1]));
      // SOCKS5 CONNECT reply codes describe this destination request. Do not
      // let one rejected host open the endpoint-wide circuit breaker.
      send_fast_failure_to_client(s, "upstream_socks5_connect_rejected");
      close_session(s);
      return;
    }
    size_t addr_len = 0;
    if (b[3] == 0x01)
      addr_len = 4;
    else if (b[3] == 0x03) {
      if (b.size() < 5)
        return;
      addr_len = 1 + b[4];
    } else if (b[3] == 0x04)
      addr_len = 16;
    else {
      send_fast_failure_to_client(s, "upstream_socks5_invalid_address");
      close_session(s);
      return;
    }
    if (b.size() < 4 + addr_len + 2)
      return;

    uv_read_stop(stream);
    s->remote_reading = false;
    stop_phase_timer(s);
    b.erase(b.begin(), b.begin() + 4 + addr_len + 2);
    advance_upstream(s);
    return;
  }
  // A peer may coalesce consecutive handshake replies. Consume only this
  // stage's bytes and let the next stage validate the remainder.
  if (!s->closing && !b.empty()) {
    const uv_buf_t empty{};
    on_upstream_socks5_read(stream, 0, &empty);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// After remote TCP connection is established
// ═════════════════════════════════════════════════════════════════════════════

static void send_upstream_socks5_greeting(Session *s) {
  const UpstreamConfig &up = session_upstreams(s)[s->upstream_idx];
  // Offer user/pass auth whenever credentials are present, regardless of
  // the auth.type label (avoids breakage when type is "userpass" etc.).
  bool have_auth = !up.auth.user.empty();
  uint8_t greeting[4] = {0x05, have_auth ? uint8_t(2) : uint8_t(1), 0x00, 0x02};
  write_to(reinterpret_cast<uv_stream_t *>(&s->remote), greeting,
           have_auth ? 4u : 3u);
  s->state = SessionState::UpstreamSocks5Greet;
  arm_phase_timer(s, session_config(s).upstream_handshake_timeout_ms,
                  "upstream_socks5_greeting_timeout");
  uv_read_start(reinterpret_cast<uv_stream_t *>(&s->remote), alloc_cb,
                on_upstream_socks5_read);
  s->remote_reading = true;
}

static void send_upstream_http_connect(Session *s) {
  const UpstreamConfig &up = session_upstreams(s)[s->upstream_idx];
  std::string chost;
  uint16_t cport;
  get_connect_target(s, chost, cport);
  std::string req = "CONNECT " + chost + ":" + std::to_string(cport) +
                    " HTTP/1.1\r\nHost: " + chost + ":" +
                    std::to_string(cport) + "\r\n";

  if (up.auth.type == "basic" && !up.auth.user.empty()) {
    static const char kB64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string creds = up.auth.user + ":" + up.auth.pass;
    std::string b64;
    b64.reserve((creds.size() + 2) / 3 * 4);
    for (size_t i = 0; i < creds.size(); i += 3) {
      unsigned char c0 = (unsigned char)creds[i];
      unsigned char c1 =
          (i + 1 < creds.size()) ? (unsigned char)creds[i + 1] : 0;
      unsigned char c2 =
          (i + 2 < creds.size()) ? (unsigned char)creds[i + 2] : 0;
      b64 += kB64[c0 >> 2];
      b64 += kB64[((c0 & 3) << 4) | (c1 >> 4)];
      b64 += (i + 1 < creds.size()) ? kB64[((c1 & 0xf) << 2) | (c2 >> 6)] : '=';
      b64 += (i + 2 < creds.size()) ? kB64[c2 & 0x3f] : '=';
    }
    req += "Proxy-Authorization: Basic " + b64 + "\r\n";
  }
  req += "\r\n";
  write_str(reinterpret_cast<uv_stream_t *>(&s->remote), req);
  s->state = SessionState::UpstreamHttpConnect;
  arm_phase_timer(s, session_config(s).upstream_handshake_timeout_ms,
                  "upstream_http_connect_timeout");
  uv_read_start(reinterpret_cast<uv_stream_t *>(&s->remote), alloc_cb,
                on_upstream_http_connect_read);
  s->remote_reading = true;
}

// ─── advance_upstream
// ───────────────────────────────────────────────────────── Called when one
// upstream hop's handshake finishes.  Advances the hop index and either starts
// the next hop's handshake (SOCKS5 greeting or HTTP CONNECT) or — when all hops
// are done — signals the client that the tunnel is ready.
static void advance_upstream(Session *s) {
  mark_upstream_success(s, s->upstream_idx);
  emit_route_step(s, s->upstream_idx, "success");
  ++s->upstream_idx;
  const auto &upstreams = session_upstreams(s);
  if (s->upstream_idx < upstreams.size()) {
    if (fail_fast_if_circuit_open(s, s->upstream_idx))
      return;
    const UpstreamConfig &up = upstreams[s->upstream_idx];
    emit_route_step(s, s->upstream_idx, "begin");
    if (up.type == UpstreamType::Socks5) {
      send_upstream_socks5_greeting(s);
      if (!s->upstream_buf.empty()) {
        const uv_buf_t empty{};
        on_upstream_socks5_read(reinterpret_cast<uv_stream_t *>(&s->remote), 0, &empty);
      }
    } else if (up.type == UpstreamType::HttpConnect) {
      send_upstream_http_connect(s);
      if (!s->upstream_buf.empty()) {
        const uv_buf_t empty{};
        on_upstream_http_connect_read(reinterpret_cast<uv_stream_t *>(&s->remote), 0, &empty);
      }
    } else {
      // Unexpected upstream type — treat as connected.
      send_connect_established(s);
    }
  } else {
    // All hops exhausted — the tunnel to the final destination is ready.
    send_connect_established(s);
  }
}

static void on_remote_connected(uv_connect_t *req, int status) {
  Session *s = static_cast<Session *>(req->data);
  stop_phase_timer(s);

  if (status < 0 || s->closing) {
    if (!s->closing) {
      emit_bridge_error(
          s, status,
          session_upstreams(s).empty() ? "target_connect_failed"
                                       : "upstream_connect_failed",
          std::string(session_upstreams(s).empty()
                          ? "Connecting to target failed: "
                          : "Connecting to upstream route failed: ") +
              uv_strerror(status));
      if (!session_upstreams(s).empty())
        mark_upstream_failure(s, "upstream_connect_failed");
      send_fast_failure_to_client(
          s, session_upstreams(s).empty() ? "target_connect_failed"
                                          : "upstream_connect_failed");
      close_session(s);
    }
    return;
  }

  ProxyBridgeImpl *b = s->bridge;
  // ── TCP fingerprint of the remote connection ──────────────────────────────
  // Probes negotiated kernel TCP parameters (MSS, buffers, RTT) to characterise
  // the upstream proxy's or target server's network stack.
  if (session_config(s).tcp_fingerprint && b->callbacks.on_tcp_fingerprint) {
    TcpFingerprint tfp;
    if (tcp_fp::probe(&s->remote, tfp)) {
      std::string j = tfp.to_json();
      b->callbacks.on_tcp_fingerprint(b, s, j.c_str());
    }
  }

  if (!session_upstreams(s).empty()) {
    // The initial client request has already been parsed. HTTP forwarding keeps
    // its rewritten request in replay_buf. Remote replies use upstream_buf;
    // client request bytes must never be treated as upstream handshake bytes.
    s->client_buf.clear();
    s->upstream_buf.clear();
    const UpstreamConfig &up = session_upstreams(s)[s->upstream_idx];
    if (up.type == UpstreamType::Socks5) {
      send_upstream_socks5_greeting(s);
      return;
    } else if (up.type == UpstreamType::HttpConnect) {
      send_upstream_http_connect(s);
      return;
    }
  }

  // Direct connection — reply to client and start relaying
  if (s->state == SessionState::HttpForwardConnecting ||
      s->state == SessionState::HttpForwarding) {
    start_http_forward_relay(s);
  } else {
    send_connect_established(s);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// DNS resolution
// ═════════════════════════════════════════════════════════════════════════════

static void on_resolved_v2(uv_getaddrinfo_t *req, int status,
                           struct addrinfo *res) {
  Session *s = static_cast<Session *>(req->data);
  if (s->resolver_pending) {
    s->resolver_pending = false;
    s->bridge->pending_dns.fetch_sub(1, std::memory_order_relaxed);
  }

  if (s->closing) {
    if (res)
      uv_freeaddrinfo(res);
    if (!s->remote_initialized)
      s->remote_closed = true;
    try_free_session(s);
    return;
  }

  if (status < 0 || !res) {
    if (status < 0) {
      emit_bridge_error(s, status, "dns_resolve_failed",
                        std::string("DNS resolve failed: ") +
                            uv_strerror(status));
    } else {
      emit_bridge_error(s, 0, "dns_resolve_failed",
                        "DNS resolve returned no addresses.");
    }
    if (res)
      uv_freeaddrinfo(res);
    if (!session_upstreams(s).empty())
      mark_upstream_failure(s, "upstream_dns_resolve_failed");
    send_fast_failure_to_client(s, "dns_resolve_failed");
    close_session(s);
    return;
  }

  ProxyBridgeImpl *b = s->bridge;
  uv_tcp_init(b->loop, &s->remote);
  s->remote.data = s;
  s->remote_initialized = true;

  s->conn_req.data = s;
  const bool direct_target = session_upstreams(s).empty();
  arm_phase_timer(s,
                  direct_target
                      ? session_config(s).target_connect_timeout_ms
                      : session_config(s).upstream_connect_timeout_ms,
                  direct_target ? "target_connect_timeout"
                                : "upstream_connect_timeout");
  int r;
  {
    ExecutionFence fence(s, UVBRG_EXECUTE_DIAL);
    if (!fence.Allowed()) {
      uv_freeaddrinfo(res);
      close_session(s);
      return;
    }
    r = uv_tcp_connect(&s->conn_req, &s->remote, res->ai_addr,
                       on_remote_connected);
  }
  uv_freeaddrinfo(res);
  if (r != 0) {
    emit_bridge_error(s, r, "tcp_connect_request_failed",
                      std::string("Scheduling TCP connect failed: ") +
                          uv_strerror(r));
    if (!session_upstreams(s).empty())
      mark_upstream_failure(s, "upstream_tcp_connect_request_failed");
    send_fast_failure_to_client(s, "tcp_connect_request_failed");
    close_session(s);
  }
}

static void start_dns_resolve(Session *s, const std::string &host,
                              uint16_t port) {
  struct addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  s->resolver.data = s;
  s->resolver_pending = true;
  s->bridge->pending_dns.fetch_add(1, std::memory_order_relaxed);
  const bool direct_target = session_upstreams(s).empty();
  arm_phase_timer(s,
                  direct_target
                      ? session_config(s).target_dns_timeout_ms
                      : session_config(s).upstream_dns_timeout_ms,
                  direct_target ? "target_dns_resolve_timeout"
                                : "upstream_dns_resolve_timeout");

  std::string port_str = std::to_string(port);
  int r = uv_getaddrinfo(s->bridge->loop, &s->resolver, on_resolved_v2,
                         host.c_str(), port_str.c_str(), &hints);
  if (r != 0) {
    s->resolver_pending = false;
    s->bridge->pending_dns.fetch_sub(1, std::memory_order_relaxed);
    emit_bridge_error(s, r, "dns_resolve_request_failed",
                      std::string("Scheduling DNS resolve failed: ") +
                          uv_strerror(r));
    if (!session_upstreams(s).empty())
      mark_upstream_failure(s, "upstream_dns_resolve_request_failed");
    send_fast_failure_to_client(s, "dns_resolve_request_failed");
    close_session(s);
  }
}

// ─── Resolve target or upstream proxy ────────────────────────────────────────
static void on_pending_tunnel_read(uv_stream_t *stream, ssize_t nread,
                                   const uv_buf_t *buf) {
  auto *s = static_cast<Session *>(stream->data);
  if (!s->closing && nread > 0) {
    // SOCKS/CONNECT clients normally wait for success. Bound any optimistic
    // bytes, but continue observing EOF so cancelling a waiting browser tab
    // does not occupy admission/stream credit until the connect timer fires.
    if (s->client_buf.size() + static_cast<size_t>(nread) > 32768)
      close_session(s);
    else
      s->client_buf.insert(s->client_buf.end(), buf->base, buf->base + nread);
  } else if (!s->closing && nread < 0) {
    observe_eof(s, nread);
    close_session(s);
  }
  delete[] buf->base;
}

static void resolve_target(Session *s) {
  // All external HTTP/CONNECT/SOCKS5 routes converge here. Keep this guard
  // even though configuration APIs reject conflicts: a strict route must
  // never become DIRECT or bypass its proxy chain via a host adapter.
  if (s->config.strict_proxy &&
      (session_upstreams(s).empty() || s->bridge->tunnel_callbacks.open)) {
    emit_bridge_error(s, UV_EINVAL, "strict_proxy_route_rejected",
                      "Strict proxy route has no usable upstream chain or conflicts with a tunnel adapter.");
    send_fast_failure_to_client(s, "strict_proxy_route_rejected");
    close_session(s);
    return;
  }
  if (s->bridge->tunnel_callbacks.open) {
    s->tunnel = true;
    // HTTP's parser retains the original header for internal routes. It must
    // not be replayed inside CONNECT. Only already-read tunnel bytes remain.
    if (s->client_protocol == ClientProtocol::HttpConnect)
      s->client_buf = std::move(s->http_body_tail);
    else if (s->client_protocol == ClientProtocol::HttpForward)
      s->client_buf.clear();
    s->remote.data = s;
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
    s->client_reading = false;
    arm_phase_timer(s, s->config.tunnel_connect_timeout_ms, "tunnel_connect_timeout");
    const auto &cb = s->bridge->tunnel_callbacks;
    if (cb.open(cb.user_data, s->internal_connection, s->target_host.c_str(),
                 s->target_port) != 0) {
      send_fast_failure_to_client(s, "tunnel_rejected");
      close_session(s);
    }
    if (!s->closing && s->client_protocol != ClientProtocol::HttpForward) {
      const int rc = uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client),
                                   alloc_cb, on_pending_tunnel_read);
      s->client_reading = rc == 0;
      if (rc != 0) close_session(s);
    }
    return;
  }
  if (!session_upstreams(s).empty()) {
    const UpstreamConfig &up = session_upstreams(s)[0];
    if (fail_fast_if_circuit_open(s, 0))
      return;
    emit_route_step(s, 0, "begin");
    start_dns_resolve(s, up.addr, up.port);
  } else {
    start_dns_resolve(s, s->target_host, s->target_port);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Authentication
// ═════════════════════════════════════════════════════════════════════════════

static bool authenticate(Session *s, const std::string &proxy_auth_value) {
  ProxyBridgeImpl *b = s->bridge;
  const BridgeConfig &config = session_config(s);
  if (config.auth_mode == AuthMode::None) {
    s->auth_ok = true;
    return true;
  }

  std::lock_guard<std::mutex> lk(b->instances_mu);

  if (config.auth_mode == AuthMode::Token) {
    std::string token = decode_bearer(proxy_auth_value);
    if (token.empty())
      token = trim(proxy_auth_value);
    const InstanceInfo *info = b->find_instance_locked(token);
    if (!info)
      return false;
    s->auth_ok = true;
    s->instance_id = info->instance_id;
    s->profile_id = info->profile_id;
    return true;
  }

  if (config.auth_mode == AuthMode::Basic) {
    std::string creds = decode_basic_auth(proxy_auth_value);
    auto colon = creds.find(':');
    if (colon == std::string::npos)
      return false;
    std::string user = creds.substr(0, colon);
    std::string token = creds.substr(colon + 1);
    const InstanceInfo *info = b->find_instance_locked(token);
    if (!info || info->instance_id != user)
      return false;
    s->auth_ok = true;
    s->instance_id = info->instance_id;
    s->profile_id = info->profile_id;
    return true;
  }
  return false;
}

// ═════════════════════════════════════════════════════════════════════════════
// HTTP request parsing
// ═════════════════════════════════════════════════════════════════════════════

struct ParsedHttp {
  std::string method, url, version;
  std::vector<std::pair<std::string, std::string>> headers;
  size_t header_end{0};
};

static bool parse_http_headers(const uint8_t *buf, size_t len,
                               ParsedHttp &out) {
  const std::string raw(reinterpret_cast<const char *>(buf), len);
  auto hdr_end = raw.find("\r\n\r\n");
  if (hdr_end == std::string::npos)
    return false;
  out.header_end = hdr_end + 4;

  auto line_end = raw.find("\r\n");
  if (line_end == std::string::npos)
    return false;
  std::string req_line = raw.substr(0, line_end);
  auto sp1 = req_line.find(' ');
  auto sp2 = req_line.rfind(' ');
  if (sp1 == std::string::npos || sp2 == sp1)
    return false;
  out.method = req_line.substr(0, sp1);
  out.url = req_line.substr(sp1 + 1, sp2 - sp1 - 1);
  out.version = req_line.substr(sp2 + 1);

  size_t pos = line_end + 2;
  while (pos < hdr_end) {
    auto next = raw.find("\r\n", pos);
    if (next == std::string::npos)
      break;
    std::string hline = raw.substr(pos, next - pos);
    pos = next + 2;
    auto colon = hline.find(':');
    if (colon == std::string::npos)
      continue;
    out.headers.emplace_back(trim(hline.substr(0, colon)),
                             trim(hline.substr(colon + 1)));
  }
  return true;
}

static std::string
get_header(const std::vector<std::pair<std::string, std::string>> &hdrs,
           const std::string &name) {
  std::string nl = to_lower(name);
  for (auto &h : hdrs)
    if (to_lower(h.first) == nl)
      return h.second;
  return {};
}

static void copy_cstr(char *dst, size_t cap, const std::string &value) {
  if (!dst || cap == 0)
    return;
  const size_t n = std::min(cap - 1, value.size());
  if (n > 0)
    std::memcpy(dst, value.data(), n);
  dst[n] = '\0';
}

static std::string normalize_host_for_match(std::string host) {
  host = trim(std::move(host));
  if (host.empty())
    return {};
  if (host.front() == '[') {
    const auto close = host.find(']');
    if (close != std::string::npos)
      host = host.substr(1, close - 1);
  } else {
    const auto colon = host.rfind(':');
    if (colon != std::string::npos)
      host = host.substr(0, colon);
  }
  return to_lower(trim(host));
}

static bool is_internal_route_host(const BridgeConfig &cfg,
                                   const std::string &host) {
  if (!cfg.internal_route_enable)
    return false;
  const std::string normalized = normalize_host_for_match(host);
  if (normalized.empty())
    return false;
  for (const auto &item : cfg.internal_route_hosts) {
    if (normalize_host_for_match(item) == normalized)
      return true;
  }
  return false;
}

static bool is_internal_route_session(const Session *s) {
  return s && is_internal_route_host(session_config(s), s->target_host);
}

static std::string parsed_http_path(const ParsedHttp &parsed) {
  std::string target = parsed.url.empty() ? "/" : parsed.url;
  const std::string lower = to_lower(target);
  if (lower.rfind("http://", 0) == 0) {
    const auto path = target.find_first_of("/?", 7);
    target = path == std::string::npos ? "/" : target.substr(path);
  }
  if (target.empty())
    target = "/";
  if (target.front() != '/')
    target.insert(target.begin(), '/');
  return target;
}

static std::string path_without_query(const std::string &path_query) {
  const auto pos = path_query.find('?');
  return pos == std::string::npos ? path_query : path_query.substr(0, pos);
}

static std::string query_from_path(const std::string &path_query) {
  const auto pos = path_query.find('?');
  return pos == std::string::npos ? std::string() : path_query.substr(pos + 1);
}

static size_t http_content_length(const ParsedHttp &parsed) {
  const std::string value = trim(get_header(parsed.headers, "Content-Length"));
  if (value.empty())
    return 0;
  size_t out = 0;
  for (char c : value) {
    if (!std::isdigit(static_cast<unsigned char>(c)))
      return 0;
    out = out * 10 + static_cast<size_t>(c - '0');
  }
  return out;
}

static std::string headers_to_json(
    const std::vector<std::pair<std::string, std::string>> &headers) {
  std::string out = "{";
  bool first = true;
  for (const auto &h : headers) {
    if (!first)
      out += ",";
    first = false;
    out += "\"";
    out += json_escape(h.first);
    out += "\":\"";
    out += json_escape(h.second);
    out += "\"";
  }
  out += "}";
  return out;
}

static std::string http_status_text(int status) {
  switch (status) {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 413:
    return "Payload Too Large";
  case 501:
    return "Not Implemented";
  case 503:
    return "Service Unavailable";
  default:
    return status >= 500 ? "Internal Server Error" : "Error";
  }
}

static std::string build_internal_http_response(int status,
                                                const std::string &content_type,
                                                const std::string &body,
                                                const std::string &origin = {}) {
  const std::string reason = http_status_text(status);
  std::string response = "HTTP/1.1 " + std::to_string(status) + " " + reason +
                         "\r\n";
  response += "Content-Type: " +
              (content_type.empty() ? "application/json" : content_type) +
              "\r\n";
  if (!origin.empty()) {
    response += "Access-Control-Allow-Origin: " + origin + "\r\n";
    response += "Vary: Origin\r\n";
  }
  response += "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n";
  response +=
      "Access-Control-Allow-Headers: "
      "Content-Type,Accept,X-BroSDK-Extension-Client\r\n";
  response += "Cache-Control: no-store\r\n";
  response += "Connection: close\r\n";
  response += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
  response += body;
  return response;
}

static bool internal_route_origin_allowed(const BridgeConfig &cfg,
                                          const ParsedHttp &parsed,
                                          bool allow_extension_client_marker,
                                          std::string *origin_out) {
  const std::string origin = trim(get_header(parsed.headers, "Origin"));
  if (origin_out)
    *origin_out = origin;
  if (origin.empty()) {
    // Some Chromium builds omit Origin for extension host-permission fetches.
    // The marker fallback is HTTP-only; WebSocket handshakes stay origin-only.
    if (allow_extension_client_marker) {
      const std::string marker = to_lower(trim(get_header(
          parsed.headers, "X-BroSDK-Extension-Client")));
      if (marker == "brosdk-mcp-bridge/1.0")
        return true;
      if (!marker.empty())
        return false;
    }
    return !cfg.internal_route_require_origin;
  }

  std::string normalized = to_lower(origin);
  while (normalized.size() > std::string("chrome-extension://").size() &&
         normalized.back() == '/') {
    normalized.pop_back();
  }
  for (const auto &configured : cfg.internal_route_allowed_origins) {
    std::string pattern = to_lower(trim(configured));
    while (pattern.size() > std::string("chrome-extension://").size() &&
           pattern.back() == '/') {
      pattern.pop_back();
    }
    if (pattern.empty())
      continue;
    if (pattern == normalized)
      return true;
    if (pattern.back() != '*')
      continue;
    const std::string prefix = pattern.substr(0, pattern.size() - 1);
    if (normalized.rfind(prefix, 0) != 0)
      continue;
    const std::string suffix = normalized.substr(prefix.size());
    if (prefix == "chrome-extension://") {
      if (suffix.size() == 32 &&
          std::all_of(suffix.begin(), suffix.end(), [](char c) {
            return c >= 'a' && c <= 'p';
          })) {
        return true;
      }
      continue;
    }
    if (!suffix.empty() && suffix.find_first_of("/\\?#") == std::string::npos)
      return true;
  }
  return false;
}

static std::string base64_encode(const uint8_t *data, size_t len) {
  if (!data || len == 0)
    return {};
  std::string out(4 * ((len + 2) / 3), '\0');
  const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(&out[0]),
                                data, static_cast<int>(len));
  if (n < 0)
    return {};
  out.resize(static_cast<size_t>(n));
  return out;
}

static std::string websocket_accept_key(const std::string &key) {
  static constexpr char kWsGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  const std::string material = trim(key) + kWsGuid;
  uint8_t digest[EVP_MAX_MD_SIZE] = {};
  unsigned int digest_len = 0;
  if (EVP_Digest(material.data(), material.size(), digest, &digest_len,
                 EVP_sha1(), nullptr) != 1) {
    return {};
  }
  return base64_encode(digest, digest_len);
}

static bool is_websocket_upgrade_request(const ParsedHttp &parsed) {
  if (to_lower(parsed.method) != "get")
    return false;
  const std::string upgrade = to_lower(get_header(parsed.headers, "Upgrade"));
  const std::string connection =
      to_lower(get_header(parsed.headers, "Connection"));
  return upgrade == "websocket" && connection.find("upgrade") != std::string::npos &&
         !get_header(parsed.headers, "Sec-WebSocket-Key").empty();
}

static std::vector<uint8_t> build_ws_frame(uint8_t opcode,
                                           const std::string &payload) {
  std::vector<uint8_t> out;
  out.reserve(payload.size() + 16);
  out.push_back(static_cast<uint8_t>(0x80 | (opcode & 0x0f)));
  const uint64_t len = static_cast<uint64_t>(payload.size());
  if (len <= 125) {
    out.push_back(static_cast<uint8_t>(len));
  } else if (len <= 0xffff) {
    out.push_back(126);
    out.push_back(static_cast<uint8_t>((len >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>(len & 0xff));
  } else {
    out.push_back(127);
    for (int shift = 56; shift >= 0; shift -= 8)
      out.push_back(static_cast<uint8_t>((len >> shift) & 0xff));
  }
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

static int write_ws_frame(Session *s, uint8_t opcode,
                          const std::string &payload) {
  const auto frame = build_ws_frame(opcode, payload);
  if (!frame.empty())
    return write_to(reinterpret_cast<uv_stream_t *>(&s->client), frame.data(),
                    frame.size());
  return UV_EINVAL;
}

static bool dispatch_internal_ws_text(Session *s, const std::string &message) {
  const auto &callbacks = s->bridge->internal_route_callbacks;
  int result = 0;
  if (callbacks.on_ws_message) {
    result = callbacks.on_ws_message(
        s->bridge, s->internal_connection, message.data(), message.size(),
        callbacks.user_data);
  }
  if (result == 0 && callbacks.on_ws_event) {
    result = callbacks.on_ws_event(s->bridge, s, message.c_str(),
                                   callbacks.user_data);
  }
  return result == 0;
}

static bool process_internal_ws_buffer(Session *s, bool *need_more) {
  if (need_more)
    *need_more = false;
  auto &buf = s->client_buf;
  while (true) {
    if (buf.size() < 2) {
      if (need_more)
        *need_more = true;
      return false;
    }

    const uint8_t b0 = buf[0];
    const uint8_t b1 = buf[1];
    const bool fin = (b0 & 0x80) != 0;
    const uint8_t opcode = b0 & 0x0f;
    const bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7f;
    size_t pos = 2;
    if (len == 126) {
      if (buf.size() < pos + 2) {
        if (need_more)
          *need_more = true;
        return false;
      }
      len = (static_cast<uint64_t>(buf[pos]) << 8) | buf[pos + 1];
      pos += 2;
    } else if (len == 127) {
      if (buf.size() < pos + 8) {
        if (need_more)
          *need_more = true;
        return false;
      }
      len = 0;
      for (int i = 0; i < 8; ++i)
        len = (len << 8) | buf[pos + static_cast<size_t>(i)];
      pos += 8;
    }

    if (!masked ||
        len > session_config(s).internal_route_max_websocket_frame_bytes) {
      write_ws_frame(s, 0x8, "");
      close_session(s);
      return true;
    }
    if (buf.size() < pos + 4 + static_cast<size_t>(len)) {
      if (need_more)
        *need_more = true;
      return false;
    }

    uint8_t mask[4] = {buf[pos], buf[pos + 1], buf[pos + 2], buf[pos + 3]};
    pos += 4;
    std::string payload;
    payload.resize(static_cast<size_t>(len));
    for (size_t i = 0; i < static_cast<size_t>(len); ++i)
      payload[i] = static_cast<char>(buf[pos + i] ^ mask[i % 4]);
    buf.erase(buf.begin(), buf.begin() + static_cast<ptrdiff_t>(pos + len));

    if (opcode == 0x8) {
      write_ws_frame(s, 0x8, "");
      close_session(s);
      return true;
    }
    if (opcode == 0x9) {
      write_ws_frame(s, 0xA, payload);
      continue;
    }
    if (opcode == 0xA) {
      continue;
    }
    if (opcode != 0x1 || !fin) {
      write_ws_frame(s, 0x8, "");
      close_session(s);
      return true;
    }
    if (!dispatch_internal_ws_text(s, payload)) {
      s->internal_close_reason = "message_rejected";
      write_ws_frame(s, 0x8, "");
      close_session(s);
      return true;
    }
  }
}

static bool internal_http_body_ready(Session *s, const ParsedHttp &parsed,
                                     size_t *content_length_out = nullptr) {
  const size_t content_length = http_content_length(parsed);
  if (content_length_out)
    *content_length_out = content_length;
  if (content_length > session_config(s).internal_route_max_body_bytes) {
    const std::string body =
        "{\"ok\":false,\"error\":{\"code\":\"PAYLOAD_TOO_LARGE\"}}";
    write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                        build_internal_http_response(413, "application/json",
                                                     body));
    return false;
  }
  return s->client_buf.size() >= parsed.header_end + content_length;
}

static void handle_internal_http_request(Session *s, const ParsedHttp &parsed,
                                         size_t content_length) {
  const std::string path_query = parsed_http_path(parsed);
  const std::string path = path_without_query(path_query);
  const std::string query = query_from_path(path_query);
  const std::string prefix = session_config(s).internal_route_path_prefix.empty()
                                 ? "/extension"
                                 : session_config(s).internal_route_path_prefix;
  const std::string websocket_path =
      session_config(s).internal_route_websocket_path.empty()
          ? prefix + "/ws"
          : session_config(s).internal_route_websocket_path;
  std::string origin;
  const bool websocket_route = path == websocket_path;
  if (!internal_route_origin_allowed(session_config(s), parsed,
                                     !websocket_route, &origin)) {
    const std::string body =
        "{\"ok\":false,\"error\":{\"code\":\"FORBIDDEN_ORIGIN\"}}";
    write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                        build_internal_http_response(403, "application/json",
                                                     body));
    return;
  }

  if (to_lower(parsed.method) == "options") {
    write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                        build_internal_http_response(204, "application/json",
                                                     "", origin));
    return;
  }

  if (path != prefix && path.rfind(prefix + "/", 0) != 0) {
    const std::string body =
        "{\"ok\":false,\"error\":{\"code\":\"NOT_FOUND\"}}";
    write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                        build_internal_http_response(404, "application/json",
                                                     body, origin));
    return;
  }

  if (path == websocket_path) {
    if (!is_websocket_upgrade_request(parsed)) {
      const std::string body =
          "{\"ok\":false,\"error\":{\"code\":\"BAD_WEBSOCKET_UPGRADE\"}}";
      write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                          build_internal_http_response(400, "application/json",
                                                       body, origin));
      return;
    }
    const std::string accept =
        websocket_accept_key(get_header(parsed.headers, "Sec-WebSocket-Key"));
    if (accept.empty()) {
      const std::string body =
          "{\"ok\":false,\"error\":{\"code\":\"BAD_WEBSOCKET_KEY\"}}";
      write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                          build_internal_http_response(400, "application/json",
                                                       body, origin));
      return;
    }
    std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        accept + "\r\n\r\n";
    write_str(reinterpret_cast<uv_stream_t *>(&s->client), response);
    const size_t consumed = parsed.header_end + content_length;
    if (consumed <= s->client_buf.size()) {
      s->client_buf.erase(s->client_buf.begin(),
                          s->client_buf.begin() +
                              static_cast<ptrdiff_t>(consumed));
    } else {
      s->client_buf.clear();
    }
    s->state = SessionState::InternalWebSocket;
    s->internal_ws_open_notified = true;
    const auto &callbacks = s->bridge->internal_route_callbacks;
    if (callbacks.on_ws_open) {
      callbacks.on_ws_open(s->bridge, s->internal_connection,
                           callbacks.user_data);
    }
    bool need_more = false;
    process_internal_ws_buffer(s, &need_more);
    if (!s->closing && !s->client_reading) {
      uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                    on_internal_ws_client_read);
      s->client_reading = true;
    }
    return;
  }

  const std::string headers_json = headers_to_json(parsed.headers);
  std::string body_text;
  if (content_length > 0) {
    body_text.assign(reinterpret_cast<const char *>(s->client_buf.data()) +
                         parsed.header_end,
                     content_length);
  }

  std::vector<char> response_body(kDefaultInternalRouteMaxBodyBytes + 1, 0);
  char content_type[128] = "application/json";
  uvbrg_internal_http_request_t request{};
  request.size = sizeof(request);
  request.method = parsed.method.c_str();
  request.path = path.c_str();
  request.query = query.c_str();
  request.host = s->target_host.c_str();
  request.port = s->target_port;
  request.headers_json = headers_json.c_str();
  request.body = body_text.c_str();
  request.body_size = body_text.size();
  request.connection = s->internal_connection;

  uvbrg_internal_http_response_t response{};
  response.size = sizeof(response);
  response.status_code = 503;
  response.content_type = content_type;
  response.content_type_cap = sizeof(content_type);
  response.body = response_body.data();
  response.body_cap = response_body.size();
  const std::string unavailable =
      "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_ROUTE_UNAVAILABLE\"}}";
  copy_cstr(response.body, response.body_cap, unavailable);
  response.body_size = unavailable.size();

  const auto &callbacks = s->bridge->internal_route_callbacks;
  int callback_result = UVBRG_INTERNAL_HTTP_COMPLETE;
  if (callbacks.on_http) {
    callback_result = callbacks.on_http(s->bridge, s, &request, &response,
                                        callbacks.user_data);
    if (callback_result != UVBRG_INTERNAL_HTTP_COMPLETE &&
        callback_result != UVBRG_INTERNAL_HTTP_DEFERRED &&
        response.status_code < 400) {
      response.status_code = 500;
    }
  }

  if (callback_result == UVBRG_INTERNAL_HTTP_DEFERRED) {
    s->internal_http_deferred = true;
    s->internal_http_origin = origin;
    s->client_buf.clear();
    if (!s->closing && !s->client_reading) {
      uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                    on_internal_deferred_http_read);
      s->client_reading = true;
    }
    return;
  }

  if (response.status_code <= 0)
    response.status_code = 500;
  if (response.body_size >= response.body_cap)
    response.body_size = response.body_cap > 0 ? response.body_cap - 1 : 0;
  const std::string final_body(response.body, response.body + response.body_size);
  write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                      build_internal_http_response(
                          response.status_code, response.content_type,
                          final_body, origin));
}

static bool process_internal_http_buffer(Session *s, bool *need_more) {
  if (need_more)
    *need_more = false;
  ParsedHttp parsed;
  if (!parse_http_headers(s->client_buf.data(), s->client_buf.size(), parsed)) {
    if (s->client_buf.size() >= kMaxHeaderBuf) {
      close_session(s);
      return true;
    }
    if (need_more)
      *need_more = true;
    return false;
  }

  size_t content_length = 0;
  if (!internal_http_body_ready(s, parsed, &content_length)) {
    if (content_length > session_config(s).internal_route_max_body_bytes)
      return true;
    if (need_more)
      *need_more = true;
    return false;
  }

  if (s->client_reading) {
    uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
    s->client_reading = false;
  }
  handle_internal_http_request(s, parsed, content_length);
  return true;
}

static bool parse_hostport(const std::string &hp, std::string &host,
                           uint16_t &port, uint16_t def = 80) {
  if (!hp.empty() && hp[0] == '[') {
    auto close = hp.find(']');
    if (close == std::string::npos)
      return false;
    host = hp.substr(1, close - 1);
    port = def;
    if (close + 1 < hp.size() && hp[close + 1] == ':')
      try {
        port = static_cast<uint16_t>(std::stoul(hp.substr(close + 2)));
      } catch (...) {
        return false;
      }
    return !host.empty();
  }
  auto colon = hp.rfind(':');
  if (colon == std::string::npos) {
    host = hp;
    port = def;
    return !host.empty();
  }
  host = hp.substr(0, colon);
  try {
    port = static_cast<uint16_t>(std::stoul(hp.substr(colon + 1)));
  } catch (...) {
    return false;
  }
  return !host.empty();
}

static void handle_http_request(Session *s, const ParsedHttp &parsed) {
  ProxyBridgeImpl *b = s->bridge;

  std::string proxy_auth = get_header(parsed.headers, "Proxy-Authorization");
  if (!authenticate(s, proxy_auth)) {
    write_str(reinterpret_cast<uv_stream_t *>(&s->client),
              "HTTP/1.1 407 Proxy Authentication Required\r\n"
              "Proxy-Authenticate: Bearer realm=\"libbridge\"\r\n"
              "Content-Length: 0\r\n\r\n");
    close_session(s);
    return;
  }

  if (b->callbacks.on_new_session)
    b->callbacks.on_new_session(b, s, s->session_id.c_str(),
                                s->instance_id.c_str());

  // ── HTTP fingerprint (JA4H + OS/browser detection) ───────────────────────
  if (session_config(s).http_fingerprint && b->callbacks.on_http_fingerprint) {
    HttpFingerprint hfp;
    if (http_fp::analyze(parsed.method, parsed.version, parsed.headers, hfp)) {
      std::string j = hfp.to_json();
      b->callbacks.on_http_fingerprint(b, s, j.c_str());
    }
  }

  const std::string method = to_lower(parsed.method);
  if (method == "connect") {
    s->client_protocol = ClientProtocol::HttpConnect;
    if (!parse_hostport(parsed.url, s->target_host, s->target_port, 443)) {
      write_str(reinterpret_cast<uv_stream_t *>(&s->client),
                "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
      close_session(s);
      return;
    }
    if (is_internal_route_session(s)) {
      const std::string body =
          "{\"ok\":false,\"error\":{\"code\":\"INTERNAL_TLS_UNSUPPORTED\"}}";
      write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                          build_internal_http_response(501, "application/json",
                                                       body));
      return;
    }
    if (maybe_redirect_or_block_security(s, "http_connect"))
      return;
    apply_proxy_bypass(s);
    s->state = SessionState::HttpConnecting;
    resolve_target(s);
  } else {
    s->client_protocol = ClientProtocol::HttpForward;
    // Plain HTTP forward proxy
    std::string url = parsed.url;
    uint16_t def_port = 80;
    if (url.size() > 7 && to_lower(url.substr(0, 7)) == "http://") {
      url = url.substr(7);
      auto slash = url.find('/');
      std::string hp = url.substr(0, slash);
      s->http_path = (slash == std::string::npos) ? "/" : url.substr(slash);
      if (!parse_hostport(hp, s->target_host, s->target_port, def_port)) {
        parse_hostport(get_header(parsed.headers, "Host"), s->target_host,
                       s->target_port, def_port);
      }
    } else {
      s->http_path = url;
      parse_hostport(get_header(parsed.headers, "Host"), s->target_host,
                     s->target_port, def_port);
    }
    if (is_internal_route_session(s)) {
      s->state = SessionState::InternalHttp;
      bool need_more = false;
      process_internal_http_buffer(s, &need_more);
      if (need_more && !s->client_reading) {
        uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                      on_internal_client_read);
        s->client_reading = true;
      }
      return;
    }
    if (maybe_redirect_or_block_security(s, "http_forward"))
      return;
    apply_proxy_bypass(s);

    // Build stripped/forwarded request
    std::string fwd =
        parsed.method + " " + s->http_path + " " + parsed.version + "\r\n";
    for (auto &h : parsed.headers) {
      std::string hl = to_lower(h.first);
      if (hl == "proxy-authorization" || hl == "proxy-connection" ||
          hl == "proxy-authenticate")
        continue;
      if (hl == "connection")
        fwd += "Connection: close\r\n";
      else
        fwd += h.first + ": " + h.second + "\r\n";
    }
    fwd += "\r\n";
    fwd.append(reinterpret_cast<const char *>(s->http_body_tail.data()),
               s->http_body_tail.size());

    s->replay_buf.assign(fwd.begin(), fwd.end());
    s->state = SessionState::HttpForwardConnecting;
    resolve_target(s);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// SOCKS5 protocol state machine
// ═════════════════════════════════════════════════════════════════════════════

static void process_socks5(Session *s);

static void on_socks5_client_read(uv_stream_t *stream, ssize_t nread,
                                  const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0)
    s->client_buf.insert(s->client_buf.end(), buf->base, buf->base + nread);
  delete[] buf->base;
  observe_eof(s, nread);
  if (nread < 0 || s->closing) {
    if (!s->closing)
      close_session(s);
    return;
  }
  process_socks5(s);
}

static void on_internal_client_read(uv_stream_t *stream, ssize_t nread,
                                    const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0) {
    if (s->client_buf.size() + static_cast<size_t>(nread) >
        kMaxHeaderBuf + session_config(s).internal_route_max_body_bytes) {
      delete[] buf->base;
      const std::string body =
          "{\"ok\":false,\"error\":{\"code\":\"PAYLOAD_TOO_LARGE\"}}";
      write_str_and_close(reinterpret_cast<uv_stream_t *>(&s->client),
                          build_internal_http_response(413, "application/json",
                                                       body));
      return;
    }
    s->client_buf.insert(s->client_buf.end(), buf->base, buf->base + nread);
  }
  delete[] buf->base;
  observe_eof(s, nread);
  if (nread < 0 || s->closing) {
    if (!s->closing)
      close_session(s);
    return;
  }

  bool need_more = false;
  process_internal_http_buffer(s, &need_more);
}

static void on_internal_ws_client_read(uv_stream_t *stream, ssize_t nread,
                                       const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  if (nread > 0) {
    if (s->client_buf.size() + static_cast<size_t>(nread) >
        session_config(s).internal_route_max_body_bytes + 64) {
      delete[] buf->base;
      write_ws_frame(s, 0x8, "");
      close_session(s);
      return;
    }
    s->client_buf.insert(s->client_buf.end(), buf->base, buf->base + nread);
  }
  delete[] buf->base;
  observe_eof(s, nread);
  if (nread < 0 || s->closing) {
    if (!s->closing)
      close_session(s);
    return;
  }

  bool need_more = false;
  process_internal_ws_buffer(s, &need_more);
}

static void on_internal_deferred_http_read(uv_stream_t *stream, ssize_t nread,
                                           const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);
  delete[] buf->base;
  if (s->closing)
    return;
  observe_eof(s, nread);
  if (nread != 0) {
    s->internal_close_reason =
        nread < 0 ? "http_client_disconnected" : "unexpected_http_data";
    close_session(s);
  }
}

static void process_socks5(Session *s) {
  auto &b = *s->bridge;
  const BridgeConfig &config = session_config(s);
  auto &buf = s->client_buf;

  if (s->state == SessionState::Socks5Greeting) {
    if (buf.size() < 2)
      return;
    if (buf[0] != 0x05) {
      close_session(s);
      return;
    }
    uint8_t nmethods = buf[1];
    if (buf.size() < size_t(2 + nmethods))
      return;

    bool client_offers_up = false;
    for (uint8_t i = 0; i < nmethods; ++i)
      if (buf[2 + i] == 0x02) {
        client_offers_up = true;
        break;
      }

    bool need_auth = (config.auth_mode != AuthMode::None);
    s->socks5_need_auth = need_auth && client_offers_up;

    if (need_auth && !client_offers_up) {
      const uint8_t rej[] = {0x05, 0xff};
      write_to(reinterpret_cast<uv_stream_t *>(&s->client), rej, 2);
      close_session(s);
      return;
    }

    uint8_t method = s->socks5_need_auth ? 0x02 : 0x00;
    const uint8_t reply[] = {0x05, method};
    write_to(reinterpret_cast<uv_stream_t *>(&s->client), reply, 2);
    buf.erase(buf.begin(), buf.begin() + 2 + nmethods);
    s->state = s->socks5_need_auth ? SessionState::Socks5Auth
                                   : SessionState::Socks5ConnectReq;
    if (!buf.empty())
      process_socks5(s);

  } else if (s->state == SessionState::Socks5Auth) {
    // VER(1) ULEN(1) UNAME(ULEN) PLEN(1) PASSWD(PLEN)
    if (buf.size() < 3)
      return;
    if (buf[0] != 0x01) {
      close_session(s);
      return;
    }
    uint8_t ulen = buf[1];
    if (buf.size() < size_t(2 + ulen + 1))
      return;
    uint8_t plen = buf[2 + ulen];
    if (buf.size() < size_t(2 + ulen + 1 + plen))
      return;

    std::string uname(reinterpret_cast<char *>(buf.data()) + 2, ulen);
    std::string passwd(reinterpret_cast<char *>(buf.data()) + 2 + ulen + 1,
                       plen);

    bool ok = false;
    if (config.auth_mode == AuthMode::None) {
      ok = true;
      s->auth_ok = true;
    } else {
      std::lock_guard<std::mutex> lk(b.instances_mu);
      // username = instance_id, password = token
      const InstanceInfo *info = b.find_instance_locked(passwd);
      if (info && info->instance_id == uname) {
        ok = true;
        s->auth_ok = true;
        s->instance_id = info->instance_id;
        s->profile_id = info->profile_id;
      }
    }

    const uint8_t ar[] = {0x01, ok ? uint8_t(0x00) : uint8_t(0x01)};
    write_to(reinterpret_cast<uv_stream_t *>(&s->client), ar, 2);
    if (!ok) {
      close_session(s);
      return;
    }
    buf.erase(buf.begin(), buf.begin() + 2 + ulen + 1 + plen);
    s->state = SessionState::Socks5ConnectReq;
    if (!buf.empty())
      process_socks5(s);

  } else if (s->state == SessionState::Socks5ConnectReq) {
    // VER(1) CMD(1) RSV(1) ATYP(1) DST.ADDR DST.PORT(2)
    if (buf.size() < 4)
      return;
    if (buf[0] != 0x05) {
      close_session(s);
      return;
    }
    uint8_t cmd = buf[1];
    uint8_t atyp = buf[3];
    if (cmd != 0x01) { // only CONNECT
      const uint8_t err[] = {0x05, 0x07, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
      write_to(reinterpret_cast<uv_stream_t *>(&s->client), err, sizeof(err));
      close_session(s);
      return;
    }
    s->socks5_atyp = atyp;
    if (atyp == 0x01) {
      if (buf.size() < 10)
        return;
      char ip[INET_ADDRSTRLEN] = {};
      inet_ntop(AF_INET, buf.data() + 4, ip, sizeof(ip));
      s->target_host = ip;
      s->target_port = (uint16_t(buf[8]) << 8) | buf[9];
      buf.erase(buf.begin(), buf.begin() + 10);
    } else if (atyp == 0x03) {
      if (buf.size() < 5)
        return;
      uint8_t dlen = buf[4];
      if (buf.size() < size_t(4 + 1 + dlen + 2))
        return;
      s->target_host.assign(reinterpret_cast<char *>(buf.data()) + 5, dlen);
      s->target_port = (uint16_t(buf[5 + dlen]) << 8) | buf[5 + dlen + 1];
      buf.erase(buf.begin(), buf.begin() + 4 + 1 + dlen + 2);
    } else if (atyp == 0x04) {
      if (buf.size() < 22)
        return;
      char ip[INET6_ADDRSTRLEN] = {};
      inet_ntop(AF_INET6, buf.data() + 4, ip, sizeof(ip));
      s->target_host = ip;
      s->target_port = (uint16_t(buf[20]) << 8) | buf[21];
      buf.erase(buf.begin(), buf.begin() + 22);
    } else {
      const uint8_t err[] = {0x05, 0x08, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
      write_to(reinterpret_cast<uv_stream_t *>(&s->client), err, sizeof(err));
      close_session(s);
      return;
    }

    if (is_internal_route_session(s)) {
      if (b.callbacks.on_new_session)
        b.callbacks.on_new_session(&b, s, s->session_id.c_str(),
                                   s->instance_id.c_str());

      const uint8_t ok[] = {0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0};
      write_to(reinterpret_cast<uv_stream_t *>(&s->client), ok, sizeof(ok));
      uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
      s->client_reading = false;
      s->state = SessionState::InternalHttp;
      bool need_more = false;
      process_internal_http_buffer(s, &need_more);
      if (need_more && !s->client_reading) {
        uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                      on_internal_client_read);
        s->client_reading = true;
      }
      return;
    }

    if (maybe_redirect_or_block_security(s, "socks5"))
      return;
    apply_proxy_bypass(s);

    if (b.callbacks.on_new_session)
      b.callbacks.on_new_session(&b, s, s->session_id.c_str(),
                                 s->instance_id.c_str());

    uv_read_stop(reinterpret_cast<uv_stream_t *>(&s->client));
    s->client_reading = false;
    s->state = SessionState::Socks5Connecting;
    resolve_target(s);
  }
}

// ═════════════════════════════════════════════════════════════════════════════
// Initial client read — detect protocol
// ═════════════════════════════════════════════════════════════════════════════

static void on_initial_client_read(uv_stream_t *stream, ssize_t nread,
                                   const uv_buf_t *buf) {
  Session *s = static_cast<Session *>(stream->data);

  if (nread > 0) {
    if (s->client_buf.size() + (size_t)nread > kMaxHeaderBuf) {
      delete[] buf->base;
      close_session(s);
      return;
    }
    s->client_buf.insert(s->client_buf.end(), buf->base, buf->base + nread);
  }
  delete[] buf->base;

  observe_eof(s, nread);
  if (nread < 0 || s->closing) {
    if (!s->closing)
      close_session(s);
    return;
  }
  if (s->client_buf.empty())
    return;

  uint8_t first = s->client_buf[0];

  if (first == 0x05) {
    // SOCKS5
    s->client_protocol = ClientProtocol::Socks5;
    s->state = SessionState::Socks5Greeting;
    // Switch read callback to socks5 handler
    uv_read_stop(stream);
    s->client_reading = false;
    process_socks5(s);
    if (s->state == SessionState::Socks5Greeting ||
        s->state == SessionState::Socks5Auth ||
        s->state == SessionState::Socks5ConnectReq) {
      uv_read_start(stream, alloc_cb, on_socks5_client_read);
      s->client_reading = true;
    }
    return;
  }

  // HTTP — wait for complete headers
  const std::string raw(s->client_buf.begin(), s->client_buf.end());
  if (raw.find("\r\n\r\n") == std::string::npos)
    return; // need more

  ParsedHttp parsed;
  if (!parse_http_headers(s->client_buf.data(), s->client_buf.size(), parsed)) {
    close_session(s);
    return;
  }

  if (parsed.header_end < s->client_buf.size())
    s->http_body_tail.assign(s->client_buf.begin() + parsed.header_end,
                             s->client_buf.end());

  uv_read_stop(stream);
  s->client_reading = false;
  handle_http_request(s, parsed);
}

// ═════════════════════════════════════════════════════════════════════════════
// New connection accepted from the listener
// ═════════════════════════════════════════════════════════════════════════════

static void on_new_connection(uv_stream_t *server, int status) {
  if (status < 0)
    return;
  auto *listener = static_cast<Listener *>(server->data);
  if (!listener || !listener->bridge ||
      listener->closing.load(std::memory_order_acquire))
    return;
  ProxyBridgeImpl *b = listener->bridge;

  const auto budget = b->session_budget.load(std::memory_order_relaxed);
  if (listener->config.max_sessions > 0 || budget != UINT32_MAX) {
    bool reject = false;
    {
      std::lock_guard<std::mutex> lk(b->sessions_mu);
      uint32_t active_for_listener = 0;
      for (const auto &item : b->sessions) {
        if (item.second && item.second->listener == listener)
          ++active_for_listener;
      }
      reject = (listener->config.max_sessions > 0 &&
                active_for_listener >= listener->config.max_sessions) ||
               b->sessions.size() >= budget;
    }
    if (reject) {
      b->rejected_sessions.fetch_add(1, std::memory_order_relaxed);
      listener->rejected_sessions.fetch_add(1, std::memory_order_relaxed);
      auto *client = new uv_tcp_t;
      uv_tcp_init(b->loop, client);
      client->data = nullptr;
      uv_accept(server, reinterpret_cast<uv_stream_t *>(client));
      uv_close(reinterpret_cast<uv_handle_t *>(client),
               [](uv_handle_t *h) { delete reinterpret_cast<uv_tcp_t *>(h); });
      if (b->callbacks.on_error) {
        b->callbacks.on_error(
            b, nullptr, UV_EMFILE,
            "{\"reason\":\"max_sessions_reached\",\"message\":\"bridge "
            "session limit reached\"}");
      }
      return;
    }
  }

  Session *s = new Session;
  s->bridge = b;
  s->listener = listener;
  s->config = listener->config;
  s->session_id = b->next_session_id();
  s->internal_connection.id = ++b->internal_connection_counter;
  s->internal_connection.generation = b->internal_connection_generation;
  s->start_ms = now_ms();
  s->client.data = s;
  s->remote.data = s;

  uv_tcp_init(b->loop, &s->client);
  if (uv_accept(server, reinterpret_cast<uv_stream_t *>(&s->client)) != 0) {
    uv_close(reinterpret_cast<uv_handle_t *>(&s->client),
             [](uv_handle_t *h) { delete static_cast<Session *>(h->data); });
    return;
  }

  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    b->sessions[s->session_id] = s;
  }
  b->accepted_sessions.fetch_add(1, std::memory_order_relaxed);
  listener->accepted_sessions.fetch_add(1, std::memory_order_relaxed);

  uv_read_start(reinterpret_cast<uv_stream_t *>(&s->client), alloc_cb,
                on_initial_client_read);
  s->client_reading = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// ProxyBridgeImpl::sessions_json_locked
// ═════════════════════════════════════════════════════════════════════════════

std::string ProxyBridgeImpl::sessions_json_locked() const {
  std::string j = "[";
  bool first = true;
  for (auto &kv : sessions) {
    const Session *s = kv.second;
    if (!first)
      j += ',';
    first = false;
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"session_id\":\"%s\",\"instance_id\":\"%s\","
             "\"listen_port\":%u,"
             "\"target\":\"%s:%u\",\"bytes_up\":%llu,\"bytes_down\":%llu}",
             s->session_id.c_str(), s->instance_id.c_str(),
             static_cast<unsigned>(s->listener ? s->listener->port.load(
                                                      std::memory_order_relaxed)
                                                : 0),
             s->target_host.c_str(), (unsigned)s->target_port,
             (unsigned long long)s->bytes_up,
             (unsigned long long)s->bytes_down);
    j += buf;
  }
  return j + "]";
}

// ═════════════════════════════════════════════════════════════════════════════
// Config parsing
// ═════════════════════════════════════════════════════════════════════════════

static bool valid_strict_upstream(const nlohmann::json &u) {
  if (!u.is_object() || !u.contains("type") || !u["type"].is_string() ||
      !u.contains("addr") || !u["addr"].is_string() ||
      !u.contains("port") || !u["port"].is_number_unsigned())
    return false;
  const auto type = to_lower(u["type"].get<std::string>());
  const auto &addr = u["addr"].get_ref<const std::string &>();
  const auto port = u["port"].get<uint64_t>();
  if ((type != "http" && type != "socks5") || addr.empty() ||
      std::any_of(addr.begin(), addr.end(), [](unsigned char c) {
        return c <= 0x20 || c == 0x7f;
      }) || port == 0 || port > UINT16_MAX)
    return false;
  if (!u.contains("auth"))
    return true;
  const auto &auth = u["auth"];
  if (!auth.is_object() || !auth.contains("type") || !auth["type"].is_string() ||
      !auth.contains("user") || !auth["user"].is_string() ||
      !auth.contains("pass") || !auth["pass"].is_string())
    return false;
  const auto &auth_type = auth["type"].get_ref<const std::string &>();
  const auto &user = auth["user"].get_ref<const std::string &>();
  const auto &pass = auth["pass"].get_ref<const std::string &>();
  if (user.empty() || user.find('\0') != std::string::npos ||
      pass.find('\0') != std::string::npos)
    return false;
  if (type == "http")
    return auth_type == "basic" && user.find(':') == std::string::npos;
  return (auth_type == "socks5" || auth_type == "userpass") &&
         user.size() <= 255 && !pass.empty() && pass.size() <= 255;
}

static bool parse_config_fields(const char *json_str, BridgeConfig &cfg) {
  if (!json_str || !*json_str)
    return true;
  unsigned strict_keys = 0;
  const auto key_check = [&strict_keys](int depth, nlohmann::json::parse_event_t event,
                                        nlohmann::json &value) {
    if (depth == 1 && event == nlohmann::json::parse_event_t::key &&
        value == "strict_proxy")
      ++strict_keys;
    return true;
  };
  auto doc = nlohmann::json::parse(json_str, key_check, false);
  if (!doc.is_object() || strict_keys > 1)
      return false;
  if (doc.contains("strict_proxy")) {
    if (!doc["strict_proxy"].is_boolean())
      return false;
    const bool strict = doc["strict_proxy"].get<bool>();
    // Enabling strict mode requires the original, complete chain, rather
    // than a legacy snapshot whose invalid entries may have been discarded.
    if (strict && !cfg.strict_proxy && !doc.contains("upstreams"))
      return false;
    cfg.strict_proxy = strict;
  }
  if (cfg.strict_proxy && doc.contains("upstreams")) {
    const auto &upstreams = doc["upstreams"];
    if (!upstreams.is_array() || upstreams.empty() ||
        !std::all_of(upstreams.begin(), upstreams.end(), valid_strict_upstream))
      return false;
  }
  if (cfg.strict_proxy && doc.contains("security") && doc["security"].is_object()) {
    const auto &security = doc["security"];
    for (const char *key : {"proxyBypassList", "proxy_bypass_list"}) {
      if (security.contains(key) &&
          (!security[key].is_array() || !security[key].empty()))
        return false;
    }
  }

  if (doc.contains("listen") && doc["listen"].is_object()) {
      auto& L = doc["listen"];
      if (L.contains("addr") && L["addr"].is_string())
          cfg.listen_addr = L["addr"].get_ref<const std::string&>().c_str();
      if (L.contains("port") && (L["port"].is_number_unsigned() && L["port"].get<uint64_t>() <= UINT32_MAX))
          cfg.listen_port = static_cast<uint16_t>(L["port"].get<uint32_t>());
      if (L.contains("auth_mode") && L["auth_mode"].is_string()) {
          std::string am = to_lower(L["auth_mode"].get_ref<const std::string&>().c_str());
          cfg.auth_mode = (am == "token")   ? AuthMode::Token
                          : (am == "basic") ? AuthMode::Basic
                                            : AuthMode::None;
      }
  }

  if (doc.contains("instances") && doc["instances"].is_object()) {
      auto& I = doc["instances"];
      if (I.contains("token_lifetime_sec") && (I["token_lifetime_sec"].is_number_unsigned() && I["token_lifetime_sec"].get<uint64_t>() <= UINT32_MAX))
          cfg.token_lifetime_sec = I["token_lifetime_sec"].get<uint32_t>();
  }

  if (doc.contains("upstreams") && doc["upstreams"].is_array()) {
      cfg.upstreams.clear();
      for (auto& u : doc["upstreams"]) {
          if (!u.is_object())
              continue;
          UpstreamConfig uc;
          if (u.contains("type") && u["type"].is_string()) {
              std::string t = to_lower(u["type"].get_ref<const std::string&>().c_str());
              uc.type = (t == "socks5") ? UpstreamType::Socks5
                        : (t == "http") ? UpstreamType::HttpConnect
                                        : UpstreamType::None;
          }
          if (u.contains("addr") && u["addr"].is_string())
              uc.addr = u["addr"].get_ref<const std::string&>().c_str();
          if (u.contains("port") && (u["port"].is_number_unsigned() && u["port"].get<uint64_t>() <= UINT32_MAX))
              uc.port = static_cast<uint16_t>(u["port"].get<uint32_t>());
          if (u.contains("auth") && u["auth"].is_object()) {
              auto& A = u["auth"];
              if (A.contains("type") && A["type"].is_string())
                  uc.auth.type = A["type"].get_ref<const std::string&>().c_str();
              if (A.contains("user") && A["user"].is_string())
                  uc.auth.user = A["user"].get_ref<const std::string&>().c_str();
              if (A.contains("pass") && A["pass"].is_string())
                  uc.auth.pass = A["pass"].get_ref<const std::string&>().c_str();
          }
          if (uc.type != UpstreamType::None && !uc.addr.empty() && uc.port)
              cfg.upstreams.push_back(std::move(uc));
      }
  }

  cfg.security_enable = false;
  cfg.security_auto_match_subdomains = false;
  cfg.security_strategy.clear();
  cfg.security_whitelist.clear();
  cfg.security_blacklist.clear();
  cfg.security_bypass_list.clear();

  if (doc.contains("security") && doc["security"].is_object()) {
      auto& S = doc["security"];
      if (S.contains("enable") && S["enable"].is_boolean())
          cfg.security_enable = S["enable"].get<bool>();
      if (S.contains("autoMatchSubdomains") &&
          S["autoMatchSubdomains"].is_boolean())
          cfg.security_auto_match_subdomains =
              S["autoMatchSubdomains"].get<bool>();
      if (S.contains("strategy") && S["strategy"].is_string())
          cfg.security_strategy = to_lower(S["strategy"].get_ref<const std::string&>().c_str());
      if (S.contains("whitelist") && S["whitelist"].is_array()) {
          cfg.security_whitelist.clear();
          for (auto& item : S["whitelist"]) {
              if (item.is_string())
                  cfg.security_whitelist.emplace_back(item.get_ref<const std::string&>().c_str());
          }
      }
      if (S.contains("blacklist") && S["blacklist"].is_array()) {
          cfg.security_blacklist.clear();
          for (auto& item : S["blacklist"]) {
              if (item.is_string())
                  cfg.security_blacklist.emplace_back(item.get_ref<const std::string&>().c_str());
          }
      }
      if (S.contains("bypassList") && S["bypassList"].is_array()) {
          cfg.security_bypass_list.clear();
          for (auto& item : S["bypassList"]) {
              if (item.is_string())
                  cfg.security_bypass_list.emplace_back(item.get_ref<const std::string&>().c_str());
          }
      }
      if (S.contains("bypass_list") && S["bypass_list"].is_array()) {
          cfg.security_bypass_list.clear();
          for (auto& item : S["bypass_list"]) {
              if (item.is_string())
                  cfg.security_bypass_list.emplace_back(item.get_ref<const std::string&>().c_str());
          }
      }
      if (S.contains("proxyBypassList") && S["proxyBypassList"].is_array()) {
          cfg.proxy_bypass_list.clear();
          for (auto& item : S["proxyBypassList"]) {
              if (item.is_string())
                  cfg.proxy_bypass_list.emplace_back(item.get_ref<const std::string&>().c_str());
          }
      }
      if (S.contains("proxy_bypass_list") &&
          S["proxy_bypass_list"].is_array()) {
          cfg.proxy_bypass_list.clear();
          for (auto& item : S["proxy_bypass_list"]) {
              if (item.is_string())
                  cfg.proxy_bypass_list.emplace_back(item.get_ref<const std::string&>().c_str());
          }
      }
  }

  if (doc.contains("features") && doc["features"].is_object()) {
      auto& F = doc["features"];
      if (F.contains("tls_fingerprint") && F["tls_fingerprint"].is_boolean())
          cfg.tls_fingerprint = F["tls_fingerprint"].get<bool>();
      if (F.contains("traffic_features") && F["traffic_features"].is_boolean())
          cfg.traffic_features = F["traffic_features"].get<bool>();
      if (F.contains("http_fingerprint") && F["http_fingerprint"].is_boolean())
          cfg.http_fingerprint = F["http_fingerprint"].get<bool>();
      if (F.contains("tcp_fingerprint") && F["tcp_fingerprint"].is_boolean())
          cfg.tcp_fingerprint = F["tcp_fingerprint"].get<bool>();
      if (F.contains("proxy_fingerprint") && F["proxy_fingerprint"].is_boolean())
          cfg.proxy_fingerprint = F["proxy_fingerprint"].get<bool>();
  }
  if (doc.contains("limits") && doc["limits"].is_object()) {
      auto& L = doc["limits"];
      if (L.contains("max_sessions") && (L["max_sessions"].is_number_unsigned() && L["max_sessions"].get<uint64_t>() <= UINT32_MAX))
          cfg.max_sessions = L["max_sessions"].get<uint32_t>();
      if (L.contains("tunnel_connect_timeout_ms") && (L["tunnel_connect_timeout_ms"].is_number_unsigned() && L["tunnel_connect_timeout_ms"].get<uint64_t>() <= UINT32_MAX))
          cfg.tunnel_connect_timeout_ms = std::clamp(L["tunnel_connect_timeout_ms"].get<uint32_t>(), 1000u, 60000u);
      if (L.contains("write_high_watermark") &&
          L["write_high_watermark"].is_number_unsigned())
          cfg.write_high_watermark = L["write_high_watermark"].get<uint64_t>();
      if (L.contains("write_low_watermark") &&
          L["write_low_watermark"].is_number_unsigned())
          cfg.write_low_watermark = L["write_low_watermark"].get<uint64_t>();
      if (cfg.write_low_watermark > cfg.write_high_watermark)
          cfg.write_low_watermark = cfg.write_high_watermark;
  }
  const nlohmann::json* internal_route = nullptr;
  if (doc.contains("internalRoute") && doc["internalRoute"].is_object())
      internal_route = &doc["internalRoute"];
  else if (doc.contains("mcpInternalBridge") &&
           doc["mcpInternalBridge"].is_object())
      internal_route = &doc["mcpInternalBridge"];
  if (internal_route) {
    const auto &R = *internal_route;
    if (R.contains("enabled") && R["enabled"].is_boolean())
        cfg.internal_route_enable = R["enabled"].get<bool>();
    if (R.contains("hosts") && R["hosts"].is_array()) {
        cfg.internal_route_hosts.clear();
        for (auto& item : R["hosts"]) {
            if (item.is_string() && *item.get_ref<const std::string&>().c_str())
                cfg.internal_route_hosts.emplace_back(item.get_ref<const std::string&>().c_str());
        }
    }
    if (R.contains("httpBasePath") && R["httpBasePath"].is_string()) {
        cfg.internal_route_path_prefix = R["httpBasePath"].get_ref<const std::string&>().c_str();
        if (cfg.internal_route_path_prefix.empty() ||
            cfg.internal_route_path_prefix.front() != '/') {
            cfg.internal_route_path_prefix = "/extension";
        }
    }
    if (R.contains("webSocketPath") && R["webSocketPath"].is_string()) {
        cfg.internal_route_websocket_path = R["webSocketPath"].get_ref<const std::string&>().c_str();
        if (cfg.internal_route_websocket_path.empty() ||
            cfg.internal_route_websocket_path.front() != '/') {
            cfg.internal_route_websocket_path =
                cfg.internal_route_path_prefix + "/ws";
        }
    }
    else {
        cfg.internal_route_websocket_path =
            cfg.internal_route_path_prefix + "/ws";
    }
    if (R.contains("allowedOrigins") && R["allowedOrigins"].is_array()) {
        cfg.internal_route_allowed_origins.clear();
        for (const auto& item : R["allowedOrigins"]) {
            if (item.is_string() && *item.get_ref<const std::string&>().c_str())
                cfg.internal_route_allowed_origins.emplace_back(item.get_ref<const std::string&>().c_str());
        }
    }
    if (R.contains("requireOrigin") && R["requireOrigin"].is_boolean())
        cfg.internal_route_require_origin = R["requireOrigin"].get<bool>();
    if (R.contains("maxHttpBodyBytes") &&
        R["maxHttpBodyBytes"].is_number_unsigned()) {
        const uint64_t value = R["maxHttpBodyBytes"].get<uint64_t>();
        cfg.internal_route_max_body_bytes =
            static_cast<size_t>(std::min<uint64_t>(
                std::max<uint64_t>(value, 1024),
                kDefaultInternalRouteMaxBodyBytes));
    }
    if (R.contains("maxWebSocketFrameBytes") &&
        R["maxWebSocketFrameBytes"].is_number_unsigned()) {
        const uint64_t value = R["maxWebSocketFrameBytes"].get<uint64_t>();
        cfg.internal_route_max_websocket_frame_bytes =
            static_cast<size_t>(std::min<uint64_t>(
                std::max<uint64_t>(value, 1024),
                kDefaultInternalRouteMaxBodyBytes));
    }
    if (cfg.internal_route_enable && cfg.internal_route_hosts.empty())
      cfg.internal_route_enable = false;
    if (cfg.internal_route_enable && cfg.internal_route_require_origin &&
        cfg.internal_route_allowed_origins.empty())
      cfg.internal_route_enable = false;
  }
  if (doc.contains("timeouts") && doc["timeouts"].is_object()) {
      auto& T = doc["timeouts"];
      if (T.contains("dns_ms") && (T["dns_ms"].is_number_unsigned() && T["dns_ms"].get<uint64_t>() <= UINT32_MAX))
          cfg.upstream_dns_timeout_ms = clamp_timeout_ms(
              T["dns_ms"].get<uint32_t>(), kDefaultUpstreamDnsTimeoutMs);
      if (T.contains("upstream_dns_ms") && (T["upstream_dns_ms"].is_number_unsigned() && T["upstream_dns_ms"].get<uint64_t>() <= UINT32_MAX))
          cfg.upstream_dns_timeout_ms = clamp_timeout_ms(
              T["upstream_dns_ms"].get<uint32_t>(), kDefaultUpstreamDnsTimeoutMs);
      if (T.contains("target_dns_ms") && (T["target_dns_ms"].is_number_unsigned() && T["target_dns_ms"].get<uint64_t>() <= UINT32_MAX))
          cfg.target_dns_timeout_ms = clamp_timeout_ms(
              T["target_dns_ms"].get<uint32_t>(), kDefaultTargetDnsTimeoutMs);
      if (T.contains("connect_ms") && (T["connect_ms"].is_number_unsigned() && T["connect_ms"].get<uint64_t>() <= UINT32_MAX))
          cfg.upstream_connect_timeout_ms = clamp_timeout_ms(
              T["connect_ms"].get<uint32_t>(), kDefaultUpstreamConnectTimeoutMs);
      if (T.contains("target_connect_ms") && (T["target_connect_ms"].is_number_unsigned() && T["target_connect_ms"].get<uint64_t>() <= UINT32_MAX))
          cfg.target_connect_timeout_ms = clamp_timeout_ms(
              T["target_connect_ms"].get<uint32_t>(), kDefaultTargetConnectTimeoutMs);
      if (T.contains("upstream_connect_ms") &&
          (T["upstream_connect_ms"].is_number_unsigned() && T["upstream_connect_ms"].get<uint64_t>() <= UINT32_MAX))
          cfg.upstream_connect_timeout_ms = clamp_timeout_ms(
              T["upstream_connect_ms"].get<uint32_t>(), kDefaultUpstreamConnectTimeoutMs);
      if (T.contains("upstream_handshake_ms") &&
          (T["upstream_handshake_ms"].is_number_unsigned() && T["upstream_handshake_ms"].get<uint64_t>() <= UINT32_MAX)) {
          cfg.upstream_handshake_timeout_ms = clamp_timeout_ms(
              T["upstream_handshake_ms"].get<uint32_t>(),
              kDefaultUpstreamHandshakeTimeoutMs);
      }
      if (T.contains("circuit_breaker_open_ms") &&
          (T["circuit_breaker_open_ms"].is_number_unsigned() && T["circuit_breaker_open_ms"].get<uint64_t>() <= UINT32_MAX)) {
          cfg.circuit_breaker_open_ms = clamp_timeout_ms(
              T["circuit_breaker_open_ms"].get<uint32_t>(),
              kDefaultCircuitBreakerOpenMs);
      }
      if (T.contains("circuit_breaker_failure_threshold") &&
          (T["circuit_breaker_failure_threshold"].is_number_unsigned() && T["circuit_breaker_failure_threshold"].get<uint64_t>() <= UINT32_MAX)) {
          cfg.circuit_breaker_failure_threshold =
              clamp_circuit_breaker_threshold(
                  T["circuit_breaker_failure_threshold"].get<uint32_t>());
      }
  }
  return !cfg.strict_proxy ||
         (!cfg.upstreams.empty() && cfg.proxy_bypass_list.empty());
}

static bool parse_config(const char *json_str, BridgeConfig &cfg) {
  BridgeConfig next = cfg;
  if (!parse_config_fields(json_str, next))
    return false;
  cfg = std::move(next);
  return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// C ABI
// ═════════════════════════════════════════════════════════════════════════════

static int init_bridge_async_handles(ProxyBridgeImpl *b) {
  if (!b || !b->loop)
    return UV_EINVAL;
  b->stop_async.data = b;
  int rc = uv_async_init(b->loop, &b->stop_async, on_stop_requested);
  if (rc != 0)
    return rc;
  b->stop_async_initialized.store(true, std::memory_order_release);
  b->command_async.data = b;
  rc = uv_async_init(b->loop, &b->command_async, on_command_requested);
  if (rc == 0) {
    b->command_async_initialized.store(true, std::memory_order_release);
    return 0;
  }
  uv_close(reinterpret_cast<uv_handle_t *>(&b->stop_async),
           on_bridge_async_closed);
  return rc;
}

extern "C" {

uvbrg_handle_t uvbrg_init(const char *config_json) {
  auto *b = new ProxyBridgeImpl;
  if (!parse_config(config_json, b->config)) {
    delete b;
    return nullptr;
  }
  if (uv_loop_init(b->loop) != 0) {
    delete b;
    return nullptr;
  }
  b->loop_initialized = true;
  b->lifecycle_state = BridgeLifecycleState::New;
  if (init_bridge_async_handles(b) != 0) {
    uv_run(b->loop, UV_RUN_DEFAULT);
    uv_loop_close(b->loop);
    delete b;
    return nullptr;
  }
  return b;
}

uvbrg_handle_t uvbrg_init_shared(const char *config_json,
                                 const char *routing_key,
                                 uint32_t reactor_count) {
  auto *b = new ProxyBridgeImpl;
  if (!parse_config(config_json, b->config)) {
    delete b;
    return nullptr;
  }
  std::string error;
  auto lease = uvbrg::internal::AcquireSharedLoop(
      routing_key ? routing_key : "", reactor_count, &error);
  if (!lease) {
    delete b;
    return nullptr;
  }
  struct AttachSignal {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    bool abandoned = false;
    int result = UV_ECANCELED;
  };
  auto signal = std::make_shared<AttachSignal>();
  b->shared_reactor = true;
  b->shared_reactor_index = lease->reactor_index();
  b->shared_loop_lease = lease;
  const bool posted = lease->Post(
      0,
      [b, signal](uv_loop_t *loop) {
        b->loop = loop;
        b->loop_initialized = true;
        const int rc = init_bridge_async_handles(b);
        {
          std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
          if (rc == 0) {
            b->running.store(true, std::memory_order_release);
            b->lifecycle_state = BridgeLifecycleState::Running;
            b->loop_thread_id = std::this_thread::get_id();
          } else {
            b->lifecycle_state = BridgeLifecycleState::Stopped;
          }
        }
        bool abandoned = false;
        {
          std::lock_guard<std::mutex> lk(signal->mtx);
          abandoned = signal->abandoned;
          signal->done = true;
          signal->result = rc;
        }
        signal->cv.notify_all();
        if (rc != 0) {
          b->destroy_when_stopped.store(true, std::memory_order_release);
          maybe_finish_shared_bridge(b);
          return;
        }
        if (abandoned) {
          b->destroy_when_stopped.store(true, std::memory_order_release);
          {
            std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
            b->lifecycle_state = BridgeLifecycleState::Stopping;
          }
          on_stop_requested(&b->stop_async);
        }
      },
      [b, signal]() {
        {
          std::lock_guard<std::mutex> lk(signal->mtx);
          signal->done = true;
          signal->result = UV_ECANCELED;
        }
        signal->cv.notify_all();
        delete b;
      });
  if (!posted) {
    lease->Release();
    delete b;
    return nullptr;
  }
  std::unique_lock<std::mutex> lk(signal->mtx);
  if (!signal->cv.wait_for(lk, std::chrono::seconds(5),
                           [&signal]() { return signal->done; })) {
    signal->abandoned = true;
    return nullptr;
  }
  if (signal->result != 0) {
    return nullptr;
  }
  return b;
}

void uvbrg_set_callbacks(uvbrg_handle_t h, const uvbrg_callbacks_t *cbs) {
  if (h && cbs)
    static_cast<ProxyBridgeImpl *>(h)->callbacks = *cbs;
}

int uvbrg_set_execution_guard(uvbrg_handle_t h, const uvbrg_execution_guard_t *cb) {
  if (!h || !cb || cb->size != sizeof(*cb) || !cb->enter || !cb->leave)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  if (b->running || !b->sessions.empty() || !b->listeners.empty()) return UV_EBUSY;
  b->execution_guard = *cb;
  return 0;
}

int uvbrg_set_tunnel_callbacks(uvbrg_handle_t h, const uvbrg_tunnel_callbacks_t *cb) {
  if (!h || !cb || cb->size != sizeof(*cb) || !cb->open || !cb->data ||
      !cb->closed || !cb->eof || !cb->consumed) return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  if (b->running || !b->sessions.empty() || !b->listeners.empty()) return UV_EBUSY;
  if (b->config.strict_proxy) return UV_EINVAL;
  b->tunnel_callbacks = *cb;
  return 0;
}

int uvbrg_tunnel_event(uvbrg_handle_t h, uvbrg_internal_connection_t id,
                       int event, const char *data, size_t size) {
  if (!h || !id.id || !id.generation || size > 32768 || (!data && size) ||
      event < UVBRG_TUNNEL_CONNECTED || event > UVBRG_TUNNEL_CLOSE ||
      (event != UVBRG_TUNNEL_DATA && size)) return UV_EINVAL;
  auto cmd = std::make_shared<BridgeCommand>();
  cmd->kind = BridgeCommand::Kind::TunnelEvent;
  cmd->connection = id;
  cmd->status_code = event;
  if (size) cmd->payload.assign(data, size);
  return run_command_sync(static_cast<ProxyBridgeImpl *>(h), cmd);
}

int uvbrg_set_internal_route_callbacks(
    uvbrg_handle_t h, const uvbrg_internal_route_callbacks_t *cbs) {
  if (!h)
    return -1;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  if (!cbs) {
    b->internal_route_callbacks = {};
    return 0;
  }
  if (cbs->size < offsetof(uvbrg_internal_route_callbacks_t, user_data) +
                      sizeof(cbs->user_data))
    return -1;
  b->internal_route_callbacks = {};
  b->internal_route_callbacks.size = sizeof(uvbrg_internal_route_callbacks_t);
  b->internal_route_callbacks.on_http = cbs->on_http;
  b->internal_route_callbacks.user_data = cbs->user_data;
  if (cbs->size >= offsetof(uvbrg_internal_route_callbacks_t, on_ws_event) +
                       sizeof(cbs->on_ws_event)) {
    b->internal_route_callbacks.on_ws_event = cbs->on_ws_event;
  }
  if (cbs->size >= offsetof(uvbrg_internal_route_callbacks_t, on_ws_open) +
                       sizeof(cbs->on_ws_open)) {
    b->internal_route_callbacks.on_ws_open = cbs->on_ws_open;
  }
  if (cbs->size >= offsetof(uvbrg_internal_route_callbacks_t, on_ws_message) +
                       sizeof(cbs->on_ws_message)) {
    b->internal_route_callbacks.on_ws_message = cbs->on_ws_message;
  }
  if (cbs->size >= offsetof(uvbrg_internal_route_callbacks_t, on_ws_close) +
                       sizeof(cbs->on_ws_close)) {
    b->internal_route_callbacks.on_ws_close = cbs->on_ws_close;
  }
  return 0;
}

int uvbrg_internal_ws_send(uvbrg_handle_t h,
                           uvbrg_internal_connection_t connection,
                           const char *message, size_t message_size) {
  if (!h || connection.id == 0 || connection.generation == 0 ||
      (!message && message_size != 0))
    return UV_EINVAL;
  if (message_size > kDefaultInternalRouteMaxBodyBytes)
    return UV_EMSGSIZE;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  auto command = std::make_shared<BridgeCommand>();
  command->kind = BridgeCommand::Kind::InternalWsSend;
  command->connection = connection;
  if (message_size != 0)
    command->payload.assign(message, message + message_size);
  return run_command_sync(b, command);
}

int uvbrg_internal_http_complete(uvbrg_handle_t h,
                                 uvbrg_internal_connection_t connection,
                                 int status_code, const char *content_type,
                                 const char *body, size_t body_size) {
  if (!h || connection.id == 0 || connection.generation == 0 ||
      status_code < 100 || status_code > 599 || (!body && body_size != 0))
    return UV_EINVAL;
  if (body_size > kDefaultInternalRouteMaxBodyBytes)
    return UV_EMSGSIZE;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  auto command = std::make_shared<BridgeCommand>();
  command->kind = BridgeCommand::Kind::InternalHttpComplete;
  command->connection = connection;
  command->status_code = status_code;
  command->content_type = content_type ? content_type : "application/json";
  if (command->content_type.size() > 127)
    return UV_EINVAL;
  if (body_size != 0)
    command->payload.assign(body, body + body_size);
  return run_command_sync(b, command);
}

int uvbrg_register_instance(uvbrg_handle_t h, const char *token,
                            const char *meta_json) {
  if (!h || !token || !*token)
    return -1;
  auto *b = static_cast<ProxyBridgeImpl *>(h);

  InstanceInfo info;
  info.instance_id = token;
  info.registered_ms = now_ms();
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    info.lifetime_sec = b->config.token_lifetime_sec;
  }

  if (meta_json && *meta_json) {
      auto doc = nlohmann::json::parse(meta_json, nullptr, false);
      if (doc.is_object()) {
          if (doc.contains("profile_id") && doc["profile_id"].is_string())
              info.profile_id = doc["profile_id"].get_ref<const std::string&>().c_str();
          if (doc.contains("instance_id") && doc["instance_id"].is_string())
              info.instance_id = doc["instance_id"].get_ref<const std::string&>().c_str();
          if (doc.contains("user_agent") && doc["user_agent"].is_string())
              info.user_agent = doc["user_agent"].get_ref<const std::string&>().c_str();
      }
  }

  std::lock_guard<std::mutex> lk(b->instances_mu);
  b->instances[token] = std::move(info);
  return 0;
}

int uvbrg_unregister_instance(uvbrg_handle_t h, const char *token) {
  if (!h || !token)
    return -1;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  std::lock_guard<std::mutex> lk(b->instances_mu);
  return b->instances.erase(token) ? 0 : -1;
}

static int add_listener_api(uvbrg_handle_t h, const char *addr, uint16_t port,
                            uint16_t *bound_port,
                            uvbrg_listener_t *listener_out,
                            bool make_default) {
  if (!h || !addr)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  std::unique_lock<std::mutex> lifecycle_lk(b->lifecycle_mu);
  if (b->lifecycle_state == BridgeLifecycleState::Stopping ||
      b->lifecycle_state == BridgeLifecycleState::Stopped)
    return UV_ECANCELED;
  if (b->lifecycle_state == BridgeLifecycleState::Destroyed)
    return UV_EINVAL;
  const bool running = b->running.load(std::memory_order_acquire);
  BridgeConfig listener_config;
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    listener_config = b->config;
  }

  if (!running) {
    Listener *listener = nullptr;
    const int rc = add_listener_on_loop(b, listener_config, addr, port,
                                        bound_port, &listener, make_default);
    if (rc == 0 && listener_out)
      *listener_out = listener;
    return rc;
  }
  lifecycle_lk.unlock();

  if (!b->command_async_initialized.load(std::memory_order_acquire))
    return UV_EINVAL;

  auto cmd = std::make_shared<BridgeCommand>();
  cmd->kind = BridgeCommand::Kind::AddListener;
  cmd->addr = addr;
  cmd->requested_port = port;
  cmd->make_default = make_default;
  const int rc = run_command_sync(b, cmd);
  if (rc == 0) {
    if (bound_port)
      *bound_port = cmd->bound_port;
    if (listener_out)
      *listener_out = cmd->listener;
  }
  return rc;
}

int uvbrg_add_listener(uvbrg_handle_t h, const char *addr, uint16_t port,
                       uint16_t *bound_port, uvbrg_listener_t *listener_out) {
  return add_listener_api(h, addr, port, bound_port, listener_out, false);
}

int uvbrg_add_listener_with_config(uvbrg_handle_t h,
                                   const char *listener_config_json,
                                   uint16_t *bound_port,
                                   uvbrg_listener_t *listener_out) {
  if (!h || !listener_config_json)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  std::unique_lock<std::mutex> lifecycle_lk(b->lifecycle_mu);
  if (b->lifecycle_state == BridgeLifecycleState::Stopping ||
      b->lifecycle_state == BridgeLifecycleState::Stopped)
    return UV_ECANCELED;
  if (b->lifecycle_state == BridgeLifecycleState::Destroyed)
    return UV_EINVAL;
  const bool running = b->running.load(std::memory_order_acquire);
  BridgeConfig listener_config;
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    listener_config = b->config;
  }
  if (!parse_config(listener_config_json, listener_config))
    return UV_EINVAL;

  if (!running) {
    Listener *listener = nullptr;
    const int rc = add_listener_on_loop(
        b, listener_config, listener_config.listen_addr.c_str(),
        listener_config.listen_port, bound_port, &listener);
    if (rc == 0 && listener_out)
      *listener_out = listener;
    return rc;
  }
  lifecycle_lk.unlock();

  if (!b->command_async_initialized.load(std::memory_order_acquire))
    return UV_EINVAL;

  auto cmd = std::make_shared<BridgeCommand>();
  cmd->kind = BridgeCommand::Kind::AddListener;
  cmd->config_json = listener_config_json;
  const int rc = run_command_sync(b, cmd);
  if (rc == 0) {
    if (bound_port)
      *bound_port = cmd->bound_port;
    if (listener_out)
      *listener_out = cmd->listener;
  }
  return rc;
}

int uvbrg_start_listener_ex(uvbrg_handle_t h, const char *addr, uint16_t port,
                            uint16_t *bound_port) {
  return uvbrg_start_listener_with_handle(h, addr, port, bound_port, nullptr);
}

int uvbrg_start_listener_with_handle(uvbrg_handle_t h, const char *addr,
                                     uint16_t port, uint16_t *bound_port,
                                     uvbrg_listener_t *listener_out) {
  uvbrg_listener_t listener = nullptr;
  const int rc =
      add_listener_api(h, addr, port, bound_port, &listener, true);
  if (rc == 0 && listener_out)
    *listener_out = listener;
  return rc;
}

int uvbrg_start_listener(uvbrg_handle_t h, const char *addr, uint16_t port) {
  return uvbrg_start_listener_ex(h, addr, port, nullptr);
}

int uvbrg_close_listener(uvbrg_handle_t h, uvbrg_listener_t listener_handle) {
  if (!h || !listener_handle)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  Listener *listener = nullptr;
  std::unique_lock<std::mutex> lifecycle_lk(b->lifecycle_mu);
  if (b->lifecycle_state == BridgeLifecycleState::Destroyed)
    return UV_EINVAL;
  if (b->lifecycle_state == BridgeLifecycleState::Stopping ||
      b->lifecycle_state == BridgeLifecycleState::Stopped)
    return UV_ECANCELED;
  {
    std::lock_guard<std::mutex> state_lk(b->state_mu);
    listener = find_listener_locked(b, listener_handle, 0, true);
    if (!listener)
      return UV_EINVAL;
  }
  const uint64_t listener_id = listener->id;
  const bool running = b->running.load(std::memory_order_acquire);
  if (!running)
    return close_listener_on_loop(b, listener, listener_id);
  lifecycle_lk.unlock();

  if (!b->command_async_initialized.load(std::memory_order_acquire))
    return UV_EINVAL;

  auto cmd = std::make_shared<BridgeCommand>();
  cmd->kind = BridgeCommand::Kind::CloseListener;
  cmd->listener = listener;
  cmd->listener_id = listener_id;
  return run_command_sync(b, cmd);
}

int uvbrg_update_listener_config(uvbrg_handle_t h,
                                 uvbrg_listener_t listener_handle,
                                 const char *listener_config_json,
                                 int close_sessions) {
  if (!h || !listener_handle || !listener_config_json ||
      !*listener_config_json)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  std::unique_lock<std::mutex> lifecycle_lk(b->lifecycle_mu);
  if (b->lifecycle_state == BridgeLifecycleState::Destroyed)
    return UV_EINVAL;
  if (b->lifecycle_state == BridgeLifecycleState::Stopping ||
      b->lifecycle_state == BridgeLifecycleState::Stopped)
    return UV_ECANCELED;

  Listener *listener = nullptr;
  {
    std::lock_guard<std::mutex> state_lk(b->state_mu);
    listener = find_listener_locked(b, listener_handle, 0, true);
    if (!listener)
      return UV_EINVAL;
  }
  const uint64_t listener_id = listener->id;

  const bool running = b->running.load(std::memory_order_acquire);
  if (!running) {
    BridgeConfig current_config;
    {
      std::lock_guard<std::mutex> state_lk(b->state_mu);
      current_config = listener->config;
    }
    BridgeConfig next_config = current_config;
    if (!parse_config(listener_config_json, next_config) ||
        (next_config.strict_proxy && b->tunnel_callbacks.open))
      return UV_EINVAL;
    if (next_config.listen_port == 0)
      next_config.listen_port = current_config.listen_port;
    if (next_config.listen_addr.empty())
      next_config.listen_addr = current_config.listen_addr;
    if (next_config.listen_addr != current_config.listen_addr ||
        next_config.listen_port != current_config.listen_port)
      return UV_EINVAL;
    if (close_sessions) {
      if (internal_transport_config_changed(current_config, next_config))
        close_listener_sessions(b, listener);
      else
        close_listener_external_sessions(b, listener);
    }
    {
      std::lock_guard<std::mutex> state_lk(b->state_mu);
      listener->config = next_config;
      if (b->default_listener == listener)
        b->config = next_config;
    }
    b->circuit_breakers.clear();
    return 0;
  }
  lifecycle_lk.unlock();

  if (!b->command_async_initialized.load(std::memory_order_acquire))
    return UV_EINVAL;
  auto cmd = std::make_shared<BridgeCommand>();
  cmd->kind = BridgeCommand::Kind::UpdateListenerConfig;
  cmd->listener = listener;
  cmd->listener_id = listener_id;
  cmd->config_json = listener_config_json;
  cmd->close_sessions = close_sessions != 0;
  return run_command_sync(b, cmd);
}

int uvbrg_run(uvbrg_handle_t h) {
  if (!h)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  if (b->shared_reactor)
    return UV_EALREADY;
  {
    std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
    if (b->lifecycle_state == BridgeLifecycleState::Destroyed)
      return UV_EINVAL;
    if (b->lifecycle_state == BridgeLifecycleState::Stopping ||
        b->lifecycle_state == BridgeLifecycleState::Stopped)
      return UV_ECANCELED;
    if (!b->command_async_initialized.load(std::memory_order_acquire))
      return UV_EINVAL;
    bool expected = false;
    if (!b->running.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire))
      return UV_EBUSY;
    b->lifecycle_state = BridgeLifecycleState::Running;
    b->loop_thread_id = std::this_thread::get_id();
  }
  const int rc = uv_run(b->loop, UV_RUN_DEFAULT);
  {
    std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
    b->running.store(false, std::memory_order_release);
    b->loop_thread_id = {};
    if (b->lifecycle_state != BridgeLifecycleState::Destroyed)
      b->lifecycle_state = BridgeLifecycleState::Stopped;
  }
  return rc;
}

void uvbrg_stop(uvbrg_handle_t h) {
  if (!h)
    return;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  bool should_request_stop = false;
  bool should_cancel_commands = false;
  {
    std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
    if (b->lifecycle_state == BridgeLifecycleState::Destroyed)
      return;
    if (b->lifecycle_state == BridgeLifecycleState::Stopped ||
        b->lifecycle_state == BridgeLifecycleState::Stopping)
      return;
    if (b->lifecycle_state == BridgeLifecycleState::New) {
      b->lifecycle_state = BridgeLifecycleState::Stopped;
      should_cancel_commands = true;
    } else {
      b->lifecycle_state = BridgeLifecycleState::Stopping;
      should_request_stop = true;
    }
  }
  if (should_cancel_commands)
    cancel_pending_commands(b);
  if (should_request_stop)
    b->request_stop();
}

int uvbrg_wait_stopped(uvbrg_handle_t h, uint32_t timeout_ms) {
  if (!h)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  if (!b->shared_reactor)
    return b->running.load(std::memory_order_acquire) ? UV_EBUSY : 0;
  if (b->shared_loop_lease && b->shared_loop_lease->IsLoopThread())
    return UV_EBUSY;
  {
    std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
    if (b->lifecycle_state == BridgeLifecycleState::Stopped)
      return 0;
  }
  std::unique_lock<std::mutex> lk(b->stopped_mu);
  const auto timeout =
      std::chrono::milliseconds(timeout_ms == 0 ? 5000 : timeout_ms);
  return b->stopped_cv.wait_for(lk, timeout,
                                [b]() { return b->stopped_signaled; })
             ? 0
             : UV_ETIMEDOUT;
}

int uvbrg_is_shared(uvbrg_handle_t h) {
  if (!h)
    return 0;
  return static_cast<ProxyBridgeImpl *>(h)->shared_reactor ? 1 : 0;
}

int uvbrg_get_shared_reactor_stats(uvbrg_shared_reactor_stats_t *stats) {
  if (!stats)
    return UV_EINVAL;
  const auto snapshot = uvbrg::internal::SharedLoopDiagnostics();
  std::memset(stats, 0, sizeof(*stats));
  stats->reactor_count = snapshot.reactor_count;
  stats->active_leases = snapshot.active_leases;
  stats->queued_tasks = snapshot.queued_tasks;
  stats->queued_bytes = snapshot.queued_bytes;
  stats->rejected_tasks = snapshot.rejected_tasks;
  stats->post_failures = snapshot.post_failures;
  stats->loop_close_failures = snapshot.loop_close_failures;
  stats->accepting = snapshot.accepting ? 1 : 0;
  stats->quarantined = snapshot.quarantined ? 1 : 0;
  return 0;
}

int uvbrg_shutdown_shared_reactors(uint32_t timeout_ms) {
  const auto status = uvbrg::internal::ShutdownSharedLoops(
      std::chrono::milliseconds(timeout_ms == 0 ? 5000 : timeout_ms));
  switch (status) {
  case uvbrg::internal::SharedLoopShutdownStatus::Stopped:
  case uvbrg::internal::SharedLoopShutdownStatus::AlreadyStopped:
    return 0;
  case uvbrg::internal::SharedLoopShutdownStatus::Busy:
    return UV_EBUSY;
  case uvbrg::internal::SharedLoopShutdownStatus::TimedOut:
    return UV_ETIMEDOUT;
  case uvbrg::internal::SharedLoopShutdownStatus::LoopCloseFailed:
    return UV_EBUSY;
  case uvbrg::internal::SharedLoopShutdownStatus::CalledFromReactorThread:
    return UV_EBUSY;
  }
  return UV_EINVAL;
}

char *uvbrg_query_sessions(uvbrg_handle_t h, const char * /*filter*/) {
  if (!h)
    return nullptr;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  std::string json;
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    json = b->sessions_json_locked();
  }
  char *out = new char[json.size() + 1];
  std::memcpy(out, json.c_str(), json.size() + 1);
  return out;
}

int uvbrg_set_session_budget(uvbrg_handle_t h, uint32_t maximum) {
  if (!h) return UV_EINVAL;
  static_cast<ProxyBridgeImpl *>(h)->session_budget.store(maximum, std::memory_order_relaxed);
  return 0;
}

int uvbrg_get_stats(uvbrg_handle_t h, uvbrg_stats_t *stats) {
  if (!h || !stats)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  std::memset(stats, 0, sizeof(*stats));
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    stats->active_sessions = static_cast<uint32_t>(b->sessions.size());
  }
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    stats->listen_port = b->config.listen_port;
    stats->listener_count = static_cast<uint32_t>(b->listeners.size());
    stats->max_sessions = b->config.max_sessions;
  }
  stats->accepted_sessions =
      b->accepted_sessions.load(std::memory_order_relaxed);
  stats->closed_sessions = b->closed_sessions.load(std::memory_order_relaxed);
  stats->rejected_sessions =
      b->rejected_sessions.load(std::memory_order_relaxed);
  stats->pending_dns = b->pending_dns.load(std::memory_order_relaxed);
  stats->queued_write_bytes =
      b->queued_write_bytes.load(std::memory_order_relaxed);
  stats->max_queued_write_bytes =
      b->max_queued_write_bytes.load(std::memory_order_relaxed);
  stats->pending_write_requests =
      b->pending_write_requests.load(std::memory_order_relaxed);
  stats->max_pending_write_requests =
      b->max_pending_write_requests.load(std::memory_order_relaxed);
  stats->queued_commands =
      b->queued_commands.load(std::memory_order_relaxed);
  stats->queued_command_bytes =
      b->queued_command_bytes.load(std::memory_order_relaxed);
  stats->max_queued_commands =
      b->max_queued_commands.load(std::memory_order_relaxed);
  stats->max_queued_command_bytes =
      b->max_queued_command_bytes.load(std::memory_order_relaxed);
  stats->command_queue_rejections =
      b->command_queue_rejections.load(std::memory_order_relaxed);
  stats->eof_events = b->eof_events.load(std::memory_order_relaxed);
  stats->read_pause_events =
      b->read_pause_events.load(std::memory_order_relaxed);
  stats->read_resume_events =
      b->read_resume_events.load(std::memory_order_relaxed);
  stats->paused_read_sides =
      b->paused_read_sides.load(std::memory_order_relaxed);
  stats->listener_close_events =
      b->listener_close_events.load(std::memory_order_relaxed);
  stats->loop_close_failures =
      b->loop_close_failures.load(std::memory_order_relaxed);
  stats->shared_reactor_index = b->shared_reactor_index;
  stats->shared_reactor = b->shared_reactor ? 1 : 0;
  return 0;
}

int uvbrg_get_listener_stats(uvbrg_handle_t h, uvbrg_listener_t listener_handle,
                             uvbrg_listener_stats_t *stats) {
  if (!h || !listener_handle || !stats)
    return UV_EINVAL;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  Listener *listener = nullptr;
  {
    std::lock_guard<std::mutex> lk(b->state_mu);
    listener = find_listener_locked(b, listener_handle);
    if (!listener)
      return UV_EINVAL;
    std::memset(stats, 0, sizeof(*stats));
    stats->listen_port = listener->port.load(std::memory_order_relaxed);
    stats->closing = listener->closing.load(std::memory_order_acquire) ? 1 : 0;
    stats->accepted_sessions =
        listener->accepted_sessions.load(std::memory_order_relaxed);
    stats->rejected_sessions =
        listener->rejected_sessions.load(std::memory_order_relaxed);
  }
  {
    std::lock_guard<std::mutex> lk(b->sessions_mu);
    uint32_t active = 0;
    for (const auto &item : b->sessions) {
      if (item.second && item.second->listener == listener)
        ++active;
    }
    stats->active_sessions = active;
  }
  return 0;
}

void uvbrg_free_string(char *s) {
  delete[] s;
}

static int drain_bridge_loop(ProxyBridgeImpl *b) {
  if (!b || !b->loop_initialized)
    return UV_EINVAL;
  if (b->shared_reactor)
    return UV_EINVAL;
  if (b->running.load(std::memory_order_acquire)) {
    b->request_stop();
    return UV_EBUSY;
  }

  on_stop_requested(&b->stop_async);

  int run_rc = 0;
  do {
    run_rc = uv_run(b->loop, UV_RUN_DEFAULT);
    close_finished_listeners(b);
  } while (run_rc != 0);

  const int close_rc = uv_loop_close(b->loop);
  if (close_rc == 0) {
    b->loop_initialized = false;
    return 0;
  }
  b->loop_close_failures.fetch_add(1, std::memory_order_relaxed);

  uv_walk(
      b->loop,
      [](uv_handle_t *h, void *) {
        if (!uv_is_closing(h))
          uv_close(h, nullptr);
      },
      nullptr);
  uv_run(b->loop, UV_RUN_DEFAULT);
  const int forced_close_rc = uv_loop_close(b->loop);
  if (forced_close_rc == 0)
    b->loop_initialized = false;
  else
    b->loop_close_failures.fetch_add(1, std::memory_order_relaxed);
  return close_rc;
}

void uvbrg_destroy(uvbrg_handle_t h) {
  if (!h)
    return;
  auto *b = static_cast<ProxyBridgeImpl *>(h);
  bool should_request_stop = false;
  bool should_cancel_commands = false;
  {
    std::lock_guard<std::mutex> lifecycle_lk(b->lifecycle_mu);
    if (b->lifecycle_state == BridgeLifecycleState::Destroyed)
      return;
    if (b->running.load(std::memory_order_acquire) ||
        b->lifecycle_state == BridgeLifecycleState::Running) {
      b->lifecycle_state = BridgeLifecycleState::Stopping;
      should_request_stop = true;
      should_cancel_commands = true;
    } else if (b->lifecycle_state == BridgeLifecycleState::Stopping) {
      should_cancel_commands = true;
    } else {
      b->lifecycle_state = BridgeLifecycleState::Destroyed;
    }
  }
  if (should_request_stop)
    b->request_stop();
  if (should_cancel_commands) {
    cancel_pending_commands(b);
    return;
  }
  if (b->shared_reactor) {
    if (b->shared_loop_lease)
      b->shared_loop_lease->Release();
    b->shared_loop_lease.reset();
    b->loop = nullptr;
    b->loop_initialized = false;
  } else if (b->loop_initialized) {
    drain_bridge_loop(b);
  }
  delete b;
}

uvbrg_handle_t pvb_init(const char *config_json) {
  return uvbrg_init(config_json);
}

void pvb_set_callbacks(uvbrg_handle_t h, const uvbrg_callbacks_t *cbs) {
  uvbrg_set_callbacks(h, cbs);
}

int pvb_register_instance(uvbrg_handle_t h, const char *token,
                          const char *meta_json) {
  return uvbrg_register_instance(h, token, meta_json);
}

int pvb_unregister_instance(uvbrg_handle_t h, const char *token) {
  return uvbrg_unregister_instance(h, token);
}

int pvb_start_listener(uvbrg_handle_t h, const char *addr, uint16_t port) {
  return uvbrg_start_listener(h, addr, port);
}

int pvb_run(uvbrg_handle_t h) {
  return uvbrg_run(h);
}

void pvb_stop(uvbrg_handle_t h) {
  uvbrg_stop(h);
}

char *pvb_query_sessions(uvbrg_handle_t h, const char *filter) {
  return uvbrg_query_sessions(h, filter);
}

void pvb_free_string(char *s) {
  uvbrg_free_string(s);
}

void pvb_destroy(uvbrg_handle_t h) {
  uvbrg_destroy(h);
}

} // extern "C"

// ═════════════════════════════════════════════════════════════════════════════
// C++ UvBridge wrapper
// ═════════════════════════════════════════════════════════════════════════════

UvBridge::UvBridge() : impl_(new ProxyBridgeImpl) {
  if (uv_loop_init(impl_->loop) != 0) {
    impl_.reset();
    return;
  }
  impl_->loop_initialized = true;
  if (init_bridge_async_handles(impl_.get()) != 0) {
    uv_run(impl_->loop, UV_RUN_DEFAULT);
    uv_loop_close(impl_->loop);
    impl_.reset();
  }
}

UvBridge::~UvBridge() {
  if (!impl_)
    return;

  if (impl_->running.load(std::memory_order_acquire)) {
    uvbrg_stop(impl_.get());
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(5);
    while (impl_->running.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (impl_->running.load(std::memory_order_acquire)) {
      std::fprintf(stderr,
                   "UvBridge destroyed while uvbrg_run is still active; "
                   "call stop() and join the run thread before destruction.\n");
      impl_.release();
      return;
    }
  }

  uvbrg_destroy(impl_.release());
}

bool UvBridge::init(const std::string &config_json) {
  if (!impl_)
    return false;
  return parse_config(config_json.c_str(), impl_->config);
}

bool UvBridge::start_listener(const std::string &addr, uint16_t port) {
  if (!impl_)
    return false;
  return uvbrg_start_listener(impl_.get(), addr.c_str(), port) == 0;
}

bool UvBridge::start_listener(const std::string &addr, uint16_t requested_port,
                              uint16_t &bound_port) {
  if (!impl_)
    return false;
  return uvbrg_start_listener_ex(impl_.get(), addr.c_str(), requested_port,
                                 &bound_port) == 0;
}

bool UvBridge::add_listener(const std::string &addr, uint16_t requested_port,
                            uint16_t &bound_port, Listener &listener) {
  if (!impl_)
    return false;
  return uvbrg_add_listener(impl_.get(), addr.c_str(), requested_port,
                            &bound_port, &listener) == 0;
}

bool UvBridge::close_listener(Listener listener) {
  if (!impl_)
    return false;
  return uvbrg_close_listener(impl_.get(), listener) == 0;
}

bool UvBridge::update_listener_config(Listener listener,
                                      const std::string &config_json,
                                      bool close_sessions) {
  if (!impl_ || !listener || config_json.empty())
    return false;
  return uvbrg_update_listener_config(impl_.get(), listener,
                                      config_json.c_str(),
                                      close_sessions ? 1 : 0) == 0;
}

bool UvBridge::register_instance(const std::string &token,
                                 const std::string &meta_json) {
  if (!impl_)
    return false;
  return uvbrg_register_instance(impl_.get(), token.c_str(),
                                 meta_json.c_str()) == 0;
}

bool UvBridge::unregister_instance(const std::string &token) {
  if (!impl_)
    return false;
  return uvbrg_unregister_instance(impl_.get(), token.c_str()) == 0;
}

void UvBridge::run() {
  if (impl_)
    uvbrg_run(impl_.get());
}
void UvBridge::stop() {
  if (impl_)
    uvbrg_stop(impl_.get());
}

std::string UvBridge::query_sessions(const std::string &filter) {
  if (!impl_)
    return "[]";
  char *raw = uvbrg_query_sessions(impl_.get(), filter.c_str());
  if (!raw)
    return "[]";
  std::string result(raw);
  uvbrg_free_string(raw);
  return result;
}
