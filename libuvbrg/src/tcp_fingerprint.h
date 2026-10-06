#pragma once
// Passive TCP socket parameter extraction for fingerprinting.
// Reads negotiated kernel TCP parameters via getsockopt from a live libuv
// connection to characterise the remote peer's stack.
//
// What can be learned at the application layer (no raw sockets / pcap needed):
//   • Negotiated MSS   — reflects path MTU; 1460 = Ethernet, 1452 = PPPoE, etc.
//   • Socket buffers   — OS defaults differ: Linux auto-tunes, Windows classic
//   = 87380 • RTT + variance   — round-trip latency to peer (Linux/macOS
//   TCP_INFO) • Congestion window— Linux TCP_INFO snd_cwnd • Platform hint    —
//   heuristic inference from the above values
//
// The TCP initial window size that a remote observer sees in our SYN is a
// kernel-level value.  We surface the closest observable proxy (SO_RCVBUF)
// rather than silently omitting any window-related information.
//
// Usage:
//   TcpFingerprint fp;
//   if (tcp_fp::probe(&session->remote, fp)) { /* fp populated */ }

extern "C" {
#include <uv.h>
}

#include <cstdint>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <netinet/tcp_fsm.h>
#endif
#endif
#endif

// ── TcpFingerprint struct
// ─────────────────────────────────────────────────────
struct TcpFingerprint {
  uint32_t mss{0};        // negotiated MSS (TCP_MAXSEG)
  uint32_t recv_buf{0};   // kernel receive buffer size (SO_RCVBUF)
  uint32_t send_buf{0};   // kernel send buffer size (SO_SNDBUF)
  uint32_t rtt_us{0};     // smoothed RTT in microseconds (Linux/macOS/Win)
  uint32_t rtt_var_us{0}; // RTT variance in microseconds (Linux only)
  uint32_t cwnd_segs{0};  // congestion window in segments (Linux only)
  uint32_t retrans{0};    // total retransmissions (Linux only)
  // Heuristic OS guess based on socket parameter patterns
  std::string platform_hint; // "windows-classic" | "linux-large-buf" |
                             // "macos-likely" | "unknown"

  bool extracted{false};

  std::string to_json() const;
};

// ── Implementation namespace
// ──────────────────────────────────────────────────
namespace tcp_fp {

// Heuristic: infer likely OS from socket buffer defaults.
// These are rough heuristics — auto-tuning blurs distinctions on modern
// kernels.
static inline std::string _infer_platform(uint32_t mss, uint32_t rcvbuf,
                                          uint32_t rtt_us) {
  (void)mss;
  // Classic Windows non-auto-tuned receiver
  if (rcvbuf == 87380)
    return "windows-classic";
  // macOS default
  if (rcvbuf == 131072 || rcvbuf == 196608)
    return "macos-likely";
  // Large Linux auto-tuned buffers
  if (rcvbuf >= 4194304)
    return "linux-large-buf";
  // Sub-millisecond RTT → same host / loopback (virtualised or local proxy)
  if (rtt_us > 0 && rtt_us < 200)
    return "loopback-or-vlan";
  return "unknown";
}

/// Probe TCP socket parameters from a live libuv handle.
/// MUST be called from the libuv event loop thread (no locking required).
/// \param tcp  A connected uv_tcp_t handle
/// \param out  Populated on success
/// \return true on success
inline bool probe(const uv_tcp_t *tcp, TcpFingerprint &out) {
  uv_os_fd_t fd_raw = {};
  if (uv_fileno(reinterpret_cast<const uv_handle_t *>(tcp), &fd_raw) != 0)
    return false;

#ifdef _WIN32
  // On Windows, uv_os_fd_t is HANDLE (void*); libuv returns the SOCKET cast as
  // HANDLE.
  SOCKET fd = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(fd_raw));

#if defined(TCP_MAXSEG)
  // MSS
  {
    DWORD v = 0;
    int l = sizeof(v);
    if (getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, reinterpret_cast<char *>(&v),
                   &l) == 0)
      out.mss = static_cast<uint32_t>(v);
  }
    #endif
  // Receive + send buffers
  {
    DWORD v = 0;
    int l = sizeof(v);
    if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char *>(&v),
                   &l) == 0)
      out.recv_buf = static_cast<uint32_t>(v);
  }
  {
    DWORD v = 0;
    int l = sizeof(v);
    if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char *>(&v),
                   &l) == 0)
      out.send_buf = static_cast<uint32_t>(v);
  }
#if defined(SIO_TCP_INFO)
  // RTT via SIO_TCP_INFO (Windows 10 1607 / Server 2016 and later)
  {
    TCP_INFO_v0 info{};
    DWORD bytes = 0, ver = 0;
    if (WSAIoctl(fd, SIO_TCP_INFO, &ver, sizeof(ver), &info, sizeof(info),
                 &bytes, nullptr, nullptr) == 0) {
      out.rtt_us = static_cast<uint32_t>(info.RttUs);
    }
  }
#endif

#else // POSIX
  int fd = static_cast<int>(fd_raw);
  socklen_t sl;

#if defined(TCP_MAXSEG)
  // MSS
  {
    int v = 0;
    sl = sizeof(v);
    if (getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &v, &sl) == 0)
      out.mss = static_cast<uint32_t>(v);
  }
    #endif
  // Receive + send buffers
  {
    int v = 0;
    sl = sizeof(v);
    if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &v, &sl) == 0)
      out.recv_buf = static_cast<uint32_t>(v);
  }
  {
    int v = 0;
    sl = sizeof(v);
    if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &v, &sl) == 0)
      out.send_buf = static_cast<uint32_t>(v);
  }

#if defined(__linux__) && !defined(__ANDROID__)
  // Linux: full tcp_info structure — RTT, variance, cwnd, retrans
  {
    struct tcp_info info{};
    sl = sizeof(info);
    if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &sl) == 0) {
      out.rtt_us = info.tcpi_rtt;
      out.rtt_var_us = info.tcpi_rttvar;
      out.cwnd_segs = info.tcpi_snd_cwnd;
      out.retrans = info.tcpi_retrans;
    }
  }
#elif defined(__APPLE__) && TARGET_OS_OSX
  // macOS: TCP_CONNECTION_INFO (srtt in milliseconds)
  {
    struct tcp_connection_info info{};
    sl = sizeof(info);
    if (getsockopt(fd, IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &sl) == 0)
      out.rtt_us = static_cast<uint32_t>(info.tcpi_srtt) * 1000u;
  }
#endif
#endif // _WIN32

  out.platform_hint = _infer_platform(out.mss, out.recv_buf, out.rtt_us);
  out.extracted = true;
  return true;
}

} // namespace tcp_fp

// ── TcpFingerprint::to_json
// ───────────────────────────────────────────────────
inline std::string TcpFingerprint::to_json() const {
  std::string j;
  j.reserve(256);
  j += "{\"extracted\":";
  j += extracted ? "true" : "false";
  j += ",\"mss\":";
  j += std::to_string(mss);
  j += ",\"recv_buf\":";
  j += std::to_string(recv_buf);
  j += ",\"send_buf\":";
  j += std::to_string(send_buf);
  j += ",\"rtt_us\":";
  j += std::to_string(rtt_us);
  j += ",\"rtt_var_us\":";
  j += std::to_string(rtt_var_us);
  j += ",\"cwnd_segs\":";
  j += std::to_string(cwnd_segs);
  j += ",\"retrans\":";
  j += std::to_string(retrans);
  j += ",\"platform_hint\":\"";
  j += platform_hint;
  j += '"';
  j += "}";
  return j;
}
