#include <libsys.h>
#include "egress_probe_executor.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <future>
#include <functional>
#include <exception>
#include <string>
#include <vector>

namespace {

const char *kGlobalProbeUrl = "http://google.com/";
const char *kDomesticProbeUrl = "http://baidu.com/";

const char *CapabilityName(ISystem::EgressProbeInfo::Capability capability) {
  switch (capability) {
  case ISystem::EgressProbeInfo::Capability::GlobalEgress:
    return "GlobalEgress";
  case ISystem::EgressProbeInfo::Capability::DomesticOnly:
    return "DomesticOnly";
  case ISystem::EgressProbeInfo::Capability::Offline:
    return "Offline";
  default:
    return "Unknown";
  }
}

struct ParsedUrl {
  bool valid = false;
  bool secure = false;
  std::string host;
  uint16_t port = 0;
  std::string path = "/";
  std::string absolute_url;
};

ParsedUrl ParseUrl(const std::string &url) {
  ParsedUrl out;
  out.absolute_url = url;
  if (url.empty())
    return out;

  auto scheme_pos = url.find("://");
  if (scheme_pos == std::string::npos)
    return out;
  std::string scheme = url.substr(0, scheme_pos);
  for (auto &c : scheme)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

  const std::string rest = url.substr(scheme_pos + 3);
  const auto slash = rest.find('/');
  std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
  out.path = slash == std::string::npos ? "/" : rest.substr(slash);
  if (out.path.empty())
    out.path = "/";

  const auto colon = hostport.rfind(':');
  if (colon != std::string::npos) {
    out.host = hostport.substr(0, colon);
    try {
      out.port = static_cast<uint16_t>(std::stoul(hostport.substr(colon + 1)));
    } catch (...) {
      return out;
    }
  } else {
    out.host = hostport;
    out.port = scheme == "https" ? 443 : 80;
  }

  out.secure = scheme == "https";
  out.valid = !out.host.empty() && out.port != 0 &&
              (scheme == "http" || scheme == "https");
  return out;
}

std::string LowerCopy(const std::string &value) {
  std::string out = value;
  for (auto &c : out)
    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return out;
}

bool HostMatchesBypass(const std::string &host, const std::string &rule) {
  if (rule.empty())
    return false;
  const std::string host_lc = LowerCopy(host);
  std::string rule_lc = LowerCopy(rule);
  if (rule_lc == "*")
    return true;
  if (rule_lc == "<local>")
    return host_lc.find('.') == std::string::npos;
  if (rule_lc.rfind("*.", 0) == 0)
    rule_lc.erase(rule_lc.begin());
  if (!rule_lc.empty() && rule_lc.front() == '.')
    rule_lc.erase(rule_lc.begin());
  if (host_lc == rule_lc)
    return true;
  if (host_lc.size() > rule_lc.size() &&
      host_lc.compare(host_lc.size() - rule_lc.size(), rule_lc.size(),
                      rule_lc) == 0 &&
      host_lc[host_lc.size() - rule_lc.size() - 1] == '.')
    return true;
  return false;
}

bool ShouldBypassProxy(const std::string &host,
                       const std::vector<std::string> &bypass_list) {
  for (const auto &rule : bypass_list) {
    if (HostMatchesBypass(host, rule))
      return true;
  }
  return false;
}

bool WaitForFd(int fd, bool want_write, uint32_t timeout_ms, std::string &error) {
  fd_set rfds;
  fd_set wfds;
  FD_ZERO(&rfds);
  FD_ZERO(&wfds);
  if (want_write)
    FD_SET(fd, &wfds);
  else
    FD_SET(fd, &rfds);

  timeval tv{};
  tv.tv_sec = static_cast<long>(timeout_ms / 1000);
  tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
  const int rc = select(fd + 1, want_write ? nullptr : &rfds,
                        want_write ? &wfds : nullptr, nullptr, &tv);
  if (rc > 0)
    return true;
  if (rc == 0) {
    error = "timeout";
    return false;
  }
  error = std::string("select_failed_") + std::to_string(errno);
  return false;
}

int ConnectTcp(const std::string &host, uint16_t port, uint32_t timeout_ms,
               std::string &error) {
  error.clear();
  addrinfo hints{};
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_family = AF_UNSPEC;

  addrinfo *res = nullptr;
  const std::string port_str = std::to_string(port);
  const int gai = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
  if (gai != 0) {
    error = std::string("getaddrinfo_") + gai_strerror(gai);
    return -1;
  }

  int sock = -1;
  for (auto *it = res; it; it = it->ai_next) {
    sock = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (sock < 0)
      continue;

    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
      fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    const int rc = connect(sock, it->ai_addr, it->ai_addrlen);
    if (rc == 0) {
      if (flags >= 0)
        fcntl(sock, F_SETFL, flags);
      break;
    }
    if (errno == EINPROGRESS) {
      std::string wait_err;
      if (!WaitForFd(sock, true, timeout_ms, wait_err)) {
        error = wait_err;
        close(sock);
        sock = -1;
        continue;
      }
      int so_error = 0;
      socklen_t len = sizeof(so_error);
      if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 &&
          so_error == 0) {
        if (flags >= 0)
          fcntl(sock, F_SETFL, flags);
        break;
      }
      error = std::string("connect_failed_") +
              std::to_string(so_error != 0 ? so_error : errno);
    } else {
      error = std::string("connect_failed_") + std::to_string(errno);
    }

    close(sock);
    sock = -1;
  }

