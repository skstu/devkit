#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <limits>
#include <optional>
#include <string>

namespace libnet {

class HttpClient final {
public:
  using Headers = std::map<std::string, std::string>;
  using Params = std::map<std::string, std::string>;
  using HeadersHandler =
      std::function<bool(long status_code, const Headers &headers)>;

  struct Options {
    long connect_timeout_ms = 10000;
    long timeout_ms = 30000;
    long low_speed_limit = 1;
    long low_speed_time = 30;
    long max_redirects = 5;
    bool verify_tls = true;
    bool follow_redirects = true;
    bool accept_encoding = true;
    bool tcp_keepalive = true;
    // nullopt uses environment/system proxy; empty string explicitly disables it.
    std::optional<std::string> proxy;
    std::string ca_pem;
    std::size_t max_response_bytes = (std::numeric_limits<std::size_t>::max)();
    // Called during transfer; true cancels. Exceptions are treated as cancellation.
    std::function<bool()> canceled;
    std::string user_agent = "SovKit-libnet/1.0";
    Headers headers;
  };

  struct Response {
    bool ok = false;
    bool canceled = false;
    long status_code = 0;
    int curl_code = 0;
    long os_error = 0;
    long http_version = 0;
    long primary_port = 0;
    long redirect_count = 0;
    long ssl_verify_result = 0;
    long connections_created = 0;
    std::int64_t dns_ms = 0;
    std::int64_t tcp_connect_ms = 0;
    std::int64_t tls_handshake_ms = 0;
    std::int64_t request_ready_ms = 0;
    std::int64_t server_wait_ms = 0;
    std::int64_t total_ms = 0;
    std::string content_type;
    std::string body;
    Headers headers;
    std::string error;

    bool IsCurlOk() const { return curl_code == 0; }
    bool IsHttpOk() const {
      return status_code >= 200 && status_code < 400;
    }
  };

  static const char *Version();

  static Response Request(const std::string &method, const std::string &url,
                          const std::string &body = {},
                          const Headers &headers = {});
  static Response Request(const std::string &method, const std::string &url,
                          const std::string &body, const Headers &headers,
                          const Options &options,
                          HeadersHandler headers_handler = {});

  static Response Head(const std::string &url,
                       const Headers &headers = {});
  static Response Head(const std::string &url, const Headers &headers,
                       const Options &options,
                       HeadersHandler headers_handler = {});
  static Response Get(const std::string &url,
                      const Headers &headers = {});
  static Response Get(const std::string &url, const Headers &headers,
                      const Options &options,
                      HeadersHandler headers_handler = {});
  static Response GetWithParams(const std::string &url, const Params &params,
                                const Headers &headers = {});
  static Response GetWithParams(const std::string &url, const Params &params,
                                const Headers &headers,
                                const Options &options,
                                HeadersHandler headers_handler = {});
  static Response Post(const std::string &url, const std::string &body,
                       const Headers &headers = {});
  static Response Post(const std::string &url, const std::string &body,
                       const Headers &headers, const Options &options,
                       HeadersHandler headers_handler = {});
  static Response PostJson(const std::string &url, const std::string &body,
                           const Headers &headers = {});
  static Response PostJson(const std::string &url, const std::string &body,
                           const Headers &headers, const Options &options,
                           HeadersHandler headers_handler = {});
  static Response Put(const std::string &url, const std::string &body,
                      const Headers &headers = {});
  static Response Put(const std::string &url, const std::string &body,
                      const Headers &headers, const Options &options,
                      HeadersHandler headers_handler = {});
  static Response PutJson(const std::string &url, const std::string &body,
                          const Headers &headers = {});
  static Response PutJson(const std::string &url, const std::string &body,
                          const Headers &headers, const Options &options,
                          HeadersHandler headers_handler = {});
};

} // namespace libnet