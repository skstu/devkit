#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

#include <libsys.h>
#include "egress_probe_executor.hpp"
#include <exception>
#include <future>
#include <memory>
#include <utility>

namespace {

const char *kGlobalProbeUrl = "https://www.google.com/generate_204";
const char *kGlobalFallbackProbeUrl =
    "https://connectivitycheck.gstatic.com/generate_204";
const char *kDomesticProbeUrl = "https://www.baidu.com/";

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

bool ProbeUrl(const std::string &url_string, uint32_t timeout_ms, int &status_code,
              std::string &error, bool use_get) {
  status_code = 0;
  error.clear();

  struct CallbackResult {
    NSInteger status = 0;
    std::string error;
  };

  @autoreleasepool {
    NSString *urlText =
        [NSString stringWithUTF8String:url_string.c_str()];
    NSURL *url = [NSURL URLWithString:urlText];
    if (!url) {
      error = "invalid_url";
      return false;
    }

    auto callback_result = std::make_shared<CallbackResult>();
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    NSURLSessionConfiguration *cfg =
        [NSURLSessionConfiguration ephemeralSessionConfiguration];
    cfg.timeoutIntervalForRequest = timeout_ms / 1000.0;
    cfg.timeoutIntervalForResource = timeout_ms / 1000.0;
    cfg.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    cfg.URLCache = nil;
    cfg.HTTPCookieStorage = nil;
    cfg.URLCredentialStorage = nil;

    NSURLSession *session =
        [NSURLSession sessionWithConfiguration:cfg];
    NSMutableURLRequest *req =
        [NSMutableURLRequest requestWithURL:url
                                cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                            timeoutInterval:timeout_ms / 1000.0];
    req.HTTPMethod = use_get ? @"GET" : @"HEAD";
    [req setValue:@"close" forHTTPHeaderField:@"Connection"];

    NSURLSessionDataTask *task =
        [session dataTaskWithRequest:req
                   completionHandler:^(NSData *data, NSURLResponse *response,
                                       NSError *taskError) {
                     (void)data;
                     if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
                       callback_result->status =
                           [(NSHTTPURLResponse *)response statusCode];
                     }
                     if (taskError) {
                       NSString *description = [taskError localizedDescription];
                       const char *utf8 = [description UTF8String];
                       callback_result->error =
                           utf8 ? std::string(utf8) : "request_failed";
                     }
                     dispatch_semaphore_signal(sem);
                   }];
    [task resume];

    const int64_t timeout_ns =
        static_cast<int64_t>(timeout_ms) * static_cast<int64_t>(NSEC_PER_MSEC);
    const long wait_result =
        dispatch_semaphore_wait(sem,
                                dispatch_time(DISPATCH_TIME_NOW, timeout_ns));
    if (wait_result != 0) {
      [task cancel];
      [session invalidateAndCancel];
      // Keep the semaphore alive until a late cancellation callback releases
      // its block capture. ARC owns the modern dispatch object; avoiding a
      // manual release on the legacy path is preferable to signalling freed
      // storage after timeout.
      error = "timeout";
      return false;
    }

    [session finishTasksAndInvalidate];
    status_code = static_cast<int>(callback_result->status);
    error = callback_result->error;
  }

  if (status_code == 0 && error.empty())
    error = "non_http_response";
  return status_code >= 200 && status_code < 400;
}

struct ProbeObservation {
  bool ok = false;
  int status = 0;
  std::string error;
};

ProbeObservation ObserveUrl(const std::string &url, uint32_t timeout_ms,
                            bool use_get) noexcept {
  ProbeObservation result;
  try {
    result.ok =
        ProbeUrl(url, timeout_ms, result.status, result.error, use_get);
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

  ProbeObservation global;
  ProbeObservation domestic;
  std::future<ProbeObservation> global_future;
  std::future<ProbeObservation> global_fallback_future;
  std::future<ProbeObservation> domestic_future;
  try {
    global_future = libsys::detail::GetEgressProbeExecutor().Submit(
        [url = out.global_url, timeout_ms]() {
          return ObserveUrl(url, timeout_ms, true);
        });
  } catch (...) {
  }
  try {
    global_fallback_future = libsys::detail::GetEgressProbeExecutor().Submit(
        [timeout_ms]() {
          return ObserveUrl(kGlobalFallbackProbeUrl, timeout_ms, true);
        });
  } catch (...) {
  }
  try {
    domestic_future = libsys::detail::GetEgressProbeExecutor().Submit(
        [url = out.domestic_url, timeout_ms]() {
          return ObserveUrl(url, timeout_ms, false);
        });
  } catch (...) {
  }
  try {
    global = global_future.valid() ? global_future.get()
                                   : ObserveUrl(out.global_url, timeout_ms, true);
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

  if (!global.ok) {
    ProbeObservation fallback;
    try {
      fallback = global_fallback_future.valid()
                     ? global_fallback_future.get()
                     : ObserveUrl(kGlobalFallbackProbeUrl, timeout_ms, true);
    } catch (...) {
      fallback.error = "probe_async_exception";
    }
    if (fallback.ok) {
      global = std::move(fallback);
      out.global_url = kGlobalFallbackProbeUrl;
    } else if (!fallback.error.empty()) {
      if (!global.error.empty())
        global.error.append(";");
      global.error.append("fallback:").append(fallback.error);
    }
  }
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
                   : ObserveUrl(out.domestic_url, timeout_ms, false);
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