  freeaddrinfo(res);
  return sock;
}

bool SendAll(int fd, const uint8_t *data, size_t len, uint32_t timeout_ms,
             std::string &error) {
  size_t sent = 0;
  while (sent < len) {
    if (!WaitForFd(fd, true, timeout_ms, error))
      return false;
    const ssize_t rc =
        send(fd, reinterpret_cast<const char *>(data + sent), len - sent, 0);
    if (rc <= 0) {
      error = std::string("send_failed_") + std::to_string(errno);
      return false;
    }
    sent += static_cast<size_t>(rc);
  }
  return true;
}

bool SendString(int fd, const std::string &data, uint32_t timeout_ms,
                std::string &error) {
  return SendAll(fd, reinterpret_cast<const uint8_t *>(data.data()), data.size(),
                 timeout_ms, error);
}

bool RecvExact(int fd, uint8_t *data, size_t len, uint32_t timeout_ms,
               std::string &error) {
  size_t recvd = 0;
  while (recvd < len) {
    if (!WaitForFd(fd, false, timeout_ms, error))
      return false;
    const ssize_t rc =
        recv(fd, reinterpret_cast<char *>(data + recvd), len - recvd, 0);
    if (rc <= 0) {
      error = std::string("recv_failed_") + std::to_string(errno);
      return false;
    }
    recvd += static_cast<size_t>(rc);
  }
  return true;
}

