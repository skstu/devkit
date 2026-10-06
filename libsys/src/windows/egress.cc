#include <libsys.h>
#include "egress_probe_executor.hpp"

#include <winhttp.h>

#include <exception>
#include <future>

#pragma comment(lib, "winhttp.lib")

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

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

std::wstring Utf8ToWide(const std::string &s) {
  if (s.empty())
    return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  if (n <= 0)
    return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
  if (!out.empty() && out.back() == L'\0')
    out.pop_back();
  return out;
}

std::string WinErrString(DWORD err) {
  return "winhttp_error_" + std::to_string(static_cast<unsigned long>(err));
}

struct ParsedUrl {
  bool valid = false;
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = 0;
  bool secure = false;
};

ParsedUrl ParseUrl(const std::string &url) {
  ParsedUrl out;
  const std::wstring wurl = Utf8ToWide(url);
  if (wurl.empty())
    return out;

  URL_COMPONENTS uc{};
  wchar_t host[256] = {};
  wchar_t path[2048] = {};
  wchar_t extra[2048] = {};
  uc.dwStructSize = sizeof(uc);
  uc.lpszHostName = host;
  uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));

  if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc))
    return out;

  out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
  out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.dwExtraInfoLength > 0)
    out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  if (out.path.empty())
    out.path = L"/";
  out.port = uc.nPort;
  out.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
  out.valid = !out.host.empty() && out.port != 0;
  return out;
}

bool ProbeUrl(const std::string &url, uint32_t timeout_ms, int &status_code,
              std::string &error, bool use_automatic_proxy) {
  status_code = 0;
  error.clear();

  const auto parsed = ParseUrl(url);
  if (!parsed.valid) {
    error = "invalid_url";
    return false;
  }

  HINTERNET session = WinHttpOpen(
      L"libsys-egress/1.0",
      use_automatic_proxy ? WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
                          : WINHTTP_ACCESS_TYPE_NO_PROXY,
      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session && use_automatic_proxy) {
    session = WinHttpOpen(L"libsys-egress/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  }
  if (!session) {
    error = WinErrString(GetLastError());
    return false;
  }

  WinHttpSetTimeouts(session, static_cast<int>(timeout_ms),
                     static_cast<int>(timeout_ms),
                     static_cast<int>(timeout_ms),
                     static_cast<int>(timeout_ms));

  HINTERNET connect =
      WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
  if (!connect) {
    error = WinErrString(GetLastError());
    WinHttpCloseHandle(session);
    return false;
  }

  DWORD flags = parsed.secure ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET request =
      WinHttpOpenRequest(connect, L"HEAD", parsed.path.c_str(), nullptr,
                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                         flags);
  if (!request) {
    error = WinErrString(GetLastError());
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
  }

  const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS,
                                       0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
  if (!sent) {
    error = WinErrString(GetLastError());
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
  }

  const BOOL received = WinHttpReceiveResponse(request, nullptr);
  if (!received) {
    error = WinErrString(GetLastError());
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
  }

  DWORD code = 0;
  DWORD code_size = sizeof(code);
  if (!WinHttpQueryHeaders(request,
                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &code, &code_size,
                           WINHTTP_NO_HEADER_INDEX)) {
    error = WinErrString(GetLastError());
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return false;
  }

  status_code = static_cast<int>(code);
  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return status_code >= 200 && status_code < 400;
}

struct ProbeObservation {
  bool ok = false;
  int status = 0;
  std::string error;
};

ProbeObservation ObserveUrl(const std::string &url, uint32_t timeout_ms,
                            bool use_automatic_proxy) noexcept {
  ProbeObservation result;
  try {
    result.ok = ProbeUrl(url, timeout_ms, result.status, result.error,
                         use_automatic_proxy);
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

  // Google commonly reaches its timeout on mainland-China direct networks.
  // Start the domestic observation at the same time so classification remains
  // one bounded probe round instead of Google timeout + Baidu round-trip.
  const bool use_automatic_proxy =
      !env.proxy.detection_known || out.system_proxy_present ||
      out.proxy_auto_detect || !out.proxy_pac_url.empty();
  ProbeObservation global;
  ProbeObservation domestic;
  std::future<ProbeObservation> global_future;
  std::future<ProbeObservation> domestic_future;
  try {
    global_future = libsys::detail::GetEgressProbeExecutor().Submit(
        [url = out.global_url, timeout_ms, use_automatic_proxy]() {
          return ObserveUrl(url, timeout_ms, use_automatic_proxy);
        });
  } catch (...) {
  }
  try {
    domestic_future = libsys::detail::GetEgressProbeExecutor().Submit(
        [url = out.domestic_url, timeout_ms, use_automatic_proxy]() {
          return ObserveUrl(url, timeout_ms, use_automatic_proxy);
        });
  } catch (...) {
  }
  try {
    global = global_future.valid()
                 ? global_future.get()
                 : ObserveUrl(out.global_url, timeout_ms, use_automatic_proxy);
  } catch (...) {
    global.error = "probe_async_exception";
  }
  out.global_checked = true;
  out.global_ok = global.ok;
  out.global_status = global.status;
  out.global_error = global.error;
  if (out.global_ok) {
    // Preserve the historical public contract: the domestic fallback is not
    // reported as checked when the global route already answered.
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
                   : ObserveUrl(out.domestic_url, timeout_ms,
                                use_automatic_proxy);
  } catch (...) {
    domestic.error = "probe_async_exception";
  }
  out.domestic_checked = true;
  out.domestic_ok = domestic.ok;
  out.domestic_status = domestic.status;
  out.domestic_error = domestic.error;

  out.capability = out.domestic_ok ? EgressProbeInfo::Capability::DomesticOnly
                                   : EgressProbeInfo::Capability::Offline;
  out.capability_str = CapabilityName(out.capability);
  return out;
}
