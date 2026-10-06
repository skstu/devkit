#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>

namespace libnet {

struct DownloadOptions {
  std::size_t parallelism = 4;
  std::uint64_t min_part_size = 1024 * 1024;
  long connect_timeout_ms = 10000;
  long timeout_ms = 0;
  long low_speed_limit = 1;
  long low_speed_time = 30;
  long max_redirects = 5;
  int retry_count = 2;
  long retry_delay_ms = 500;
  bool resume = true;
  bool verify_tls = true;
  std::string user_agent = "SovKit-libnet/1.0";
  std::map<std::string, std::string> headers;
};

struct DownloadResult {
  bool ok = false;
  bool canceled = false;
  long status_code = 0;
  int curl_code = 0;
  std::uint64_t total_bytes = 0;
  std::string error;
};

using DownloadProgress =
    std::function<bool(std::uint64_t total, std::uint64_t downloaded)>;

class Downloader final {
public:
  static const char *Version();
  static DownloadResult Download(const std::string &url,
                                 const std::filesystem::path &target,
                                 DownloadProgress progress = {});
  static DownloadResult Download(const std::string &url,
                                 const std::filesystem::path &target,
                                 const DownloadOptions &options,
                                 DownloadProgress progress = {});
};

} // namespace libnet