bool ReadStatusLine(int fd, uint32_t timeout_ms, int &status_code,
                    std::string &error) {
  status_code = 0;
  std::string line;
  while (line.find("\r\n") == std::string::npos && line.size() < 4096) {
    if (!WaitForFd(fd, false, timeout_ms, error))
      return false;
    char buf[512] = {};
    const ssize_t rc = recv(fd, buf, sizeof(buf), 0);
    if (rc <= 0) {
      error = std::string("recv_failed_") + std::to_string(errno);
      return false;
    }
    line.append(buf, static_cast<size_t>(rc));
  }

  const auto eol = line.find("\r\n");
  if (eol == std::string::npos) {
    error = "invalid_http_response";
    return false;
  }

  const std::string status_line = line.substr(0, eol);
  const auto sp1 = status_line.find(' ');
  if (sp1 == std::string::npos) {
    error = "invalid_http_status_line";
    return false;
  }
  const auto sp2 = status_line.find(' ', sp1 + 1);
  const std::string code_str = status_line.substr(
      sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
  try {
    status_code = std::stoi(code_str);
  } catch (...) {
    error = "invalid_http_status_code";
    return false;
  }
  return true;
}

bool Socks5Connect(int fd, const ParsedUrl &target, uint32_t timeout_ms,
                   std::string &error) {
  const uint8_t greeting[] = {0x05, 0x01, 0x00};
  if (!SendAll(fd, greeting, sizeof(greeting), timeout_ms, error))
    return false;

  uint8_t greet_reply[2] = {};
  if (!RecvExact(fd, greet_reply, sizeof(greet_reply), timeout_ms, error))
    return false;
  if (greet_reply[0] != 0x05 || greet_reply[1] != 0x00) {
    error = "socks5_auth_not_supported";
    return false;
  }

  std::vector<uint8_t> req;
  req.push_back(0x05);
  req.push_back(0x01);
  req.push_back(0x00);
  req.push_back(0x03);
  req.push_back(static_cast<uint8_t>(target.host.size()));
  req.insert(req.end(), target.host.begin(), target.host.end());
  req.push_back(static_cast<uint8_t>((target.port >> 8) & 0xFF));
  req.push_back(static_cast<uint8_t>(target.port & 0xFF));
  if (!SendAll(fd, req.data(), req.size(), timeout_ms, error))
    return false;

  uint8_t reply[4] = {};
  if (!RecvExact(fd, reply, sizeof(reply), timeout_ms, error))
    return false;
  if (reply[1] != 0x00) {
    error = "socks5_connect_failed_" + std::to_string(reply[1]);
    return false;
  }

  size_t remain = 0;
  switch (reply[3]) {
  case 0x01:
    remain = 4 + 2;
    break;
  case 0x03: {
    uint8_t len = 0;
    if (!RecvExact(fd, &len, 1, timeout_ms, error))
      return false;
    remain = static_cast<size_t>(len) + 2;
    break;
  }
  case 0x04:
    remain = 16 + 2;
    break;
  default:
    error = "socks5_invalid_atyp";
    return false;
  }
  if (remain > 0) {
    std::vector<uint8_t> discard(remain);
    if (!RecvExact(fd, discard.data(), discard.size(), timeout_ms, error))
      return false;
  }
  return true;
}

bool Socks4Connect(int fd, const ParsedUrl &target, uint32_t timeout_ms,
                   std::string &error) {
  std::vector<uint8_t> req;
  req.push_back(0x04);
  req.push_back(0x01);
  req.push_back(static_cast<uint8_t>((target.port >> 8) & 0xFF));
  req.push_back(static_cast<uint8_t>(target.port & 0xFF));
  req.push_back(0x00);
  req.push_back(0x00);
  req.push_back(0x00);
  req.push_back(0x01); // SOCKS4a: resolve domain at proxy
  req.push_back(0x00); // user id terminator
  req.insert(req.end(), target.host.begin(), target.host.end());
  req.push_back(0x00);
  if (!SendAll(fd, req.data(), req.size(), timeout_ms, error))
    return false;

  uint8_t reply[8] = {};
  if (!RecvExact(fd, reply, sizeof(reply), timeout_ms, error))
    return false;
  if (reply[1] != 0x5A) {
    error = "socks4_connect_failed_" + std::to_string(reply[1]);
    return false;
  }
  return true;
}

bool ProbeThroughSocket(int fd, const ParsedUrl &target, uint32_t timeout_ms,
                        int &status_code, std::string &error) {
  const std::string request =
      "HEAD " + target.path + " HTTP/1.1\r\nHost: " + target.host +
      "\r\nConnection: close\r\nUser-Agent: libsys-egress/1.0\r\n\r\n";
  if (!SendString(fd, request, timeout_ms, error))
    return false;
  return ReadStatusLine(fd, timeout_ms, status_code, error);
}

bool ProbeDirect(const ParsedUrl &target, uint32_t timeout_ms, int &status_code,
                 std::string &error) {
  if (target.secure) {
    error = "https_target_not_supported_on_linux_probe";
    return false;
  }

  int fd = ConnectTcp(target.host, target.port, timeout_ms, error);
  if (fd < 0)
    return false;

  const bool ok = ProbeThroughSocket(fd, target, timeout_ms, status_code, error);
  close(fd);
  return ok && status_code >= 200 && status_code < 400;
}

bool ProbeHttpProxy(const ParsedUrl &target,
                    const ISystem::NetEnvInfo::ProxyInfo &proxy,
                    uint32_t timeout_ms, int &status_code, std::string &error) {
  if (target.secure) {
    error = "https_target_not_supported_on_linux_http_proxy_probe";
    return false;
  }

  int fd = ConnectTcp(proxy.host, proxy.port, timeout_ms, error);
  if (fd < 0)
    return false;

  const std::string request =
      "HEAD " + target.absolute_url +
      " HTTP/1.1\r\nHost: " + target.host +
      "\r\nConnection: close\r\nUser-Agent: libsys-egress/1.0\r\n\r\n";
  bool ok = false;
  if (SendString(fd, request, timeout_ms, error)) {
    ok = ReadStatusLine(fd, timeout_ms, status_code, error);
  }
  close(fd);
  return ok && status_code >= 200 && status_code < 400;
}

bool ProbeSocksProxy(const ParsedUrl &target,
                     const ISystem::NetEnvInfo::ProxyInfo &proxy,
                     uint32_t timeout_ms, int &status_code,
                     std::string &error) {
  if (target.secure) {
    error = "https_target_not_supported_on_linux_socks_probe";
    return false;
  }

  int fd = ConnectTcp(proxy.host, proxy.port, timeout_ms, error);
  if (fd < 0)
    return false;

  bool ok = false;
  switch (proxy.type) {
  case ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS4:
    ok = Socks4Connect(fd, target, timeout_ms, error);
    break;
  case ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS5:
    ok = Socks5Connect(fd, target, timeout_ms, error);
    break;
  default:
    error = "unsupported_socks_proxy_type";
    break;
  }

  if (ok)
    ok = ProbeThroughSocket(fd, target, timeout_ms, status_code, error);
  close(fd);
  return ok && status_code >= 200 && status_code < 400;
}

bool ProbeUrl(const ParsedUrl &target, const ISystem::NetEnvInfo &env,
              uint32_t timeout_ms, int &status_code, std::string &error) {
  if (!target.valid) {
    error = "invalid_url";
    return false;
  }

  const auto &proxy = env.proxy;
  if (proxy.type == ISystem::NetEnvInfo::ProxyInfo::Type::None ||
      ShouldBypassProxy(target.host, proxy.bypass_list)) {
    return ProbeDirect(target, timeout_ms, status_code, error);
  }

  switch (proxy.type) {
  case ISystem::NetEnvInfo::ProxyInfo::Type::HTTP:
    return ProbeHttpProxy(target, proxy, timeout_ms, status_code, error);
  case ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS4:
  case ISystem::NetEnvInfo::ProxyInfo::Type::SOCKS5:
    return ProbeSocksProxy(target, proxy, timeout_ms, status_code, error);
  case ISystem::NetEnvInfo::ProxyInfo::Type::HTTPS:
    error = "unsupported_https_proxy_on_linux_probe";
    return false;
  case ISystem::NetEnvInfo::ProxyInfo::Type::PAC:
  case ISystem::NetEnvInfo::ProxyInfo::Type::AutoDetect:
    error = "unsupported_auto_proxy_on_linux_probe";
    return false;
  default:
    error = "unsupported_proxy_type";
    return false;
  }
}

struct ProbeObservation {
  bool ok = false;
  int status = 0;
  std::string error;
};

ProbeObservation ObserveUrl(const ParsedUrl &target,
                            const ISystem::NetEnvInfo &env,
                            uint32_t timeout_ms) noexcept {
  ProbeObservation result;
  try {
    result.ok =
        ProbeUrl(target, env, timeout_ms, result.status, result.error);
  } catch (...) {
    result.ok = false;
    result.error = "probe_observation_exception";
  }
  return result;
}

} // namespace

ISystem::EgressProbeInfo ISystem::DetectSystemEgress(uint32_t timeout_ms) {
  EgressProbeInfo out;
  out.timeout_ms = timeout_ms;
  out.global_url = kGlobalProbeUrl;
  out.domestic_url = kDomesticProbeUrl;

  const auto env = GetNetEnvironment();
  out.system_proxy_present =
      env.proxy.type != NetEnvInfo::ProxyInfo::Type::None;
  out.system_proxy_type = env.proxy.type_str;
  out.system_proxy_host = env.proxy.host;
  out.system_proxy_port = env.proxy.port;
  out.proxy_auto_detect = env.proxy.auto_detect;
  out.proxy_pac_url = env.proxy.pac_url;
  out.tun_active = env.tun_vpn.tun_active;
  out.vpn_active = env.tun_vpn.vpn_active;
  out.connection_type = env.connection_type_str;

  const auto global_target = ParseUrl(out.global_url);
  const auto domestic_target = ParseUrl(out.domestic_url);

  ProbeObservation global;
  ProbeObservation domestic;
  std::future<ProbeObservation> global_future;
  std::future<ProbeObservation> domestic_future;
  try {
    global_future = libsys::detail::GetEgressProbeExecutor().Submit(
        [global_target, env, timeout_ms]() {
          return ObserveUrl(global_target, env, timeout_ms);
        });
  } catch (...) {
  }
  try {
    domestic_future = libsys::detail::GetEgressProbeExecutor().Submit(
        [domestic_target, env, timeout_ms]() {
          return ObserveUrl(domestic_target, env, timeout_ms);
        });
  } catch (...) {
  }
  try {
    global = global_future.valid()
                 ? global_future.get()
                 : ObserveUrl(global_target, env, timeout_ms);
  } catch (...) {
    global.error = "probe_async_exception";
  }
  out.global_checked = true;
  out.global_ok = global.ok;
  out.global_status = global.status;
  out.global_error = global.error;
  if (out.global_ok) {
    out.domestic_checked = false;
    out.domestic_ok = false;
    out.domestic_status = 0;
    out.domestic_error.clear();
    out.capability = EgressProbeInfo::Capability::GlobalEgress;
    out.capability_str = CapabilityName(out.capability);
    return out;
  }

  try {
    domestic = domestic_future.valid()
                   ? domestic_future.get()
                   : ObserveUrl(domestic_target, env, timeout_ms);
  } catch (...) {
    domestic.error = "probe_async_exception";
  }
  out.domestic_checked = true;
  out.domestic_ok = domestic.ok;
  out.domestic_status = domestic.status;
  out.domestic_error = domestic.error;

  // PAC / AutoDetect / HTTPS proxy on Linux have no single universal runtime
  // stack we can delegate to here. Returning Unknown is safer than pretending
  // the system is offline based on a direct-path fallback.
  if (env.proxy.type == NetEnvInfo::ProxyInfo::Type::PAC ||
      env.proxy.type == NetEnvInfo::ProxyInfo::Type::AutoDetect ||
      env.proxy.type == NetEnvInfo::ProxyInfo::Type::HTTPS) {
    out.capability = EgressProbeInfo::Capability::Unknown;
    out.capability_str = CapabilityName(out.capability);
    out.domestic_checked = false;
    out.domestic_ok = false;
    out.domestic_error = out.global_error;
    return out;
  }

  out.capability = out.domestic_ok ? EgressProbeInfo::Capability::DomesticOnly
                                   : EgressProbeInfo::Capability::Offline;
  out.capability_str = CapabilityName(out.capability);
  return out;
}
